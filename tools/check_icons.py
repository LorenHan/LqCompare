#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""图标与命令注册表一致性校验（PRD: ENG-009、UI-025）。

校验三件事：
1. Code/Pictures/Pictures.qrc 中声明的每个 SVG 文件都真实存在；
2. 代码里出现的 ":/Pictures/xxx.svg" 引用都能在 .qrc 中找到；
3. 没有「.qrc 里存在但代码从不引用」的孤立图标（白名单除外）。

第 3 条是刻意加的：图标集最容易在删除按钮后留下垃圾文件，
而垃圾文件又会诱导下一个人「反正有现成的就直接用」，最终形成第二套视觉语言。

用法
----
    python3 tools/check_icons.py
"""

from __future__ import annotations

import argparse
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CODE_ROOT = os.path.join(REPO_ROOT, "Code")
PICTURES_DIR = os.path.join(CODE_ROOT, "Pictures")
QRC_PATH = os.path.join(PICTURES_DIR, "Pictures.qrc")

QRC_FILE_PATTERN = re.compile(r"<file>([^<]+)</file>")
# 两种引用写法都要认：
#   1. 完整资源路径 “:/Pictures/ribbon_open.svg”
#   2. 只写文件名 “ribbon_open.svg”（由 App 的 icon() 辅助函数补前缀）
# 只认第一种会把大量合法引用误判成「未使用」，护栏就会变成噪声，最终被忽略。
REFERENCE_PATTERN = re.compile(r':/Pictures/([A-Za-z0-9_\-]+\.svg)')
BARE_NAME_PATTERN = re.compile(r'"([A-Za-z0-9_\-]+\.svg)"')

# 允许「已提交但暂未被引用」的图标：通常是随规格一起先落地的资源。
UNUSED_WHITELIST: set[str] = set()

SOURCE_SUFFIXES = (".h", ".hpp", ".cpp", ".cc", ".cxx")


def qrc_entries() -> list[str]:
    if not os.path.exists(QRC_PATH):
        return []
    with open(QRC_PATH, encoding="utf-8") as handle:
        return QRC_FILE_PATTERN.findall(handle.read())


def referenced_icons() -> set[str]:
    known = set(qrc_entries())
    used: set[str] = set()
    for root, _dirs, files in os.walk(CODE_ROOT):
        for name in files:
            if not name.endswith(SOURCE_SUFFIXES):
                continue
            path = os.path.join(root, name)
            try:
                with open(path, encoding="utf-8") as handle:
                    content = handle.read()
            except OSError:
                continue
            used.update(REFERENCE_PATTERN.findall(content))
            for candidate in BARE_NAME_PATTERN.findall(content):
                if candidate in known:
                    used.add(candidate)
    return used


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="列出全部图标与引用状态")
    args = parser.parse_args()

    entries = qrc_entries()
    if not entries:
        print("Pictures.qrc 中没有图标条目：%s" % QRC_PATH, file=sys.stderr)
        return 2

    problems: list[str] = []

    for name in entries:
        if not os.path.exists(os.path.join(PICTURES_DIR, name)):
            problems.append("Pictures.qrc 声明了 %s，但文件不存在" % name)

    used = referenced_icons()
    for name in sorted(used):
        if name not in entries:
            problems.append("代码引用了 :/Pictures/%s，但 Pictures.qrc 未声明" % name)

    if args.list:
        print("%-32s %s" % ("图标", "引用状态"))
        for name in sorted(entries):
            print("%-32s %s" % (name, "已引用" if name in used else "未被引用"))

    for name in sorted(entries):
        if name not in used and name not in UNUSED_WHITELIST:
            problems.append("图标 %s 已提交但从未被引用（删除它，或加入白名单并说明原因）" % name)

    if problems:
        print("发现 %d 处图标问题：" % len(problems), file=sys.stderr)
        for problem in problems:
            print("  - " + problem, file=sys.stderr)
        return 1

    print("图标检查通过：%d 个图标，声明、引用、文件三者一致。" % len(entries))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
