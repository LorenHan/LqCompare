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
#   MAKE=/path/to/mingw32-make Code/Tests/run-tests.sh
#   LQCOMPARE_TEST_SKIP="AppIntegration CommandActions" Code/Tests/run-tests.sh
#       # 排除依赖可选模块（LqRibbon，在私有仓 MyClass 里）的套件。
#       # 排除项会在开头与末尾汇总里显式打出，不会被当成「全都验证过了」。
#
# 兼容性约束：本脚本要同时在 macOS 与 Windows（Git Bash）上跑，因此不得使用
# bash 4 语法，也不得使用 GNU 工具扩展。具体踩过的坑：
#   1. `mapfile` 是 bash 4.0 才有的内建，macOS 自带 bash 3.2 上直接报
#      "mapfile: command not found"。
#   2. 在 `set -u` 下对空数组取值（`${arr[@]}`）会被当成未定义变量而报错。
#   3. BSD sed 不支持 GNU 的 `\+`；写成 `\([0-9]\+\)` 会静默匹配不上，
#      导致每行显示「14 passed」但合计是 0 —— 看起来还挺正常，最难发现。
#   4. macOS + Rosetta 上未签名的 x86_64 二进制**跑不起来**（进程卡在 `U`
#      状态、CPU 恒为 0、连 `SIGKILL` 都进不去），因此构建完要补一次
#      ad-hoc 签名。详见下面构建成功之后那段注释。
#   5. Windows（Git Bash）上没有 `make`，MinGW 装的是 `mingw32-make`；
#      而且可执行文件带 `.exe` 后缀，`[[ -x foo ]]` 不会自动补。
#      这两条都不探测的话，Windows 上表现是「每个套件都构建失败」。
#   6. Qt 的 `-o -,txt`（把结果写到 stdout）在 Windows 上**一行都不输出**，
#      文件产物却正常。所以只写文件、再由脚本 `cat` 回来，不依赖 stdout 约定。
# 下面用可移植写法规避这六点。
set -uo pipefail

CODE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "${CODE_ROOT}/.." && pwd)"
BUILD_ROOT="${LQCOMPARE_TEST_BUILD_ROOT:-${REPO_ROOT}/_test-build}"
FILTER="${1:-}"

# 可选依赖缺失时要排除的套件：空格分隔的子串，匹配规则与位置参数 FILTER 一致。
# 用在「这台机器上没有某个可选模块」的场合——目前只有 LqRibbon 一个，它在**私有**
# 仓 MyClass 里，公开 CI 与没有该仓的贡献者都拿不到。实测 66 个套件里只有
# `AppIntegration` 与 `CommandActions` 真的依赖它（两个 .pro 里 include 了
# `ThirdParty/lqribbon.pri`），其余 64 个都不依赖。
#
# **刻意不做成静默**：被排除的套件会连同数量打在开头与末尾汇总里。
# 否则「全部套件通过」会被读成「所有套件都验证过了」——那是 CI 里最危险的一种绿。
SKIP="${LQCOMPARE_TEST_SKIP:-}"

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

# 「哪个 make」同样要探测，而不是直接写 `make`：Windows（Git Bash）上通常**没有**
# `make`，MinGW 提供的是 `mingw32-make`。不探测的话，Windows 上每个套件都会停在
# 「构建失败」，而真正的报错（`make: command not found`）被下面那句 `>/dev/null 2>&1`
# 吞掉了，于是看起来像「代码编不过」——比真实原因难查得多。
# `MAKE` 同时是 make 自己的内建变量：从外部传进来的值会被尊重（用于覆盖）。
if [[ -z "${MAKE:-}" ]]; then
    for candidate in mingw32-make make gmake; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            MAKE="${candidate}"
            break
        fi
    done
fi

if [[ -z "${MAKE:-}" ]]; then
    echo "找不到 make/mingw32-make。请设置 MAKE=/path/to/make。" >&2
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
skipped_suites=""
skipped_count=0
no_summary_suites=""
no_summary_count=0
ran_suites=0

if [[ -n "${SKIP}" ]]; then
    echo "按 LQCOMPARE_TEST_SKIP 排除（这些套件本轮**没有验证**）：${SKIP}"
fi

