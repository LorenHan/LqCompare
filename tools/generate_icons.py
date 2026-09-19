#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 LqCompare 的 Ribbon 图标集。

为什么用脚本生成而不是手绘一堆 SVG
------------------------------------
PRD UI-025 要求「同一语义只用一个图标，且不引入第二套视觉语言」。
把绘制语言固化在这里，新增图标时只能沿用同一套描边宽度、圆角与配色，
比人工复制粘贴更不容易走样。生成结果提交进仓库（Code/Pictures/*.svg），
构建期由 ENG-009 校验「命令注册表声明的图标都存在」。

绘制语言（与 Ailecium 一致）
---------------------------
- 画布 24×24，有效图形限制在 2..22 之间
- 主描边 #0F6CBD，宽度 1.7，圆头圆角
- 填充强调色 #EAF3FC
- 不使用渐变、阴影、多色（保证深色主题下只需替换描边色即可复用）

用法
----
    python3 tools/generate_icons.py            # 生成缺失的图标
    python3 tools/generate_icons.py --force    # 全部重新生成
    python3 tools/generate_icons.py --list     # 列出全部图标名
"""

from __future__ import annotations

import argparse
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT_DIR = os.path.join(REPO_ROOT, "Code", "Pictures")

STROKE = "#0F6CBD"
FILL = "#EAF3FC"

HEADER = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24">\n'
    '  <g fill="none" stroke="{stroke}" stroke-width="1.7" '
    'stroke-linecap="round" stroke-linejoin="round">\n'
).format(stroke=STROKE)

FOOTER = "  </g>\n</svg>\n"

# 复用形状 -------------------------------------------------------------------
FOLDER = ('    <path d="M3 7.5A1.5 1.5 0 0 1 4.5 6h4l2 2.5h8.5A1.5 1.5 0 0 1 20.5 10v7'
          'A1.5 1.5 0 0 1 19 18.5H4.5A1.5 1.5 0 0 1 3 17z" fill="{fill}"/>\n').format(fill=FILL)
DOC = ('    <path d="M6 3h7.5L18 7.5V21H6z" fill="{fill}"/>\n'
       '    <path d="M13.5 3v4.5H18"/>\n').format(fill=FILL)

ICONS: dict[str, str] = {
    # --- 文件与文件操作 -----------------------------------------------------
    "ribbon_open": FOLDER + '    <path d="M9 13.5l3-3 3 3"/>\n    <path d="M12 10.5V17"/>\n',
    "ribbon_save": '    <path d="M4 4h12l4 4v12H4z" fill="' + FILL + '"/>\n'
                   '    <path d="M8 4v5h7V4"/>\n    <path d="M8 14h8v6H8z"/>\n',
    "ribbon_saveas": '    <path d="M4 4h11l3 3v6" fill="' + FILL + '"/>\n'
                     '    <path d="M4 4v16h7"/>\n    <path d="M7 4v4h6V4"/>\n'
                     '    <path d="M15 21v-5l5-5 3 3-5 5z"/>\n',
    "ribbon_reload": '    <path d="M20 12a8 8 0 1 1-2.6-5.9"/>\n'
                     '    <path d="M20 4v4h-4"/>\n',
    "ribbon_edit": '    <path d="M4 20l1-4L16 5l3 3L8 19z" fill="' + FILL + '"/>\n'
                   '    <path d="M14 7l3 3"/>\n',
    "ribbon_exit": '    <path d="M11 4H5v16h6"/>\n    <path d="M15 8l4 4-4 4"/>\n'
                   '    <path d="M19 12H9"/>\n',
    "ribbon_new": DOC + '    <path d="M12 11v7"/>\n    <path d="M8.5 14.5h7"/>\n',
    "ribbon_close": '    <path d="M6 6l12 12"/>\n    <path d="M18 6L6 18"/>\n',
    "ribbon_recent": '    <circle cx="12" cy="12" r="8"/>\n'
                     '    <path d="M12 7.5V12l3 2"/>\n',
    "ribbon_browse": '    <path d="M4 5h9"/>\n    <path d="M4 10h6"/>\n    <path d="M4 15h6"/>\n'
                     '    <circle cx="16" cy="15" r="3.5"/>\n    <path d="M18.6 17.6L21 20"/>\n',

    # --- 导航 ---------------------------------------------------------------
    "ribbon_prev": '    <path d="M6 14.5l6-6 6 6"/>\n',
    "ribbon_next": '    <path d="M6 9.5l6 6 6-6"/>\n',
    "ribbon_prev_conflict": '    <path d="M5 12.5l5-5 5 5"/>\n'
                            '    <path d="M18 7v5"/>\n    <circle cx="18" cy="15.5" r="0.9"/>\n',
    "ribbon_next_conflict": '    <path d="M5 11.5l5 5 5-5"/>\n'
                            '    <path d="M18 7v5"/>\n    <circle cx="18" cy="15.5" r="0.9"/>\n',
    "ribbon_gotoline": '    <path d="M4 6h9"/>\n    <path d="M4 12h5"/>\n    <path d="M4 18h9"/>\n'
                       '    <path d="M17 8v8"/>\n    <path d="M14.5 13.5L17 16l2.5-2.5"/>\n',

    # --- 编辑 ---------------------------------------------------------------
    "ribbon_undo": '    <path d="M4 11a8 8 0 1 1 2.6 5.9" />\n'
                   '    <path d="M4 6v5h5"/>\n',
    "ribbon_redo": '    <path d="M20 11a8 8 0 1 0-2.6 5.9"/>\n'
                   '    <path d="M20 6v5h-5"/>\n',
    "ribbon_search": '    <circle cx="11" cy="11" r="6" fill="' + FILL + '"/>\n'
                     '    <path d="M15.5 15.5L20 20"/>\n',

    # --- 视图与界面 ---------------------------------------------------------
    "ribbon_output": '    <path d="M3 5h18v14H3z" fill="' + FILL + '"/>\n    <path d="M3 10h18"/>\n'
                     '    <path d="M6 14h6"/>\n    <path d="M6 16.5h9"/>\n',
    "ribbon_minimize": '    <path d="M3 5h18v5H3z" fill="' + FILL + '"/>\n'
                       '    <path d="M3 14h18"/>\n    <path d="M8 19l4-3.5 4 3.5"/>\n',
    "ribbon_options": '    <circle cx="12" cy="12" r="3" fill="' + FILL + '"/>\n'
                      '    <path d="M12 3v3"/>\n    <path d="M12 18v3"/>\n'
                      '    <path d="M3 12h3"/>\n    <path d="M18 12h3"/>\n'
                      '    <path d="M5.6 5.6l2.1 2.1"/>\n    <path d="M16.3 16.3l2.1 2.1"/>\n'
                      '    <path d="M18.4 5.6l-2.1 2.1"/>\n    <path d="M7.7 16.3l-2.1 2.1"/>\n',
    "ribbon_about": '    <circle cx="12" cy="12" r="8.5" fill="' + FILL + '"/>\n'
                    '    <path d="M12 11v5.5"/>\n    <circle cx="12" cy="7.8" r="0.9"/>\n',
    "ribbon_validate": '    <path d="M12 3l7 3v6c0 4-3 7-7 9-4-2-7-5-7-9V6z" fill="' + FILL + '"/>\n'
                       '    <path d="M8.5 12l2.5 2.5 4.5-5"/>\n',
    "ribbon_reference": '    <path d="M4 5.5A1.5 1.5 0 0 1 5.5 4H11v16H5.5A1.5 1.5 0 0 1 4 18.5z"'
                        ' fill="' + FILL + '"/>\n'
                        '    <path d="M20 5.5A1.5 1.5 0 0 0 18.5 4H13v16h5.5A1.5 1.5 0 0 0 20 18.5z"/>\n',
    "ribbon_manual": '    <path d="M5 4h11a2 2 0 0 1 2 2v14H7a2 2 0 0 1-2-2z" fill="' + FILL + '"/>\n'
                     '    <path d="M18 20H7a2 2 0 0 1-2-2"/>\n    <path d="M9 4v7l2-1.8L13 11V4"/>\n',
    "ribbon_log": DOC + '    <path d="M9 12h6"/>\n    <path d="M9 15.5h6"/>\n',
    "ribbon_reset": '    <path d="M4 12a8 8 0 1 1 2.6 5.9"/>\n    <path d="M4 7v5h5"/>\n'
                    '    <path d="M9.5 13.5h5"/>\n',
}


def render(name: str, body: str) -> str:
    return HEADER + body + FOOTER


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="已存在的图标也重新生成")
    parser.add_argument("--list", action="store_true", help="只列出图标名")
    args = parser.parse_args()

    if args.list:
        for name in sorted(ICONS):
            print(name)
        return 0

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    written = 0
    skipped = 0
    for name in sorted(ICONS):
        path = os.path.join(OUTPUT_DIR, name + ".svg")
        if os.path.exists(path) and not args.force:
            skipped += 1
            continue
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(render(name, ICONS[name]))
        written += 1

    print("图标目录：%s" % os.path.relpath(OUTPUT_DIR, REPO_ROOT))
    print("生成 %d 个，跳过 %d 个（已存在，用 --force 覆盖）" % (written, skipped))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
