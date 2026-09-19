#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把产品规格发布为 GitHub issue，并生成规格书 Markdown。

用法
----
  python3 tools/publish_issues.py prd                 # 只生成 docs/PRD-actions.md
  python3 tools/publish_issues.py labels              # 只创建标签
  python3 tools/publish_issues.py issues              # 只创建 issue
  python3 tools/publish_issues.py all                 # 全部执行
  python3 tools/publish_issues.py all --dry-run       # 只打印，不改远端

设计要点
--------
1. 规格数据是唯一事实来源（tools/spec 包），本脚本是唯一的发布出口。
2. 幂等：已存在的 issue（按标题中的 ACTION-ID 匹配）不会重复创建。
3. 并发但保守：默认 4 个并发，遇到限流自动退避重试。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

from spec import MODULES, STATE_LABELS, load_actions  # noqa: E402

REPO = os.environ.get("LQCOMPARE_REPO", "LorenHan/LqCompare")
API = "https://api.github.com"
PRD_PATH = os.path.join(REPO_ROOT, "docs", "PRD-actions.md")
ISSUES_JSON = os.path.join(REPO_ROOT, "docs", "github", "prd-issues.json")

# 状态标签的语义色（与 Ailecium 工作流一致：需求/待实现/进行中/部分完成/待审核/待定/已完成/已取消）
STATE_LABEL_COLORS = {
    "需求": "1d76db",
    "待实现": "fbca04",
    "进行中": "0e8a16",
    "部分完成": "c2e0c6",
    "待审核": "d4c5f9",
    "待定": "bfd4f2",
    "已完成": "5319e7",
    "已取消": "cccccc",
}

# 模块标签统一用中性色，靠名称区分而非颜色。
MODULE_LABEL_COLOR = "006b75"

# 优先级标签
PRIO_LABELS = {"P0": "b60205", "P1": "e99695", "P2": "f9d0c4"}

# ---------------------------------------------------------------------------
# 优先级策略：P0 = 「跑通最小闭环」所必需的地基条目，其余 P1。
# 之所以在脚本里定义而不是逐条写在数据里，是为了让策略集中、可整体调整。
# 这是优先级的唯一来源：规格条目不再自带 prio 字段
# （见 tools/spec/__init__.py 的设计约束 5）。
# ---------------------------------------------------------------------------
P0_RANGES = {
    "UI": range(1, 19),      # Ribbon 外壳、页面、命令注册、图标、状态栏、显示可见性
    "SESS": range(1, 11),    # 会话基类、注册表、Home、设置框架、保存加载
    "TXT": range(1, 9),      # 文本会话、对齐算法、忽略规则、编码
    "DIR": range(1, 13),     # 目录扫描、快速测试、内容比对、状态判定、着色
    "FMT": range(1, 3),      # 文件格式定义模型与管理界面
    "VCS": range(1, 3),      # 版本控制后端抽象层与 HEAD 比对
    "ENG": range(1, 5),      # 骨架、模块构建、测试框架、CI
    "PLAT": range(1, 3),     # 构建系统、文件系统抽象
    "DOC": range(1, 2),      # 用户手册
}


def priority_of(action_id: str) -> str:
    prefix, number = action_id.split("-", 1)
    try:
        number = int(number)
    except ValueError:
        return "P1"
    if number in P0_RANGES.get(prefix, ()):  # type: ignore[arg-type]
        return "P0"
    return "P1"


# ---------------------------------------------------------------------------
# GitHub API
# ---------------------------------------------------------------------------
def token() -> str:
    env = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if env:
        return env.strip()
    try:
        return subprocess.check_output(["gh", "auth", "token"], text=True).strip()
    except Exception as exc:  # pragma: no cover - 环境问题
        raise SystemExit("无法获取 GitHub token（请设置 GH_TOKEN 或登录 gh）：%s" % exc)


def api(method: str, path: str, payload=None, *, retries: int = 5):
    url = path if path.startswith("http") else API + path
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    headers = {
        "Authorization": "Bearer " + token(),
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "lqcompare-spec-publisher",
    }
    delay = 2.0
    last_error = None
    for attempt in range(retries):
        request = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                body = response.read()
                return json.loads(body) if body else None
        except urllib.error.HTTPError as exc:
            body = exc.read().decode("utf-8", "replace")
            last_error = "HTTP %s: %s" % (exc.code, body[:400])
            # 403/429 多为限流，422 是校验失败（重试无意义）
            if exc.code in (403, 429) or exc.code >= 500:
                # 「二级限流」针对的是内容创建频率，GitHub 要求静默至少 1 分钟。
                # 只按 2/4/8/16/32 秒退避的话，5 次重试合计才 62 秒且每次都撞墙，
                # 结果是白跑一轮还丢掉一批条目。这里对它单独拉长等待。
                wait = delay
                if "secondary rate limit" in body or "secondary-rate" in body:
                    wait = max(wait, 60.0)
                time.sleep(wait)
                delay *= 2
                continue
            raise RuntimeError(last_error) from exc
        except urllib.error.URLError as exc:
            last_error = "网络错误：%s" % exc
            time.sleep(delay)
            delay *= 2
    raise RuntimeError("重试 %d 次后仍失败：%s" % (retries, last_error))


