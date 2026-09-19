#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""分层依赖校验（PRD: ENG-001）。

依赖方向铁律：App -> Views -> Services。Services 层**不得** include 任何
Views/ 或 App/ 的头文件。这条规则是「业务逻辑可脱离界面测试」的前提，
一旦被破坏，服务层的单元测试会莫名其妙地需要链接整个界面。

为什么用脚本而不是靠代码审查：这类违规是「加一行 include」级别的改动，
评审时极易漏掉，而它带来的后果（服务层不可测）要过很久才暴露。

用法
----
    python3 tools/check_layering.py            # 检查，发现违规时退出码非零
    python3 tools/check_layering.py -v         # 打印每个文件的分层判定
"""

from __future__ import annotations

import argparse
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CODE_ROOT = os.path.join(REPO_ROOT, "Code")

# 层次顺序：数字越小越上层。依赖只允许从上层指向下层。
LAYERS = {
    "App": 0,
    "Views": 1,
    "Services": 2,
    "ThirdParty": 3,
}

# 允许的跨层 include 目标（按源层）。
ALLOWED_TARGETS = {
    "App": {"App", "Views", "Services", "ThirdParty"},
    "Views": {"Views", "Services", "ThirdParty"},
    "Services": {"Services", "ThirdParty"},
    "ThirdParty": {"ThirdParty"},
}

INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)

SOURCE_SUFFIXES = (".h", ".hpp", ".cpp", ".cc", ".cxx")


def layer_of(path: str) -> str | None:
    relative = os.path.relpath(path, CODE_ROOT)
    head = relative.split(os.sep, 1)[0]
    return head if head in LAYERS else None


def owner_of_header(include: str) -> str | None:
    """把一个 include 路径映射到它所属的层。

    只看第一个路径段：模块之间的 include 都写成 "commandregistry.h" 这类
    扁平形式（靠 INCLUDEPATH 解析），因此需要按文件名反查它属于哪个目录。
    """
    normalized = include.replace("\\", "/")
    segments = [s for s in normalized.split("/") if s not in ("", ".")]
    if not segments:
        return None
    if segments[0] in LAYERS:
        return segments[0]
    return None


def build_header_index() -> dict[str, str]:
    """建立 {头文件名: 所属层} 索引，用于解析扁平 include。"""
    index: dict[str, str] = {}
    for root, _dirs, files in os.walk(CODE_ROOT):
        layer = layer_of(root)
        if not layer:
            continue
        for name in files:
            if name.endswith((".h", ".hpp")):
                index.setdefault(name, layer)
    return index


def check(verbose: bool) -> list[str]:
    header_index = build_header_index()
    problems: list[str] = []

    for root, _dirs, files in os.walk(CODE_ROOT):
        layer = layer_of(root)
        if not layer:
            continue
        for name in sorted(files):
            if not name.endswith(SOURCE_SUFFIXES):
                continue
            path = os.path.join(root, name)
            try:
                with open(path, encoding="utf-8") as handle:
                    content = handle.read()
            except OSError as exc:
                problems.append("%s：无法读取（%s）" % (path, exc))
                continue

            allowed = ALLOWED_TARGETS[layer]
            for include in INCLUDE_PATTERN.findall(content):
                target = owner_of_header(include) or header_index.get(
                    os.path.basename(include).replace("\\", "/")
                )
                if target is None or target == layer:
                    continue
                if target not in allowed:
                    problems.append(
                        "%s（层 %s）include 了 %s 层的 %s —— 依赖方向被破坏"
                        % (os.path.relpath(path, REPO_ROOT), layer, target, include)
                    )
            if verbose:
                print("  %-60s 层=%s" % (os.path.relpath(path, REPO_ROOT), layer))

    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-v", "--verbose", action="store_true", help="打印每个文件的层判定")
    args = parser.parse_args()

    if not os.path.isdir(CODE_ROOT):
        print("找不到源码目录：%s" % CODE_ROOT, file=sys.stderr)
        return 2

    problems = check(args.verbose)
    if problems:
        print("发现 %d 处分层违规：" % len(problems), file=sys.stderr)
        for problem in problems:
            print("  - " + problem, file=sys.stderr)
        return 1

    print("分层检查通过：App -> Views -> Services，Services 未反向依赖界面。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
