#!/usr/bin/env bash
#
# 测试运行器（PRD: ENG-003）
#
# 职责：找出 Code/Tests/*/*.pro，逐个构建并用 offscreen 平台运行，
# **并行**跑（默认 4 个套件同时进行），**每个套件有超时上限**，
# 汇总通过/失败/跳过统计，并在失败时给出可直接复制的复现命令。
#
# 用法：
#   Code/Tests/run-tests.sh                 # 全部套件
#   Code/Tests/run-tests.sh CommandRegistry # 只跑名字匹配的套件
#   Code/Tests/run-tests.sh --self-test     # 运行器自测（见下）
#   QMAKE=/path/to/qmake Code/Tests/run-tests.sh
#   MAKE=/path/to/mingw32-make Code/Tests/run-tests.sh
#   LQCOMPARE_TEST_SKIP="AppIntegration CommandActions" Code/Tests/run-tests.sh
#       # 排除依赖可选模块（LqRibbon，在私有仓 MyClass 里）的套件。
#       # 排除项会在开头与末尾汇总里显式打出，不会被当成「全都验证过了」。
#
# 可调环境变量：
#   LQCOMPARE_TEST_JOBS       并行套件数，默认 min(4, CPU 核数)；设为 1 即串行
#   LQCOMPARE_TEST_TIMEOUT    单个套件的运行超时（秒），默认 600
#   LQCOMPARE_TEST_ROOT       套件搜索根目录，默认 Code/Tests（自测用它指向临时目录）
#   LQCOMPARE_TEST_BUILD_ROOT 构建产物根目录，默认 <仓库根>/_test-build
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
#   7. `wait -n`（等「任意一个」子进程）是 bash 4.3 才有的，macOS 的 3.2 上
#      不存在。并行调度因此用「按 PID 队列等**最老**的那个」实现。
#   8. GNU `timeout` 在 macOS 上**默认不存在**，所以套件超时是自建的轮询实现，
#      不依赖任何外部命令。轮询用 `kill -0` 判存活是可行的：bash 会异步回收
#      后台子进程，子进程退出后 `kill -0` 立即为假、而 `wait` 仍能取回退出码
#      （这一条由 `--self-test` 每轮实测）。
set -uo pipefail

SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
CODE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "${CODE_ROOT}/.." && pwd)"
BUILD_ROOT="${LQCOMPARE_TEST_BUILD_ROOT:-${REPO_ROOT}/_test-build}"
TESTS_ROOT="${LQCOMPARE_TEST_ROOT:-${CODE_ROOT}/Tests}"
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

# ---------------------------------------------------------------------------
# 并行度
#
# 为什么默认就并行：串行跑完 66 个套件要将近 4 分钟，而单个套件的构建与运行
# 大多彼此独立（各有自己的 shadow build 目录、自己的 QTemporaryDir），
# 没有共享状态，因此压缩的只是墙钟时间，不是验证强度。
#
# 但并行度不能直接等于核数：每个套件的 `make` 自己也会并行（-jN），两者相乘
# 会超订成 N² 个编译进程。所以把「套件数 × 每个套件的 make 并行度」控制在
# 核数量级：JOBS=4 时每个套件 make -j(核数/4)。
# 串行（JOBS=1）时每个套件仍用满核数——与并行化之前的行为逐字一致。
# ---------------------------------------------------------------------------
CPU_COUNT="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
case "${CPU_COUNT}" in ''|*[!0-9]*) CPU_COUNT=4 ;; esac
[[ "${CPU_COUNT}" -lt 1 ]] && CPU_COUNT=1

JOBS="${LQCOMPARE_TEST_JOBS:-}"
if [[ -z "${JOBS}" ]]; then
    JOBS="${CPU_COUNT}"
    # 上限 4：再往上收益很小，而内存与磁盘 I/O 的抖动会开始让「偶发失败」变多。
    [[ "${JOBS}" -gt 4 ]] && JOBS=4
fi
case "${JOBS}" in ''|*[!0-9]*) echo "LQCOMPARE_TEST_JOBS 必须是数字：${JOBS}" >&2; exit 2 ;; esac
[[ "${JOBS}" -lt 1 ]] && JOBS=1

MAKE_JOBS=$(( CPU_COUNT / JOBS ))
[[ "${MAKE_JOBS}" -lt 1 ]] && MAKE_JOBS=1

