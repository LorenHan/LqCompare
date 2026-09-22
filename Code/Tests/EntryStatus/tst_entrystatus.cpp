#include "tst_entrystatus.h"

#include "entrystatus.h"

#include <QFile>
#include <QSet>
#include <functional>

using namespace LqCompare;

namespace {

Folder::Side fileSide(const QString &name, quint64 size, qint64 seconds)
{
    Folder::Side side;
    side.kind = Folder::Kind::File;
    side.info.path = QStringLiteral("/tree/") + name;
    side.info.name = name;
    side.info.size = size;
    side.info.exists = true;
    side.info.lastModified = Files::FileTime::fromSecondsSinceEpoch(seconds);
    return side;
}

Folder::Side directorySide(const QString &name, qint64 seconds)
{
    Folder::Side side = fileSide(name, 0, seconds);
    side.kind = Folder::Kind::Directory;
    side.info.isDirectory = true;
    return side;
}

Folder::Side linkSide(const QString &name, qint64 seconds)
{
    Folder::Side side = fileSide(name, 0, seconds);
    side.kind = Folder::Kind::SymbolicLink;
    side.info.isSymLink = true;
    return side;
}

Folder::Side missingSide(const QString &name)
{
    Folder::Side side;
    side.kind = Folder::Kind::Missing;
    side.info.path = QStringLiteral("/tree/") + name;
    side.info.name = name;
    return side;
}

Folder::Entry entryOf(const QString &path, const Folder::Side &left, const Folder::Side &right)
{
    Folder::Entry entry;
    entry.relativePath = path;
    entry.left = left;
    entry.right = right;
    return entry;
}

Folder::BaselineView baselineOf(const QString &path, quint64 size, qint64 seconds)
{
    Folder::BaselineView baseline;
    baseline.leftRoot = QStringLiteral("/left");
    baseline.rightRoot = QStringLiteral("/right");
    Folder::AncestorRecord record;
    record.size = size;
    record.modified = Files::FileTime::fromSecondsSinceEpoch(seconds);
    baseline.ancestors.insert(path, record);
    baseline.valid = true;
    return baseline;
}

QString joinedReasonText(const QVector<Folder::ReasonLine> &lines)
{
    QStringList text;
    for (const auto &line : lines)
        text << line.text;
    return text.join(QLatin1Char('\n'));
}

QStringList textsOfKind(const QVector<Folder::ReasonLine> &lines, Folder::ReasonKind kind)
{
    QStringList text;
    for (const auto &line : lines) {
        if (line.kind == kind)
            text << line.text;
    }
    return text;
}

} // namespace

// -----------------------------------------------------------------------------
// A 主状态：互斥集合、稳定标识符、图标
// -----------------------------------------------------------------------------

void EntryStatusTests::mainStatusTableCoversTheSpecifiedSetExactlyOnce()
{
    const auto &table = Folder::mainStatusTable();
    QCOMPARE(table.size(), 9);
    QSet<int> seen;
    for (const auto &row : table) {
        QVERIFY2(!seen.contains(int(row.value)), "主状态表里出现重复取值 —— 互斥集合被破坏了");
        seen.insert(int(row.value));
    }
    // 规格点名的六档一个都不能少。逐个写出而不是数个数：少一档又混进一个
    // 别的取值时，个数仍然对得上。
    for (Folder::Status status : {Folder::Status::Same, Folder::Status::Different,
             Folder::Status::LeftOnly, Folder::Status::RightOnly,
             Folder::Status::Error, Folder::Status::Unknown}) {
        QVERIFY2(seen.contains(int(status)),
                 qPrintable(QStringLiteral("主状态表缺少规格点名的取值 %1").arg(int(status))));
    }
}

void EntryStatusTests::mainStatusIdentifiersAreTheEstablishedMachineWords()
{
    // 这一组字面量是命令行的 JSON 契约。它们同时是报表与命令行的机器标识，
    // 因此断言里把确切的值写出来——「唯一」这件事由下一段保证，
    // 而「换成别的词」必须让本用例变红。
    QCOMPARE(Folder::statusIdentifier(Folder::Status::Same), QStringLiteral("equal"));
    QCOMPARE(Folder::statusIdentifier(Folder::Status::Different), QStringLiteral("different"));
    QCOMPARE(Folder::statusIdentifier(Folder::Status::LeftOnly), QStringLiteral("left-only"));
    QCOMPARE(Folder::statusIdentifier(Folder::Status::RightOnly), QStringLiteral("right-only"));
    QCOMPARE(Folder::statusIdentifier(Folder::Status::TypeConflict), QStringLiteral("type-conflict"));
    QCOMPARE(Folder::statusIdentifier(Folder::Status::Error), QStringLiteral("error"));
    QCOMPARE(Folder::statusIdentifier(Folder::Status::Unknown), QStringLiteral("not-compared"));
    QCOMPARE(Folder::statusIdentifier(Folder::Status::BothChanged), QStringLiteral("both-changed"));
    QCOMPARE(Folder::statusIdentifier(Folder::Status::Conflict), QStringLiteral("conflict"));

    QSet<QString> identifiers;
    for (const auto &row : Folder::mainStatusTable()) {
        const QString identifier = Folder::statusIdentifier(row.value);
        QVERIFY2(!identifier.isEmpty(), "主状态表里出现空标识符");
        QVERIFY2(!identifiers.contains(identifier), qPrintable(identifier));
        identifiers.insert(identifier);
    }
    QCOMPARE(identifiers.size(), Folder::mainStatusTable().size());
}

void EntryStatusTests::everyStatusHasItsOwnIcon()
{
    QSet<QString> icons;
    for (const auto &row : Folder::mainStatusTable()) {
        const QString icon = Folder::statusIconKey(row.value);
        QVERIFY2(!icon.isEmpty(), qPrintable(QStringLiteral("取值 %1 没有状态图标 —— 颜色之外就只剩颜色了")
                                                .arg(int(row.value))));
        QVERIFY2(!icons.contains(icon), qPrintable(icon));
        icons.insert(icon);
    }
    QCOMPARE(icons.size(), Folder::mainStatusTable().size());
}

