#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""规格数据自检（PRD: UI-024、DOC-005）。

检查六件事：
1. `tools/spec/` 的数据本身合法（ID 唯一、前缀与功能域一致、字段非空）；
2. `docs/PRD-actions.md` 与规格数据一致（重新生成后逐字节相同）；
3. `docs/github/prd-issues.json` 覆盖全部条目且链接格式合法；
4. 每个条目都有竞品对标出处或明确标注为超越性补充；
5. PRD 的功能域统计表与条目总数一致；
6. 优先级策略（`P0_RANGES`）里的前缀都能真的选中条目。

第 2 条是最重要的：PRD 是生成物，一旦有人手工改过 PRD（而不是改数据），
下一次重新生成就会把他的手改冲掉。宁可让 CI 在这里报错。

用法
----
    python3 tools/check_spec.py
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))

from spec import MODULES, STATE_LABELS, load_actions  # noqa: E402
from publish_issues import P0_RANGES, priority_of, render_prd  # noqa: E402, I001

PRD_PATH = os.path.join(REPO_ROOT, "docs", "PRD-actions.md")
ISSUES_JSON = os.path.join(REPO_ROOT, "docs", "github", "prd-issues.json")

URL_PATTERN = re.compile(r"^https://github\.com/[^/]+/[^/]+/issues/\d+$")


def check_prd_in_sync(actions) -> list[str]:
    problems: list[str] = []
    if not os.path.exists(PRD_PATH):
        return ["docs/PRD-actions.md 不存在，请运行 tools/publish_issues.py prd"]

    with open(PRD_PATH, encoding="utf-8") as handle:
        current = handle.read()

    # 生成时间不同不影响一致性判断，把它从两侧同时剔除。
    def strip_timestamp(text: str) -> str:
        return re.sub(r"^> 生成时间：.*$", "> 生成时间：<ignored>", text, flags=re.MULTILINE)

    with tempfile.NamedTemporaryFile("w+", encoding="utf-8", suffix=".md") as handle:
        handle.write(render_prd(actions, "1970-01-01 00:00 UTC") + "\n")
        handle.flush()
        with open(handle.name, encoding="utf-8") as reader:
            expected = reader.read()

    if strip_timestamp(current) != strip_timestamp(expected):
        problems.append(
            "docs/PRD-actions.md 与 tools/spec/ 不一致 —— PRD 是生成物，"
            "请修改 tools/spec/ 的数据后重新运行 tools/publish_issues.py prd"
        )
    return problems


def check_issue_map(actions) -> list[str]:
    problems: list[str] = []
    if not os.path.exists(ISSUES_JSON):
        return ["docs/github/prd-issues.json 不存在，请运行 tools/publish_issues.py"]
    with open(ISSUES_JSON, encoding="utf-8") as handle:
        payload = json.load(handle)

    mapped = payload.get("issues", {})
    for action in actions:
        entry = mapped.get(action["id"])
        if entry is None:
            problems.append("docs/github/prd-issues.json 缺少条目 %s" % action["id"])
            continue
        url = entry.get("url")
        if url is not None and not URL_PATTERN.match(url):
            problems.append("%s 的 issue 链接格式非法：%s" % (action["id"], url))
    extra = set(mapped) - {a["id"] for a in actions}
    for key in sorted(extra):
        problems.append("docs/github/prd-issues.json 多出条目 %s（规格中已不存在）" % key)
    return problems


def check_actions(actions) -> list[str]:
    problems: list[str] = []
    if not actions:
        return ["规格数据为空"]

    module_counts: dict[str, int] = {}
    for action in actions:
        module_counts[action["module"]] = module_counts.get(action["module"], 0) + 1
        if len(action["criteria"]) < 2:
            problems.append("%s 的完成标准少于 2 条，无法独立验收" % action["id"])
        if not action["ref"]:
            problems.append("%s 缺少竞品对标出处（若为超越性能力请显式写明）" % action["id"])

    for module in MODULES:
        if module not in module_counts:
            problems.append("功能域 %s 没有任何条目，规格书的目录会缺一栏" % module)
    for label in STATE_LABELS:
        if not label.strip():
            problems.append("状态标签集合里有空值")
    return problems


def check_prd_module_table(actions) -> list[str]:
    """PRD 的功能域统计表必须与实际条目数一致。"""
    problems: list[str] = []
    with open(PRD_PATH, encoding="utf-8") as handle:
        content = handle.read()
    total_row = re.search(r"\*\*合计\*\* \| \*\*(\d+)\*\*", content)
    if not total_row:
        problems.append("docs/PRD-actions.md 缺少合计行")
        return problems
    if int(total_row.group(1)) != len(actions):
        problems.append(
            "docs/PRD-actions.md 合计 %s 与实际条目数 %d 不一致"
            % (total_row.group(1), len(actions))
        )
    return problems


def check_priority_policy(actions) -> list[str]:
    """优先级策略是 P0 的唯一来源，写错前缀会静默失效。

    `P0_RANGES` 用「前缀 -> 序号区间」表达优先级。若前缀拼错（例如把 `VCS` 写成
    `VC`），`priority_of()` 只会安静地返回 P1，不会报错——P0 标签就少了一批，
    而且没人会注意到。所以在这里把前缀与覆盖情况都核一遍。
    """
    problems: list[str] = []
    known = {a["id"].split("-", 1)[0] for a in actions}
    for prefix in sorted(P0_RANGES):
        if prefix not in known:
            problems.append(
                "P0_RANGES 里的前缀 %s 在规格里没有对应条目，该区间永远选不中东西" % prefix
            )
    if not any(priority_of(a["id"]) == "P0" for a in actions):
        problems.append("优先级策略没有选中任何 P0 条目，P0 标签会形同虚设")
    return problems


def git_tracked_prd() -> list[str]:
    """交付物不应该被 git 跟踪时却跟踪了，会污染别人的工作区（见 ENG-011）。"""
    problems: list[str] = []
    try:
        output = subprocess.check_output(
            ["git", "ls-files", "dist"], cwd=REPO_ROOT, text=True, stderr=subprocess.DEVNULL
        ).strip()
    except Exception:  # noqa: BLE001 - 非 git 环境下跳过
        return problems
    if output:
        problems.append("dist/ 下的构建产物被 git 跟踪了（ENG-011）：%s" % output.replace("\n", ", "))
    return problems


def main() -> int:
    actions = load_actions()
    problems: list[str] = []
    problems += check_actions(actions)
    problems += check_prd_in_sync(actions)
    problems += check_issue_map(actions)
    problems += check_prd_module_table(actions)
    problems += check_priority_policy(actions)
    problems += git_tracked_prd()

    p0 = sum(1 for a in actions if priority_of(a["id"]) == "P0")
    print("规格条目：%d 条（P0 %d 条），功能域 %d 个"
          % (len(actions), p0, len({a["module"] for a in actions})))

    if problems:
        print("发现 %d 处规格问题：" % len(problems), file=sys.stderr)
        for problem in problems:
            print("  - " + problem, file=sys.stderr)
        return 1

    print("规格自检通过。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
