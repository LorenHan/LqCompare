#include "tst_sessiontype.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

using namespace LqCompare;

// -----------------------------------------------------------------------------
// 辅助函数
//
// 这里刻意**不放任何 QVERIFY**：QVERIFY 展开后带 `return`，放在非 void 函数里
// 直接编译不过（与 Tests/Filter 里同一条记录）。断言留在用例函数里。
// -----------------------------------------------------------------------------

namespace {

/// 一个不依赖界面、也不依赖平台的合法描述子。
///
/// 显示名与英文原名带上各自的前缀，是为了让「按名字查」的用例能分辨出
/// 命中的到底是哪一个字段——写成同一个字符串的话，`findByName` 内部三轮
/// 比对中哪一轮起了作用就看不出来了。
SessionType syntheticType(const QString &id, SessionGroup group = SessionGroup::Text)
{
    SessionType type;
    type.id = id;
    type.displayName = QStringLiteral("显示名-") + id;
    type.englishName = QStringLiteral("English-") + id;
    type.summary = QStringLiteral("合成类型（测试用）");
    type.group = group;
    return type;
}

QString readSourceFile(const QString &relativePath)
{
    QFile file(QStringLiteral(LQCOMPARE_CODE_ROOT) + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

/// 取出 `homepage.cpp` 里 `sections()` 的函数体。
///
/// 为什么能靠文本定位：`sections()` 是一段纯字面量的 return，
/// 函数体里除了各分组的标题与卡片文案之外没有别的逻辑，因此「标题目录」
/// 与「代码」这两件事不会分家。真正要防的是**它被改名或换签名**——
/// 那样这里返回空串，调用方会直接报「找不到函数体」而不是静默地
/// 「比对了一个空集合」（后者会恒真通过）。
QString sectionsBodyOfImpl(const QString &source)
{
    const QString marker = QStringLiteral("QList<HomePage::Section> HomePage::sections() const");
    const int start = source.indexOf(marker);
    if (start < 0) {
        return QString();
    }
    const int end = source.indexOf(QStringLiteral("\n}\n"), start);
    return end < 0 ? source.mid(start) : source.mid(start, end - start);
}

/// 从 `sections()` 的函数体里取出全部类型卡片声明的 ID，按出现顺序。
///
/// 只认 `QStringLiteral("…")` 这一种写法：它是这个函数里唯一可能出现类型 ID
/// 的形式（卡片标题走 `tr()`，分组标题也走 `tr()`）。写死这一种形式看起来
/// 脆，但它换来的是**换写法就会红**——比写成宽松的「任意引号里的字符串」
/// 然后把 `tr("Text Compare")` 一起收进来好得多。
QStringList typeIdsInBody(const QString &body)
{
    QStringList ids;
    if (body.isEmpty()) {
        return ids;
    }
    const QRegularExpression pattern(QStringLiteral("QStringLiteral\\(\"([^\"]*)\"\\)"));
    QRegularExpressionMatchIterator iterator = pattern.globalMatch(body);
    while (iterator.hasNext()) {
        ids.append(iterator.next().captured(1));
    }
    return ids;
}

/// `Pictures.qrc` 里声明的图标文件名。
QStringList qrcFileEntries(const QString &qrc)
{
    QStringList names;
    const QRegularExpression pattern(QStringLiteral("<file>([^<]+)</file>"));
    QRegularExpressionMatchIterator iterator = pattern.globalMatch(qrc);
    while (iterator.hasNext()) {
        names.append(iterator.next().captured(1).trimmed());
    }
    return names;
}

/// 列出「类型声明了图标键、但 `Pictures.qrc` 里没有这个名字」的那些。
QStringList undeclaredIconKeys(const QVector<SessionType> &types, const QStringList &declared)
{
    QStringList problems;
    for (const SessionType &type : types) {
        if (type.iconKey.isEmpty()) {
            continue;
        }
        const QString name = type.iconKey.section(QLatin1Char('/'), -1);
        if (!declared.contains(name)) {
            problems.append(name);
        }
    }
    return problems;
}

} // namespace

QString TstSessionType::sectionsBodyOf(const QString &source)
{
    return sectionsBodyOfImpl(source);
}

SessionType TstSessionType::makeType(const QString &id, SessionGroup group)
{
    return syntheticType(id, group);
}

namespace QTest {

// QCOMPARE 失败时要打印值。三个枚举各写一个特化，否则看到的是
// 「Compared values are not the same」加两个整数——而这三个字段恰恰是
// 判断「哪条规则没生效」的关键信息。
//
// 必须是 `template <>`（而不是普通重载）：QCOMPARE 内部用的是带显式模板实参的
// `toString<T>(x)`，普通重载不参与重载决议，写了也拿不到值，而且能编译通过。
template <>
char *toString(const LqCompare::SessionGroup &group)
{
    return qstrdup(qPrintable(sessionGroupLabel(group)));
}

template <>
char *toString(const LqCompare::SessionEdition &edition)
{
    return qstrdup(qPrintable(sessionEditionLabel(edition)));
}

template <>
char *toString(const LqCompare::SessionPlatformScope &scope)
{
    const QString label = sessionPlatformScopeLabel(scope);
    return qstrdup(qPrintable(label.isEmpty() ? QStringLiteral("全部") : label));
}

} // namespace QTest

// -----------------------------------------------------------------------------
// 前置检查
// -----------------------------------------------------------------------------

void TstSessionType::initTestCase()
{
    // 两道源码级护栏要读文件。先在这里失败一次，让「文件读不到」与自己要验证的
    // 结论分开报告——否则现象会是「类型 ID 与 Home 页不一致」，而真实原因是
    // 构建目录里那个宏指错了地方。
    QVERIFY2(!readSourceFile(QStringLiteral("/Views/Shell/homepage.cpp")).isEmpty(),
             "读不到 Code/Views/Shell/homepage.cpp：LQCOMPARE_CODE_ROOT 指向不对？");
    QVERIFY2(!readSourceFile(QStringLiteral("/Pictures/Pictures.qrc")).isEmpty(),
             "读不到 Code/Pictures/Pictures.qrc：LQCOMPARE_CODE_ROOT 指向不对？");
}

// =============================================================================
// A 条目字段齐备（SESS-002 第 1 条）
// -----------------------------------------------------------------------------
// 完成标准原文：注册表条目包含：类型 ID、显示名、图标、默认文件掩码、
// 创建工厂、是否 Pro 特性、是否平台限定。
// 下面逐项断言，而不是只断「表里有 14 条」——条目数对而上线时发现少一个字段，
// 正是只数条数会漏掉的那类问题。
// =============================================================================

void TstSessionType::builtInTableHasFourteenEntries()
{
    const QVector<SessionType> types = builtInSessionTypes();
    QCOMPARE(types.size(), 14);

    // 14 与「研究基线 §1 写的共 13 种」的差额来自压缩包比对：BC 把压缩包当作
    // 文件夹处理（ARC-001），不单列成会话类型；而我们已在 Home 页与 CLI-002 的
    // 规格里把它列成一个类型。这个差额是**已知且有意留下的**——两条一起断言，
    // 改的时候必须做一次决定，而不是让某一处悄悄漂移。
    int archiveCount = 0;
    for (const SessionType &type : types) {
        if (type.id == QStringLiteral("archive")) {
            ++archiveCount;
        }
    }
    QCOMPARE(archiveCount, 1);
    QCOMPARE(types.size() - 1, 13);
}

void TstSessionType::everyBuiltInEntryHasAllRequiredFields()
{
    // 逐项断言「条目字段齐备」（第 1 条），而不是只断表里有 14 条——
    // 条目数对而上线时发现少一个字段，正是只数条数会漏掉的那类问题。
    //
    // 这里只查**入参层面**的齐备。图标键写法、掩码大小写这些「手写表时的
    // 约定」由 validate() 查（它才是那条规则的唯一实现），本用例断言
    // 「内置表 validate() 为空」即可，不另抄一份判断。
    const QVector<SessionType> types = builtInSessionTypes();
    for (const SessionType &type : types) {
        QVERIFY2(isValidSessionTypeId(type.id), qPrintable(type.id));
        QVERIFY2(!type.displayName.isEmpty(), qPrintable(type.id));
        QVERIFY2(!type.summary.isEmpty(), qPrintable(type.id));

        switch (type.group) {
        case SessionGroup::Text:
        case SessionGroup::Folders:
        case SessionGroup::Data:
        case SessionGroup::Advanced:
            break;
        default:
            QFAIL("分组取值超出枚举");
        }

        switch (type.edition) {
        case SessionEdition::Standard:
        case SessionEdition::Pro:
            break;
        default:
            QFAIL("版本取值超出枚举");
        }

        switch (type.platforms) {
        case SessionPlatformScope::All:
        case SessionPlatformScope::WindowsOnly:
            break;
        default:
            QFAIL("平台取值超出枚举");
        }

        // 掩码条目不能是空串。空串会静默地什么都不匹配，而列表看起来"有掩码"。
        for (const QString &mask : type.fileMasks) {
            QVERIFY2(!mask.trimmed().isEmpty(), qPrintable(type.id));
        }
    }
}

void TstSessionType::builtInTypesCarryNoFactory()
{
    // 内置表只提供**描述子**：创建工厂要等各会话类型真正实现时才由应用层给。
    // 这条不是形式要求——如果内置表里悄悄塞了工厂，注册表就会变成「哪里可以用
    // 哪个视图」的第二个事实来源，而真正的实现在别处。
    SessionTypeRegistry registry;
    QString error;
    QCOMPARE(registry.addBuiltInTypes(&error), 14);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    for (const SessionTypeEntry *entry : registry.entries()) {
        QVERIFY2(!entry->hasFactory(), qPrintable(entry->type.id));
    }
}

void TstSessionType::entryFieldsSurviveRegistration()
{
    SessionTypeRegistry registry;
    SessionType type = syntheticType(QStringLiteral("alpha"));
    type.fileMasks << QStringLiteral("*.aaa") << QStringLiteral("*.bbb");
    type.iconKey = QStringLiteral(":/Pictures/") + QStringLiteral("ribbon_open") + QLatin1Char('.')
            + QStringLiteral("svg"); // 拼出来而不是写完整路径，理由见图标那条用例。
    type.edition = SessionEdition::Pro;
    type.platforms = SessionPlatformScope::WindowsOnly;
    type.group = SessionGroup::Data;

    QString error;
    QVERIFY2(registry.add(type, SessionFactory(), &error), qPrintable(error));

    const SessionTypeEntry *entry = registry.find(QStringLiteral("alpha"));
    QVERIFY(entry != nullptr);

    // 逐字段比对而不是只比 ID：只比 ID 的话，add() 里少复制一个字段（例如
    // 掩码）完全看不出来，而现象是「按掩码的自动选择对某些扩展名失效」。
    QCOMPARE(entry->type.id, type.id);
    QCOMPARE(entry->type.displayName, type.displayName);
    QCOMPARE(entry->type.englishName, type.englishName);
    QCOMPARE(entry->type.summary, type.summary);
    QCOMPARE(entry->type.iconKey, type.iconKey);
    QCOMPARE(entry->type.fileMasks, type.fileMasks);
    QCOMPARE(entry->type.group, type.group);
    QCOMPARE(entry->type.edition, type.edition);
    QCOMPARE(entry->type.platforms, type.platforms);
    QVERIFY(entry->type.hasFileMasks());
}

void TstSessionType::factoryIsKeptAndCallable()
{
    SessionTypeRegistry registry;
    int calls = 0;
    SessionFactory factory = [&calls](QObject *) -> CompareSession * {
        ++calls;
        return nullptr;
    };
    QVERIFY(registry.add(syntheticType(QStringLiteral("alpha")), factory));

    const SessionTypeEntry *entry = registry.find(QStringLiteral("alpha"));
    QVERIFY(entry != nullptr);
    QVERIFY(entry->hasFactory());

    // 工厂真的能叫起来并返回它给的东西（这里返回 nullptr 是刻意的：
    // 本套件只链接 QtCore，造不出 CompareSession —— 那正是服务层该有的样子）。
    QCOMPARE(entry->factory(nullptr), static_cast<CompareSession *>(nullptr));
    QCOMPARE(calls, 1);
}

void TstSessionType::absentFactoryIsDistinguishableFromUnknownType()
{
    SessionTypeRegistry registry;
    QVERIFY(registry.add(syntheticType(QStringLiteral("alpha"))));

    const SessionTypeEntry *registered = registry.find(QStringLiteral("alpha"));
    const SessionTypeEntry *missing = registry.find(QStringLiteral("no-such-type"));

    QVERIFY(registered != nullptr);
    QVERIFY(!registered->hasFactory());
    // 这两件事必须分得开：前者是「这一版还没有这个视图」，后者是拼写错误。
    // 合并成一个 nullptr 的话，命令行与 Home 页就都得用同一句话解释两种问题。
    QVERIFY(missing == nullptr);
}

void TstSessionType::proFlagMatchesTheResearchBaseline()
{
    // `docs/research/beyondcompare-features.md` 的标注约定是
    // 「Pro 专有功能用 [Pro] 标注」——没有标记即 Standard。
    // 当前标了 [Pro] 的会话类型恰好三条，本用例把它们逐个点名锁住：
    // 要加第四个 Pro 类型，必须先回去改研究文档的标注。
    const QStringList expected = {QStringLiteral("text-merge"), QStringLiteral("folder-merge"),
                                  QStringLiteral("registry")};
    QStringList actual;
    for (const SessionType &type : builtInSessionTypes()) {
        if (type.edition == SessionEdition::Pro) {
            actual.append(type.id);
        }
    }
    QCOMPARE(actual, expected);
}

void TstSessionType::windowsOnlyFlagMatchesTheResearchBaseline()
{
    // [Win] 标注的两条：注册表比对与版本比对。
    // 注册表那条还同时带 [Pro]，是本表里唯一两者兼具的类型。
    const QStringList expected = {QStringLiteral("registry"), QStringLiteral("version")};
    QStringList actual;
    for (const SessionType &type : builtInSessionTypes()) {
        if (type.platforms == SessionPlatformScope::WindowsOnly) {
            actual.append(type.id);
        }
    }
    QCOMPARE(actual, expected);

    // 顺带钉住「两者兼具」这件事：它决定界面上要不要同时显示两种标记。
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    const SessionTypeEntry *registryType = registry.find(QStringLiteral("registry"));
    QVERIFY(registryType != nullptr);
    QCOMPARE(registryType->type.edition, SessionEdition::Pro);
    QCOMPARE(registryType->type.platforms, SessionPlatformScope::WindowsOnly);
}

void TstSessionType::platformScopeLabelSaysWindowsOnly()
{
    // REG-001 的边界原话是「必须在 Home 页明确标注「仅 Windows」」——
    // 这四个字就是那句话的唯一来源，改文案要连带确认那条完成标准。
    QCOMPARE(sessionPlatformScopeLabel(SessionPlatformScope::WindowsOnly),
             QStringLiteral("仅 Windows"));
    // 全平台可用时**没有**标签，而不是「全部平台」：后者会在每一张卡片上
    // 加一句废话，反而把真正需要注意的「仅 Windows」淹掉。
    QVERIFY(sessionPlatformScopeLabel(SessionPlatformScope::All).isEmpty());

    QCOMPARE(QString::fromLatin1(sessionPlatformScopeIdentifier(SessionPlatformScope::All)),
             QStringLiteral("all"));
    QCOMPARE(QString::fromLatin1(sessionPlatformScopeIdentifier(SessionPlatformScope::WindowsOnly)),
             QStringLiteral("windows"));
}

void TstSessionType::unavailableReasonExplainsWhyAndIsEmptyWhenAvailable()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    const SessionTypeEntry *folder = registry.find(QStringLiteral("folder"));
    QVERIFY(folder != nullptr);
    QVERIFY(folder->isAvailableHere());
    QVERIFY(folder->unavailableReason().isEmpty());

    const SessionTypeEntry *registryType = registry.find(QStringLiteral("registry"));
    QVERIFY(registryType != nullptr);
    if (currentPlatformAllows(SessionPlatformScope::WindowsOnly)) {
        QVERIFY(registryType->isAvailableHere());
        QVERIFY(registryType->unavailableReason().isEmpty());
    } else {
        QVERIFY(!registryType->isAvailableHere());
        // 空串会让界面显示一个没有任何解释的置灰项；而「当前平台不支持」这种
        // 兜底话等于没说。要求它点出平台原因。
        const QString reason = registryType->unavailableReason();
        QVERIFY2(!reason.isEmpty(), "不可用的类型必须给出一句能直接显示的说明");
        QVERIFY2(reason.contains(QStringLiteral("Windows")), qPrintable(reason));
    }
}

void TstSessionType::directoryTypesCarryNoFileMask()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    const QStringList directoryTypes = {QStringLiteral("folder"), QStringLiteral("folder-sync"),
                                        QStringLiteral("folder-merge")};
    for (const QString &id : directoryTypes) {
        const SessionTypeEntry *entry = registry.find(id);
        QVERIFY2(entry != nullptr, qPrintable(id));
        // 目录类型的 Specs 是两个目录。给它挂一串扩展名，会让「双击一个 .tar」
        // 落到文件夹比对——那不是用户要的。
        QVERIFY2(!entry->type.hasFileMasks(), qPrintable(id));
    }
}