def list_all_issues() -> dict:
    """返回 {ACTION-ID: issue}。"""
    result = {}
    found = set()
    page = 1
    while True:
        items = api(
            "GET",
            "/repos/%s/issues?state=all&per_page=100&page=%d" % (REPO, page),
        )
        if not items:
            break
        for item in items:
            if "pull_request" in item:
                continue
            found.add(item["number"])
            match = re.match(r"\[([A-Z]+-\d+)\]", item.get("title", ""))
            if match:
                result[match.group(1)] = item
        if len(items) < 100:
            break
        page += 1
    return result


# ---------------------------------------------------------------------------
# 渲染
# ---------------------------------------------------------------------------
def render_issue_body(action: dict) -> str:
    lines = []
    lines.append("## PRD Action ID")
    lines.append("")
    lines.append(action["id"])
    lines.append("")
    lines.append("## 入口、作用对象与行为边界")
    lines.append("")
    lines.append(action["entry"])
    lines.append("")
    lines.append("## 完成标准")
    lines.append("")
    for item in action["criteria"]:
        lines.append("- [ ] " + item)
    lines.append("")
    if action["ref"]:
        lines.append("## 竞品对标出处")
        lines.append("")
        lines.append(action["ref"])
        lines.append("")
    lines.append("## 实现与关联提交")
    lines.append("")
    lines.append(
        "每个小功能单独 commit，提交消息包含本 Issue 号；已有提交在评论中追加链接。"
    )
    lines.append("")
    lines.append("---")
    lines.append("")
    lines.append(
        "> 本条目的规格来源：`tools/spec/`（本仓库的产品规格数据）。"
        "修改规格请改数据文件而不是改 issue 正文。"
    )
    return "\n".join(lines)


def render_prd(actions: list, generated_at: str) -> str:
    by_module: dict = {}
    for action in actions:
        by_module.setdefault(action["module"], []).append(action)

    out = []
    out.append("# LqCompare 产品规格（PRD-actions）")
    out.append("")
    out.append("> 本文档由 `tools/publish_issues.py` 从 `tools/spec/` 自动生成，请勿手工编辑。")
    out.append(">")
    out.append("> 规格条目与 GitHub issue 一一对应，标题格式为 `[ACTION-ID] 功能名`。")
    out.append(">")
    out.append("> 生成时间：" + generated_at)
    out.append("")
    out.append("## 1. 产品定位")
    out.append("")
    out.append(
        "LqCompare 是一个 Qt 5.15.2 / C++17 的**文件与文件夹比对工具**，"
        "对标 Beyond Compare 5（功能面的主要依据）与 TortoiseGit 的比对/合并界面"
        "（Ribbon 布局参考）。两者行为冲突处以 **Beyond Compare 为准**。"
    )
    out.append("")
    out.append(
        "界面使用 **LqRibbon**（与 Ailecium 同一套 Ribbon 外壳），"
        "代码分层为 `App → Views → Services`，Services 层不得依赖任何 UI。"
    )
    out.append("")
    out.append("## 2. 功能域与条目统计")
    out.append("")
    out.append("| 功能域 | 条目数 | 优先级 P0 | 前缀 |")
    out.append("| --- | --- | --- | --- |")
    total = 0
    p0_total = 0
    for module in MODULES:
        items = by_module.get(module, [])
        if not items:
            continue
        prefixes = sorted({i["id"].split("-")[0] for i in items})
        p0 = sum(1 for i in items if priority_of(i["id"]) == "P0")
        total += len(items)
        p0_total += p0
        out.append(
            "| %s | %d | %d | %s |" % (module, len(items), p0, ", ".join(prefixes))
        )
    out.append("| **合计** | **%d** | **%d** | 27 个前缀 |" % (total, p0_total))
    out.append("")
    out.append("## 3. 竞品对标基线")
    out.append("")
    out.append(
        "| 文档 | 内容 | 用途 |\n| --- | --- | --- |\n"
        "| `docs/research/beyondcompare-features.md` | Beyond Compare 5 全功能测绘（38 个功能域） | 功能面覆盖的依据 |\n"
        "| `docs/research/tortoisegit-diff-features.md` | TortoiseGit 比对/合并界面与客户端入口测绘 | Ribbon 布局与交互参考 |"
    )
    out.append("")
    out.append(
        "两份测绘文档中「一个功能点」不等于「一个 issue」："
        "本规格把它们收敛为可独立实现与验收的条目，避免 issue 碎到无法评审。"
    )
    out.append("")
    out.append("## 4. 条目清单")
    out.append("")

    for module in MODULES:
        items = by_module.get(module, [])
        if not items:
            continue
        out.append("### %s（%d 条）" % (module, len(items)))
        out.append("")
        for action in items:
            prio = priority_of(action["id"])
            out.append("#### `%s` %s" % (action["id"], action["title"]))
            out.append("")
            out.append("*优先级*：%s　*模块*：%s" % (prio, action["module"]))
            out.append("")
            out.append("**入口、作用对象与行为边界**")
            out.append("")
            out.append(action["entry"])
            out.append("")
            out.append("**完成标准**")
            out.append("")
            for item in action["criteria"]:
                out.append("- [ ] " + item)
            out.append("")
            if action["ref"]:
                out.append("**竞品对标出处**：" + action["ref"])
                out.append("")
        out.append("")

    out.append("## 5. 状态标签工作流")
    out.append("")
    out.append(
        "状态标签取值：" + "、".join(STATE_LABELS) + "。"
        "关闭 issue 为 completed 时自动置「已完成」，"
        "以 not_planned 关闭时置「已取消」；该同步由 "
        "`.github/workflows/requirement-labels.yml` 完成。"
    )
    out.append("")
    out.append("## 6. 与 issue 的对应关系")
    out.append("")
    out.append("发布映射保存在 `docs/github/prd-issues.json`。")
    out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# 动作