void EntryStatusTests::statusIconsAreDeclaredInTheResourceFile()
{
    QFile qrc(QStringLiteral(LQCOMPARE_CODE_ROOT) + QStringLiteral("/Pictures/Pictures.qrc"));
    QVERIFY2(qrc.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(qrc.fileName()));
    const QString content = QString::fromUtf8(qrc.readAll());
    for (const auto &row : Folder::mainStatusTable()) {
        const QString key = Folder::statusIconKey(row.value);
        const QString name = key.section(QLatin1Char('/'), -1);
        QVERIFY2(content.contains(QStringLiteral("<file>%1</file>").arg(name)),
                 qPrintable(QStringLiteral("%1 没有登记进 Pictures.qrc —— 生产环境里会是一个空图标，"
                                           "而只查服务层的用例看不出这件事").arg(name)));
    }
    // 反向验证：这条护栏不是恒真的。写一个不存在的名字进去必须断言失败。
    QVERIFY(!content.contains(QStringLiteral("<file>status_this_does_not_exist.svg</file>")));
}

void EntryStatusTests::onlyBaselineDerivedStatusesRequireABaseline()
{
    QVERIFY(Folder::statusRequiresBaseline(Folder::Status::BothChanged));
    QVERIFY(Folder::statusRequiresBaseline(Folder::Status::Conflict));
    for (const auto &row : Folder::mainStatusTable()) {
        const bool expected = row.value == Folder::Status::BothChanged
            || row.value == Folder::Status::Conflict;
        QCOMPARE(row.requiresBaseline, expected);
    }
    // 反过来也要问一次：把某个常规取值标成「需要基线」，比较引擎在没有基线时
    // 就再也不敢产出它了 —— 这处必须被两条用例同时守住。
    QVERIFY(!Folder::statusRequiresBaseline(Folder::Status::Different));
    QVERIFY(!Folder::statusRequiresBaseline(Folder::Status::Unknown));
}

void EntryStatusTests::knownStatusRejectsOutOfRangeIntegers()
{
    for (const auto &row : Folder::mainStatusTable())
        QVERIFY(Folder::isKnownStatus(int(row.value)));
    QVERIFY(!Folder::isKnownStatus(-1));
    QVERIFY(!Folder::isKnownStatus(-2));
    QVERIFY(!Folder::isKnownStatus(int(Folder::mainStatusTable().size())));
    QVERIFY(!Folder::isKnownStatus(9999));
}

// -----------------------------------------------------------------------------
// B 内容证据
// -----------------------------------------------------------------------------

void EntryStatusTests::contentEvidenceTableKeepsTheSpecifiedTiers()
{
    const auto &table = Folder::contentEvidenceTable();
    QCOMPARE(table.size(), 8);
    QSet<int> seen;
    for (const auto &row : table) {
        QVERIFY(!seen.contains(int(row.value)));
        seen.insert(int(row.value));
    }
    // 规格点名的五档：字节相同 / 规则相同 / CRC 相同 / 未比较 / 部分比较。
    for (Folder::ContentEvidence evidence : {Folder::ContentEvidence::ByteIdentical,
             Folder::ContentEvidence::RuleIdentical, Folder::ContentEvidence::CrcIdentical,
             Folder::ContentEvidence::NotCompared, Folder::ContentEvidence::Partial}) {
        QVERIFY2(seen.contains(int(evidence)),
                 qPrintable(QStringLiteral("内容证据表缺少取值 %1").arg(int(evidence))));
    }
    QSet<QString> identifiers;
    for (const auto &row : table) {
        const QString identifier = Folder::contentEvidenceIdentifier(row.value);
        QVERIFY(!identifier.isEmpty());
        QVERIFY(!identifiers.contains(identifier));
        identifiers.insert(identifier);
    }
}

void EntryStatusTests::claimingIdentityRequiresHavingReadEverything()
{
    QVERIFY(Folder::contentEvidenceProvesIdentity(Folder::ContentEvidence::ByteIdentical));
    QVERIFY(Folder::contentEvidenceProvesIdentity(Folder::ContentEvidence::RuleIdentical));
    QVERIFY(Folder::contentEvidenceProvesIdentity(Folder::ContentEvidence::CrcIdentical));
    // 这三档是最容易写反的地方。`Partial` 是 DIR-008 三条边界里最要紧的一条：
    // 限内全同**不**证明相同。
    QVERIFY(!Folder::contentEvidenceProvesIdentity(Folder::ContentEvidence::Partial));
    QVERIFY(!Folder::contentEvidenceProvesIdentity(Folder::ContentEvidence::NotCompared));
    QVERIFY(!Folder::contentEvidenceProvesIdentity(Folder::ContentEvidence::ByteDifferent));
    QVERIFY(!Folder::contentEvidenceProvesIdentity(Folder::ContentEvidence::RuleDifferent));
    QVERIFY(!Folder::contentEvidenceProvesIdentity(Folder::ContentEvidence::CrcDifferent));

    // 「差」也是看完了才敢说的，因此它覆盖全部内容，只是不证明相同。
    QVERIFY(Folder::contentEvidenceCoversWholeContent(Folder::ContentEvidence::ByteDifferent));
    QVERIFY(Folder::contentEvidenceCoversWholeContent(Folder::ContentEvidence::CrcDifferent));
    QVERIFY(!Folder::contentEvidenceCoversWholeContent(Folder::ContentEvidence::Partial));
    QVERIFY(!Folder::contentEvidenceCoversWholeContent(Folder::ContentEvidence::NotCompared));

    // 表级不变式：声称证明相同 ⇒ 已覆盖全部内容。
    for (const auto &row : Folder::contentEvidenceTable())
        QVERIFY2(!(row.provesContentIdentity && !row.coversWholeContent), row.identifier);
}

void EntryStatusTests::contentEvidenceIdentifiersAreStable()
{
    QCOMPARE(Folder::contentEvidenceIdentifier(Folder::ContentEvidence::NotCompared),
             QStringLiteral("not-compared"));
    QCOMPARE(Folder::contentEvidenceIdentifier(Folder::ContentEvidence::Partial),
             QStringLiteral("partial"));
    QCOMPARE(Folder::contentEvidenceIdentifier(Folder::ContentEvidence::ByteIdentical),
             QStringLiteral("byte-identical"));
    QCOMPARE(Folder::contentEvidenceIdentifier(Folder::ContentEvidence::ByteDifferent),
             QStringLiteral("byte-different"));

    // `Entry::partialComparison()` 只是证据这一维的一个视图，不再单独存储。
    Folder::Entry entry;
    entry.contentEvidence = Folder::ContentEvidence::Partial;
    QVERIFY(entry.partialComparison());
    entry.contentEvidence = Folder::ContentEvidence::ByteIdentical;
    QVERIFY(!entry.partialComparison());
}