# 单套件超时（秒）。默认值取得很宽松：本机全量 66 个套件串行只要 3 分 53 秒，
# 也就是平均每个套件 3.5 秒；600 秒只可能被「真的挂住了」触发，
# 不会因为 CI 机器慢而误伤。要测超时路径请显式调小（`--self-test` 就是这么做的）。
TIMEOUT="${LQCOMPARE_TEST_TIMEOUT:-600}"
case "${TIMEOUT}" in ''|*[!0-9]*) echo "LQCOMPARE_TEST_TIMEOUT 必须是数字：${TIMEOUT}" >&2; exit 2 ;; esac
[[ "${TIMEOUT}" -lt 1 ]] && TIMEOUT=1

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
# 「构建失败」，而真正的报错（`make: command not found`）被 `>/dev/null 2>&1`
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

# ---------------------------------------------------------------------------
# 运行器自测（ENG-003 第 2、3 条的可重复证据）
#
# 为什么需要它：本文件是整个仓库唯一的验证入口。它自己的行为（并行、超时、
# 失败路径的报错文案、并发下的计数）如果错了，**没有任何东西会变红**——
# 恰恰是它本该报告的那种静默失败。所以按仓库约定（handoff §5 第 7 条
# 「静态检查脚本必须能自证会报错」）给它一个 `--self-test`：在临时目录里现造
# 四个探针套件（通过 / 失败 / 挂死 / 硬退出），用同一个运行器跑两遍
# （并行 4 与串行 1），逐条断言它该说的话，并断言两遍的合计**逐字相同**。
#
# 探针**只存在于临时目录**、跑完即删：把探针留在 Code/Tests/ 下会让全量运行
# 永远失败（这一条上一轮已经用三个临时探针验过，见 handoff §1.23）。
# ---------------------------------------------------------------------------
selftest_tmp=""
selftest_failures=0

st_expect() {
    # $1 断言描述；$2 为 0 表示成立
    if [[ "$2" -eq 0 ]]; then
        echo "  ✓ $1"
    else
        echo "  ✗ $1"
        selftest_failures=$((selftest_failures + 1))
    fi
}

st_contains() {
    # $1 描述 $2 文件 $3 模式（ERE）
    grep -qE "$3" "$2" 2>/dev/null
    st_expect "$1" "$?"
}

st_max_concurrency() {
    # 从探针写下的「epoch毫秒 名字 start|end」轨迹里算出最大并发数。
    #
    # **只统计「同时写下了 start 与 end」的探针**：挂死与硬退出两条探针永远走不到
    # end，把它们的 start 记进去会让计数只增不减，于是串行跑也会报出并发 2
    # ——一个测量误差冒充成「并行生效了」。
    #
    # 同一毫秒内先处理 end 再处理 start（sort 第二列升序，结束记 -1），
    # 结果只会偏保守：串行跑出来一定是 1，不受时间戳抖动影响。
    awk 'NR == FNR { if ($3 == "end") closed[$2] = 1; next }
         ($2 in closed) { print $1, ($3 == "start" ? 1 : -1) }' "$1" "$1" \
        | sort -k1,1n -k2,2n \
        | awk '{ run += $2; if (run > peak) peak = run } END { print peak + 0 }'
}