for project in "${PROJECTS[@]}"; do
    suite="$(basename "$(dirname "${project}")")"
    if [[ -n "${FILTER}" && "${suite}" != *"${FILTER}"* ]]; then
        continue
    fi
    # 这里故意不引号包 ${SKIP}：要的就是按空白拆成多个模式。
    matched_skip=0
    for pattern in ${SKIP}; do
        if [[ "${suite}" == *"${pattern}"* ]]; then
            matched_skip=1
            break
        fi
    done
    if [[ "${matched_skip}" -eq 1 ]]; then
        skipped_suites="${skipped_suites}${suite} "
        skipped_count=$((skipped_count + 1))
        continue
    fi
    ran_suites=$((ran_suites + 1))

    build_dir="${BUILD_ROOT}/${suite}"
    mkdir -p "${build_dir}"

    echo "──────────────────────────────────────────────────────────────"
    echo "▶ ${suite}"

    # 构建输出一律落盘到 `<build_dir>/build.log`。以前是直接丢进 /dev/null，
    # 于是 CI 里 17 个（Ubuntu）/ 26 个（Windows）套件「构建失败」而**一个字的原因
    # 都没有**——只有「哪个套件红了」，没有「为什么红」。那正好是这条流水线要
    # 回答的问题（ENG-004 第 3 条），所以失败时把末尾若干行打出来，完整输出留给
    # 上传的构建产物。
    build_log="${build_dir}/build.log"
    if ! (cd "${build_dir}" && "${QMAKE}" "${project}" >"${build_log}" 2>&1); then
        echo "  ✗ qmake 失败"
        echo "    完整输出：${build_log}；末尾："
        tail -n 20 "${build_log}" | sed 's/^/    | /'
        failed_suites="${failed_suites}${suite} "
        total_fail=$((total_fail + 1))
        continue
    fi
    if ! (cd "${build_dir}" && "${MAKE}" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >>"${build_log}" 2>&1); then
        echo "  ✗ 构建失败"
        echo "    复现：cd ${build_dir} && ${MAKE}"
        echo "    完整输出：${build_log}；末尾："
        tail -n 30 "${build_log}" | sed 's/^/    | /'
        failed_suites="${failed_suites}${suite} "
        total_fail=$((total_fail + 1))
        continue
    fi

    # 套件可执行文件由各 .pro 的 DESTDIR 决定，统一约定为 <build_dir>/bin。
    binary="${build_dir}/bin/$(basename "${project}" .pro)"
    # Windows 上可执行文件带 `.exe`，而 `[[ -x foo ]]` **不会**自动补后缀。
    # 不显式处理的话会掉到下面的 `find` 兜底：结果虽然也对，但要绕一圈，
    # 而且依赖 `find -perm` 在 MSYS 下的行为——那是另一处不可靠的地方。
    if [[ ! -x "${binary}" && -x "${binary}.exe" ]]; then
        binary="${binary}.exe"
    fi
    if [[ ! -x "${binary}" ]]; then
        binary="$(find "${build_dir}" -maxdepth 2 -type f -perm -u+x -name 'tst_*' | head -n 1)"
    fi
    if [[ -z "${binary}" || ! -x "${binary}" ]]; then
        echo "  ✗ 找不到套件可执行文件"
        failed_suites="${failed_suites}${suite} "
        total_fail=$((total_fail + 1))
        continue
    fi

    # macOS + Rosetta：**未签名的 x86_64 二进制在 Apple Silicon 上跑不起来**。
    # 现象很有欺骗性——进程进入 `U`（不可中断等待）状态、CPU 时间恒为 0，
    # `SIGKILL` 与 `SIGALRM` 都进不去，于是既跑不完也超时不了，看起来像
    # 「测试挂住了」或「qmake/链接器坏了」，很容易被误判成代码问题。
    # Qt 5.15.2 clang_64 产出的是 x86_64，链接器不会自动补签名，因此每次
    # 构建完都补一次 ad-hoc 签名。签名是幂等的，失败也不该让测试跑不起来
    # （arm64 原生构建上根本没有这个问题），所以这里只静默重试。
    if [[ "$(uname -s)" == "Darwin" ]]; then
        codesign -f -s - "${binary}" >/dev/null 2>&1 || true
    fi

    # 输出同时落盘两份：一份纯文本（给人看，也是 CI 失败时要上传的「日志」），
    # 一份 JUnit XML（给 CI 消费，见 ENG-003 第 3 条与 ENG-004 第 3 条）。
    #
    # 格式必须是 `junitxml` 而不是 `xml`：Qt 的 `xml` 是它自己的私有格式
    # （根节点 `<TestCase>`），任何 CI 的测试报告解析器都不认；`junitxml` 才
    # 产出 `<testsuite failures=... tests=...>` 这种 JUnit 根节点，失败用例清单
    # 才能被 CI 当作「失败用例清单」直接消费（ENG-004 第 3 条）。
    #
    # **不要再加 `-o -,txt` 去拿 stdout**：实测在 Windows（Git Bash）上这一路
    # 一行输出都没有——文件产物正常写出，stdout 是空的。于是 Windows 腿的日志里
    # 既没有 `Totals:` 也没有任何失败用例，只剩「哪个套件红了」，
    # 而合计还被算成 `0 passed`。改成「只写文件、再由脚本把文件读回来」之后，
    # 路径由脚本自己拼、与上传的产物**逐字一致**，也不依赖 MSYS 的参数转换。
    #
    # 跑之前先删掉上一轮的两份产物：套件崩溃时不会写文件，留着旧的会被当成
    # 「这一轮的输出」——那会让一次崩溃看起来像一次通过。
    results_txt="${build_dir}/results.txt"
    results_xml="${build_dir}/results.xml"
    rm -f "${results_txt}" "${results_xml}"
    "${binary}" -o "${results_txt},txt" -o "${results_xml},junitxml" >/dev/null 2>&1
    status=$?
    output=""
    if [[ -f "${results_txt}" ]]; then
        output="$(cat "${results_txt}")"
    fi
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
        echo "    复现：QT_QPA_PLATFORM=${QT_QPA_PLATFORM} ${binary} -o ${results_txt},txt"
        # 崩溃的套件（例如死在 initTestCase）不会写出 `Totals:` 行，于是它
        # **一条用例都不计入合计**。CI 上就出现过「34 个套件红了」而合计只有
        # 「0 passed, 26 failed」——两个数字对不上，看日志的人会以为算错了。
        # 单独报一行，不去把「用例数」与「套件数」这两个单位混在一起。
        if [[ -z "${summary}" ]]; then
            echo "    （没有产出 Totals 行：套件可能在初始化阶段就崩了）"
            no_summary_suites="${no_summary_suites}${suite} "
            no_summary_count=$((no_summary_count + 1))
        fi
        failed_suites="${failed_suites}${suite} "
    else
        echo "  ✓ 通过"
    fi