// -----------------------------------------------------------------------------
// C 时间关系
// -----------------------------------------------------------------------------

void EntryStatusTests::timeRelationIsIndependentOfStatus()
{
    const auto &table = Folder::timeRelationTable();
    QCOMPARE(table.size(), 4);
    QSet<QString> identifiers;
    for (const auto &row : table) {
        const QString identifier = Folder::timeRelationIdentifier(row.value);
        QVERIFY(!identifier.isEmpty());
        QVERIFY(!identifiers.contains(identifier));
        identifiers.insert(identifier);
    }
    // 三档关系全都在表里（左侧较新 / 右侧较新 / 相同），另加「不可比」。
    for (QString expected : {QStringLiteral("left-newer"), QStringLiteral("right-newer"),
             QStringLiteral("same"), QStringLiteral("unknown")})
        QVERIFY2(identifiers.contains(expected), qPrintable(expected));

    // 独立性：同样的时间戳配三种不同的主状态，时间关系必须一字不差。
    // 一个「时间关系从主状态推出来」的实现会让本用例变红。
    QVector<Folder::Status> statuses = {Folder::Status::Same, Folder::Status::Different,
                                        Folder::Status::Error};
    for (Folder::Status status : statuses) {
        Folder::Entry entry = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 500),
                                      fileSide(QStringLiteral("a.txt"), 10, 500));
        entry.status = status;
        QCOMPARE(Folder::timeRelationFor(entry), Folder::TimeRelation::Same);
    }
    Folder::Entry newer = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 900),
                                  fileSide(QStringLiteral("a.txt"), 10, 500));
    newer.status = Folder::Status::Same;
    QCOMPARE(Folder::timeRelationFor(newer), Folder::TimeRelation::LeftNewer);
}

void EntryStatusTests::compareTimesHonoursToleranceAndInvalidInput()
{
    const auto base = Files::FileTime::fromSecondsSinceEpoch(1700000000);
    const auto later = Files::FileTime::fromSecondsSinceEpoch(1700000001);
    QCOMPARE(Folder::compareTimes(base, base), Folder::TimeRelation::Same);
    QCOMPARE(Folder::compareTimes(later, base), Folder::TimeRelation::LeftNewer);
    QCOMPARE(Folder::compareTimes(base, later), Folder::TimeRelation::RightNewer);

    // 容差默认 0（严格）。开了容差之后恰好落在边界上的那一对也算相同 ——
    // 判据是闭区间：FAT 的 2 秒粒度意味着「差正好 2 秒」是同一秒。
    QCOMPARE(Folder::compareTimes(later, base, 0), Folder::TimeRelation::LeftNewer);
    QCOMPARE(Folder::compareTimes(later, base, 1000), Folder::TimeRelation::Same);
    QCOMPARE(Folder::compareTimes(later, base, 999), Folder::TimeRelation::LeftNewer);
    const auto far = Files::FileTime::fromSecondsSinceEpoch(1700000003);
    QCOMPARE(Folder::compareTimes(far, base, 2000), Folder::TimeRelation::LeftNewer);

    // 拿不到时间戳时必须说「不可比」，不能猜一个方向。
    const Files::FileTime unknown;
    QCOMPARE(Folder::compareTimes(unknown, base), Folder::TimeRelation::Unknown);
    QCOMPARE(Folder::compareTimes(base, unknown), Folder::TimeRelation::Unknown);
    QCOMPARE(Folder::compareTimes(unknown, unknown), Folder::TimeRelation::Unknown);
    // 不可比优先于容差：一个无效时间戳不能被容差「抹平」成相同。
    QCOMPARE(Folder::compareTimes(unknown, base, 100000), Folder::TimeRelation::Unknown);

    QCOMPARE(Folder::timeRelationLabel(Folder::TimeRelation::LeftNewer), QStringLiteral("左侧较新"));
    QCOMPARE(Folder::timeRelationLabel(Folder::TimeRelation::RightNewer), QStringLiteral("右侧较新"));
}

void EntryStatusTests::orphanEntriesHaveNoTimeRelation()
{
    // 孤儿项没有时间关系。「缺失」与「很旧」是两件事：把缺失当成很旧，
    // 会凭空造出一个「左侧较新」的结论，而左侧根本不存在。
    Folder::Entry leftMissing = entryOf(QStringLiteral("a.txt"), missingSide(QStringLiteral("a.txt")),
                                        fileSide(QStringLiteral("a.txt"), 10, 900));
    leftMissing.status = Folder::Status::RightOnly;
    QCOMPARE(leftMissing.timeRelation, Folder::TimeRelation::Unknown);
    QCOMPARE(Folder::timeRelationFor(leftMissing), Folder::TimeRelation::Unknown);

    Folder::Entry rightMissing = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 900),
                                         missingSide(QStringLiteral("a.txt")));
    rightMissing.status = Folder::Status::LeftOnly;
    QCOMPARE(Folder::timeRelationFor(rightMissing), Folder::TimeRelation::Unknown);

    Folder::Entry both = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 900),
                                 fileSide(QStringLiteral("a.txt"), 10, 900));
    QCOMPARE(Folder::timeRelationFor(both), Folder::TimeRelation::Same);
}

// -----------------------------------------------------------------------------
// D 存在性与孤儿项
// -----------------------------------------------------------------------------