# ---------------------------------------------------------------------------
def ensure_labels(dry_run: bool) -> None:
    labels = []
    for name, color in STATE_LABEL_COLORS.items():
        labels.append({"name": name, "color": color, "description": "规格状态"})
    for name in MODULES:
        labels.append({"name": name, "color": MODULE_LABEL_COLOR, "description": "功能域"})
    for name, color in PRIO_LABELS.items():
        labels.append({"name": name, "color": color, "description": "优先级"})

    if dry_run:
        print("[dry-run] 将创建/更新 %d 个标签" % len(labels))
        for label in labels:
            print("  -", label["name"])
        return

    existing = {item["name"] for item in (api("GET", "/repos/%s/labels?per_page=100" % REPO) or [])}
    created = 0
    for label in labels:
        if label["name"] in existing:
            api("PATCH", "/repos/%s/labels/%s" % (REPO, urllib.parse.quote(label["name"])), label)
        else:
            api("POST", "/repos/%s/labels" % REPO, label)
            created += 1
    print("标签就绪：新建 %d 个，已存在 %d 个" % (created, len(labels) - created))


def publish_issues(actions: list, dry_run: bool, workers: int) -> dict:
    if dry_run:
        print("[dry-run] 将创建 %d 个 issue" % len(actions))
        for action in actions[:10]:
            print("  - [%s] %s" % (action["id"], action["title"]))
        if len(actions) > 10:
            print("  ... 其余 %d 个" % (len(actions) - 10))
        return {}

    existing = list_all_issues()
    print("远端已有 issue：%d 个（其中带 ACTION-ID 的 %d 个）" % (
        len({i["number"] for i in existing.values()}), len(existing)))

    pending = [a for a in actions if a["id"] not in existing]
    print("待创建：%d 个；已存在跳过：%d 个" % (len(pending), len(actions) - len(pending)))
    if not pending:
        return {aid: item["number"] for aid, item in existing.items()}

    mapping = {aid: item["number"] for aid, item in existing.items()}
    failures = []

    def create(action):
        title = "[%s] %s" % (action["id"], action["title"])
        payload = {
            "title": title,
            "body": render_issue_body(action),
            "labels": ["需求", "待实现", action["module"], priority_of(action["id"])],
        }
        issue = api("POST", "/repos/%s/issues" % REPO, payload)
        return action["id"], issue["number"]

    done = 0
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(create, a): a for a in pending}
        for future in as_completed(futures):
            action = futures[future]
            try:
                aid, number = future.result()
                mapping[aid] = number
                done += 1
                if done % 25 == 0 or done == len(pending):
                    print("  已创建 %d/%d" % (done, len(pending)))
            except Exception as exc:  # noqa: BLE001
                failures.append((action["id"], str(exc)))
                print("  失败 %s：%s" % (action["id"], exc))

    if failures:
        print("失败 %d 个：" % len(failures))
        for aid, error in failures:
            print("  - %s: %s" % (aid, error))
    print("issue 发布完成：成功 %d，失败 %d" % (done, len(failures)))
    return mapping