void TstSessionType::fallbackAndAuxiliaryTypesCarryNoFileMask()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    const QStringList maskless = {
        QStringLiteral("hex"),        // 按「不是其它任何格式」兜底
        QStringLiteral("text-merge"), // 要三个以上文件参数才会进入
        QStringLiteral("text-edit")   // 只由显式入口打开
    };
    for (const QString &id : maskless) {
        const SessionTypeEntry *entry = registry.find(id);
        QVERIFY2(entry != nullptr, qPrintable(id));
        QVERIFY2(!entry->type.hasFileMasks(), qPrintable(id));
    }

    // 反面：有掩码的类型不能悄悄变空。挑两个最容易被误删的
    // ——补丁视图的 .diff/.patch 与注册表的 .reg。
    for (const QString &id : {QStringLiteral("text-patch"), QStringLiteral("registry")}) {
        const SessionTypeEntry *entry = registry.find(id);
        QVERIFY2(entry != nullptr, qPrintable(id));
        QVERIFY2(entry->type.hasFileMasks(), qPrintable(id));
    }
}

void TstSessionType::registrationRejectsAnEmptyMaskEntry()
{
    SessionTypeRegistry registry;
    SessionType type = syntheticType(QStringLiteral("alpha"));
    type.fileMasks << QStringLiteral("*.aaa") << QStringLiteral("   ");
    QString error;
    QVERIFY(!registry.add(type, SessionFactory(), &error));
    QVERIFY2(!error.isEmpty(), "拒绝时要说明原因");
    QVERIFY2(error.contains(QStringLiteral("空条目")), qPrintable(error));
    // 整条拒绝，不做部分接受：接受了的话，一个「ID 进去了但掩码全丢了」的条目
    // 会让按掩码的自动选择静默失效。
    QVERIFY(registry.find(QStringLiteral("alpha")) == nullptr);
}