void EntryStatusTests::existenceFollowsTheTwoSides()
{
    Folder::Entry both = entryOf(QStringLiteral("a"), fileSide(QStringLiteral("a"), 1, 5),
                                 fileSide(QStringLiteral("a"), 1, 5));
    QCOMPARE(Folder::existenceOf(both), Folder::Existence::Both);
    Folder::Entry left = entryOf(QStringLiteral("a"), fileSide(QStringLiteral("a"), 1, 5),
                                 missingSide(QStringLiteral("a")));
    QCOMPARE(Folder::existenceOf(left), Folder::Existence::LeftOnly);
    Folder::Entry right = entryOf(QStringLiteral("a"), missingSide(QStringLiteral("a")),
                                  fileSide(QStringLiteral("a"), 1, 5));
    QCOMPARE(Folder::existenceOf(right), Folder::Existence::RightOnly);
    Folder::Entry neither = entryOf(QStringLiteral("a"), missingSide(QStringLiteral("a")),
                                    missingSide(QStringLiteral("a")));
    QCOMPARE(Folder::existenceOf(neither), Folder::Existence::Neither);

    QCOMPARE(Folder::existenceIdentifier(Folder::Existence::Both), QStringLiteral("both"));
    QCOMPARE(Folder::existenceIdentifier(Folder::Existence::LeftOnly), QStringLiteral("left-only"));
    QCOMPARE(Folder::existenceIdentifier(Folder::Existence::RightOnly), QStringLiteral("right-only"));
    QCOMPARE(Folder::existenceLabel(Folder::Existence::Both), QStringLiteral("两侧都有"));
}

void EntryStatusTests::orphanSetIsADisplaySetNotASecondStatus()
{
    const auto orphans = Folder::orphanStatuses();
    QCOMPARE(orphans.size(), 2);
    QVERIFY(orphans.contains(Folder::Status::LeftOnly));
    QVERIFY(orphans.contains(Folder::Status::RightOnly));
    QVERIFY(Folder::isOrphan(Folder::Status::LeftOnly));
    QVERIFY(Folder::isOrphan(Folder::Status::RightOnly));
    for (const auto &row : Folder::mainStatusTable()) {
        const bool expected = row.value == Folder::Status::LeftOnly
            || row.value == Folder::Status::RightOnly;
        QCOMPARE(Folder::isOrphan(row.value), expected);
    }
    // 「孤儿项不再另作互斥状态」这句话的可执行形式：主状态表里被算作孤儿的
    // 取值**恰好**是两个。多出一个（例如 Status::Orphan）会让本用例变红。
    int count = 0;
    for (const auto &row : Folder::mainStatusTable())
        count += Folder::isOrphan(row.value) ? 1 : 0;
    QCOMPARE(count, 2);
}

// -----------------------------------------------------------------------------
// E 基线
// -----------------------------------------------------------------------------

void EntryStatusTests::baselineMustBeBoundAndPopulated()
{
    const auto good = baselineOf(QStringLiteral("a.txt"), 10, 500);
    QVERIFY2(Folder::validateBaselineView(good).isEmpty(),
             qPrintable(Folder::validateBaselineView(good).join(QLatin1Char('; '))));

    Folder::BaselineView invalidFlag = good;
    invalidFlag.valid = false;
    QCOMPARE(Folder::validateBaselineView(invalidFlag).size(), 1);
    // 未标记为有效的基线必须先被拒绝：只看内容的话，一个「正好填满了字段
    // 但从来没被验证过」的基线会被当成真基线用。
    QVERIFY(!Folder::validateBaselineView(invalidFlag).isEmpty());

    Folder::BaselineView noLeft = good;
    noLeft.leftRoot.clear();
    QVERIFY(noLeft.ancestors.size() == 1);
    QVERIFY(!Folder::validateBaselineView(noLeft).isEmpty());

    Folder::BaselineView noRight = good;
    noRight.rightRoot = QStringLiteral("  ");
    QVERIFY(!Folder::validateBaselineView(noRight).isEmpty());

    Folder::BaselineView empty = good;
    empty.ancestors.clear();
    QVERIFY(!Folder::validateBaselineView(empty).isEmpty());

    // 键必须是相对两侧根目录的路径。绝对路径的键永远查不中任何条目，
    // 于是整条基线会静默失效 —— 只看「非空」是发现不了的。
    Folder::BaselineView absolute = baselineOf(QStringLiteral("/etc/passwd"), 10, 500);
    QVERIFY(!Folder::validateBaselineView(absolute).isEmpty());
    Folder::BaselineView windows = baselineOf(QStringLiteral("C:/x/a.txt"), 10, 500);
    QVERIFY(!Folder::validateBaselineView(windows).isEmpty());
    Folder::BaselineView emptyKey = baselineOf(QString(), 10, 500);
    QVERIFY(!Folder::validateBaselineView(emptyKey).isEmpty());

    // 没有修改时间的祖先记录无法回答「这一侧动过没有」。
    Folder::BaselineView timeless = good;
    Folder::AncestorRecord record;
    record.size = 10;
    timeless.ancestors.insert(QStringLiteral("a.txt"), record);
    QVERIFY(!record.isValid());
    QVERIFY(!Folder::validateBaselineView(timeless).isEmpty());

    // 一条坏记录要能指出是哪一条。
    const QStringList problems = Folder::validateBaselineView(timeless);
    QVERIFY(problems.first().contains(QStringLiteral("a.txt")));
    QVERIFY(good.hasAncestorFor(QStringLiteral("a.txt")));
    QVERIFY(!good.hasAncestorFor(QStringLiteral("b.txt")));
}

void EntryStatusTests::baselineOnlyRefinesLeafEntries()
{
    Folder::BaselineView baseline = baselineOf(QStringLiteral("sub"), 0, 500);
    baseline.ancestors.insert(QStringLiteral("a.txt"),
                              Folder::AncestorRecord{10, Files::FileTime::fromSecondsSinceEpoch(500)});

    // 目录的结论由子条目汇总（第 5 条），基线细化只作用在叶子上；
    // 否则一个目录的时间戳一动就会被判成「两侧均改」，与它的子条目矛盾。
    Folder::Entry directory = entryOf(QStringLiteral("sub"), directorySide(QStringLiteral("sub"), 900),
                                      directorySide(QStringLiteral("sub"), 900));
    directory.status = Folder::Status::Same;
    QVERIFY(!Folder::applyBaselineStatus(directory, baseline));
    QCOMPARE(directory.status, Folder::Status::Same);

    Folder::Entry orphan = entryOf(QStringLiteral("a.txt"), missingSide(QStringLiteral("a.txt")),
                                   fileSide(QStringLiteral("a.txt"), 10, 900));
    orphan.status = Folder::Status::RightOnly;
    QVERIFY(!Folder::applyBaselineStatus(orphan, baseline));
    QCOMPARE(orphan.status, Folder::Status::RightOnly);

    Folder::Entry unknown = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 900),
                                    fileSide(QStringLiteral("a.txt"), 10, 900));
    unknown.status = Folder::Status::Unknown;
    QVERIFY(!Folder::applyBaselineStatus(unknown, baseline));

    Folder::Entry error = unknown;
    error.status = Folder::Status::Error;
    QVERIFY(!Folder::applyBaselineStatus(error, baseline));

    // 被扫描掩码排除的条目已经有了自己的结论，不该再被细化。
    Folder::Entry masked = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 900),
                                   fileSide(QStringLiteral("a.txt"), 10, 900));
    masked.status = Folder::Status::Same;
    masked.excludedByMask = true;
    QVERIFY(!Folder::applyBaselineStatus(masked, baseline));
    QCOMPARE(masked.status, Folder::Status::Same);
}

