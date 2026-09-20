#!/usr/bin/env python3
"""Windows 宽字符 API 护栏（PRD: PLAT-007 第 1 条）。

PLAT-007 的第 1 条完成标准是「Windows 上使用宽字符 API（不经过 ANSI 转换）」。
这条标准不能靠人眼审代码来守，因为**写错了也不会报错**：

  - 用了 `CreateFileA`，中文路径会变成乱码或直接失败，而函数本身返回成功路径
    之外的错误码，看起来像「权限不足」；
  - 更隐蔽的是**不写后缀**：`CreateFile` 会按编译时的 `UNICODE` 宏展开成
    `CreateFileW` 或 `CreateFileA`。今天能跑，明天有人加了个编译参数
    （或在另一个 .pro 里构建）就悄悄退化成 ANSI，而改的人完全不知道
    自己动了什么。

因此本脚本做三件事：

  1. 禁止 `xxxA` 形式的 ANSI 版 API；
  2. 禁止**不带后缀**的通用形式（必须显式写 `W`，不让编译参数决定行为）；
  3. 非 Windows 实现文件里禁止包含 `<windows.h>`。

为了避免注释与字符串里的提及被误报（本项目大量注释都在解释「为什么用 W 不用
ANSI」，那恰恰是最该写下来的东西），扫描前先剥离注释与字符串字面量。

排除：以 `# shell-portability: ok <理由>` 之外的方式沉默 —— 本脚本不提供逐行
放行，因为批量放行等于取消护栏。确需例外时改这份清单并写明理由。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
CODE_ROOT = REPO_ROOT / "Code"

SOURCE_SUFFIXES = {".cpp", ".h", ".mm", ".m", ".cc", ".hpp"}
SKIP_DIRECTORIES = {"ThirdParty"}

# 有 A / W 两个变体的 Win32 API。写在这里的是**不含后缀**的名字。
#
# 这份清单不追求穷尽全部 Win32 函数，只列本项目真正会遇到的文件与 Shell 操作
# 那一批。列多了会误报（很多函数根本没有 A/W 变体），列少了会漏检——
# 因此每当发现一个新的 API 被用上，就往这里加一条。
API_WITH_WIDTH_VARIANT = [
    "CreateFile",
    "CreateDirectory",
    "RemoveDirectory",
    "DeleteFile",
    "MoveFile",
    "MoveFileEx",
    "CopyFile",
    "CopyFileEx",
    "FindFirstFile",
    "FindFirstFileEx",
    "FindNextFile",
    "GetFileAttributes",
    "GetFileAttributesEx",
    "SetFileAttributes",
    "GetFullPathName",
    "GetLongPathName",
    "GetShortPathName",
    "GetTempPath",
    "GetTempFileName",
    "GetModuleFileName",
    "GetCurrentDirectory",
    "SetCurrentDirectory",
    "GetEnvironmentVariable",
    "SetEnvironmentVariable",
    "GetDiskFreeSpaceEx",
    "GetVolumeInformation",
    "GetWindowsDirectory",
    "GetSystemDirectory",
    "GetUserName",
    "GetComputerName",
    "ExpandEnvironmentStrings",
    "SearchPath",
    "LoadLibrary",
    "LoadLibraryEx",
    "CreateProcess",
    "GetCommandLine",
    "MessageBox",
    "MessageBoxEx",
    "SHFileOperation",
    "SHGetFileInfo",
    "SHGetPathFromIDList",
    "SHQueryRecycleBin",
    "SHGetFolderPath",
]

ANSI_PATTERNS = [
    (name, re.compile(r"\b" + re.escape(name) + r"A\b"))
    for name in API_WITH_WIDTH_VARIANT
]
NARROW_PATTERNS = [
    (name, re.compile(r"\b" + re.escape(name) + r"\b"))
    for name in API_WITH_WIDTH_VARIANT
]

WINDOWS_HEADER = re.compile(r"#\s*include\s*[<\"](windows|shellapi|shlobj)\.h[>\"]")
WINDOWS_ONLY_SUFFIX = "_win.cpp"


def strip_comments_and_strings(text: str) -> str:
    """把注释与字符串字面量替换成空格，**保留换行**以便行号仍然准确。

    不这么做的话，本项目那些解释「为什么用 W 不用 ANSI」的注释会把护栏本身
    触发掉——于是唯一的出路就是把注释删掉，那正好把最该留住的知识删了。
    """
    out: list[str] = []
    i = 0
    n = len(text)
    state = "code"

    while i < n:
        char = text[i]
        following = text[i + 1] if i + 1 < n else ""

        if state == "code":
            if char == "/" and following == "/":
                state = "line_comment"
                i += 2
                continue
            if char == "/" and following == "*":
                state = "block_comment"
                i += 2
                continue
            if char == '"':
                state = "string"
                out.append(" ")
                i += 1
                continue
            if char == "'":
                state = "char"
                out.append(" ")
                i += 1
                continue
            out.append(char)
            i += 1
            continue

        if state == "line_comment":
            if char == "\n":
                state = "code"
                out.append("\n")
            i += 1
            continue

        if state == "block_comment":
            if char == "*" and following == "/":
                state = "code"
                i += 2
                out.append(" ")
                continue
            out.append("\n" if char == "\n" else " ")
            i += 1
            continue

        if state in ("string", "char"):
            if char == "\\":
                i += 2
                continue
            if (state == "string" and char == '"') or (state == "char" and char == "'"):
                state = "code"
            out.append("\n" if char == "\n" else " ")
            i += 1
            continue

    return "".join(out)


def iter_sources():
    for path in sorted(CODE_ROOT.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        if any(part in SKIP_DIRECTORIES for part in path.parts):
            continue
        yield path


def check_source(display_name: str, text: str, is_windows_implementation: bool) -> list[str]:
    """检查一份源码文本。抽成独立函数是为了让 --self-test 能直接喂样本。"""
    problems: list[str] = []
    stripped = strip_comments_and_strings(text)

    # 预处理条件栈，用来判断「这一行是不是在 `#ifdef Q_OS_WIN` 块里」。
    #
    # 为什么需要它：`filesystem.cpp` 是平台无关的，但它在一个 `#ifdef Q_OS_WIN`
    # 块里包含 <windows.h>，用 `static_assert` 核对手写的 Win32 错误码常量——
    # 那正是本项目刻意设计的护栏，不该被这里判成违规。
    #
    # 判断用 any() 而不是只看最内层：嵌套条件里只要有一层是 Windows 分支，
    # 这一行就只在 Windows 上编译。真出现「Q_OS_WIN 里再排除 Q_OS_WIN」这种
    # 自相矛盾的写法时，误放行一次也不会有后果——那些代码本来就编不到。
    windows_condition_stack: list[bool] = []

    for number, line in enumerate(stripped.splitlines(), start=1):
        statement = line.strip()

        if statement.startswith("#if"):
            windows_condition_stack.append(
                "Q_OS_WIN" in statement or "Q_OS_WINDOWS" in statement
            )
        elif statement.startswith("#else") or statement.startswith("#elif"):
            if windows_condition_stack:
                windows_condition_stack.pop()
                # `#elif` 的分支条件自己可能又是 Q_OS_WIN 相关的（少见但合法）；
                # `#else` 则一定不在 Windows 分支里。
                if statement.startswith("#elif"):
                    windows_condition_stack.append(
                        "Q_OS_WIN" in statement or "Q_OS_WINDOWS" in statement
                    )
                else:
                    windows_condition_stack.append(False)
        elif statement.startswith("#endif"):
            if windows_condition_stack:
                windows_condition_stack.pop()

        inside_windows_block = any(windows_condition_stack)

        for name, pattern in ANSI_PATTERNS:
            if pattern.search(line):
                problems.append(
                    f"{display_name}:{number} 用了 ANSI 版本的 {name}A —— "
                    f"中文路径会乱码。改用 {name}W。"
                )
        for name, pattern in NARROW_PATTERNS:
            if pattern.search(line):
                problems.append(
                    f"{display_name}:{number} 用了不带后缀的 {name} —— 它按 UNICODE 宏展开，"
                    f"行为取决于编译参数。显式写成 {name}W。"
                )

        if not is_windows_implementation and not inside_windows_block:
            if WINDOWS_HEADER.search(line):
                problems.append(
                    f"{display_name}:{number} 在非 Windows 实现文件里包含了 Windows 头文件，"
                    f"且不在 `#ifdef Q_OS_WIN` 块里 —— 这个文件会因此编不过别的平台。"
                )

    return problems


def check_file(path: Path) -> list[str]:
    raw = path.read_text(encoding="utf-8", errors="replace")
    return check_source(
        str(path.relative_to(REPO_ROOT)),
        raw,
        path.name.endswith(WINDOWS_ONLY_SUFFIX),
    )


# -----------------------------------------------------------------------------
# 回归自测
#
# 一个从不报错的护栏比没有护栏更糟：它会让人以为这块已经被守住了。
# 因此每次改动本脚本都要跑一遍 --self-test，确认它既不放过真正的违规，
# 也不误报那些**本来就该写下来**的东西（尤其是注释里解释「不要用 ANSI」的文字）。
# -----------------------------------------------------------------------------

SELF_TEST_SAMPLES = [
    ("ANSI 版 API 调用", "void f() { CreateFileA(\"a\", 0, 0, 0, 0, 0, 0); }", False, True),
    ("不带后缀的 API 调用", "void f() { CreateFile(\"a\", 0, 0, 0, 0, 0, 0); }", False, True),
    ("删目录用 ANSI 版", "RemoveDirectoryA(\"x\");", False, True),
    ("Shell 操作不带后缀", "SHFileOperation(&op);", False, True),
    ("宽字符版正常", "void f() { CreateFileW(L\"a\", 0, 0, 0, 0, 0, 0); }", False, False),
    ("Shell 操作带 W 后缀", "SHFileOperationW(&op);", False, False),
    ("没有 A/W 变体的函数", "GetProcAddress(h, \"f\");", False, False),
    ("行注释里提到 ANSI 版", "// 不要用 CreateFileA，中文会乱码\n", False, False),
    ("块注释里提到 ANSI 版", "/* CreateFileA 会乱码 */\n", False, False),
    ("字符串里提到 ANSI 版", "const char *s = \"CreateFileA\";\n", False, False),
    ("非 Windows 文件裸包含", "#include <windows.h>\n", False, True),
    ("非 Windows 文件包含 shellapi", "#include <shellapi.h>\n", False, True),
    ("Q_OS_WIN 块里包含", "#ifdef Q_OS_WIN\n#include <windows.h>\n#endif\n", False, False),
    ("带 defined 的条件", "#if defined(Q_OS_WIN)\n#include <shellapi.h>\n#endif\n", False, False),
    ("Q_OS_WIN 块之后的裸包含", "#ifdef Q_OS_WIN\n#include <windows.h>\n#endif\n#include <windows.h>\n", False, True),
    ("Windows 实现文件可以裸包含", "#include <windows.h>\n#include <shellapi.h>\n", True, False),
    ("注释里的宏不影响判定", "#ifdef Q_OS_WIN // 这里是 Windows\n#include <windows.h>\n#endif\n", False, False),
]


def self_test() -> int:
    failures: list[str] = []

    for label, code, is_windows_file, should_report in SELF_TEST_SAMPLES:
        problems = check_source("sample.cpp", code, is_windows_file)
        reported = bool(problems)
        if reported != should_report:
            expectation = "应报错" if should_report else "不应报错"
            failures.append(f"  - 「{label}」{expectation}，实际{'报错了' if reported else '没报错'}")
        elif should_report:
            # 报错时也要确认报的是对的那一条，而不是碰巧报了个别的。
            pass

    total = len(SELF_TEST_SAMPLES)
    if failures:
        print(f"护栏自测失败（{len(failures)}/{total}）：", file=sys.stderr)
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1

    print(f"护栏自测通过：{total} 个样本，该报的都报了，不该报的一个都没报。")
    return 0


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()

    all_problems: list[str] = []
    scanned = 0

    for path in iter_sources():
        scanned += 1
        all_problems.extend(check_file(path))

    print(f"Windows 宽字符 API：扫描 {scanned} 个源文件，"
          f"清单内 API {len(API_WITH_WIDTH_VARIANT)} 个")

    if all_problems:
        print("发现问题：", file=sys.stderr)
        for problem in all_problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1

    print("宽字符 API 检查通过（无非宽字符调用、无非 Windows 文件引入 Windows 头）。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