// =============================================================================
// B 类型 ID 的稳定性（SESS-002 第 2 条）
// -----------------------------------------------------------------------------
// 完成标准原文：类型 ID 有稳定性测试：已发布 ID 的字符串值被快照断言锁定。
// =============================================================================

void TstSessionType::publishedIdsMatchSnapshot()
{
    // 顺序也一起锁：注册顺序就是按掩码查询的优先级（第 3 条），
    // 因此「改了顺序」和「改了 ID」一样是一次会被感知的变更。
    const QStringList expected = {
        QStringLiteral("text"),        QStringLiteral("text-merge"), QStringLiteral("text-edit"),
        QStringLiteral("text-patch"),  QStringLiteral("folder"),     QStringLiteral("folder-sync"),
        QStringLiteral("folder-merge"),QStringLiteral("table"),      QStringLiteral("hex"),
        QStringLiteral("picture"),     QStringLiteral("media"),      QStringLiteral("registry"),
        QStringLiteral("version"),     QStringLiteral("archive")};

    QCOMPARE(builtInSessionTypeIds(), expected);

    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    QStringList registered;
    for (const SessionTypeEntry *entry : registry.entries()) {
        registered.append(entry->type.id);
    }
    QCOMPARE(registered, expected);
}

void TstSessionType::publishedIdsAreUniqueAndWellFormed()
{
    const QStringList ids = builtInSessionTypeIds();
    QSet<QString> seen;
    for (const QString &id : ids) {
        QVERIFY2(isValidSessionTypeId(id), qPrintable(id));
        QVERIFY2(!seen.contains(id), qPrintable(QStringLiteral("重复 ID：") + id));
        seen.insert(id);
    }
    QCOMPARE(seen.size(), ids.size());
}