void EntryStatusTests::withoutAValidBaselineNothingIsRefined()
{
    Folder::Entry entry = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 900),
                                  fileSide(QStringLiteral("a.txt"), 10, 900));
    entry.status = Folder::Status::Same;

    Folder::BaselineView invalid;
    invalid.leftRoot = QStringLiteral("/left");
    invalid.rightRoot = QStringLiteral("/right");
    invalid.ancestors.insert(QStringLiteral("a.txt"),
                             Folder::AncestorRecord{10, Files::FileTime::fromSecondsSinceEpoch(500)});
    invalid.valid = false;
    QVERIFY(!Folder::applyBaselineStatus(entry, invalid));
    QCOMPARE(entry.status, Folder::Status::Same);

    Folder::BaselineView empty;
    empty.leftRoot = QStringLiteral("/left");
    empty.rightRoot = QStringLiteral("/right");
    empty.valid = true;
    QVERIFY(!Folder::applyBaselineStatus(entry, empty));
    QCOMPARE(entry.status, Folder::Status::Same);

    // 有效基线但**没有这一条**的共同祖先：不得推断。没有共同祖先时
    // 「两侧都改了」和「只是一侧新建」看起来一模一样。
    Folder::BaselineView other = baselineOf(QStringLiteral("b.txt"), 10, 500);
    QVERIFY(!Folder::applyBaselineStatus(entry, other));
    QCOMPARE(entry.status, Folder::Status::Same);

    // 只有一侧相对祖先动过：主状态保持原样。那是「左新右旧」这类信息，
    // 已经由时间维度与内容证据表达，再抬成主状态就是第四套说法了。
    Folder::BaselineView single = baselineOf(QStringLiteral("a.txt"), 10, 500);
    Folder::Entry onlyLeft = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 900),
                                     fileSide(QStringLiteral("a.txt"), 10, 500));
    onlyLeft.status = Folder::Status::Different;
    QVERIFY(!Folder::applyBaselineStatus(onlyLeft, single));
    QCOMPARE(onlyLeft.status, Folder::Status::Different);
}

void EntryStatusTests::conflictNeedsBothSidesToHaveMoved()
{
    Folder::BaselineView baseline = baselineOf(QStringLiteral("a.txt"), 10, 500);

    Folder::Entry identical = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 12, 900),
                                      fileSide(QStringLiteral("a.txt"), 12, 900));
    identical.status = Folder::Status::Same;
    QVERIFY(Folder::applyBaselineStatus(identical, baseline));
    QCOMPARE(identical.status, Folder::Status::BothChanged);
    QVERIFY(!identical.explanation.isEmpty());

    Folder::Entry diverged = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 12, 900),
                                     fileSide(QStringLiteral("a.txt"), 13, 950));
    diverged.status = Folder::Status::Different;
    QVERIFY(Folder::applyBaselineStatus(diverged, baseline));
    QCOMPARE(diverged.status, Folder::Status::Conflict);
    QVERIFY(!diverged.explanation.isEmpty());

    // 只有一侧动过 → 不动主状态。
    Folder::Entry oneSide = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 12, 900),
                                    fileSide(QStringLiteral("a.txt"), 10, 500));
    oneSide.status = Folder::Status::Different;
    QVERIFY(!Folder::applyBaselineStatus(oneSide, baseline));
    QCOMPARE(oneSide.status, Folder::Status::Different);

    // 两侧都没动过 → 什么都不该发生（这一条挡的是「有基线就一律升格」）。
    Folder::Entry untouched = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 500),
                                      fileSide(QStringLiteral("a.txt"), 10, 500));
    untouched.status = Folder::Status::Same;
    QVERIFY(!Folder::applyBaselineStatus(untouched, baseline));
    QCOMPARE(untouched.status, Folder::Status::Same);

    // 只比大小不足以说明动过：时间戳变了、大小没变，一样算动过。
    Folder::Entry timeOnly = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 900),
                                     fileSide(QStringLiteral("a.txt"), 10, 900));
    timeOnly.status = Folder::Status::Same;
    QVERIFY(Folder::applyBaselineStatus(timeOnly, baseline));
    QCOMPARE(timeOnly.status, Folder::Status::BothChanged);
}

// -----------------------------------------------------------------------------
// F 父子一致
// -----------------------------------------------------------------------------