def render_issue_index(actions: list, mapping: dict, generated_at: str) -> str:
    by_module: dict = {}
    for action in actions:
        by_module.setdefault(action["module"], []).append(action)

    out = []
    out.append("# PRD Issue 索引")
    out.append("")
    out.append("> 由 `tools/publish_issues.py` 生成，请勿手工编辑。生成时间：" + generated_at)
    out.append(">")
    out.append("> 条目总数 %d，已发布 %d。规格正文见 [PRD-actions](../PRD-actions.md)。"
               % (len(actions), len(mapping)))
    out.append("")
    for module in MODULES:
        items = by_module.get(module, [])
        if not items:
            continue
        out.append("## %s（%d）" % (module, len(items)))
        out.append("")
        for action in items:
            number = mapping.get(action["id"])
            link = (
                "[#%d](https://github.com/%s/issues/%d)" % (number, REPO, number)
                if number
                else "（未发布）"
            )
            out.append(
                "- `%s` %s　%s　*%s*"
                % (action["id"], action["title"], link, priority_of(action["id"]))
            )
        out.append("")
    return "\n".join(out)


def load_existing_mapping() -> dict:
    """读取上一次发布留下的 {ACTION-ID: issue 号} 映射。

    只生成规格书（`prd`）或演练（`--dry-run`）时，必须把这份映射带下去。
    否则 `write_artifacts()` 会用空映射覆盖 prd-issues.json，
    把「哪条规格对应哪个 issue」这个唯一反查入口清空——而 issue 还在远端躺着。
    """
    if not os.path.exists(ISSUES_JSON):
        return {}
    try:
        with open(ISSUES_JSON, encoding="utf-8") as handle:
            payload = json.load(handle)
    except (OSError, ValueError):
        return {}
    return {
        aid: entry["number"]
        for aid, entry in (payload.get("issues") or {}).items()
        if entry.get("number")
    }


def write_artifacts(actions: list, mapping: dict) -> None:
    generated_at = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    os.makedirs(os.path.dirname(PRD_PATH), exist_ok=True)
    with open(PRD_PATH, "w", encoding="utf-8") as handle:
        handle.write(render_prd(actions, generated_at) + "\n")
    print("已生成 %s" % os.path.relpath(PRD_PATH, REPO_ROOT))

    index_path = os.path.join(os.path.dirname(ISSUES_JSON), "issue-index.md")
    with open(index_path, "w", encoding="utf-8") as handle:
        handle.write(render_issue_index(actions, mapping, generated_at) + "\n")
    print("已生成 %s" % os.path.relpath(index_path, REPO_ROOT))

    os.makedirs(os.path.dirname(ISSUES_JSON), exist_ok=True)
    payload = {
        "repository": REPO,
        "generated_at": generated_at,
        "total": len(actions),
        "published": len(mapping),
        "issues": {
            action["id"]: {
                "number": mapping.get(action["id"]),
                "url": (
                    "https://github.com/%s/issues/%d" % (REPO, mapping[action["id"]])
                    if action["id"] in mapping
                    else None
                ),
                "title": action["title"],
                "module": action["module"],
                "priority": priority_of(action["id"]),
            }
            for action in actions
        },
    }
    with open(ISSUES_JSON, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2, sort_keys=False)
        handle.write("\n")
    print("已生成 %s" % os.path.relpath(ISSUES_JSON, REPO_ROOT))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["prd", "labels", "issues", "all"])
    parser.add_argument("--dry-run", action="store_true", help="只打印，不改远端")
    parser.add_argument("--workers", type=int, default=4, help="并发数（默认 4）")
    args = parser.parse_args()

    actions = load_actions()
    print("规格条目：%d 条" % len(actions))

    if args.command in ("prd", "all"):
        # 不带映射时沿用已有映射，避免把发布记录清掉（见 load_existing_mapping）。
        mapping = load_existing_mapping()
        if args.command == "all" and not args.dry_run:
            mapping = publish_issues(actions, True, args.workers) or mapping
        write_artifacts(actions, mapping)
        if args.command == "prd":
            return 0

    if args.command in ("labels", "all"):
        ensure_labels(args.dry_run)

    if args.command in ("issues", "all"):
        if args.dry_run:
            mapping = load_existing_mapping()
        else:
            mapping = publish_issues(actions, False, args.workers)
        write_artifacts(actions, mapping)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