void TstSessionType::idValidityFollowsTheDocumentedRule()
{
    // 合法
    for (const QString &id : {QStringLiteral("a"), QStringLiteral("text"),
                              QStringLiteral("text-merge"), QStringLiteral("hex2"),
                              QStringLiteral("a1-b2")}) {
        QVERIFY2(isValidSessionTypeId(id), qPrintable(id));
    }
    // 非法：空、大写、下划线、空格、数字开头、连字符开头、带点的扩展名、
    // 路径分隔符、Unicode。
    for (const QString &id : {QString(), QStringLiteral("Text"), QStringLiteral("text_merge"),
                              QStringLiteral("text merge"), QStringLiteral("2hex"),
                              QStringLiteral("-hex"), QStringLiteral("text.compare"),
                              QStringLiteral("text/merge"), QStringLiteral("文本")}) {
        QVERIFY2(!isValidSessionTypeId(id), qPrintable(QStringLiteral("本该拒绝：") + id));
    }
}

void TstSessionType::registryRejectsInvalidIdentifiers()
{
    SessionTypeRegistry registry;
    for (const QString &id : {QStringLiteral("Text"), QStringLiteral("text merge"),
                              QStringLiteral("2hex")}) {
        SessionType type = syntheticType(id);
        QString error;
        QVERIFY2(!registry.add(type, SessionFactory(), &error), qPrintable(id));
        QVERIFY2(!error.isEmpty(), qPrintable(id));
    }
    QVERIFY(registry.isEmpty());
}

void TstSessionType::registryRejectsDuplicateIdentifiers()
{
    SessionTypeRegistry registry;
    QVERIFY(registry.add(syntheticType(QStringLiteral("alpha"))));
    QString error;
    QVERIFY(!registry.add(syntheticType(QStringLiteral("alpha")), SessionFactory(), &error));
    QVERIFY2(error.contains(QStringLiteral("已经登记过")), qPrintable(error));
    // 重复登记不能悄悄覆盖前一条：覆盖会让「后登记的那份实现生效」成为
    // 一个只有读 add() 才知道的事实。
    QCOMPARE(registry.count(), 1);
}

void TstSessionType::registryRejectsEmptyDisplayName()
{
    SessionTypeRegistry registry;
    SessionType type = syntheticType(QStringLiteral("alpha"));
    type.displayName.clear();
    QString error;
    QVERIFY(!registry.add(type, SessionFactory(), &error));
    QVERIFY2(error.contains(QStringLiteral("没有显示名")), qPrintable(error));
}

void TstSessionType::registryRejectsUnparseableMask()
{
    SessionTypeRegistry registry;
    SessionType type = syntheticType(QStringLiteral("alpha"));
    // 未闭合的字符集。掩码语言把这一条判成语法错误而不是字面量
    // （见 FILT-001）：当字面量会让「漏写 ]」变成「永远匹配不到」。
    type.fileMasks << QStringLiteral("[abc");
    QString error;
    QVERIFY(!registry.add(type, SessionFactory(), &error));
    QVERIFY2(error.contains(QStringLiteral("[abc")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("alpha")), qPrintable(error));
    QVERIFY(registry.isEmpty());
}

void TstSessionType::addClearsTheErrorOnSuccess()
{
    SessionTypeRegistry registry;
    QString error = QStringLiteral("上一次的残留");
    QVERIFY(registry.add(syntheticType(QStringLiteral("alpha")), SessionFactory(), &error));
    // 不清空的话，调用方「先尝试登记若干条、最后统一看 error」的写法会把
    // 首次成功之前的一条旧错误当成最终结论。
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void TstSessionType::builtInTableValidatesClean()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    const QStringList problems = registry.validate();
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
}

void TstSessionType::idsAreStableAcrossRebuilds()
{
    // builtInSessionTypes() **每次调用都重建**（表里有可翻译文案，缓存成
    // 函数内静态会让「先取表、后切换语言」把旧语言留在界面里）。
    // 这条用例盯的就是那次重建：ID 是机器键，绝不能跟着重建变化。
    const QStringList first = builtInSessionTypeIds();
    const QStringList second = builtInSessionTypeIds();
    QCOMPARE(first, second);

    // 英文原名同理——它是 /fv 的取值，也必须是稳定的。
    QStringList firstEnglish;
    QStringList secondEnglish;
    for (const SessionType &type : builtInSessionTypes()) {
        firstEnglish.append(type.englishName);
    }
    for (const SessionType &type : builtInSessionTypes()) {
        secondEnglish.append(type.englishName);
    }
    QCOMPARE(firstEnglish, secondEnglish);
}

void TstSessionType::englishNamesAreStableAndNotTranslated()
{
    // 英文原名是对照研究基线与命令行 /fv 的锚点，不能被翻译。
    // 「不含中日韩字符」是一个便宜但有效的检查：哪天有人把 displayName 的中文
    // 顺手复制进 englishName，这条会红。
    for (const SessionType &type : builtInSessionTypes()) {
        for (const QChar &character : type.englishName) {
            QVERIFY2(character.unicode() < 0x2E80,
                     qPrintable(type.id + QStringLiteral(" 的英文原名含非拉丁字符：")
                                + type.englishName));
        }
    }
    // 官方英文名与 ID 一一对应：ID 只是官方名的机器化写法，因此不含空格，
    // 且两者去空格后忽略大小写应当有可辨认的对应关系（这里只做一个弱断言：
    // 任何英文名都不等于自己的 ID，否则说明有人偷懒把 ID 抄进英文名）。
    for (const SessionType &type : builtInSessionTypes()) {
        QVERIFY2(type.englishName.compare(type.id, Qt::CaseInsensitive) != 0,
                 qPrintable(type.id));
    }
}

// =============================================================================
// C 按掩码查询与注册顺序优先（SESS-002 第 3 条）
// -----------------------------------------------------------------------------
// 完成标准原文：按掩码查询匹配类型时按注册顺序（优先级）返回首个命中。
// =============================================================================

void TstSessionType::findByMaskReturnsTheFirstRegisteredMatch()
{
    SessionTypeRegistry registry;
    SessionType first = syntheticType(QStringLiteral("first-type"));
    first.fileMasks << QStringLiteral("*.same");
    SessionType second = syntheticType(QStringLiteral("second-type"));
    second.fileMasks << QStringLiteral("*.same");
    QVERIFY(registry.add(first));
    QVERIFY(registry.add(second));

    const SessionTypeEntry *found = registry.findByFileMask(QStringLiteral("x.same"));
    QVERIFY(found != nullptr);
    QCOMPARE(found->type.id, QStringLiteral("first-type"));
}

void TstSessionType::findByMaskFollowsRegistrationOrderNotTableOrder()
{
    // 与上一条的区别：这次把两个类型**反着登记**，结论必须跟着反过来。
    // 只跑一种顺序的话，一个「总是返回第一个条目」的实现也能通过——
    // 那它就不是在按注册顺序，而是在按插入位置碰巧对上了。
    SessionTypeRegistry registry;
    SessionType second = syntheticType(QStringLiteral("second-type"));
    second.fileMasks << QStringLiteral("*.same");
    SessionType first = syntheticType(QStringLiteral("first-type"));
    first.fileMasks << QStringLiteral("*.same");
    QVERIFY(registry.add(second));
    QVERIFY(registry.add(first));

    const SessionTypeEntry *found = registry.findByFileMask(QStringLiteral("x.same"));
    QVERIFY(found != nullptr);
    QCOMPARE(found->type.id, QStringLiteral("second-type"));
}