void EntryStatusTests::parentAggregateTruthTable()
{
    struct Case
    {
        const char *name;
        QVector<Folder::Status> children;
        Folder::Status expected;
        bool included;
        bool excluded;
    };
    const QVector<Case> cases = {
        {"空目录", {}, Folder::Status::Same, false, false},
        {"一个相同", {Folder::Status::Same}, Folder::Status::Same, true, false},
        {"两个相同", {Folder::Status::Same, Folder::Status::Same}, Folder::Status::Same, true, false},
        {"一个未知", {Folder::Status::Unknown}, Folder::Status::Unknown, true, false},
        {"未知在前", {Folder::Status::Unknown, Folder::Status::Same}, Folder::Status::Unknown, true, false},
        {"未知在后", {Folder::Status::Same, Folder::Status::Unknown}, Folder::Status::Unknown, true, false},
        {"不同在前", {Folder::Status::Different, Folder::Status::Unknown}, Folder::Status::Different, true, false},
        {"不同在后", {Folder::Status::Unknown, Folder::Status::Different}, Folder::Status::Different, true, false},
        {"错误压过不同", {Folder::Status::Different, Folder::Status::Error}, Folder::Status::Error, true, false},
        {"错误在前", {Folder::Status::Error, Folder::Status::Same}, Folder::Status::Error, true, false},
        {"仅左算不同", {Folder::Status::LeftOnly}, Folder::Status::Different, true, false},
        {"仅右算不同", {Folder::Status::RightOnly}, Folder::Status::Different, true, false},
        {"类型冲突算不同", {Folder::Status::TypeConflict}, Folder::Status::Different, true, false},
        // 这两档在父目录上刻意塌成「不同」：目录没有自己的内容证据，
        // 把子条目的「两侧均改」再抬成主状态就需要一套新的优先级规则。
        {"两侧均改塌成不同", {Folder::Status::BothChanged}, Folder::Status::Different, true, false},
        {"冲突塌成不同", {Folder::Status::Conflict}, Folder::Status::Different, true, false},
    };
    for (const auto &c : cases) {
        QVector<Folder::ChildStatus> children;
        for (Folder::Status status : c.children)
            children.append({status, true});
        const auto aggregate = Folder::aggregateChildren(children);
        QVERIFY2(aggregate.status == c.expected,
                 qPrintable(QStringLiteral("子状态组合「%1」聚合成了 %2，期望 %3")
                                .arg(QString::fromUtf8(c.name))
                                .arg(int(aggregate.status)).arg(int(c.expected))));
        QCOMPARE(aggregate.hasIncludedDescendants, c.included);
        QCOMPARE(aggregate.hasExcludedDescendants, c.excluded);
    }

    // 被扫描掩码排除的子条目不算「已包含」，但也不许被当成「不存在」——
    // 它是「有后代、只是没比」。这两面分开记是「隐藏空文件夹不得藏起读错误」
    // 那条边界能成立的前提。
    const QVector<Folder::ChildStatus> onlyExcluded = {{Folder::Status::Same, false}};
    const auto masked = Folder::aggregateChildren(onlyExcluded);
    QCOMPARE(masked.status, Folder::Status::Same);
    QVERIFY(!masked.hasIncludedDescendants);
    QVERIFY(masked.hasExcludedDescendants);

    const QVector<Folder::ChildStatus> mixed = {{Folder::Status::Same, false},
                                                {Folder::Status::Different, true}};
    const auto both = Folder::aggregateChildren(mixed);
    QCOMPARE(both.status, Folder::Status::Different);
    QVERIFY(both.hasIncludedDescendants);
    QVERIFY(both.hasExcludedDescendants);
}

void EntryStatusTests::cancelledScanNeverAggregatesToSame()
{
    // 取消之后一个「全相同」的聚合结果是不成立的：剩下的子条目根本没看。
    const QVector<Folder::ChildStatus> allSame = {{Folder::Status::Same, true},
                                                  {Folder::Status::Same, true}};
    QCOMPARE(Folder::aggregateChildren(allSame).status, Folder::Status::Same);
    QCOMPARE(Folder::aggregateChildren(allSame, true).status, Folder::Status::Unknown);
    // 空目录也一样：没扫过不等于「里面什么都没有」。
    QCOMPARE(Folder::aggregateChildren({}, true).status, Folder::Status::Unknown);
    // 但已经拿到的结论不许被取消降级：不同仍然是不同，错误仍然是错误。
    QCOMPARE(Folder::aggregateChildren({{Folder::Status::Different, true}}, true).status,
             Folder::Status::Different);
    QCOMPARE(Folder::aggregateChildren({{Folder::Status::Error, true}}, true).status,
             Folder::Status::Error);
}

// -----------------------------------------------------------------------------
// G 模型自检
// -----------------------------------------------------------------------------

void EntryStatusTests::modelSelfCheckCatchesEveryImpossibleCombination()
{
    // 每一格都是「看着像那么回事、其实不可能」的组合。判据不是「自检有输出」，
    // 而是「每坏一处都必须有输出」——自检恒真是这一族最容易犯的错。
    struct Case
    {
        const char *name;
        std::function<void(Folder::Entry &)> breakIt;
        bool baselineValid;
    };
    const auto noop = [](Folder::Entry &) {};
    const QVector<Case> cases = {
        {"没有基线却判成两侧均改",
         [](Folder::Entry &e) { e.status = Folder::Status::BothChanged; }, false},
        {"没有基线却判成冲突",
         [](Folder::Entry &e) { e.status = Folder::Status::Conflict; }, false},
        {"只比了前 N 字节却判成相同",
         [](Folder::Entry &e) { e.contentEvidence = Folder::ContentEvidence::Partial; }, true},
        {"根本没比内容却判成相同",
         [](Folder::Entry &e) { e.contentEvidence = Folder::ContentEvidence::NotCompared; }, true},
        {"左侧不存在却判成相同",
         [](Folder::Entry &e) { e.left = missingSide(QStringLiteral("a.txt")); }, true},
        {"两侧都在却判成仅左",
         [](Folder::Entry &e) { e.status = Folder::Status::LeftOnly; }, true},
        {"两侧都在却判成仅右",
         [](Folder::Entry &e) { e.status = Folder::Status::RightOnly; }, true},
        {"左侧报错却判成相同",
         [](Folder::Entry &e) { e.left.error = QStringLiteral("permission denied"); }, true},
        {"右侧报错却判成相同",
         [](Folder::Entry &e) { e.right.error = QStringLiteral("permission denied"); }, true},
        {"左侧报错却断定仅右存在",
         [](Folder::Entry &e) { e.status = Folder::Status::RightOnly;
                                e.left = missingSide(QStringLiteral("a.txt"));
                                e.left.error = QStringLiteral("permission denied"); }, true},
        {"右侧报错却断定仅左存在",
         [](Folder::Entry &e) { e.status = Folder::Status::LeftOnly;
                                e.right = missingSide(QStringLiteral("a.txt"));
                                e.right.error = QStringLiteral("permission denied"); }, true},
        {"读了错误却声称已经看完内容",
         [](Folder::Entry &e) { e.status = Folder::Status::Error;
                                e.contentEvidence = Folder::ContentEvidence::ByteIdentical; }, true},
        {"两侧缺一侧却给出时间关系",
         [](Folder::Entry &e) { e.timeRelation = Folder::TimeRelation::LeftNewer;
                                e.right = missingSide(QStringLiteral("a.txt")); }, true},
        // 反向验证用的那一格：一个完全合规的条目必须一条问题都报不出来。
        {"完全合规的条目", noop, true},
    };
    for (const auto &c : cases) {
        Folder::Entry entry = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 500),
                                      fileSide(QStringLiteral("a.txt"), 10, 500));
        entry.status = Folder::Status::Same;
        entry.contentEvidence = Folder::ContentEvidence::ByteIdentical;
        entry.timeRelation = Folder::TimeRelation::Same;
        c.breakIt(entry);
        const QStringList problems = Folder::statusModelViolations(entry, c.baselineValid);
        const bool compliant = QString::fromUtf8(c.name) == QStringLiteral("完全合规的条目");
        if (compliant) {
            QVERIFY2(problems.isEmpty(),
                     qPrintable(QStringLiteral("合规条目被误报：%1").arg(problems.join(QLatin1Char('; ')))));
        } else {
            QVERIFY2(!problems.isEmpty(),
                     qPrintable(QStringLiteral("「%1」没有被自检抓到").arg(QString::fromUtf8(c.name))));
        }
    }
}

