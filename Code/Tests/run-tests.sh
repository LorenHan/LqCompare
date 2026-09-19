#!/usr/bin/env bash
#
# 测试运行器（PRD: ENG-003）
#
# 职责：找出 Code/Tests/*/*.pro，逐个构建并用 offscreen 平台运行，
# 汇总通过/失败/跳过统计，并在失败时给出可直接复制的复现命令。
#
# 用法：
#   Code/Tests/run-tests.sh                 # 全部套件
#   Code/Tests/run-tests.sh CommandRegistry # 只跑名字匹配的套件
#   QMAKE=/path/to/qmake Code/Tests/run-tests.sh
#
# 兼容性约束：本脚本要同时在 macOS 与 Windows（Git Bash）上跑，因此不得使用
# bash 4 语法，也不得使用 GNU 工具扩展。具体踩过的坑：
#   1. `mapfile` 是 bash 4.0 才有的内建，macOS 自带 bash 3.2 上直接报
#      "mapfile: command not found"。
#   2. 在 `set -u` 下对空数组取值（`${arr[@]}`）会被当成未定义变量而报错。
#   3. BSD sed 不支持 GNU 的 `\+`；写成 `\([0-9]\+\)` 会静默匹配不上，
#      导致每行显示「14 passed」但合计是 0 —— 看起来还挺正常，最难发现。
# 下面用可移植写法规避这三点。
set -uo pipefail

CODE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "${CODE_ROOT}/.." && pwd)"
BUILD_ROOT="${LQCOMPARE_TEST_BUILD_ROOT:-${REPO_ROOT}/_test-build}"
FILTER="${1:-}"

# 优先用环境变量指定的 qmake；否则在常见位置里找。
if [[ -z "${QMAKE:-}" ]]; then
    for candidate in \
        "$HOME/Qt/5.15.2/clang_64/bin/qmake" \
        "/c/Qt/5.15.2/mingw81_32/bin/qmake" \
        "$(command -v qmake 2>/dev/null || true)"; do
        if [[ -n "${candidate}" && -x "${candidate}" ]]; then
            QMAKE="${candidate}"
            break
        fi
    done
fi

if [[ -z "${QMAKE:-}" || ! -x "${QMAKE}" ]]; then
    echo "找不到 qmake。请设置 QMAKE=/path/to/qmake。" >&2
    exit 2
fi

# 测试必须能跑在没有显示器的机器上。
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

# 不用 mapfile/readarray：bash 4.0 才有，macOS 自带 bash 3.2 上会直接失败。
PROJECTS=()
while IFS= read -r project; do
    PROJECTS+=("${project}")
done < <(find "${CODE_ROOT}/Tests" -mindepth 2 -maxdepth 2 -name '*.pro' | sort)

if [[ "${#PROJECTS[@]:-0}" -eq 0 ]]; then
    echo "没有找到测试工程（${CODE_ROOT}/Tests/*/*.pro）。" >&2
    exit 2
fi

total_pass=0
total_fail=0
total_skip=0
# 用字符串而不是数组记录失败套件：空数组在 `set -u` 下的取值会报未定义变量。
failed_suites=""
ran_suites=0

for project in "${PROJECTS[@]}"; do
    suite="$(basename "$(dirname "${project}")")"
    if [[ -n "${FILTER}" && "${suite}" != *"${FILTER}"* ]]; then
        continue
    fi
    ran_suites=$((ran_suites + 1))

    build_dir="${BUILD_ROOT}/${suite}"
    mkdir -p "${build_dir}"

    echo "──────────────────────────────────────────────────────────────"
    echo "▶ ${suite}"
    if ! (cd "${build_dir}" && "${QMAKE}" "${project}" >/dev/null 2>&1); then
        echo "  ✗ qmake 失败"
        failed_suites="${failed_suites}${suite} "
        total_fail=$((total_fail + 1))
        continue
    fi
    if ! (cd "${build_dir}" && make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null 2>&1); then
        echo "  ✗ 构建失败"
        echo "    复现：cd ${build_dir} && make"
        failed_suites="${failed_suites}${suite} "
        total_fail=$((total_fail + 1))
        continue
    fi

    # 套件可执行文件由各 .pro 的 DESTDIR 决定，统一约定为 <build_dir>/bin。
    binary="${build_dir}/bin/$(basename "${project}" .pro)"
    if [[ ! -x "${binary}" ]]; then
        binary="$(find "${build_dir}" -maxdepth 2 -type f -perm -u+x -name 'tst_*' | head -n 1)"
    fi
    if [[ -z "${binary}" || ! -x "${binary}" ]]; then
        echo "  ✗ 找不到套件可执行文件"
        failed_suites="${failed_suites}${suite} "
        total_fail=$((total_fail + 1))
        continue
    fi

    output="$("${binary}" -o -,txt 2>&1)"
    status=$?
    summary="$(printf '%s\n' "${output}" | grep -E '^Totals:' | tail -n 1)"
    printf '%s\n' "${output}" | grep -E '^(FAIL!|PASS.*skipped)' | head -n 20

    if [[ -n "${summary}" ]]; then
        echo "  ${summary}"
        # 注意：macOS 用的是 BSD sed，不支持 GNU 的 `\+`。
        # 写成 `\([0-9]\+\)` 会静默匹配不上——解析结果全为 0，
        # 于是「合计 0 passed」却在每行显示「14 passed」，看起来还挺正常。
        pass=$(printf '%s' "${summary}" | sed -n 's/.*Totals: *\([0-9][0-9]*\) passed.*/\1/p')
        fail=$(printf '%s' "${summary}" | sed -n 's/.*, *\([0-9][0-9]*\) failed.*/\1/p')
        skip=$(printf '%s' "${summary}" | sed -n 's/.*, *\([0-9][0-9]*\) skipped.*/\1/p')
        total_pass=$((total_pass + ${pass:-0}))
        total_fail=$((total_fail + ${fail:-0}))
        total_skip=$((total_skip + ${skip:-0}))
    fi

    if [[ ${status} -ne 0 ]]; then
        echo "  ✗ 套件失败"
        echo "    复现：QT_QPA_PLATFORM=${QT_QPA_PLATFORM} ${binary}"
        failed_suites="${failed_suites}${suite} "
    else
        echo "  ✓ 通过"
    fi
done

echo "══════════════════════════════════════════════════════════════"
# 「一个测试都没跑」必须当成失败：套件改名或过滤器拼错时，
# 只报「全部套件通过」而实际 0 个用例执行，是 CI 里最危险的那种绿。
if [[ ${ran_suites} -eq 0 ]]; then
    echo "没有套件匹配过滤器「${FILTER}」——一个测试都没跑。" >&2
    exit 2
fi
echo "合计：${total_pass} passed, ${total_fail} failed, ${total_skip} skipped"
if [[ -n "${failed_suites}" ]]; then
    echo "失败套件：${failed_suites}"
    exit 1
fi
echo "全部套件通过。"