run_self_test() {
    selftest_tmp="$(mktemp -d "${TMPDIR:-/tmp}/lqcompare-selftest.XXXXXX")" || return 2
    if [[ "${LQCOMPARE_SELFTEST_KEEP:-}" == "1" ]]; then
        echo "自测临时目录保留在：${selftest_tmp}"
    else
        # 注意 trap 里的变量必须是**全局**的：函数返回之后 `local` 已经不存在，
        # 在 `set -u` 下引用它会报未定义变量，于是清理静默失败、临时目录留在盘上。
        trap 'rm -rf "${selftest_tmp}"' EXIT
    fi

    local suites="${selftest_tmp}/suites" build="${selftest_tmp}/build"
    mkdir -p "${suites}" "${build}"

    local write_probe
    write_probe() {
        # $1 套件目录名 $2 目标名 $3 C++ 主体
        local dir="${suites}/$1"
        mkdir -p "${dir}"
        cat > "${dir}/$1.pro" <<PRO
QT += core testlib
QT -= gui
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
TARGET = $2
isEmpty(DESTDIR): DESTDIR = \$\$clean_path(\$\$OUT_PWD/bin)
SOURCES += \$\$PWD/$2.cpp
PRO
        cat > "${dir}/$2.cpp" <<CPP
#include <QtTest>
#include <QFile>
#include <QThread>
#include <QDateTime>
#include <cstdio>
#include <cstdlib>
// 每一行轨迹都带毫秒时间戳，事后据此算「同时有几个套件在跑」。
static void tracePhase(const char *tag, const char *phase)
{
    const QByteArray path = qgetenv("LQCOMPARE_SELFTEST_TRACE");
    if (path.isEmpty()) return;
    QFile file(QString::fromLocal8Bit(path));
    if (!file.open(QIODevice::Append)) return;
    file.write(QByteArray::number(QDateTime::currentMSecsSinceEpoch()) + " " + tag + " " + phase + "\n");
    file.close();
}
$3
CPP
    }

    # 每条探针都真的睡一会儿，好让「并行」在轨迹里有确定的证据：
    # 串行跑时两个 start 之间一定夹着一个 end，不依赖调度抖动。
    write_probe ZZProbePass tst_zzprobepass '
class ZZProbePass : public QObject { Q_OBJECT
private slots:
    void initTestCase() { tracePhase("Pass", "start"); }
    void cleanupTestCase() { tracePhase("Pass", "end"); }
    void shortNap() { QThread::sleep(2); QCOMPARE(1 + 1, 2); }
    void comparesText() { QCOMPARE(QStringLiteral("abc"), QStringLiteral("abc")); }
};
QTEST_APPLESS_MAIN(ZZProbePass)
#include "tst_zzprobepass.moc"'

    write_probe ZZProbeFail tst_zzprobefail '
class ZZProbeFail : public QObject { Q_OBJECT
private slots:
    void initTestCase() { tracePhase("Fail", "start"); }
    void cleanupTestCase() { tracePhase("Fail", "end"); }
    void deliberatelyRed() { QThread::sleep(2); QCOMPARE(1, 2); }
};
QTEST_APPLESS_MAIN(ZZProbeFail)
#include "tst_zzprobefail.moc"'

    # 挂死探针睡 25 秒，自测把超时设成 5 秒，所以它一定被看门狗打断。
    # **刻意不睡无限长**——运行器的超时被改坏时（变异测试会真的这么做），
    # 自测要「断言失败」，而不是跟着一起永久挂住。
    write_probe ZZProbeHang tst_zzprobehang '
class ZZProbeHang : public QObject { Q_OBJECT
private slots:
    void neverFinishes() { QThread::sleep(25); QVERIFY(true); }
};
QTEST_APPLESS_MAIN(ZZProbeHang)
#include "tst_zzprobehang.moc"'

    # 「没写结果就没了」探针。**刻意不用 SIGSEGV/SIGABRT**：Qt Test 对这两个
    # 致命信号自己装了处理，会补写一份结果文件并把用例记为失败——那走的是
    # 「有统计行」的路，验不到「套件在写完结果前就死了」这条最要命的路径
    # （CI 上 `Tests/Folder` 被 glibc 的 `_FORTIFY_SOURCE` abort 掉就是这一类）。
    # `std::_Exit` 不跑 atexit、也不给任何 handler 机会，稳定复现这一类。
    write_probe ZZProbeHardExit tst_zzprobehardexit '
class ZZProbeHardExit : public QObject { Q_OBJECT
private slots:
    void initTestCase() { tracePhase("HardExit", "start"); }
    void diesWithoutWritingResults()
    {
        QThread::sleep(2);
        // 临死前把 end 写上：并发度是用「有头有尾的探针」算的，
        // 不写 end 的探针会被排除，于是它会悄悄削弱「并行确实生效」那条断言。
        tracePhase("HardExit", "end");
        std::fprintf(stderr, "ZZProbeHardExit: deliberate hard exit without results\n");
        std::fflush(stderr);
        std::_Exit(3);
    }
};
QTEST_APPLESS_MAIN(ZZProbeHardExit)
#include "tst_zzprobehardexit.moc"'

    # 构建期就失败的探针：它验的是「本轮产物必须在任何动作之前删干净」这条规则。
    # 这条规则**只在失败路径上才看得出来**：成功路径上 qmake 的 `>` 与二进制的
    # `-o` 自己就会覆盖旧文件，删不删一个样；只有构建失败、脚本提前 `return` 时，
    # 上一轮的产物才会原样留着被当成这一轮的结果上传。
    # 所以这里先埋一份「上一轮的 results.txt」，再让 qmake 故意失败，
    # 断言那份假结果必须消失。
    local bad="${suites}/ZZProbeBadBuild"
    mkdir -p "${bad}"
    cat > "${bad}/ZZProbeBadBuild.pro" <<'PRO'
# 故意让 qmake 失败：自测要的就是「构建没成功」这条路径。
error("ZZProbeBadBuild: deliberate qmake failure for the runner self-test")
PRO
    cat > "${bad}/tst_zzprobebadbuild.cpp" <<'CPP'
#include <QtTest>
class ZZProbeBadBuild : public QObject { Q_OBJECT
private slots:
    void neverRuns() { QVERIFY(false); }
};
QTEST_APPLESS_MAIN(ZZProbeBadBuild)
#include "tst_zzprobebadbuild.moc"
CPP
    # 埋下「上一轮的假结果」。它必须在本轮结束时不见了。
    mkdir -p "${build}/ZZProbeBadBuild"
    printf 'Totals: 999 passed, 0 failed, 0 skipped, 0 blacklisted, 1ms\n' \
        > "${build}/ZZProbeBadBuild/results.txt"

    local trace="${selftest_tmp}/trace.txt"
    echo "══ 运行器自测：探针套件在 ${selftest_tmp}（不进仓库） ══"

    # ---- 第一遍：并行 4，超时 5 秒 ----
    local out4="${selftest_tmp}/out-jobs4.log" code4=0
    : > "${trace}"
    LQCOMPARE_TEST_ROOT="${suites}" \
    LQCOMPARE_TEST_BUILD_ROOT="${build}" \
    LQCOMPARE_TEST_JOBS=4 \
    LQCOMPARE_TEST_TIMEOUT=5 \
    LQCOMPARE_SELFTEST_TRACE="${trace}" \
        bash "${SELF}" >"${out4}" 2>&1 || code4=$?
    local peak4
    peak4="$(st_max_concurrency "${trace}")"

    # ---- 第二遍：串行，用来把「并行」证明成真的并发，而不是「看起来快」 ----
    local out1="${selftest_tmp}/out-jobs1.log" code1=0
    : > "${trace}"
    LQCOMPARE_TEST_ROOT="${suites}" \
    LQCOMPARE_TEST_BUILD_ROOT="${build}" \
    LQCOMPARE_TEST_JOBS=1 \
    LQCOMPARE_TEST_TIMEOUT=5 \
    LQCOMPARE_SELFTEST_TRACE="${trace}" \
        bash "${SELF}" >"${out1}" 2>&1 || code1=$?
    local peak1
    peak1="$(st_max_concurrency "${trace}")"

    echo
    echo "── 断言：失败与超时的报错（日志 ${out4}）──"
    st_expect "退出码为 1（有失败套件）" "$([[ ${code4} -eq 1 ]] && echo 0 || echo 1)"
    st_contains "通过探针报成功" "${out4}" '✓ 通过'
    st_contains "失败探针报套件失败" "${out4}" '✗ 套件失败'
    st_contains "失败时给出可复制的复现命令（含平台参数）" "${out4}" '复现：QT_QPA_PLATFORM=offscreen'
    st_contains "复现命令指向该套件自己的二进制" "${out4}" 'tst_zzprobefail -o'
    st_contains "挂死探针被超时打断并单独点名" "${out4}" '✗ 套件超时'
    st_contains "超时套件在末尾汇总里单独成一行" "${out4}" '^超时套件（'
    st_contains "硬退出探针走「没有产出 Totals 行」" "${out4}" '没有产出 Totals 行'
    st_contains "硬退出探针的 stderr 被贴出来" "${out4}" 'stderr 结尾'
    # 注意 Qt 的口径：`Totals:` 行把 initTestCase 与 cleanupTestCase 也算作用例，
    # 所以「2 个真正的用例」报成 4。写成 2 会红，而它看起来像「运行器少算了两个」
    # ——其实是数错了对象。（JUnit XML 的 `tests=` 同口径，也是 4。）
    st_contains "通过探针的 4 条（2 个用例 + init/cleanup）全部通过" \
        "${out4}" 'Totals: *4 passed, 0 failed'
    # 这一条盯的是**父进程的合计**，不是某个套件自己的 `Totals:` 行。
    # 并发化最容易出的错（计数写进子 shell 的局部变量）不影响任何一条单套件输出，
    # 只会让合计变成 `0 passed`——只有断言合计本身才看得见它。
    st_contains "合计把各套件的用例数累加起来了" "${out4}" '^合计：6 passed, 1 failed, 0 skipped$'
    st_contains "构建期失败的探针报 qmake 失败" "${out4}" '✗ qmake 失败'

    echo
    echo "── 断言：产物与统计 ──"
    st_contains "JUnit XML 真的写出来了" "${build}/ZZProbePass/results.xml" '<testsuite'
    st_contains "JUnit XML 里记着 4 个 testcase（2 个用例 + init/cleanup）" \
        "${build}/ZZProbePass/results.xml" 'tests="4"'
    st_expect "通过探针的 stderr.log 是空的（正常跑完不写 stderr）" \
        "$([[ -s "${build}/ZZProbePass/stderr.log" ]] && echo 1 || echo 0)"
    st_expect "硬退出探针的 stderr.log 非空" \
        "$([[ -s "${build}/ZZProbeHardExit/stderr.log" ]] && echo 0 || echo 1)"
    st_expect "超时探针留下了超时标记" \
        "$([[ -f "${build}/ZZProbeHang/timeout.marker" ]] && echo 0 || echo 1)"
    st_expect "构建日志被测出并留档" \
        "$([[ -s "${build}/ZZProbePass/build.log" ]] && echo 0 || echo 1)"
    # 「本轮产物必须在任何动作之前删干净」唯一的可观测点：构建失败那一轮
    # （成功路径上覆盖写会掩盖这件事，见探针 ZZProbeBadBuild 处的注释）。
    st_expect "构建失败的套件里，上一轮的 results.txt 已被删掉" \
        "$([[ ! -f "${build}/ZZProbeBadBuild/results.txt" ]] && echo 0 || echo 1)"
    # 只断言「文件没了」还不够：脚本还得说清**为什么**没编出来。qmake 的报错原文
    # 在 build.log 里，运行器负责把末尾几十行贴到日志上——否则 CI 上只剩
    # 「✗ qmake 失败」一句，排查的人仍然只能去下载产物。
    st_contains "构建失败时贴出 qmake 的报错原文" "${out4}" 'deliberate qmake failure'

    echo
    echo "── 断言：并发 ──"
    st_expect "并行 4 时同时有 ≥2 个套件在跑（实测峰值 ${peak4}）" \
        "$([[ ${peak4} -ge 2 ]] && echo 0 || echo 1)"
    st_expect "串行那遍退出码同样是 1" "$([[ ${code1} -eq 1 ]] && echo 0 || echo 1)"
    st_expect "串行时同时只有一个套件在跑（实测峰值 ${peak1}）" \
        "$([[ ${peak1} -eq 1 ]] && echo 0 || echo 1)"
    # 最强的一条：并行不能改变结果。并发化最容易出的错（计数写进子 shell 的
    # 局部变量、两遍的产物互相覆盖）都会让这一条红，而只断言「两遍都退出 1」
    # 是看不出来的。
    st_expect "并行与串行的合计逐字相同" \
        "$([[ "$(grep -m1 '^合计：' "${out4}")" == "$(grep -m1 '^合计：' "${out1}")" ]] && echo 0 || echo 1)"

    echo
    if [[ ${selftest_failures} -eq 0 ]]; then
        echo "运行器自测：全部通过。"
        return 0
    fi
    echo "运行器自测：${selftest_failures} 条断言失败。" >&2
    return 1
}