void EntryStatusTests::modelSelfCheckIsSilentOnWellFormedEntries()
{
    QVector<Folder::Entry> entries;
    // 真实的「相同」：字节证据 + 时间相同。
    entries << entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 500),
                       fileSide(QStringLiteral("a.txt"), 10, 500));
    entries.last().contentEvidence = Folder::ContentEvidence::ByteIdentical;
    entries.last().timeRelation = Folder::TimeRelation::Same;

    // 真实的「不同」：大小不同、内容没比过（大小比较已经证明了不同）。
    entries << entryOf(QStringLiteral("b.txt"), fileSide(QStringLiteral("b.txt"), 10, 500),
                       fileSide(QStringLiteral("b.txt"), 20, 500));
    entries.last().status = Folder::Status::Different;
    entries.last().timeRelation = Folder::TimeRelation::Same;

    // 被掩码排除：未比较、状态未知。
    entries << entryOf(QStringLiteral("c.txt"), fileSide(QStringLiteral("c.txt"), 10, 500),
                       fileSide(QStringLiteral("c.txt"), 10, 500));
    entries.last().status = Folder::Status::Unknown;
    entries.last().excludedByMask = true;

    // 目录：没有内容证据，但状态是汇总出来的。
    entries << entryOf(QStringLiteral("sub"), directorySide(QStringLiteral("sub"), 500),
                       directorySide(QStringLiteral("sub"), 500));
    entries.last().contentEvidence = Folder::ContentEvidence::NotCompared;
    entries.last().timeRelation = Folder::TimeRelation::Same;

    // 孤儿两侧各一条。
    entries << entryOf(QStringLiteral("d.txt"), fileSide(QStringLiteral("d.txt"), 10, 500),
                       missingSide(QStringLiteral("d.txt")));
    entries.last().status = Folder::Status::LeftOnly;
    entries << entryOf(QStringLiteral("e.txt"), missingSide(QStringLiteral("e.txt")),
                       fileSide(QStringLiteral("e.txt"), 10, 500));
    entries.last().status = Folder::Status::RightOnly;

    // 读取错误：证据必须是「未比较」。
    entries << entryOf(QStringLiteral("f.txt"), fileSide(QStringLiteral("f.txt"), 10, 500),
                       fileSide(QStringLiteral("f.txt"), 10, 500));
    entries.last().status = Folder::Status::Error;
    entries.last().contentEvidence = Folder::ContentEvidence::NotCompared;

    // 有基线时的两档新结论。
    entries << entryOf(QStringLiteral("g.txt"), fileSide(QStringLiteral("g.txt"), 10, 500),
                       fileSide(QStringLiteral("g.txt"), 10, 500));
    entries.last().status = Folder::Status::BothChanged;
    entries.last().contentEvidence = Folder::ContentEvidence::ByteIdentical;
    entries.last().timeRelation = Folder::TimeRelation::Same;
    entries << entryOf(QStringLiteral("h.txt"), fileSide(QStringLiteral("h.txt"), 10, 500),
                       fileSide(QStringLiteral("h.txt"), 20, 500));
    entries.last().status = Folder::Status::Conflict;
    entries.last().contentEvidence = Folder::ContentEvidence::ByteDifferent;
    entries.last().timeRelation = Folder::TimeRelation::Same;

    // 符号链接：链接目标字符串比过，算字节证据。
    entries << entryOf(QStringLiteral("i.lnk"), linkSide(QStringLiteral("i.lnk"), 500),
                       linkSide(QStringLiteral("i.lnk"), 500));
    entries.last().contentEvidence = Folder::ContentEvidence::ByteIdentical;
    entries.last().timeRelation = Folder::TimeRelation::Same;

    for (const auto &entry : entries) {
        const QStringList problems = Folder::statusModelViolations(entry, true);
        QVERIFY2(problems.isEmpty(),
                 qPrintable(QStringLiteral("条目 %1 被误报：%2")
                                .arg(entry.relativePath, problems.join(QLatin1Char('; ')))));
    }
}

// -----------------------------------------------------------------------------
// H 为什么是这个状态
// -----------------------------------------------------------------------------