void TstSessionType::findByMaskReturnsNullWhenNothingMatches()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    // 没有任何类型认领的扩展名。调用方据此走兜底（例如按二进制判定落到十六进制）。
    QVERIFY(registry.findByFileMask(QStringLiteral("blob.zzz")) == nullptr);
}

void TstSessionType::findByMaskReturnsNullForAnEmptyName()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    // 空名字落到 nullptr 而不是「匹配到第一个带 `*` 的类型」：
    // `MaskSubject::isValid()` 为假时任何掩码都不该命中。这条方向与
    // maskfilter.h 的 decide() 一致——不可见的条目保持不可见，
    // 不要让它悄悄出现在结果里。
    QVERIFY(registry.findByFileMask(QString()) == nullptr);
    QVERIFY(registry.allByFileMask(QString()).isEmpty());
}

void TstSessionType::findByMaskSkipsTypesUnavailableOnThisPlatform()
{
    SessionTypeRegistry registry;
    SessionType windowsOnly = syntheticType(QStringLiteral("win-only"));
    windowsOnly.platforms = SessionPlatformScope::WindowsOnly;
    windowsOnly.fileMasks << QStringLiteral("*.zzz");
    SessionType general = syntheticType(QStringLiteral("general"));
    general.fileMasks << QStringLiteral("*.zzz");
    QVERIFY(registry.add(windowsOnly));
    QVERIFY(registry.add(general));

    const SessionTypeEntry *found = registry.findByFileMask(QStringLiteral("x.zzz"));
    QVERIFY(found != nullptr);

    // 期望值由平台规则算出来，而不是用 #ifdef 分两支：这样在任一平台上
    // 这条用例都在断言同一件事——「不可用的类型不参选」。
    const bool windowsAllowed = currentPlatformAllows(SessionPlatformScope::WindowsOnly);
    QCOMPARE(found->type.id,
             windowsAllowed ? QStringLiteral("win-only") : QStringLiteral("general"));

    const QVector<const SessionTypeEntry *> all = registry.allByFileMask(QStringLiteral("x.zzz"));
    QCOMPARE(all.size(), windowsAllowed ? 2 : 1);
}

void TstSessionType::allByMaskKeepsRegistrationOrder()
{
    SessionTypeRegistry registry;
    for (const QString &id : {QStringLiteral("alpha"), QStringLiteral("beta"),
                              QStringLiteral("gamma")}) {
        SessionType type = syntheticType(id);
        type.fileMasks << QStringLiteral("*.same");
        QVERIFY(registry.add(type));
    }
    const QVector<const SessionTypeEntry *> all = registry.allByFileMask(QStringLiteral("x.same"));
    QCOMPARE(all.size(), 3);
    QCOMPARE(all.at(0)->type.id, QStringLiteral("alpha"));
    QCOMPARE(all.at(1)->type.id, QStringLiteral("beta"));
    QCOMPARE(all.at(2)->type.id, QStringLiteral("gamma"));
}

void TstSessionType::allByMaskCountsEachTypeOnceWhenTwoMasksHit()
{
    SessionTypeRegistry registry;
    SessionType type = syntheticType(QStringLiteral("alpha"));
    // 两条掩码都命中 a.txt。按类型去重而不是按掩码：同一个类型出现两次会让
    // 界面上的候选列表里出现两个一模一样的条目，用户看不出该选哪个。
    type.fileMasks << QStringLiteral("*.txt") << QStringLiteral("a.*");
    QVERIFY(registry.add(type));

    const QVector<const SessionTypeEntry *> all = registry.allByFileMask(QStringLiteral("a.txt"));
    QCOMPARE(all.size(), 1);
    QCOMPARE(all.first()->type.id, QStringLiteral("alpha"));
}

void TstSessionType::allByMaskKeepsTheOnlyRealOverlap()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    // `*.html` 同时被文本比对与表格比对声明——这是内置表里**唯一**一处重叠，
    // 有意留的：它让「按注册顺序返回首个命中」这条规则有一份真实数据上的用例，
    // 而不是只在合成的小表里成立。
    //
    // 当前结论：文本胜出（文本先登记）。要让表格胜出是一次产品决定，
    // 改的时候这条用例会红，从而强制做那次决定。
    const QVector<const SessionTypeEntry *> html = registry.allByFileMask(QStringLiteral("x.html"));
    QCOMPARE(html.size(), 2);
    QCOMPARE(html.at(0)->type.id, QStringLiteral("text"));
    QCOMPARE(html.at(1)->type.id, QStringLiteral("table"));

    const SessionTypeEntry *chosen = registry.findByFileMask(QStringLiteral("x.html"));
    QVERIFY(chosen != nullptr);
    QCOMPARE(chosen->type.id, QStringLiteral("text"));

    // 反面：`.txt` 只被文本比对认领，候选列表必须只有一条。
    const QVector<const SessionTypeEntry *> txt = registry.allByFileMask(QStringLiteral("x.txt"));
    QCOMPARE(txt.size(), 1);
    QCOMPARE(txt.first()->type.id, QStringLiteral("text"));
}

void TstSessionType::onlyHtmlOverlapsInTheBuiltInTable()
{
    // 把「重叠」这件事变成可枚举的事实：内置表里任何一个扩展名被两个以上类型
    // 声明，就必须在这里显式登记。新增一处歧义而没人注意，会让「双击某个文件
    // 打开哪个视图」悄悄改变——而那种改变在界面上完全看不出来。
    QHash<QString, QStringList> owners;
    for (const SessionType &type : builtInSessionTypes()) {
        for (const QString &mask : type.fileMasks) {
            QStringList &list = owners[mask];
            if (!list.contains(type.id)) {
                list.append(type.id);
            }
        }
    }

    QStringList overlaps;
    for (auto it = owners.constBegin(); it != owners.constEnd(); ++it) {
        if (it.value().size() > 1) {
            overlaps.append(it.key() + QStringLiteral("（") + it.value().join(QLatin1Char(','))
                           + QStringLiteral("）"));
        }
    }
    overlaps.sort();
    QCOMPARE(overlaps, QStringList({QStringLiteral("*.html（text,table）")}));
}

void TstSessionType::maskMatchingHonoursTheExplicitCaseSensitivity()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    // 显式传大小写策略而不是依赖平台默认值：这样「Windows 上大小写不敏感」
    // 这条规则在 macOS 上也能被真实断言。与 mask.h 的 defaultCaseSensitivity
    // 同一手法——把平台规则抽成纯函数。
    QVERIFY(registry.findByFileMask(QStringLiteral("A.TXT"), Qt::CaseSensitive) == nullptr);

    const SessionTypeEntry *insensitive
            = registry.findByFileMask(QStringLiteral("A.TXT"), Qt::CaseInsensitive);
    QVERIFY(insensitive != nullptr);
    QCOMPARE(insensitive->type.id, QStringLiteral("text"));

    // 大小写敏感时 `.TXT` 就是没被任何类型认领——不是「匹配到文本但标记为可疑」。
    QVERIFY(registry.allByFileMask(QStringLiteral("A.TXT"), Qt::CaseSensitive).isEmpty());
}