if [[ "${FILTER}" == "--self-test" ]]; then
    run_self_test
    exit $?
fi

# 不用 mapfile/readarray：bash 4.0 才有，macOS 自带 bash 3.2 上会直接失败。
PROJECTS=()
while IFS= read -r project; do
    PROJECTS+=("${project}")
done < <(find "${TESTS_ROOT}" -mindepth 2 -maxdepth 2 -name '*.pro' | sort)

if [[ "${#PROJECTS[@]:-0}" -eq 0 ]]; then
    echo "没有找到测试工程（${TESTS_ROOT}/*/*.pro）。" >&2
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
timed_out_suites=""
timed_out_count=0
ran_suites=0

if [[ -n "${SKIP}" ]]; then
    echo "按 LQCOMPARE_TEST_SKIP 排除（这些套件本轮**没有验证**）：${SKIP}"
fi
echo "并行度 ${JOBS}（每个套件 make -j${MAKE_JOBS}），单套件超时 ${TIMEOUT}s。"

# ---------------------------------------------------------------------------
# 单个套件：构建 → 运行（带超时）→ 判定 → 落盘一行结果
#
# **必须在子 shell 里跑**（调用处直接 `&`）。子 shell 里对全局变量的赋值不会
# 传回父 shell，所以结果一律写进 `${build_dir}/summary.env`，由父进程读完再累加。
# 这一点写错了的症状是最难查的那一类：66 个套件都跑了，而合计永远是「0 passed」。
# ---------------------------------------------------------------------------
run_suite() {
    local suite="$1" project="$2"
    local build_dir="${BUILD_ROOT}/${suite}"
    local log="${build_dir}/suite.log"
    local results_txt="${build_dir}/results.txt"
    local results_xml="${build_dir}/results.xml"
    local stderr_log="${build_dir}/stderr.log"
    local build_log="${build_dir}/build.log"
    local timeout_marker="${build_dir}/timeout.marker"
    local summary_file="${build_dir}/summary.env"

    mkdir -p "${build_dir}"
    # 本套件的全部人读输出进自己的文件；父进程按启动顺序整块打印。
    # 并行时绝不能让多个套件的输出直接交织——那样失败原因会散在别人的日志中间。
    exec >"${log}" 2>&1

    # 全部产物必须在**这一轮的任何动作之前**删掉。
    #
    # 为什么不能放在「构建成功之后、启动二进制之前」：那一段在构建失败、
    # 找不到可执行文件时会 `return` 掉，于是上一轮的产物原样留着，被当作
    # 「这一轮的输出」上传——构建失败的那一轮反而会带上一份看起来正常的旧结果。
    # 这类「旧结果冒充新结果」是 CI 里最难发现的一种假信号。
    rm -f "${results_txt}" "${results_xml}" "${stderr_log}" "${build_log}" "${timeout_marker}" "${summary_file}"

    local status=255 timed_out=0 has_summary=0 pass=0 fail=0 skip=0

    if ! (cd "${build_dir}" && "${QMAKE}" "${project}" >"${build_log}" 2>&1); then
        echo "  ✗ qmake 失败"
        echo "    完整输出：${build_log}；末尾："
        tail -n 20 "${build_log}" | sed 's/^/    | /'
        printf '%d %d %d %d %d %d\n' "${status}" "${timed_out}" "${has_summary}" "${pass}" "${fail}" "${skip}" > "${summary_file}"
        return
    fi
    if ! (cd "${build_dir}" && "${MAKE}" -j"${MAKE_JOBS}" >>"${build_log}" 2>&1); then
        echo "  ✗ 构建失败"
        echo "    复现：cd ${build_dir} && ${MAKE}"
        echo "    完整输出：${build_log}；末尾："
        tail -n 30 "${build_log}" | sed 's/^/    | /'
        printf '%d %d %d %d %d %d\n' "${status}" "${timed_out}" "${has_summary}" "${pass}" "${fail}" "${skip}" > "${summary_file}"
        return
    fi

    # 套件可执行文件由各 .pro 的 DESTDIR 决定，统一约定为 <build_dir>/bin。
    local binary="${build_dir}/bin/$(basename "${project}" .pro)"
    # Windows 上可执行文件带 `.exe`，而 `[[ -x foo ]]` **不会**自动补后缀。
    # 不显式处理的话会掉到下面的 `find` 兜底：结果虽然也对，但要绕一圈，
    # 而且依赖 `find -perm` 在 MSYS 下的行为——那是另一处不可靠的地方。
    if [[ ! -x "${binary}" && -x "${binary}.exe" ]]; then
        binary="${binary}.exe"
    fi
    if [[ ! -x "${binary}" ]]; then
        # 各 .pro 的 TARGET 约定为 `tst_<小写名>`，与工程文件名并不同名
        # （`OptionsDialogTests.pro` → `bin/tst_optionsdialog`），所以上面那条
        # 精确拼法只是快路径；真正兜底的是这里。
        binary="$(find "${build_dir}" -maxdepth 2 -type f -perm -u+x -name 'tst_*' | head -n 1)"
    fi
    if [[ -z "${binary}" || ! -x "${binary}" ]]; then
        echo "  ✗ 找不到套件可执行文件"
        printf '%d %d %d %d %d %d\n' "${status}" "${timed_out}" "${has_summary}" "${pass}" "${fail}" "${skip}" > "${summary_file}"
        return
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
    "${binary}" -o "${results_txt},txt" -o "${results_xml},junitxml" >/dev/null 2>"${stderr_log}" &
    local bin_pid=$!

    # 超时保护。为什么要自己做：GNU `timeout` 在 macOS 上默认不存在，
    # 而 `perl -e 'alarm'` 之类的外挂会把「跑测试」变成「跑测试 + 一个解释器」。
    #
    # 一个挂死的套件如果没人打断，整条流水线会等到 CI 的全局超时，
    # 而日志上只显示「上一个套件还没跑完」——比失败难查得多。
    #
    # 轮询而不是 `wait` + 看门狗子 shell：后者会在每个套件上留一个孤儿 `sleep`。
    # 判存活用 `kill -0` 是可行的（见文件头第 8 条），并且 `wait` 仍能取回
    # 真实退出码——`--self-test` 两遍都跑到这里，"通过探针报成功" 那条断言
    # 就是它的证据。
    local waited=0
    while kill -0 "${bin_pid}" 2>/dev/null; do
        if [[ "${waited}" -ge "${TIMEOUT}" ]]; then
            timed_out=1
            : > "${timeout_marker}"
            # 先 TERM 再 KILL：给套件一次写残存输出的机会，但绝不为它多等。
            kill -TERM "${bin_pid}" 2>/dev/null || true
            sleep 2
            kill -KILL "${bin_pid}" 2>/dev/null || true
            break
        fi
        sleep 1
        waited=$((waited + 1))
    done
    wait "${bin_pid}"
    status=$?

    local output=""
    if [[ -f "${results_txt}" ]]; then
        output="$(cat "${results_txt}")"
    fi
    local summary
    summary="$(printf '%s\n' "${output}" | grep -E '^Totals:' | tail -n 1)"
    printf '%s\n' "${output}" | grep -E '^(FAIL!|PASS.*skipped)' | head -n 20

    if [[ -n "${summary}" ]]; then
        has_summary=1
        echo "  ${summary}"
        # 注意：macOS 用的是 BSD sed，不支持 GNU 的 `\+`。
        # 写成 `\([0-9]\+\)` 会静默匹配不上——解析结果全为 0，
        # 于是「合计 0 passed」却在每行显示「14 passed」，看起来还挺正常。
        pass=$(printf '%s' "${summary}" | sed -n 's/.*Totals: *\([0-9][0-9]*\) passed.*/\1/p')
        fail=$(printf '%s' "${summary}" | sed -n 's/.*, *\([0-9][0-9]*\) failed.*/\1/p')
        skip=$(printf '%s' "${summary}" | sed -n 's/.*, *\([0-9][0-9]*\) skipped.*/\1/p')
        pass="${pass:-0}"; fail="${fail:-0}"; skip="${skip:-0}"
    fi

    if [[ ${timed_out} -eq 1 ]]; then
        # 超时是最需要被单独说出名字的一种失败：它与「构建不过」和「用例断言失败」
        # 的排查方向完全不同，混在「套件失败」里会让人先去读代码。
        echo "  ✗ 套件超时（超过 ${TIMEOUT}s 未结束，已终止）"
        echo "    复现：QT_QPA_PLATFORM=${QT_QPA_PLATFORM} ${binary} -o ${results_txt},txt"
        if [[ -s "${stderr_log}" ]]; then
            echo "    被终止前最后的 stderr："
            tail -n 15 "${stderr_log}" | sed 's/^/    | /'
        fi
    elif [[ ${status} -ne 0 ]]; then
        echo "  ✗ 套件失败"
        echo "    复现：QT_QPA_PLATFORM=${QT_QPA_PLATFORM} ${binary} -o ${results_txt},txt"
        # 崩溃的套件（例如死在 initTestCase）不会写出 `Totals:` 行，于是它
        # **一条用例都不计入合计**。CI 上就出现过「34 个套件红了」而合计只有
        # 「0 passed, 26 failed」——两个数字对不上，看日志的人会以为算错了。
        # 单独报一行，不去把「用例数」与「套件数」这两个单位混在一起。
        if [[ ${has_summary} -eq 0 ]]; then
            echo "    （没有产出 Totals 行：套件可能在初始化阶段就崩了）"
            # 崩溃原因只出现在 stderr 里。有内容就贴结尾若干行——CI 上排查的人
            # 不必再下载产物；没内容本身也是信息（被 SIGKILL 或直接段错误带走，
            # 连一句遗言都没留下），所以这两种情况要分开说，不能都只说「崩了」。
            if [[ -s "${stderr_log}" ]]; then
                echo "    stderr 结尾（完整内容见 ${stderr_log}，随日志产物一起上传）："
                tail -n 15 "${stderr_log}" | sed 's/^/    | /'
            else
                echo "    stderr 是空的——连一句遗言都没留下（例如被 SIGKILL 或直接段错误带走的）。"
            fi
        fi
    else
        echo "  ✓ 通过"
    fi

    printf '%d %d %d %d %d %d\n' "${status}" "${timed_out}" "${has_summary}" "${pass}" "${fail}" "${skip}" > "${summary_file}"
}

