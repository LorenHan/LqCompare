#!/usr/bin/env python3
"""从现有 ribbon_about.svg 重新生成 Windows 可执行文件图标。

仓库中的 ICO 只转换格式，不引入另一套图标设计。SVG 仍是唯一图形来源，
本工具不会写入或修改它。所有尺寸均直接从矢量图渲染，包括任务栏与
资源管理器使用的小尺寸。Qt 文档要求 RC_ICONS 指向 ICO 文件：
https://doc.qt.io/archives/qt-5.15/appicon.html#setting-the-application-icon-on-windows

仅维护图标时需要 Inkscape 1.4 和 Pillow 12.3.0（仓库中 ICO 的生成版本），
正常构建应用不需要这两个依赖，本工具也不会自动安装它们。
其他渲染器或库版本可能改变抗锯齿、PNG 压缩结果；重新生成并提交 ICO 前
必须审查这些变化，不能假定跨版本生成的字节完全一致。

    python3 tools/generate_windows_icon.py              # 重新生成 ICO
    python3 tools/generate_windows_icon.py --check      # 解码并逐字节比较
    python3 tools/generate_windows_icon.py --self-test  # 确定性与损坏输入自测

--check 和 --self-test 不修改仓库。中间 PNG 与 Inkscape 用户配置均放在
临时目录中。ICO 只含图像数据，不含时间戳、源文件路径、颜色配置或其他
PNG 元数据。
"""

from __future__ import annotations

import argparse
import io
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "Code/Pictures/ribbon_about.svg"
OUTPUT = ROOT / "Code/Pictures/lqcompare.ico"
SIZES = (16, 24, 32, 48, 64, 128, 256)


def validate(data: bytes) -> None:
    """检查 ICO 目录，并逐帧独立解码其中的图像。"""
    if len(data) < 6 or struct.unpack_from("<HHH", data) != (0, 1, len(SIZES)):
        raise ValueError("Expected a Windows ICO containing all seven sizes")
    offset = 6 + 16 * len(SIZES)
    if len(data) < offset:
        raise ValueError("Truncated ICO directory")
    for index, size in enumerate(SIZES):
        width, height, colors, reserved, planes, depth, length, start = (
            struct.unpack_from("<BBBBHHII", data, 6 + 16 * index)
        )
        if ((width or 256, height or 256) != (size, size)
                or colors != 0 or reserved != 0 or planes not in (0, 1)
                or depth != 32 or start != offset or length == 0
                or start + length > len(data)):
            raise ValueError(f"Invalid {size}px ICO directory entry")
        frame_data = data[start:start + length]
        with Image.open(io.BytesIO(frame_data)) as frame:
            frame.load()
            if frame.format != "PNG" or frame.mode != "RGBA" or frame.size != (size, size):
                raise ValueError(f"Invalid {size}px embedded PNG")
            if frame.getchannel("A").getextrema() != (0, 255):
                raise ValueError(f"Expected transparent edges and opaque artwork at {size}px")
        offset += length
    if offset != len(data):
        raise ValueError("Unexpected trailing ICO data")
    with Image.open(io.BytesIO(data)) as icon:
        if icon.format != "ICO" or icon.ico.sizes() != {(n, n) for n in SIZES}:
            raise ValueError("ICO decoder did not find every expected size")
        for size in SIZES:
            icon.ico.getimage((size, size)).load()


def render() -> bytes:
    frames = []
    with tempfile.TemporaryDirectory(prefix="lqcompare-icon-") as directory:
        temporary = Path(directory)
        environment = os.environ.copy()
        environment["INKSCAPE_PROFILE_DIR"] = str(temporary / "profile")
        for size in SIZES:
            png = temporary / f"icon-{size}.png"
            subprocess.run([
                "inkscape", str(SOURCE), "--export-type=png", "--export-area-page",
                f"--export-width={size}", f"--export-height={size}",
                "--export-background-opacity=0", f"--export-filename={png}",
            ], env=environment, check=True, capture_output=True)
            with Image.open(png) as image:
                frame = image.convert("RGBA")
                frame.info.clear()
                frames.append(frame)
    output = io.BytesIO()
    frames[-1].save(output, format="ICO", sizes=[(n, n) for n in SIZES],
                    append_images=frames[:-1])
    data = output.getvalue()
    validate(data)
    return data


def self_test(data: bytes) -> None:
    if data != render():
        raise ValueError("Repeated SVG rendering did not produce identical ICO bytes")
    bad_offset = bytearray(data)
    struct.pack_into("<I", bad_offset, 18, len(data) + 1)
    bad_depth = bytearray(data)
    struct.pack_into("<H", bad_depth, 12, 8)
    cases = (b"", SOURCE.read_bytes(), data[:20], data[:-1],
             data + b"unexpected", bytes(bad_offset), bytes(bad_depth))
    for index, broken in enumerate(cases):
        try:
            validate(broken)
        except (ValueError, OSError):
            continue
        raise ValueError(f"Invalid-input self-test {index} was incorrectly accepted")
    print(f"Deterministic regeneration and {len(cases)} rejection tests passed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    source_before = SOURCE.read_bytes()
    data = render()
    if args.self_test:
        self_test(data)
    elif args.check:
        existing = OUTPUT.read_bytes()
        validate(existing)
        if existing != data:
            raise ValueError("ICO differs from its source; review and regenerate it")
        print("Windows icon matches its SVG source: seven decoded RGBA sizes")
    else:
        # 输出路径固定，不接受可能覆盖 SVG 源文件的自定义目标。
        temporary = OUTPUT.with_suffix(".ico.tmp")
        temporary.write_bytes(data)
        temporary.replace(OUTPUT)
        print(f"Wrote {OUTPUT.relative_to(ROOT)} ({len(data)} bytes)")
    if SOURCE.read_bytes() != source_before:
        raise ValueError("Source SVG changed while generating the ICO")


if __name__ == "__main__":
    main()
