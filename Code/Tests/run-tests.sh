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

mapfile -t PROJECTS < <(find "${CODE_ROOT}/Tests" -mindepth 2 -maxdepth 2 -name '*.pro' | sort)

if [[ ${#PROJECTS[@]} -eq 0 ]]; then
    echo "没有找到测试工程（${CODE_ROOT}/Tests/*/*.pro）。" >&2
    exit 2
fi

total_pass=0
total_fail=0
total_skip=0
failed_suites=()

for project in "${PROJECTS[@]}"; do
    suite="$(basename "$(dirname "${project}")")"
    if [[ -n "${FILTER}" && "${suite}" != *"${FILTER}"* ]]; then
        continue
    fi

    build_dir="${BUILD_ROOT}/${suite}"
    mkdir -p "${build_dir}"

    echo "──────────────────────────────────────────────────────────────"
    echo "▶ ${suite}"
    if ! (cd "${build_dir}" && "${QMAKE}" "${project}" >/dev/null 2>&1); then
        echo "  ✗ qmake 失败"
        failed_suites+=("${suite}")
        total_fail=$((total_fail + 1))
        continue
    fi
    if ! (cd "${build_dir}" && make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null 2>&1); then
        echo "  ✗ 构建失败"
        echo "    复现：cd ${build_dir} && make"
        failed_suites+=("${suite}")
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
        failed_suites+=("${suite}")
        total_fail=$((total_fail + 1))
        continue
    fi

    output="$("${binary}" -o -,txt 2>&1)"
    status=$?
    summary="$(printf '%s\n' "${output}" | grep -E '^Totals:' | tail -n 1)"
    printf '%s\n' "${output}" | grep -E '^(FAIL!|PASS.*skipped)' | head -n 20

    if [[ -n "${summary}" ]]; then
        echo "  ${summary}"
        pass=$(printf '%s' "${summary}" | sed -n 's/.*Totals: \([0-9]\+\) passed.*/\1/p')
        fail=$(printf '%s' "${summary}" | sed -n 's/.*, \([0-9]\+\) failed.*/\1/p')
        skip=$(printf '%s' "${summary}" | sed -n 's/.*, \([0-9]\+\) skipped.*/\1/p')
        total_pass=$((total_pass + ${pass:-0}))
        total_fail=$((total_fail + ${fail:-0}))
        total_skip=$((total_skip + ${skip:-0}))
    fi

    if [[ ${status} -ne 0 ]]; then
        echo "  ✗ 套件失败"
        echo "    复现：QT_QPA_PLATFORM=${QT_QPA_PLATFORM} ${binary}"
        failed_suites+=("${suite}")
    else
        echo "  ✓ 通过"
    fi
done

echo "══════════════════════════════════════════════════════════════"
echo "合计：${total_pass} passed, ${total_fail} failed, ${total_skip} skipped"
if [[ ${#failed_suites[@]} -gt 0 ]]; then
    echo "失败套件：${failed_suites[*]}"
    exit 1
fi
echo "全部套件通过。"