void TstSessionType::caseSensitivityOverrideAffectsTheConvenienceOverload()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    QVERIFY(!registry.isCaseSensitivityOverridden());
    // 平台默认值必须与 Filter 模块共用同一个事实来源，否则同一次比较里
    // 「按掩码过滤」与「按掩码选视图」会对同一个扩展名给出不同结论。
    QCOMPARE(registry.caseSensitivity(),
             LqCompare::Filter::defaultCaseSensitivity(LqCompare::Filter::currentMaskPlatform()));

    registry.setCaseSensitivity(Qt::CaseInsensitive);
    QVERIFY(registry.isCaseSensitivityOverridden());
    QCOMPARE(registry.caseSensitivity(), Qt::CaseInsensitive);
    QVERIFY(registry.findByFileMask(QStringLiteral("A.TXT")) != nullptr);

    registry.setCaseSensitivity(Qt::CaseSensitive);
    QVERIFY(registry.findByFileMask(QStringLiteral("A.TXT")) == nullptr);
}

void TstSessionType::clearCaseSensitivityOverrideReturnsToThePlatformDefault()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    registry.setCaseSensitivity(Qt::CaseInsensitive);
    registry.clearCaseSensitivityOverride();

    QVERIFY(!registry.isCaseSensitivityOverridden());
    QCOMPARE(registry.caseSensitivity(),
             LqCompare::Filter::defaultCaseSensitivity(LqCompare::Filter::currentMaskPlatform()));
}

void TstSessionType::masksMatchTheNameNotTheWholeRelativePath()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    // `*.txt` 不含 `/`，按**名字**匹配，因此任意目录下的 .txt 都算数。
    // 若拿整条相对路径去匹配它，`**` 不跨目录的规则会让 `src/a.txt` 落空——
    // 而「文件掩码」在所有人心里都是「任意目录下的 .txt」。
    const SessionTypeEntry *found = registry.findByFileMask(QStringLiteral("src/deep/a.txt"),
                                                            Qt::CaseSensitive);
    QVERIFY(found != nullptr);
    QCOMPARE(found->type.id, QStringLiteral("text"));
}

void TstSessionType::crossDirectoryMaskWorksBecauseTheFullMaskLanguageIsReused()
{
    // 自定义掩码（SESS-015 / FILT-007 里的预设）可以带 `/`。这条用例证明注册表
    // 复用的是**完整的掩码语言**（FILT-001），而不是自己写的一套「只看扩展名」的
    // 简化匹配——后者迟早会在 FILT 系列落地时跟真正的语法分家。
    SessionTypeRegistry registry;
    SessionType scoped = syntheticType(QStringLiteral("scoped-type"));
    scoped.fileMasks << QStringLiteral("src/*.txt");
    QVERIFY(registry.add(scoped));

    const SessionTypeEntry *inside = registry.findByFileMask(QStringLiteral("src/a.txt"),
                                                             Qt::CaseSensitive);
    QVERIFY(inside != nullptr);
    QCOMPARE(inside->type.id, QStringLiteral("scoped-type"));

    // 含 `/` 的掩码按相对路径从起点匹配，因此别处的同名子树不该被误伤。
    QVERIFY(registry.findByFileMask(QStringLiteral("other/a.txt"), Qt::CaseSensitive) == nullptr);
    // 根目录下的 a.txt 也不该命中 `src/*.txt`。
    QVERIFY(registry.findByFileMask(QStringLiteral("a.txt"), Qt::CaseSensitive) == nullptr);
}

// =============================================================================
// D 可枚举（SESS-002 第 4 条）
// -----------------------------------------------------------------------------
// 完成标准原文：注册表可枚举，供 Home 页与新建向导直接生成入口。
// =============================================================================

void TstSessionType::groupsAreReturnedInAFixedOrder()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    const QVector<SessionGroup> groups = registry.groups(false);
    QCOMPARE(groups.size(), 4);
    QCOMPARE(groups.at(0), SessionGroup::Text);
    QCOMPARE(groups.at(1), SessionGroup::Folders);
    QCOMPARE(groups.at(2), SessionGroup::Data);
    QCOMPARE(groups.at(3), SessionGroup::Advanced);

    // 顺序来自枚举本身而不是「首次出现的顺序」：后者会让 Home 页的分区
    // 在两次启动之间换位置（只要数据顺序变了一点）。
    QCOMPARE(QString::fromLatin1(sessionGroupIdentifier(SessionGroup::Text)),
             QStringLiteral("text"));
    QCOMPARE(QString::fromLatin1(sessionGroupIdentifier(SessionGroup::Advanced)),
             QStringLiteral("advanced"));
}

void TstSessionType::byGroupKeepsRegistrationOrderWithinTheGroup()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    const QVector<const SessionTypeEntry *> textGroup
            = registry.byGroup(SessionGroup::Text, false);
    QCOMPARE(textGroup.size(), 4);
    QCOMPARE(textGroup.at(0)->type.id, QStringLiteral("text"));
    QCOMPARE(textGroup.at(1)->type.id, QStringLiteral("text-merge"));
    QCOMPARE(textGroup.at(2)->type.id, QStringLiteral("text-edit"));
    QCOMPARE(textGroup.at(3)->type.id, QStringLiteral("text-patch"));

    const QVector<const SessionTypeEntry *> advancedGroup
            = registry.byGroup(SessionGroup::Advanced, false);
    QCOMPARE(advancedGroup.size(), 3);
    QCOMPARE(advancedGroup.at(0)->type.id, QStringLiteral("registry"));
    QCOMPARE(advancedGroup.at(1)->type.id, QStringLiteral("version"));
    QCOMPARE(advancedGroup.at(2)->type.id, QStringLiteral("archive"));
}

void TstSessionType::byGroupSkipsUnavailableTypesByDefault()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    const bool windowsAllowed = currentPlatformAllows(SessionPlatformScope::WindowsOnly);
    // 高级组三条里两条是仅 Windows。非 Windows 上只剩压缩包。
    QCOMPARE(registry.byGroup(SessionGroup::Advanced, false).size(), 3);
    QCOMPARE(registry.byGroup(SessionGroup::Advanced, true).size(), windowsAllowed ? 3 : 1);

    // 默认参数就是「只看可用的」：界面拿到一份可以直接点的列表，
    // 不可用的那几条由 Home 页单独置灰展示（REG-001 要求给出原因）。
    QCOMPARE(registry.byGroup(SessionGroup::Advanced).size(),
             registry.byGroup(SessionGroup::Advanced, true).size());
}

void TstSessionType::byGroupCanIncludeUnavailableTypes()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    // 置灰展示需要拿到「不可用的那几条」本身——只有排除掉它们的接口，
    // Home 页就只能自己再维护一份「哪些是仅 Windows」的名单，
    // 而那正是第二条事实来源。
    const QVector<const SessionTypeEntry *> all = registry.byGroup(SessionGroup::Advanced, false);
    int unavailable = 0;
    for (const SessionTypeEntry *entry : all) {
        if (!entry->isAvailableHere()) {
            ++unavailable;
            QVERIFY2(!entry->unavailableReason().isEmpty(), qPrintable(entry->type.id));
        }
    }
    const bool windowsAllowed = currentPlatformAllows(SessionPlatformScope::WindowsOnly);
    QCOMPARE(unavailable, windowsAllowed ? 0 : 2);
}