void EntryStatusTests::reasonLinesListCriteriaOverridesAndConclusion()
{
    Folder::Entry entry = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 500),
                                  fileSide(QStringLiteral("a.txt"), 20, 500));
    entry.status = Folder::Status::Different;
    entry.contentEvidence = Folder::ContentEvidence::NotCompared;
    entry.timeRelation = Folder::TimeRelation::Same;

    const auto lines = Folder::statusReasonLines(entry, Folder::Options());
    QVERIFY(!lines.isEmpty());
    // 三类都要在：只印一句「不同」等于没有回答「为什么」。
    for (Folder::ReasonKind kind : {Folder::ReasonKind::Criterion, Folder::ReasonKind::Override,
             Folder::ReasonKind::Conclusion})
        QVERIFY2(!textsOfKind(lines, kind).isEmpty(),
                 qPrintable(Folder::reasonKindLabel(kind)));

    const QString criteria = textsOfKind(lines, Folder::ReasonKind::Criterion).join(QLatin1Char('\n'));
    for (QString label : {QStringLiteral("存在性："), QStringLiteral("类型："), QStringLiteral("大小："),
             QStringLiteral("内容证据："), QStringLiteral("时间关系："), QStringLiteral("扫描掩码："),
             QStringLiteral("基线：")})
        QVERIFY2(criteria.contains(label), qPrintable(label));

    // 最终结论那一行要同时给出人话与机器标识，且与状态表一致。
    const QString conclusion = textsOfKind(lines, Folder::ReasonKind::Conclusion).first();
    QVERIFY(conclusion.contains(Folder::statusLabel(Folder::Status::Different)));
    QVERIFY(conclusion.contains(Folder::statusIdentifier(Folder::Status::Different)));

    QCOMPARE(Folder::reasonKindLabel(Folder::ReasonKind::Criterion), QStringLiteral("准则"));
    QCOMPARE(Folder::reasonKindLabel(Folder::ReasonKind::Override), QStringLiteral("覆盖策略"));
    QCOMPARE(Folder::reasonKindLabel(Folder::ReasonKind::Conclusion), QStringLiteral("最终结论"));
}

void EntryStatusTests::reasonLinesReportTheBaselineHonestly()
{
    Folder::Entry entry = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 500),
                                  fileSide(QStringLiteral("a.txt"), 10, 500));
    entry.status = Folder::Status::Same;
    entry.contentEvidence = Folder::ContentEvidence::ByteIdentical;

    const QString withoutBaseline =
        joinedReasonText(Folder::statusReasonLines(entry, Folder::Options(), false));
    QVERIFY(withoutBaseline.contains(QStringLiteral("没有提供有效基线")));

    const QString withBaseline =
        joinedReasonText(Folder::statusReasonLines(entry, Folder::Options(), true));
    QVERIFY(withBaseline.contains(QStringLiteral("提供了有效基线")));

    // 一档「必须有基线」的结论配上「本次没有基线」时，覆盖策略那一节必须
    // 把这件事说出来 —— 否则界面会自相矛盾地展示一个不该出现的状态。
    Folder::Entry refined = entry;
    refined.status = Folder::Status::Conflict;
    const QString contradictory =
        joinedReasonText(Folder::statusReasonLines(refined, Folder::Options(), false));
    QVERIFY(contradictory.contains(QStringLiteral("没有有效基线")));
}

void EntryStatusTests::reasonLinesExplainShortCircuitsThatActuallyHappened()
{
    Folder::Options options;

    // 大小不同 → 短路内容比较。
    Folder::Entry sized = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 500),
                                  fileSide(QStringLiteral("a.txt"), 20, 500));
    sized.status = Folder::Status::Different;
    QString overrides = textsOfKind(Folder::statusReasonLines(sized, options),
                                    Folder::ReasonKind::Override).join(QLatin1Char('\n'));
    QVERIFY(overrides.contains(QStringLiteral("大小不同时短路内容比较")));

    // 大小相同的时候不该出现「短路」那句话：它必须解释**发生过**的事。
    Folder::Entry equalSizes = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 500),
                                       fileSide(QStringLiteral("a.txt"), 10, 500));
    equalSizes.status = Folder::Status::Same;
    equalSizes.contentEvidence = Folder::ContentEvidence::ByteIdentical;
    QVERIFY(!textsOfKind(Folder::statusReasonLines(equalSizes, options),
                         Folder::ReasonKind::Override).join(QLatin1Char('\n'))
                 .contains(QStringLiteral("短路内容比较")));

    // 关掉内容比较 → 说的是另一句话，而不是短路那句。
    options.compareContent = false;
    const QString noContent = textsOfKind(Folder::statusReasonLines(sized, options),
                                          Folder::ReasonKind::Override).join(QLatin1Char('\n'));
    QVERIFY(noContent.contains(QStringLiteral("内容比较已关闭")));
    QVERIFY(!noContent.contains(QStringLiteral("短路内容比较")));

    options = Folder::Options();
    options.compareFirstBytes = 4096;
    Folder::Entry budgeted = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 500),
                                     fileSide(QStringLiteral("a.txt"), 10, 500));
    budgeted.status = Folder::Status::Unknown;
    budgeted.contentEvidence = Folder::ContentEvidence::Partial;
    QString budgetText = textsOfKind(Folder::statusReasonLines(budgeted, options),
                                     Folder::ReasonKind::Override).join(QLatin1Char('\n'));
    QVERIFY(budgetText.contains(QStringLiteral("4096")));
    QVERIFY(budgetText.contains(QStringLiteral("部分比较")));

    // 符号链接与递归关闭各自成句。
    Folder::Entry link = entryOf(QStringLiteral("a.lnk"), linkSide(QStringLiteral("a.lnk"), 500),
                                 linkSide(QStringLiteral("a.lnk"), 500));
    QVERIFY(textsOfKind(Folder::statusReasonLines(link, options), Folder::ReasonKind::Override)
                .join(QLatin1Char('\n')).contains(QStringLiteral("不跟随链接")));
    options = Folder::Options();
    options.recursive = false;
    QVERIFY(textsOfKind(Folder::statusReasonLines(link, options), Folder::ReasonKind::Override)
                .join(QLatin1Char('\n')).contains(QStringLiteral("递归已关闭")));

    // 被掩码排除：排除优先以及原因文本都要出现。
    options = Folder::Options();
    Folder::Entry masked = entryOf(QStringLiteral("a.txt"), fileSide(QStringLiteral("a.txt"), 10, 500),
                                   fileSide(QStringLiteral("a.txt"), 10, 500));
    masked.status = Folder::Status::Unknown;
    masked.excludedByMask = true;
    masked.filterReason = QStringLiteral("未命中扫描掩码的包含规则。");
    QVERIFY(textsOfKind(Folder::statusReasonLines(masked, options), Folder::ReasonKind::Override)
                .join(QLatin1Char('\n')).contains(QStringLiteral("排除优先")));
    QVERIFY(textsOfKind(Folder::statusReasonLines(masked, options), Folder::ReasonKind::Criterion)
                .join(QLatin1Char('\n')).contains(QStringLiteral("未命中包含规则")));
}

QTEST_APPLESS_MAIN(EntryStatusTests)