# 父进程在套件结束后做两件事：把它的整块日志打出来，再累加计数与失败清单。
# 单独成函数是因为「启动时打标题、结束后打内容」要写两遍（达到并行上限时一处、
# 收尾时一处），两处各写一份迟早会漂移。
reap_suite() {
    local suite="$1"
    local build_dir="${BUILD_ROOT}/${suite}"
    local summary_file="${build_dir}/summary.env"
    if [[ -f "${build_dir}/suite.log" ]]; then
        cat "${build_dir}/suite.log"
    else
        echo "  ✗ 套件没有留下日志（${build_dir}/suite.log）"
    fi

    local st=255 to=0 hs=0 ps=0 fs=0 ss=0
    if [[ -f "${summary_file}" ]]; then
        read -r st to hs ps fs ss < "${summary_file}" || true
    fi
    st="${st:-255}"; to="${to:-0}"; hs="${hs:-0}"; ps="${ps:-0}"; fs="${fs:-0}"; ss="${ss:-0}"
    if [[ ${hs} -eq 1 ]]; then
        total_pass=$((total_pass + ps))
        total_fail=$((total_fail + fs))
        total_skip=$((total_skip + ss))
    fi
    if [[ ${to} -eq 1 ]]; then
        timed_out_suites="${timed_out_suites}${suite} "
        timed_out_count=$((timed_out_count + 1))
        failed_suites="${failed_suites}${suite} "
    elif [[ ${st} -ne 0 ]]; then
        if [[ ${hs} -eq 0 ]]; then
            # 没有统计行的**非超时**套件才算「可能初始化就崩了」：超时的套件同样
            # 不会有统计行，但它上面已经单独点名；列进两个清单会让
            # 「6 个套件红了」与清单长度对不上，而这正是最容易让人怀疑合计的理由。
            no_summary_suites="${no_summary_suites}${suite} "
            no_summary_count=$((no_summary_count + 1))
        fi
        failed_suites="${failed_suites}${suite} "
    fi
}