void TstSessionType::enumerationCoversEveryIdExactlyOnce()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    QStringList collected;
    for (SessionGroup group : registry.groups(false)) {
        for (const SessionTypeEntry *entry : registry.byGroup(group, false)) {
            collected.append(entry->type.id);
        }
    }
    // 枚举顺序与注册顺序一致，因此整表可以直接比对：这同时证明了
    // 「分组枚举没有漏掉任何一条、也没有把某一条算进两个组」。
    QCOMPARE(collected, builtInSessionTypeIds());
}

void TstSessionType::homePageHardcodedIdsMatchTheRegistry()
{
    // SESS-002 落地前，类型 ID 的事实来源是 `HomePage::sections()` 里那张
    // 硬编码的表——它是事实上的第一批「已发布 ID」。注册表落地后**必须与它一致**，
    // 否则 Home 页点出来的入口会指向不存在的类型（现象是「点卡片没反应」）。
    //
    // 为什么不干脆让 HomePage 改成读注册表：那是 SESS-003（Home 视图）的范围，
    // 本轮改了会与它撞车。折中方案是用一条**源码级**用例把两边钉在一起——
    // 与 SESS-001 用源码级用例守「基类不依赖具体视图」是同一个手法。
    const QString source = readSourceFile(QStringLiteral("/Views/Shell/homepage.cpp"));
    const QString body = sectionsBodyOf(source);
    QVERIFY2(!body.isEmpty(),
             "在 homepage.cpp 里找不到 sections() 的函数体——它可能被改名或改了签名，"
             "请同步更新这条护栏（恒真的护栏比没有护栏更糟）");

    const QStringList fromHomePage = typeIdsInBody(body);
    QCOMPARE(fromHomePage, builtInSessionTypeIds());
}

void TstSessionType::homePageIdCheckCanFailOnBrokenSource()
{
    // 反向验证：拿一段**结构相同但 ID 写错**的源码文本跑同一个判定流程。
    // 如果没有这条，`homePageHardcodedIdsMatchTheRegistry` 有可能因为
    // 「正则没匹配上、两边都是空集合」而恒真通过——那比没有护栏更糟。
    const QString broken = QStringLiteral(
        "QList<HomePage::Section> HomePage::sections() const\n"
        "{\n"
        "    return {\n"
        "        {tr(\"Text\"), tr(\"忽略这一段\"),\n"
        "         {{QStringLiteral(\"text\"), tr(\"Text Compare\")},\n"
        "          {QStringLiteral(\"text-typo\"), tr(\"Text Merge\")}}},\n"
        "    };\n"
        "}\n");

    const QString body = sectionsBodyOf(broken);
    QVERIFY2(!body.isEmpty(), "函数体定位失败");

    const QStringList ids = typeIdsInBody(body);
    QCOMPARE(ids, QStringList({QStringLiteral("text"), QStringLiteral("text-typo")}));

    // 关键的一条：它必须与注册表**不等**。等于就说明这条判定恒真。
    QVERIFY(ids != builtInSessionTypeIds());

    // 另外两句反向验证：函数被改名时返回空（而不是「空对空」地通过），
    // 以及只有 QStringLiteral 会被当类型 ID（tr() 里的文案不算）。
    QVERIFY(sectionsBodyOf(QStringLiteral("void Other::thing() const\n{\n}\n")).isEmpty());
    QVERIFY(typeIdsInBody(QStringLiteral("tr(\"Text\") tr(\"Folders\")")).isEmpty());
}

// =============================================================================
// E 按名字查（为 CLI-002 备好）
// -----------------------------------------------------------------------------
// 本轮**不主张** CLI-002 已完成：那条 issue 还要求「类型与参数个数不匹配时给出
// 明确错误」与 `--list-session-types` 的输出格式，那些都没有落地。
// 这里只保证注册表能回答「用户敲的这个名字是哪个类型」。
// =============================================================================

void TstSessionType::findByNameMatchesIdCaseInsensitively()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    const SessionTypeEntry *found = registry.findByName(QStringLiteral("HEX"));
    QVERIFY(found != nullptr);
    QCOMPARE(found->type.id, QStringLiteral("hex"));
}

void TstSessionType::findByNameMatchesEnglishNameCaseInsensitively()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    // `/fv="Folder Merge"` 是研究基线里出现过的写法（SESS-010 的陷阱列）。
    const SessionTypeEntry *found = registry.findByName(QStringLiteral("folder merge"));
    QVERIFY(found != nullptr);
    QCOMPARE(found->type.id, QStringLiteral("folder-merge"));

    const SessionTypeEntry *exact = registry.findByName(QStringLiteral("Hex Compare"));
    QVERIFY(exact != nullptr);
    QCOMPARE(exact->type.id, QStringLiteral("hex"));
}

void TstSessionType::findByNameMatchesDisplayName()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    const SessionTypeEntry *found = registry.findByName(QStringLiteral("十六进制比较会话"));
    QVERIFY(found != nullptr);
    QCOMPARE(found->type.id, QStringLiteral("hex"));
}

void TstSessionType::findByNamePrefersIdOverDisplayName()
{
    // 合成表：第二条的显示名恰好等于第一条的 ID。命中哪一条不能取决于登记顺序
    // ——「用户敲的名字」这件事应该由名字的确定性决定，因此 ID 优先。
    SessionTypeRegistry registry;
    SessionType first = syntheticType(QStringLiteral("alpha"));
    SessionType second = syntheticType(QStringLiteral("beta"));
    second.displayName = QStringLiteral("alpha");
    QVERIFY(registry.add(first));
    QVERIFY(registry.add(second));

    const SessionTypeEntry *found = registry.findByName(QStringLiteral("alpha"));
    QVERIFY(found != nullptr);
    QCOMPARE(found->type.id, QStringLiteral("alpha"));
}

void TstSessionType::findByNameReturnsNullForUnknownOrEmptyName()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    QVERIFY(registry.findByName(QStringLiteral("No Such Type")) == nullptr);
    QVERIFY(registry.findByName(QString()) == nullptr);
}

// =============================================================================
// F 自检、诊断与源码级护栏
// =============================================================================

void TstSessionType::validateReportsMissingEnglishName()
{
    // `add()` 刻意不强制英文原名（它不影响任何运行时行为，只影响命令行 /fv
    // 与规格对照），因此这一条是 validate() 里真的走得通的分支之一。
    SessionTypeRegistry registry;
    SessionType type = syntheticType(QStringLiteral("alpha"));
    type.englishName.clear();
    QVERIFY(registry.add(type));

    const QStringList problems = registry.validate();
    QCOMPARE(problems.size(), 1);
    QVERIFY2(problems.first().contains(QStringLiteral("alpha")), qPrintable(problems.first()));
    QVERIFY2(problems.first().contains(QStringLiteral("英文原名")), qPrintable(problems.first()));
}

