#include "MainWindow.h"

#include "commandregistry.h"
#include "attributefilter.h"
#include "filterstack.h"
#include "logging.h"
#include "mask.h"
#include "namefilter.h"
#include "settingschema.h"
#include "sessiontype.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>

#ifndef LQCOMPARE_VERSION
#define LQCOMPARE_VERSION "0.0.0-dev"
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LqCompare"));
    QApplication::setApplicationVersion(QStringLiteral(LQCOMPARE_VERSION));
    QApplication::setOrganizationName(QStringLiteral("Ailecium"));
    QApplication::setOrganizationDomain(QStringLiteral("ailecium.org"));

    // --- 日志与诊断（ENG-006） ---------------------------------------------
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    const QString logPath = dataDir + QStringLiteral("/lqcompare.log");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("LqCompare — file and folder comparison."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption logLevelOption(
        QStringList{QStringLiteral("log-level")},
        QStringLiteral("Log level: error, warning, info, debug, trace."),
        QStringLiteral("level"), QStringLiteral("warning"));
    parser.addOption(logLevelOption);
    const QCommandLineOption noLogFileOption(
        QStringList{QStringLiteral("no-log-file")}, QStringLiteral("Do not write a log file."));
    parser.addOption(noLogFileOption);
    parser.addPositionalArgument(
        QStringLiteral("paths"),
        QStringLiteral("Files or folders to compare. Command line handling is specified in CLI-001."),
        QStringLiteral("[paths...]"));
    parser.process(app);

    // 级别的解析只有一份实现（`Log::levelFromName`）：写在这里过的话，
    // 以后加一个级别就会出现「日志模块认、命令行不认」的分歧，
    // 而用户看到的只是「未知的日志级别」，看不出其实是没同步。
    LqCompare::Log::Level level = LqCompare::Log::Level::Warning;
    const QString levelText = parser.value(logLevelOption);
    if (!LqCompare::Log::levelFromName(levelText, &level)) {
        // 认不出来时用默认值继续，并**说清楚**：静默降级会让用户以为
        // `--log-level debgu` 生效了，然后奇怪为什么日志里什么都没有。
        level = LqCompare::Log::Level::Warning;
        LQCOMPARE_WARN("app", QStringLiteral("无法识别的日志级别「%1」，改用 %2")
                                      .arg(levelText, QString::fromLatin1(
                                                              LqCompare::Log::levelIdentifier(level))));
    }
    LqCompare::Log::setLevel(level);

    if (!parser.isSet(noLogFileOption) && !LqCompare::Log::setLogFile(logPath)) {
        LQCOMPARE_WARN("app", QStringLiteral("无法写入日志文件：%1").arg(logPath));
    }

    LQCOMPARE_INFO("app",
                   QStringLiteral("启动 LqCompare %1（Qt %2，%3，日志级别 %4）")
                           .arg(QStringLiteral(LQCOMPARE_VERSION),
                                QString::fromLatin1(qVersion()), QSysInfo::prettyProductName(),
                                QString::fromLatin1(LqCompare::Log::levelIdentifier(level))));

    LqCompare::MainWindow window;

    // 命令注册表自检（UI-023 / UI-025）：缺失说明、缺失图标、快捷键冲突都会在这里暴露。
    const QStringList problems = LqCompare::CommandRegistry::instance().validate();
    for (const QString &problem : problems) {
        LQCOMPARE_ERROR("command", problem);
    }
    if (!problems.isEmpty()) {
        LQCOMPARE_WARN("app",
                       QStringLiteral("命令注册表有 %1 项问题，详见上方日志（这些是尚未补齐的规格条目）")
                               .arg(problems.size()));
    }

    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        // 位置参数的处理（会话类型判定、语言选择、返回码）按 CLI-001 ~ CLI-012 实现。
        LQCOMPARE_WARN("cli", QStringLiteral("命令行数据源尚未接入（CLI-001）：%1")
                                      .arg(positional.join(QLatin1Char(' '))));
    }

    // 会话类型注册表自检（SESS-002）：ID 格式、英文原名缺失、掩码写成大写
    // 这些「手写那张表时容易写错、写错了也不影响登记成功」的地方在这里暴露。
    //
    // 与命令注册表自检同一个手法，但注册表是**局部对象**而不是单例：
    // 单例会让「这一次测试往表里加了什么」泄漏到下一处，而注册表要被反复
    // 构造（合成的小表验证优先级、内置的大表验证快照）。
    // 它的生命周期目前到此为止——把条目喂给 Home 页与新建向导是 SESS-003 /
    // SESS-005 的事；在那之前，这台自检加上 Tests/SessionType 就是它的调用方。
    LqCompare::SessionTypeRegistry sessionTypes;
    QString sessionTypeError;
    const int registeredTypes = sessionTypes.addBuiltInTypes(&sessionTypeError);
    if (!sessionTypeError.isEmpty()) {
        LQCOMPARE_ERROR("session", sessionTypeError);
    }
    const QStringList typeProblems = sessionTypes.validate();
    for (const QString &problem : typeProblems) {
        LQCOMPARE_ERROR("session", problem);
    }
    LQCOMPARE_INFO("session",
                   QStringLiteral("会话类型注册表：%1 个类型，%2 项问题")
                           .arg(registeredTypes)
                           .arg(typeProblems.size()));

    // 会话设置声明目录自检（SESS-006）：查「键格式、说明缺失、枚举取值表与控件
    // 类型不匹配、默认值过不了自己的校验」这几件事——它们都属「手写那张表时容易
    // 写错、写错了也不影响登记成功」的一类，在界面上表现为「某个设置项没生效」
    // 或「恢复默认之后反而报错」，很难归因。
    //
    // 目录目前是**空的**（框架刻意不含任何具体设置项：文本比对有哪些设置是
    // TEXT-* / FOLD-* 的产品决定），所以这条自检现在只会打印「0 份声明」。
    // 它在这里的意义不是「眼下有问题可查」，而是把唯一的那个生产调用点摆好：
    // 各会话类型落地时只要往目录里登记声明，这条自检立刻开始替它们把关。
    // 与上面那个注册表自检同一个手法，同样不做成单例。
    LqCompare::SessionSettingsCatalog settingsCatalog;
    const QStringList settingsProblems = settingsCatalog.validate();
    for (const QString &problem : settingsProblems) {
        LQCOMPARE_ERROR("session", problem);
    }
    LQCOMPARE_INFO("session",
                   QStringLiteral("会话设置目录：%1 个类型（含 %2），%3 项问题")
                           .arg(settingsCatalog.count())
                           .arg(settingsCatalog.common() ? QStringLiteral("通用声明")
                                                         : QStringLiteral("无通用声明"))
                           .arg(settingsProblems.size()));

    // 过滤层级表自检（FILT-005）：三层顺序、落点是否完整、以及**视图层是否仍然是
    //「关标签即丢弃」**。最后一条单独列出来，因为它是本条目里最贵的一处错误：
    // 视图层的落点一旦被改成会话文件，过滤照样工作，只是关掉标签它还在——
    // 没有任何运行期现象会提示这件事，用户只会觉得「我删掉的临时过滤又回来了」。
    //
    // 与上面两条自检同一个手法：表是**数据**，自检查的是「手写那张表时容易写错、
    // 写错了也不影响别的」的几件事。眼下的调用点只有这一处与 Tests/FilterStack——
    // 三层过滤真正接进界面（设置页的过滤 Tab 与 Filters 页）属于 OPT-* 与视图批次。
    const QStringList filterLayerProblems =
        LqCompare::Filter::validateFilterLayerTable(LqCompare::Filter::filterLayerTable());
    for (const QString &problem : filterLayerProblems) {
        LQCOMPARE_ERROR("filter", problem);
    }
    LqCompare::Filter::FilterStack emptyFilterStack;
    // 空栈必须「全部保留」——这是三层都没配置时的行为，也是用户第一次打开过滤
    // 设置时看到的样子。它要是坏了，用户什么都还没设就看不到文件了。
    const bool emptyKeepsEverything = emptyFilterStack.accepts(
        LqCompare::Filter::MaskSubject::forName(QStringLiteral("probe.txt")));
    if (!emptyKeepsEverything) {
        LQCOMPARE_ERROR("filter",
                        QStringLiteral("空的三层过滤拒绝了条目——没有配置过滤时应当全部保留"));
    }
    LQCOMPARE_INFO("filter",
                   QStringLiteral("三层过滤：%1 层，%2 项问题；空栈保留条目=%3")
                           .arg(LqCompare::Filter::filterLayerTable().size())
                           .arg(filterLayerProblems.size())
                           .arg(emptyKeepsEverything ? QStringLiteral("是") : QStringLiteral("否")));

    // 属性条件表自检（FILT-003）：标识与声明键是否重复、以及**有没有哪一类条件
    // 被改成「要去读条目内容」**。最后一条单独列出来，因为它是本条目边界条款
    // （属性过滤与内容比对解耦）唯一能被自动化盯住的地方：一旦某一类属性条件
    // 去读文件内容，「扫描阶段早期生效」这条性能要求会从根上落空，
    // 而现象只是「打开大目录时变慢」，没人会想到是过滤顺序的问题。
    //
    // 与上面几条自检同一个手法：表是**数据**，自检查的是「手写那张表时容易写错、
    // 写错了也不影响别的」的几件事。眼下的调用点只有这一处与 Tests/AttributeFilter
    // ——属性过滤真正接进界面属 OPT-* 与视图批次。
    const QStringList attributeProblems = LqCompare::Filter::validateAttributeConditionTable(
        LqCompare::Filter::attributeConditionTable());
    for (const QString &problem : attributeProblems) {
        LQCOMPARE_ERROR("filter", problem);
    }
    LqCompare::Filter::AttributeFilter emptyAttributeFilter;
    // 空过滤器必须「全部保留」：没有任何条件时结论恒为放行。
    // 它要是坏了，用户什么都还没设就看不到文件了——与空的三层过滤同一条理由。
    const bool emptyAttributesKeepEverything =
        emptyAttributeFilter.accepts(LqCompare::Filter::EntryMetadata::forName(
            QStringLiteral("probe.txt")));
    if (!emptyAttributesKeepEverything) {
        LQCOMPARE_ERROR("filter",
                        QStringLiteral("空的属性过滤拒绝了条目——没有配置条件时应当全部保留"));
    }
    LQCOMPARE_INFO("filter",
                   QStringLiteral("属性过滤：%1 类条件，%2 项问题；空过滤器保留条目=%3")
                           .arg(LqCompare::Filter::attributeConditionTable().size())
                           .arg(attributeProblems.size())
                           .arg(emptyAttributesKeepEverything ? QStringLiteral("是")
                                                              : QStringLiteral("否")));

    // 名称过滤表自检（FILT-002）：模式前缀是否重复、有没有模式漏登记、
    // 三种组合语义是否各有各的解释。最后一条单独列出来，因为它是本条目里最
    // 容易写错又完全没有运行期现象的一处：两种语义共用一句解释时，界面照常
    // 显示，用户看到的却是**另一条语义**的说明，于是他按提示去改、越改越不对。
    //
    // 与上面几条自检同一个手法：两张表都当**参数**传进来，因此用例能拿一份
    // 故意写坏的表证明它真的会报（一条永远不会红的护栏比没有护栏更糟）。
    // 眼下的调用点只有这一处与 Tests/NameFilter——名称过滤真正接进界面
    // （Filters 页的名称过滤输入框）属 OPT-* 与视图批次。
    const QVector<QString> nameFilterProblems = LqCompare::Filter::validateNameFilterTables(
        LqCompare::Filter::nameMatchModePrefixTable(), LqCompare::Filter::nameCombineModeTable());
    for (const QString &problem : nameFilterProblems) {
        LQCOMPARE_ERROR("filter", problem);
    }
    LqCompare::Filter::NameFilter emptyNameFilter;
    // 空名称过滤器必须「全部保留」——与空的三层过滤、空属性过滤同一条理由：
    // 它要是坏了，用户什么都还没设就看不到文件了。
    const bool emptyNamesKeepEverything =
        emptyNameFilter.accepts(QStringLiteral("probe.txt"));
    if (!emptyNamesKeepEverything) {
        LQCOMPARE_ERROR("filter",
                        QStringLiteral("空的名称过滤拒绝了条目——没有配置表达式时应当全部保留"));
    }
    // 正则匹配的时间预算（200ms/条）是规格里写死的默认值，不是随手取的常数。
    // 它被改小时会出现「一大批条目变成不确定」，改大时会出现「界面卡住」，
    // 两种现象都不会有任何断言变红，因此在这里把默认值钉住。
    const LqCompare::Filter::NameMatchBudget nameBudget = emptyNameFilter.matchBudget();
    if (!nameBudget.isEnabled() || nameBudget.perEntryMs != 200
        || !nameBudget.hasCircuitBreaker()) {
        LQCOMPARE_ERROR("filter",
                        QStringLiteral("名称过滤的时间预算默认值被改动了：perEntryMs=%1，"
                                       "断路器阈值=%2")
                            .arg(nameBudget.perEntryMs)
                            .arg(nameBudget.consecutiveTimeoutLimit));
    }
    LQCOMPARE_INFO("filter",
                   QStringLiteral("名称过滤：%1 种模式，%2 种组合语义，%3 项问题；"
                                  "空过滤器保留条目=%4；时间预算=%5ms/条（断路器 %6 次）")
                           .arg(LqCompare::Filter::allNameMatchModes().size())
                           .arg(LqCompare::Filter::allNameCombineModes().size())
                           .arg(nameFilterProblems.size())
                           .arg(emptyNamesKeepEverything ? QStringLiteral("是")
                                                        : QStringLiteral("否"))
                           .arg(nameBudget.perEntryMs)
                           .arg(nameBudget.consecutiveTimeoutLimit));

    window.resize(1280, 820);
    window.show();
    return app.exec();
}