PIDS=()
NAMES=()
queue_head=0

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

    echo "──────────────────────────────────────────────────────────────"
    echo "▶ ${suite}"
    # 启动即打标题、结束后才打内容：并行时这样既看得到进度，
    # 又保证每个套件的细节是一整块、不会被别人的输出切开。
    run_suite "${suite}" "${project}" &
    PIDS+=("$!")
    NAMES+=("${suite}")

    # 达到并行上限就等**最老**的那个（不用 `wait -n`，bash 4.3+ 才有）。
    # 等最老而不是最早结束的，换来的是「打印顺序 = 启动顺序」，日志可读。
    while [[ $(( ${#PIDS[@]} - queue_head )) -ge ${JOBS} ]]; do
        wait "${PIDS[queue_head]}"
        reap_suite "${NAMES[queue_head]}"
        queue_head=$((queue_head + 1))
    done
done

while [[ ${queue_head} -lt ${#PIDS[@]} ]]; do
    wait "${PIDS[queue_head]}"
    reap_suite "${NAMES[queue_head]}"
    queue_head=$((queue_head + 1))
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
# 合计只统计「跑出了统计行的套件」。崩溃与超时的套件必须在合计之外单独点名，
# 否则「合计 0 failed」与「6 个套件红了」会看起来像矛盾。
if [[ ${timed_out_count} -gt 0 ]]; then
    echo "超时套件（超过 ${TIMEOUT}s，其用例数不计入上面的合计）：${timed_out_suites}"
fi
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