void TstSessionType::validateDetectsTheAuthoringConventions()
{
    // validate() 是「手写表时的约定」这条规则的唯一实现，因此必须逐条证明
    // 它真的会报——否则 `builtInTableValidatesClean` 的「空列表」可能只是
    // 因为校验器什么都不查。
    SessionTypeRegistry registry;

    SessionType badIcon = syntheticType(QStringLiteral("alpha"));
    // 拼出来而不是写成完整路径字面量：`:Pictures/<名字>.svg` 这样的完整字符串
    // 一旦出现在源码里（**注释里也算**），tools/check_icons.py 的
    // REFERENCE_PATTERN 会把它当成一次真实引用，于是那条护栏会报
    // 「引用了未声明的图标」。我们造的是一个**假的**写法，不该让静态检查
    // 以为它真的存在。这个坑在本轮真的踩了一次——最初就是写在注释里的。
    badIcon.iconKey = QStringLiteral(":/Pictures/") + QStringLiteral("ribbon_open")
            + QLatin1Char('.') + QStringLiteral("png");
    QVERIFY(registry.add(badIcon));

    SessionType badMask = syntheticType(QStringLiteral("beta"));
    badMask.fileMasks << QStringLiteral("*.TXT");
    QVERIFY(registry.add(badMask));

    const QStringList problems = registry.validate();
    QCOMPARE(problems.size(), 2);
    QVERIFY2(problems.at(0).contains(QStringLiteral("图标键")), qPrintable(problems.at(0)));
    QVERIFY2(problems.at(1).contains(QStringLiteral("大写")), qPrintable(problems.at(1)));

    // 再确认一遍内置表是干净的（与 builtInTableValidatesClean 互为反向）。
    SessionTypeRegistry builtIn;
    builtIn.addBuiltInTypes();
    QVERIFY(builtIn.validate().isEmpty());
}

void TstSessionType::registryStartsEmptyAndClearRemovesEverything()
{
    SessionTypeRegistry registry;
    QVERIFY(registry.isEmpty());
    QCOMPARE(registry.count(), 0);
    QVERIFY(registry.entries().isEmpty());
    QVERIFY(registry.groups().isEmpty());
    QVERIFY(registry.find(QStringLiteral("text")) == nullptr);

    registry.addBuiltInTypes();
    QCOMPARE(registry.count(), 14);
    registry.clear();
    QVERIFY(registry.isEmpty());
    QVERIFY(registry.find(QStringLiteral("text")) == nullptr);
}

void TstSessionType::describeListsEveryTypeAndItsFlags()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();
    const QString text = registry.describe();

    for (const QString &id : builtInSessionTypeIds()) {
        QVERIFY2(text.contains(id), qPrintable(QStringLiteral("诊断输出缺少类型：") + id));
    }
    // 标记要能被数出来：三个 Pro、两个仅 Windows。诊断输出是「按图索骥找
    // 哪个类型没实现」时要读的东西，标记漏了就等于没提供这个信息。
    QCOMPARE(text.count(QStringLiteral("[Pro]")), 3);
    QCOMPARE(text.count(QStringLiteral("[仅 Windows]")), 2);
    // 内置表全部没有工厂，因此每一条都该标「尚无实现」。
    QCOMPARE(text.count(QStringLiteral("（尚无实现）")), 14);
}

void TstSessionType::typeDescribeExplainsTheMissingMask()
{
    SessionTypeRegistry registry;
    registry.addBuiltInTypes();

    const SessionTypeEntry *folder = registry.find(QStringLiteral("folder"));
    QVERIFY(folder != nullptr);
    // 空掩码有四种成因（目录类型 / 兜底类型 / 辅助视图 / 忘了写），
    // 诊断输出必须把「本来就没有」说清楚，否则「为什么这个类型从不被自动选中」
    // 会被当成缺陷反复排查。
    QVERIFY2(folder->type.describe().contains(QStringLiteral("掩码：（无")),
             qPrintable(folder->type.describe()));

    const SessionTypeEntry *text = registry.find(QStringLiteral("text"));
    QVERIFY(text != nullptr);
    QVERIFY2(text->type.describe().contains(QStringLiteral("*.txt")),
             qPrintable(text->type.describe()));
    QVERIFY(text->type.describe().contains(QStringLiteral("Text Compare")));
    QVERIFY(text->type.describe().contains(QStringLiteral("图标：（未指定）")));
}

void TstSessionType::iconKeysPointAtDeclaredResources()
{
    // 图标键当前全部为空（会话类型的图标属于 SESS-003 的资源批次）。
    // 这条护栏是**为将来准备的**：哪天有人填进来，它必须在 Pictures.qrc 里
    // 真实存在。tools/check_icons.py 看不到运行期拼出来的资源路径，
    // 因此这一层只能由用例来守。
    const QString qrc = readSourceFile(QStringLiteral("/Pictures/Pictures.qrc"));
    const QStringList declared = qrcFileEntries(qrc);
    QVERIFY2(declared.size() >= 20, "Pictures.qrc 解析出的图标太少，正则可能不匹配了");

    const QStringList undeclared = undeclaredIconKeys(builtInSessionTypes(), declared);
    QVERIFY2(undeclared.isEmpty(),
             qPrintable(QStringLiteral("类型声明了但 qrc 里没有的图标：")
                        + undeclared.join(QLatin1Char(','))));

    // 顺带钉住「当前一个都没填」这个事实。填的时候这条会红，提醒同时确认
    // 那张卡片的视觉与 tooltip 文案，而不是让图标悄悄出现在界面上。
    int filled = 0;
    for (const SessionType &type : builtInSessionTypes()) {
        if (!type.iconKey.isEmpty()) {
            ++filled;
        }
    }
    QCOMPARE(filled, 0);
}

void TstSessionType::iconKeyCheckCanFailOnAFabricatedKey()
{
    const QString qrc = readSourceFile(QStringLiteral("/Pictures/Pictures.qrc"));
    const QStringList declared = qrcFileEntries(qrc);

    // 造一个 qrc 里不存在的图标键。
    //
    // **注意这里怎么拼出来的**：`:Pictures/<名字>.svg` 这样的完整字符串一旦
    // 出现在源码里（注释里也算），tools/check_icons.py 的 REFERENCE_PATTERN
    // 会把它当成一次真实引用，于是护栏报「代码引用了未声明的图标」。
    // 拆成三段拼接就绕开了那个正则——我们是在测试里造一个**假**路径，
    // 不该让静态检查以为它真的存在。
    const QString bogus = QStringLiteral(":/Pictures/") + QStringLiteral("session_missing")
            + QLatin1Char('.') + QStringLiteral("svg");

    QVector<SessionType> types = builtInSessionTypes();
    types.first().iconKey = bogus;

    const QStringList undeclared = undeclaredIconKeys(types, declared);
    QCOMPARE(undeclared, QStringList({QStringLiteral("session_missing.svg")}));

    // 再说一遍反向验证的意思：如果 undeclaredIconKeys 恒返回空，
    // 上面这条就会红。它不恒真，因此 iconKeysPointAtDeclaredResources
    // 的「空列表」才是有效结论。
    QVERIFY(!undeclared.isEmpty());

    // 而真实的表仍然是干净的——假路径只存在于这条用例的局部变量里。
    QVERIFY(undeclaredIconKeys(builtInSessionTypes(), declared).isEmpty());
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
// 本工程写 `QT -= gui`，于是 QTEST_MAIN 展开成 QCoreApplication —— 这也正是
// 「服务层不依赖界面」这条铁律在本套件上的体现（与 Tests/Logging 同）。
QTEST_MAIN(TstSessionType)
