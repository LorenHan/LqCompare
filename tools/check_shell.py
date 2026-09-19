#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Shell 脚本可移植性检查（PRD: ENG-003、PLAT-001）。

为什么需要这个
--------------
仓库里的脚本要在三个环境跑：macOS（自带 bash 3.2 + BSD 工具链）、
Windows Git Bash、以及 CI 的 Ubuntu（bash 5 + GNU 工具链）。
CI 只跑 Linux，所以**在 CI 上全绿不代表在 macOS 上能跑**——
bash 4 独有的内建和 GNU 工具扩展在 Ubuntu 上一切正常，到了 macOS 直接崩。

这类缺陷已经真实发生过两次，都是靠人工发现：

1. `mapfile` 是 bash 4.0 内建，macOS 的命令行会报 `mapfile: command not found`。
2. BSD sed 不支持 GNU 的 `\\+`，于是 `sed -n 's/\\([0-9]\\+\\) passed/\\1/p'` 静默匹配不上，
   解析结果全是 0 —— 每行显示「14 passed」而合计是 0，看起来还挺正常。

第 2 种是「静默失败」，比第一种危险得多：它不会报错，只会给出错误的数字。
所以宁可在这里写死一张规则表，也不指望下次还能有人碰巧发现。

检查方式与边界
--------------
逐行正则匹配，跳过注释行。这是启发式检查，不是完整的 shell 解析器：

- 会把字符串字面量里的内容也算进去（误报方向偏保守，可接受）。
- 无法检测「空数组在 set -u 下取值」这类运行期语义问题——那需要真的用
  bash 3.2 跑一遍。
- 确实需要某个非可移植写法时，在行尾加 `# shell-portability: ok <理由>` 放行。
  要求写理由，是为了让放行成为一次明确的决策而不是随手消音。

用法
----
    python3 tools/check_shell.py
"""

from __future__ import annotations

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 行尾放行标记：`# shell-portability: ok <理由>`
ALLOW_MARKER = re.compile(r"#\s*shell-portability:\s*ok\s+\S+")

# 规则表：(正则, 说明)。说明要写清「为什么不行」与「该怎么写」。
RULES = [
    (re.compile(r"\bmapfile\b"),
     "mapfile 是 bash 4.0 内建，macOS 自带 bash 3.2 没有；改用 while read 循环"),
    (re.compile(r"\breadarray\b"),
     "readarray 是 bash 4.0 内建；改用 while read 循环"),
    (re.compile(r"\bdeclare\s+-A\b"),
     "关联数组是 bash 4.0 特性；改用普通数组加前缀、或改用别的实现方式"),
    (re.compile(r"\blocal\s+-n\b"),
     "nameref 是 bash 4.3 特性；改用别的方式传递引用"),
    (re.compile(r"\$\{[A-Za-z_][A-Za-z0-9_]*(?:\[[^\]]*\])?(?:\^\^|,,)"),
     "${var^^} / ${var,,} 大小写变换是 bash 4.0 特性；改用 tr"),
    (re.compile(r"&>>"),
     "&>> 重定向是 bash 4.0 特性；改用 >> file 2>&1"),
    (re.compile(r"\bsed\b[^|;]*\\\+"),
     "BSD sed 不支持 \\+；改用 [0-9][0-9]* 或 \\{1,\\}（这类是静默失败，最危险）"),
    (re.compile(r"\bsed\b[^|;]*\\\?"),
     "BSD sed 不支持 \\?；改用 \\{0,1\\}"),
    (re.compile(r"\bgrep\b[^|;]*\s-P\b"),
     "BSD grep 没有 -P；改用 grep -E"),
    (re.compile(r"\bxargs\b[^|;]*\s-r\b"),
     "xargs -r 是 GNU 扩展；BSD/macOS 上会报非法选项"),
    (re.compile(r"\bdate\b[^|;]*\s-d\b"),
     "date -d 是 GNU 扩展；macOS 上改用 date -j -f"),
    (re.compile(r"\bstat\b[^|;]*\s-c\b"),
     "stat -c 是 GNU 扩展；macOS 上改用 stat -f"),
    (re.compile(r"\bsed\b[^|;]*\s-i\s+['\"]?[sS]/"),
     "sed -i 在 BSD 上必须带备份后缀（sed -i ''），不能直接跟表达式"),
]

SHEBANG = re.compile(r"^#!.*\b(?:bash|sh|zsh)\b")


def shell_scripts() -> list:
    """找出仓库内的 shell 脚本：.sh 结尾，或首行带 shell shebang。"""
    found = []
    for root, dirs, files in os.walk(REPO_ROOT):
        dirs[:] = [d for d in dirs
                   if d not in (".git", "dist", "_references", "_build-lqcompare")]
        for name in files:
            path = os.path.join(root, name)
            rel = os.path.relpath(path, REPO_ROOT)
            if name.endswith(".sh"):
                found.append(rel)
                continue
            try:
                with open(path, "rb") as handle:
                    head = handle.read(120)
            except OSError:
                continue
            if b"\x00" in head:
                continue
            if SHEBANG.match(head.decode("utf-8", "replace").splitlines()[0] if head else ""):
                found.append(rel)
    return sorted(found)


def check_file(rel_path: str) -> list:
    problems = []
    path = os.path.join(REPO_ROOT, rel_path)
    try:
        with open(path, encoding="utf-8") as handle:
            lines = handle.read().splitlines()
    except OSError as exc:
        return ["%s 无法读取：%s" % (rel_path, exc)]

    for number, line in enumerate(lines, 1):
        stripped = line.lstrip()
        # 注释行不参与检查：脚本里解释「哪些写法不能用」的注释本身会命中规则。
        if stripped.startswith("#"):
            continue
        if ALLOW_MARKER.search(line):
            continue
        for pattern, reason in RULES:
            if pattern.search(line):
                problems.append("%s:%d %s\n      %s" % (rel_path, number, reason, stripped[:110]))
                break  # 一行只报一次，避免同一行刷出多条
    return problems


def main() -> int:
    scripts = shell_scripts()
    if not scripts:
        print("没有找到 shell 脚本。")
        return 0

    problems = []
    for rel in scripts:
        problems += check_file(rel)

    print("Shell 脚本：%d 个（%s）" % (len(scripts), "、".join(scripts)))
    if problems:
        print("发现 %d 处可移植性问题：" % len(problems), file=sys.stderr)
        for problem in problems:
            print("  - " + problem, file=sys.stderr)
        print("\n确有必要的写法，在行尾加 `# shell-portability: ok <理由>` 放行。",
              file=sys.stderr)
        return 1
    print("shell 可移植性检查通过。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