done

echo "══════════════════════════════════════════════════════════════"
# 「一个测试都没跑」必须当成失败：套件改名或过滤器拼错时，
# 只报「全部套件通过」而实际 0 个用例执行，是 CI 里最危险的那种绿。
if [[ ${ran_suites} -eq 0 ]]; then
    if [[ -n "${skipped_suites}" ]]; then
        echo "所有匹配的套件都被 LQCOMPARE_TEST_SKIP 排除了——一个测试都没跑。" >&2
    else
        echo "没有套件匹配过滤器「${FILTER}」——一个测试都没跑。" >&2
    fi
    exit 2
fi
echo "合计：${total_pass} passed, ${total_fail} failed, ${total_skip} skipped"
# 合计只统计「跑出了统计行的套件」。崩溃的套件必须在合计之外单独点名，
# 否则「合计 0 failed」与「6 个套件红了」会看起来像矛盾。
if [[ -n "${no_summary_suites}" ]]; then
    echo "另有 ${no_summary_count} 个套件没有产出统计行（其用例数不计入上面的合计）：${no_summary_suites}"
fi
if [[ -n "${failed_suites}" ]]; then
    echo "失败套件：${failed_suites}"
    exit 1
fi
# 有套件被排除时绝不能只说「全部套件通过」：那会把「64 个套件验证过」
# 说成「所有套件都验证过」。排除的套件名与数量都打出来，让人一眼看到缺口。
if [[ -n "${skipped_suites}" ]]; then
    echo "通过（已排除 ${skipped_count} 个套件、未验证）：${skipped_suites}"
else
    echo "全部套件通过。"
fi
