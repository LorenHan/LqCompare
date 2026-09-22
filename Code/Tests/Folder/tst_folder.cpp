#include <QtTest>

#include "entrystatus.h"
#include "foldercompare.h"
#include "foldercomparesession.h"
#include "foldercompareview.h"
#include "sessiondocument.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTreeView>

#include <algorithm>

using namespace LqCompare;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

const Folder::Entry *findEntry(const Folder::Result &result, const QString &path)
{
    for (const auto &entry : result.entries) {
        if (entry.relativePath == path)
            return &entry;
    }
    return nullptr;
}

struct Pair
{
    QTemporaryDir temp;
    QString left = temp.path() + QStringLiteral("/left");
    QString right = temp.path() + QStringLiteral("/right");
    Pair() { QDir().mkpath(left); QDir().mkpath(right); }
};

// Keep real files for content I/O while injecting metadata/enumeration failures.
class FaultFileSystem : public Files::FileSystem
{
public:
    std::unique_ptr<Files::FileSystem> native{Files::createNativeFileSystem()};
    QString unreadableDirectory;
    QString unreadableFile;
    QString changingFile;
    QHash<QString, QString> aliases;
    // DIR-008 第 1 条要造一个「申报尺寸相同、实际读起来更短」的右侧：
    // physicalPaths 把逻辑路径指向另一个**真的**文件，frozenInfo 让申报的元数据
    // 在整场比较里保持第一次读到的样子（否则「大小相等」这条前置当场就不成立，
    // 循环压根进不去，那个 break 也就无从观察）。
    QHash<QString, QString> physicalPaths;
    QHash<QString, Files::FileInfo> frozenInfo;
    mutable int fileStats = 0;
    Qt::CaseSensitivity caseSensitivity() const override { return native->caseSensitivity(); }
    QChar separator() const override { return native->separator(); }
    QString pathNormalize(const QString &p, Files::ErrorCode *e) const override { return native->pathNormalize(p, e); }
    bool isAbsolutePath(const QString &p) const override { return native->isAbsolutePath(p); }
    QString toNativePath(const QString &p) const override
    {
        return native->toNativePath(physicalPaths.value(p, p));
    }
    Files::FileInfo stat(const QString &p, Files::ErrorCode *e) const override
    {
        if (frozenInfo.contains(p))
            return frozenInfo.value(p);
        if (p == unreadableFile) {
            if (e) *e = Files::FileSystemError::PermissionDenied;
            return {};
        }
        if (p == changingFile && ++fileStats == 2)
            writeFile(p, QByteArray("changed while comparing"));
        auto info = native->stat(p, e);
        if (aliases.contains(p)) info.name = aliases.value(p);
        return info;
    }
    QString linkTarget(const QString &p, Files::ErrorCode *e) const override { return native->linkTarget(p, e); }
    bool exists(const QString &p, Files::ErrorCode *e) const override { return native->exists(p, e); }
    QVector<Files::FileInfo> enumerateDirectory(const QString &p, Files::ErrorCode *e) const override
    {
        if (p == unreadableDirectory) {
            if (e) *e = Files::FileSystemError::PermissionDenied;
            return {};
        }
        auto entries = native->enumerateDirectory(p, e);
        for (auto &entry : entries) {
            if (aliases.contains(entry.path)) entry.name = aliases.value(entry.path);
        }
        return entries;
    }
    bool setTimes(const QString &p, const Files::FileTime &m, const Files::FileTime &a,
                  Files::ErrorCode *e) const override { return native->setTimes(p, m, a, e); }
    bool setAttributes(const QString &p, Files::FileAttributes a, Files::ErrorCode *e) const override
    { return native->setAttributes(p, a, e); }
    QString platformName() const override { return native->platformName(); }
};

QModelIndex indexNamed(QAbstractItemModel *model, const QString &name, const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const QModelIndex index = model->index(row, 0, parent);
        if (index.data().toString() == name)
            return index;
        const auto child = indexNamed(model, name, index);
        if (child.isValid())
            return child;
    }
    return {};
}

QVariantMap savedSettings(SessionSettings *settings)
{
    QVariantMap result;
    for (const QString &key : settings->keys())
        result.insert(key, settings->value(key));
    return result;
}

bool sameOptions(const Folder::Options &a, const Folder::Options &b)
{
    return a.recursive == b.recursive && a.compareContent == b.compareContent
        && a.maximumDepth == b.maximumDepth && a.scanMaskDeclaration == b.scanMaskDeclaration
        && a.nameCaseSensitivity == b.nameCaseSensitivity
        && a.compareFirstBytes == b.compareFirstBytes;
}

} // namespace

class FolderTests : public QObject
{
    Q_OBJECT
private slots:
    void recursivePairsAndStates()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/sub/same.txt", "same\n"));
        QVERIFY(writeFile(pair.right + "/sub/same.txt", "same\n"));
        QVERIFY(writeFile(pair.left + "/sub/different.txt", "ABC"));
        QVERIFY(writeFile(pair.right + "/sub/different.txt", "ABD"));
        QVERIFY(writeFile(pair.left + "/left.txt", "left"));
        QVERIFY(writeFile(pair.right + "/right.txt", "right"));
        QVERIFY(writeFile(pair.left + "/conflict", "file"));
        QVERIFY(QDir().mkpath(pair.right + "/conflict"));
        const auto result = Folder::compare(pair.left, pair.right);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.complete);
        QCOMPARE(result.entries.size(), 6);
        QCOMPARE(findEntry(result, "sub/same.txt")->status, Folder::Status::Same);
        QCOMPARE(findEntry(result, "sub/different.txt")->status, Folder::Status::Different);
        QCOMPARE(findEntry(result, "sub/different.txt")->firstDifference, qint64(2));
        QCOMPARE(findEntry(result, "sub")->status, Folder::Status::Different);
        QCOMPARE(findEntry(result, "left.txt")->status, Folder::Status::LeftOnly);
        QCOMPARE(findEntry(result, "right.txt")->status, Folder::Status::RightOnly);
        QCOMPARE(findEntry(result, "conflict")->status, Folder::Status::TypeConflict);
        QVERIFY(!findEntry(result, "conflict")->canCompareAsText());
        QVERIFY(findEntry(result, "left.txt")->canCompareAsText());
    }

    void exactBytesAcrossChunkBoundary()
    {
        Pair pair;
        QByteArray a(600000, 'a');
        QByteArray b = a;
        b[500000] = 'b';
        QVERIFY(writeFile(pair.left + "/large.bin", a));
        QVERIFY(writeFile(pair.right + "/large.bin", b));
        auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(result.entries.first().firstDifference, qint64(500000));
        QVERIFY(writeFile(pair.right + "/large.bin", a));
        result = Folder::compare(pair.left, pair.right);
        QCOMPARE(result.entries.first().status, Folder::Status::Same);
    }

    // DIR-008 第 2 条：开关本身。默认必须关闭（否则既有行为被悄悄改掉），
    // 打开后差异落在限外时既不能说「不同」（没读到）也不能说「相同」（没读完）。
    void partialByteLimitMarksIncompleteWithoutClaimingEquality()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/tail.bin", "ABCD-one"));
        QVERIFY(writeFile(pair.right + "/tail.bin", "ABCD-two"));

        // 默认关闭：差异必须被发现，结论完整。
        const auto full = Folder::compare(pair.left, pair.right);
        QCOMPARE(full.entries.first().status, Folder::Status::Different);
        QCOMPARE(full.entries.first().firstDifference, qint64(5));
        QVERIFY(!full.entries.first().partialComparison());
        QVERIFY(full.complete);

        // 限 = 4：差异（偏移 5）落在限外。
        Folder::Options options;
        options.compareFirstBytes = 4;
        const auto limited = Folder::compare(pair.left, pair.right, options);
        const auto &entry = limited.entries.first();
        QCOMPARE(entry.status, Folder::Status::Unknown);
        QVERIFY(entry.partialComparison());
        QCOMPARE(entry.firstDifference, qint64(-1)); // 没有结论，就不该有「首个差异」。
        // 文案「部分比较」出自 DIR-008 的验收用语，不是实现细节，因此可以断言它；
        // 但真正的判据是上面那个 partialComparison 标志——文字可以改，含义不能。
        QVERIFY(entry.explanation.contains(QStringLiteral("部分比较")));
        // 「不完整比对」必须体现在结果完整性上，而不只是某一行的说明文字里；
        // 否则报表会说「无差异（完整）」。
        QVERIFY(!limited.complete);
    }

    // DIR-008 第 1、2 条：限内发现差异时，结论**已经**被证明，不该再降级成不确定。
    // 这条同时钉住边界：差异在偏移 2 时，限 3 算「已证明」，限 2 算「没读到」。
    void partialByteLimitStillProvesDifferencesWithinTheLimit()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/edge.bin", "abX"));
        QVERIFY(writeFile(pair.right + "/edge.bin", "abY"));

        Folder::Options inside;
        inside.compareFirstBytes = 3;
        auto result = Folder::compare(pair.left, pair.right, inside);
        QCOMPARE(result.entries.first().status, Folder::Status::Different);
        QCOMPARE(result.entries.first().firstDifference, qint64(2));
        QVERIFY(!result.entries.first().partialComparison());
        QVERIFY(result.complete); // 差异已证明，结果并不「不完整」。

        // 差异正好落在限外一个字节：一个字节都不许多读。
        Folder::Options outside;
        outside.compareFirstBytes = 2;
        result = Folder::compare(pair.left, pair.right, outside);
        QCOMPARE(result.entries.first().status, Folder::Status::Unknown);
        QVERIFY(result.entries.first().partialComparison());
        QVERIFY(!result.complete);
    }

    // DIR-008 第 2、3 条：预算与分块必须夹逼，且分块大小有上界。
    void partialByteLimitIsExactAcrossBlockBoundaries()
    {
        const qint64 block = Folder::kMaximumCompareBlockSize;
        QVERIFY(block > 0);
        // 上界存在的意义：块大小一旦被放大到「一次读完」，大文件比较就会重新吃掉内存。
        // 1 MiB 是给调整留的余量，不是要卡死当前取值。
        QVERIFY(block <= 1024 * 1024);

        Pair pair;
        // 横跨两个块，差异正好落在「第一个块之后 1 字节」。
        const QByteArray a(static_cast<int>(2 * block), 'a');
        QByteArray b = a;
        b[static_cast<int>(block + 1)] = 'b';
        QVERIFY(writeFile(pair.left + "/span.bin", a));
        QVERIFY(writeFile(pair.right + "/span.bin", b));

        auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(result.entries.first().firstDifference, block + 1);

        // 限 = 块 + 1：第二块只允许读 1 字节，偏移 block+1 处的差异读不到。
        Folder::Options options;
        options.compareFirstBytes = block + 1;
        result = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(result.entries.first().status, Folder::Status::Unknown);
        QVERIFY(result.entries.first().partialComparison());
        QCOMPARE(result.entries.first().firstDifference, qint64(-1));

        // 限 = 块 + 2：差异进入射程，必须被发现。
        // 这条是上一句的对照：它证明「没发现差异」是预算算对了，而不是第二块压根没比较——
        // 少了它，一个「第二块永不比较」的实现也能让上下两条一起通过。
        options.compareFirstBytes = block + 2;
        result = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(result.entries.first().status, Folder::Status::Different);
        QCOMPARE(result.entries.first().firstDifference, block + 1);
    }

    // DIR-008 第 2 × 第 5 条的交叉：文件在**部分比较**期间被改动时，
    // 「只比较了前 N 字节」这个说法本身也不再成立，必须跟着一起收回。
    void fileChangeDuringComparisonDropsThePartialClaim()
    {
        // 一、限内全同、随后文件变了：不能再声称「比较了前 2 字节」。
        {
            Pair pair;
            QVERIFY(writeFile(pair.left + "/file", "abXY"));
            QVERIFY(writeFile(pair.right + "/file", "abXY"));
            FaultFileSystem fs;
            fs.changingFile = pair.left + "/file";
            Folder::Options options;
            options.compareFirstBytes = 2;
            const auto result = Folder::compare(pair.left, pair.right, options, nullptr, {}, &fs);
            const auto &entry = result.entries.first();
            QCOMPARE(entry.status, Folder::Status::Error);
            QVERIFY(entry.explanation.contains(QStringLiteral("变化")));
            QVERIFY2(!entry.partialComparison(), "读失败时连「比较了前 N 字节」都不成立");
            QCOMPARE(entry.firstDifference, qint64(-1));
            QVERIFY(!result.complete);
        }
        // 二、差异**已经**被找到、随后文件又变了：报表不能既说「读取失败」
        //     又给出一个首个差异偏移量——那个偏移量是对旧内容的结论。
        {
            Pair pair;
            QVERIFY(writeFile(pair.left + "/file", "abXY"));
            QVERIFY(writeFile(pair.right + "/file", "abZZ"));
            FaultFileSystem fs;
            fs.changingFile = pair.left + "/file";
            const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
            const auto &entry = result.entries.first();
            QCOMPARE(entry.status, Folder::Status::Error);
            QCOMPARE(entry.firstDifference, qint64(-1));
            QVERIFY(!entry.partialComparison());
        }
    }

    // DIR-008 第 2 条「默认关闭」+ 选项落库：值必须被校验而不是被静默改写。
    void partialByteLimitSettingsAreValidatedNotTruncated()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/same.bin", "abcd"));
        QVERIFY(writeFile(pair.right + "/same.bin", "abcd"));
        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        Folder::Options accepted;
        accepted.compareFirstBytes = 4096;
        QVERIFY(session.setComparisonOptions(accepted));
        QCOMPARE(session.sessionSettings()->value(QStringLiteral("folder.compareFirstBytes")).toLongLong(),
                 qint64(4096));
        // 视图没有这个控件，但必须原样带回：否则任何一次「读视图选项 → 写回会话」
        // 都会把用户设好的预算静默重置成 0（关闭）。
        QCOMPARE(session.view()->options().compareFirstBytes, qint64(4096));

        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QString error;
        // 0.5 截断后恰好等于 0，而 0 就是「关闭」——静默接受等于悄悄改掉用户的设置。
        QVERIFY(session.sessionSettings()->setValue(QStringLiteral("folder.compareFirstBytes"), 0.5));
        QVERIFY(!session.open(&error));
        QVERIFY(error.contains(QStringLiteral("folder.compareFirstBytes")));
        QCOMPARE(finished.count(), 0);
        QVERIFY(sameOptions(session.comparisonOptions(), accepted));

        // 字符串形式的数字同样非法：设置文件里的 "4096" 不是 4096。
        QVERIFY(session.sessionSettings()->setValue(QStringLiteral("folder.compareFirstBytes"),
                                                    QStringLiteral("4096")));
        QVERIFY(!session.open(&error));
        QVERIFY(error.contains(QStringLiteral("folder.compareFirstBytes")));
        QCOMPARE(finished.count(), 0);

        // 修好之后必须真的生效：限 2 字节、文件 4 字节且前 2 字节相同 → 未完整比较。
        QVERIFY(session.sessionSettings()->setValue(QStringLiteral("folder.compareFirstBytes"), qint64(2)));
        QVERIFY2(session.open(&error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(session.result().entries.first().status, Folder::Status::Unknown);
        QVERIFY(session.result().entries.first().partialComparison());
        QVERIFY(!session.result().complete);

        // 负数走的是另一条路（optionsError，不是设置反序列化）：也必须被拒绝，
        // 且不能把已经生效的 2 换成别的东西。
        Folder::Options negative;
        negative.compareFirstBytes = -1;
        QString reason;
        QVERIFY(!session.setComparisonOptions(negative, &reason));
        QVERIFY(reason.contains(QStringLiteral("只比较前")));
        QCOMPARE(session.comparisonOptions().compareFirstBytes, qint64(2));
    }

    // DIR-008 第 1 条的核心词是「**提前**」：遇首个不同字节就判定不同，
    // 而不是「读完之后再去找第一个差异」。这一条此前**没有任何用例守着**：
    // 把 compareFile() 里的 break 删掉，全部用例（3687 条）一条都不会红——
    // 因为既有的夹具都是「大小相同、只有一处差异」，命中那一处之后剩下的块两侧全同，
    // 循环就算不停也改写不了 firstDifference。
    //
    // 造法：申报尺寸取自注入层**冻结**的快照（两侧都报 600000），而右侧真正被打开的
    // 那个文件只有 100 字节。于是「大小相等」这条前置成立、循环进得去，第一个块就不同；
    // 而**如果循环不在命中处停下**，它会接着读第二个块，那一块（左 262144 / 右 0）
    // 同样「不同」，firstDifference 会被改写成 262144、再改成 524288。
    // 所以「断言 100」就是「有没有真的提前停下」的判据。
    void comparisonStopsAtTheFirstDifferingByte()
    {
        Pair pair;
        const QByteArray declared(600000, 'a');
        QVERIFY(writeFile(pair.left + "/trunc.bin", declared));
        QVERIFY(writeFile(pair.right + "/trunc.bin", declared));
        // 真正被读的那一份只有 100 字节，且刻意放在两个根之外（不会被枚举到）。
        const QString physicallyShort = pair.temp.path() + QStringLiteral("/outside.bin");
        QVERIFY(writeFile(physicallyShort, QByteArray(100, 'a')));

        FaultFileSystem fs;
        Files::ErrorCode error;
        fs.frozenInfo.insert(pair.left + "/trunc.bin", fs.native->stat(pair.left + "/trunc.bin", &error));
        fs.frozenInfo.insert(pair.right + "/trunc.bin", fs.native->stat(pair.right + "/trunc.bin", &error));
        fs.physicalPaths.insert(pair.right + "/trunc.bin", physicallyShort);

        const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
        QCOMPARE(result.entries.size(), 1);
        const auto &entry = result.entries.first();
        QCOMPARE(entry.status, Folder::Status::Different);
        QCOMPARE(entry.firstDifference, qint64(100));
        QVERIFY2(entry.explanation.contains(QStringLiteral("100")), qPrintable(entry.explanation));
        QVERIFY(!entry.partialComparison());
    }

    void emptyDirectoriesAndEmptyFiles()
    {
        Pair pair;
        QVERIFY(QDir().mkpath(pair.left + "/empty"));
        QVERIFY(QDir().mkpath(pair.right + "/empty"));
        QVERIFY(writeFile(pair.left + "/zero", {}));
        QVERIFY(writeFile(pair.right + "/zero", {}));
        const auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(result.entries.size(), 2);
        QCOMPARE(findEntry(result, "empty")->status, Folder::Status::Same);
        QCOMPARE(findEntry(result, "zero")->status, Folder::Status::Same);
    }

    void quickModeCannotProveEquality()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/equal-size", "aaa"));
        QVERIFY(writeFile(pair.right + "/equal-size", "bbb"));
        QVERIFY(writeFile(pair.left + "/different-size", "aaa"));
        QVERIFY(writeFile(pair.right + "/different-size", "longer"));
        Folder::Options options;
        options.compareContent = false;
        const auto result = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(findEntry(result, "equal-size")->status, Folder::Status::Unknown);
        QCOMPARE(findEntry(result, "different-size")->status, Folder::Status::Different);
        QVERIFY(!result.complete);
    }

    void timestampsDoNotOverrideContent()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/same", "same"));
        QVERIFY(writeFile(pair.right + "/same", "same"));
        std::unique_ptr<Files::FileSystem> fs(Files::createNativeFileSystem());
        Files::ErrorCode error;
        QVERIFY(fs->setTimes(pair.left + "/same", Files::FileTime::fromUnixTime(1234567890, 0), {}, &error));
        const auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(result.entries.first().status, Folder::Status::Same);
        QVERIFY(result.entries.first().left.info.lastModified != result.entries.first().right.info.lastModified);
    }

    void recursionLimitIsExplicitUnknown()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/a/b/deep", "a"));
        QVERIFY(writeFile(pair.right + "/a/b/deep", "b"));
        Folder::Options options;
        options.recursive = false;
        auto result = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(result.entries.size(), 1);
        QCOMPARE(result.entries.first().status, Folder::Status::Unknown);
        options.recursive = true;
        options.maximumDepth = 1;
        result = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(result.entries.size(), 2);
        QCOMPARE(findEntry(result, "a/b")->status, Folder::Status::Unknown);
        QCOMPARE(findEntry(result, "a")->status, Folder::Status::Unknown);
        QVERIFY(!result.complete);
    }

    void linksAreComparedWithoutFollowing()
    {
#ifdef Q_OS_WIN
        QSKIP("Creating symbolic links on Windows requires a privileged test account.");
#else
        Pair pair;
        QVERIFY(QFile::link(pair.left, pair.left + "/cycle"));
        QVERIFY(QFile::link(pair.right, pair.right + "/cycle"));
        QVERIFY(QFile::link(pair.temp.path() + "/missing", pair.left + "/dangling"));
        QVERIFY(QFile::link(pair.temp.path() + "/missing", pair.right + "/dangling"));
        // 指向自己所在子树内部的链接：不是循环，两侧目标串相同，照常判相同。
        QVERIFY(QFile::link(QStringLiteral("sub/nested"), pair.left + "/inward"));
        QVERIFY(QFile::link(QStringLiteral("sub/nested"), pair.right + "/inward"));
        const auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(result.entries.size(), 3);
        // 「不跟随」是终止递归的**手段**，DIR-003 第 5 条要的是「检测到并记下来」：
        // 指向扫描根（也就是自己的上级）的链接必须是一条错误条目，而不是一条
        // 看起来只是「目标字符串不同」的普通条目。少了这一档，用户得到的信息是
        // 「这两个链接不一样」，而真正的事实是「这棵树没被走下去」。
        QCOMPARE(findEntry(result, "cycle")->left.kind, Folder::Kind::SymbolicLink);
        QCOMPARE(findEntry(result, "cycle")->status, Folder::Status::Error);
        QVERIFY2(findEntry(result, "cycle")->explanation.contains(QStringLiteral("循环符号链接")),
                 qPrintable(findEntry(result, "cycle")->explanation));
        QVERIFY(!findEntry(result, "cycle")->canCompareAsText());
        QCOMPARE(findEntry(result, "dangling")->status, Folder::Status::Same);
        QCOMPARE(findEntry(result, "inward")->status, Folder::Status::Same);
        // 结构性错误必须自报：整次比较不能声称自己完整。
        QVERIFY(!result.complete);
#endif
    }

    void recursionTierTableAndMappingStayConsistent()
    {
        // 表自检在真表上必须是空的；而自检本身不是恒真的——下面拿五份**故意
        // 写坏**的表跑同一个判定，逐份断言它报出了对应的那一条。
        const QStringList problems =
            Folder::validateRecursionTierTable(Folder::recursionTierTable());
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral(" / "))));
        QCOMPARE(Folder::recursionTierTable().size(), 3);
        // 「完全递归」档的缺省上限与 `Options::maximumDepth` 的初值必须是同一个数。
        // 漂开之后「缺省选项」反查出来的档位就不是完全递归，下拉一打开就显示错档，
        // 而运行期没有任何别的现象。两个头文件互相 include，`static_assert` 写不出来。
        QCOMPARE(Folder::kDefaultFullDepth, Folder::Options().maximumDepth);
        QCOMPARE(Folder::recursionTierIdentifier(Folder::recursionTierOf(Folder::Options())),
                 Folder::recursionTierIdentifier(Folder::RecursionTier::Full));

        // 规格点名的三档各写回什么字段。
        Folder::Options options;
        Folder::applyRecursionTier(options, Folder::RecursionTier::DirectChildren);
        QVERIFY(!options.recursive);
        QCOMPARE(options.maximumDepth, 0);
        Folder::applyRecursionTier(options, Folder::RecursionTier::OneLevel);
        QVERIFY(options.recursive);
        QCOMPARE(options.maximumDepth, 1);
        Folder::applyRecursionTier(options, Folder::RecursionTier::Full);
        QVERIFY(options.recursive);
        QCOMPARE(options.maximumDepth, Folder::kDefaultFullDepth);

        // 档位 → 字段 → 档位：三档各自往返，标识符与文案都不是空的。
        for (const auto &row : Folder::recursionTierTable()) {
            Folder::Options roundTrip;
            Folder::applyRecursionTier(roundTrip, row.tier);
            QCOMPARE(Folder::recursionTierIdentifier(Folder::recursionTierOf(roundTrip)),
                     Folder::recursionTierIdentifier(row.tier));
            QCOMPARE(Folder::recursionTierRecurses(row.tier), row.recursive);
            QVERIFY(!Folder::recursionTierIdentifier(row.tier).isEmpty());
            QVERIFY(!Folder::recursionTierLabel(row.tier).isEmpty());
            QVERIFY(!Folder::recursionTierDescription(row.tier).isEmpty());
        }

        const auto describe = [](const QVector<Folder::RecursionTierDescriptor> &table) {
            return Folder::validateRecursionTierTable(table).join(QStringLiteral(" / "));
        };
        // ① 只写一边：「不递归」却给了非零上限，档位与行为就成了两份说法。
        auto contradictory = Folder::recursionTierTable();
        contradictory[0].recursive = true;
        QVERIFY2(describe(contradictory).contains(QStringLiteral("互相矛盾")),
                 qPrintable(describe(contradictory)));
        // ② 标识符不是机器可读的（它要进设置文件、日志与命令行）。
        auto humanReadable = Folder::recursionTierTable();
        humanReadable[1].identifier = "One Level";
        QVERIFY2(describe(humanReadable).contains(QStringLiteral("机器可读")),
                 qPrintable(describe(humanReadable)));
        // ③ 标识符重复：两档在设置里写成同一个词，读回来只能落到一个上。
        auto duplicated = Folder::recursionTierTable();
        duplicated[1].identifier = duplicated[0].identifier;
        QVERIFY2(describe(duplicated).contains(QStringLiteral("重复")),
                 qPrintable(describe(duplicated)));
        // ④ 顺序不是规格顺序（界面下拉直接按表铺，顺序就是契约）。
        auto reversed = Folder::recursionTierTable();
        std::swap(reversed[0], reversed[2]);
        QVERIFY2(describe(reversed).contains(QStringLiteral("应为档位")),
                 qPrintable(describe(reversed)));
        // ⑤ 表改了、反查没跟上：这一档在下拉里永远选不中，报的是反查那一条。
        auto unreachable = Folder::recursionTierTable();
        unreachable[1].depthLimit = 4;
        QVERIFY2(describe(unreachable).contains(QStringLiteral("反查回档位")),
                 qPrintable(describe(unreachable)));
        // ⑥ 少了「完全递归」的缺省值被改小：缺省选项再也反查不回这一档。
        auto drifted = Folder::recursionTierTable();
        drifted[2].depthLimit = 2;
        QVERIFY2(describe(drifted).contains(QStringLiteral("缺省深度上限")),
                 qPrintable(describe(drifted)));
        // ⑦ 少一行本身就是问题（三档是规格的完整集合）。
        auto shortened = Folder::recursionTierTable();
        shortened.removeLast();
        QVERIFY2(describe(shortened).contains(QStringLiteral("应有")),
                 qPrintable(describe(shortened)));
    }

    void recursionTierKeepsAUserSetDepthLimit()
    {
        // 反查按**行为**归类，不按字段原值：`recursive == false` 配 `maximumDepth == 7`
        // 在引擎里与「不递归」完全同行为，因此报第一档。这一点必须写实——
        // 照原值报成「完全递归」时，下拉一打开就显示错档。
        Folder::Options stored;
        stored.recursive = false;
        stored.maximumDepth = 7;
        QCOMPARE(Folder::recursionTierIdentifier(Folder::recursionTierOf(stored)),
                 Folder::recursionTierIdentifier(Folder::RecursionTier::DirectChildren));
        // 而档位写回时**只碰 recursive**：那个 7 是用户另设的，必须留着，
        // 否则 `.lqc` 存档里的「不递归 + 深度 7」会在一次档位往返里静默变成 0。
        Folder::applyRecursionTier(stored, Folder::RecursionTier::Full);
        QVERIFY(stored.recursive);
        QCOMPARE(stored.maximumDepth, 7);
        QCOMPARE(Folder::recursionTierIdentifier(Folder::recursionTierOf(stored)),
                 Folder::recursionTierIdentifier(Folder::RecursionTier::Full));

        // `recursive == true` 配 `maximumDepth == 0` 与「不递归」同行为，也是第一档。
        Folder::Options zeroDepth;
        zeroDepth.recursive = true;
        zeroDepth.maximumDepth = 0;
        QCOMPARE(Folder::recursionTierIdentifier(Folder::recursionTierOf(zeroDepth)),
                 Folder::recursionTierIdentifier(Folder::RecursionTier::DirectChildren));
        // 与档位自相矛盾的值（<= 1）才被提到缺省：少了这一提，一次
        // 「设定为完全递归」的操作会得到「不递归」，而两处都在说自己是完全递归。
        Folder::applyRecursionTier(zeroDepth, Folder::RecursionTier::Full);
        QCOMPARE(zeroDepth.maximumDepth, Folder::kDefaultFullDepth);
        QCOMPARE(Folder::recursionTierIdentifier(Folder::recursionTierOf(zeroDepth)),
                 Folder::recursionTierIdentifier(Folder::RecursionTier::Full));
    }

    void eachRecursionTierScansExactlyItsOwnDepth()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/top.txt", "top"));
        QVERIFY(writeFile(pair.right + "/top.txt", "top"));
        QVERIFY(writeFile(pair.left + "/a/inner.txt", "inner"));
        QVERIFY(writeFile(pair.right + "/a/inner.txt", "inner"));
        QVERIFY(writeFile(pair.left + "/a/b/c/leaf.txt", "leaf"));
        QVERIFY(writeFile(pair.right + "/a/b/c/leaf.txt", "leaf"));

        const auto paths = [](const Folder::Result &result) {
            QStringList names;
            for (const auto &entry : result.entries)
                names << entry.relativePath;
            return names;
        };
        const auto optionsFor = [](Folder::RecursionTier tier) {
            Folder::Options options;
            Folder::applyRecursionTier(options, tier);
            return options;
        };

        // 三档**真的**扫了不同的东西——不是只在显示上不同。逐条列出条目集合，
        // 因为「最外层少了三个」这种差异在只断言个数时是看不出来的。
        const auto direct = Folder::compare(pair.left, pair.right,
                                            optionsFor(Folder::RecursionTier::DirectChildren));
        QCOMPARE(paths(direct), QStringList({QStringLiteral("a"), QStringLiteral("top.txt")}));
        const auto oneLevel = Folder::compare(pair.left, pair.right,
                                              optionsFor(Folder::RecursionTier::OneLevel));
        QCOMPARE(paths(oneLevel),
                 QStringList({QStringLiteral("a"), QStringLiteral("a/b"),
                              QStringLiteral("a/inner.txt"), QStringLiteral("top.txt")}));
        const auto full = Folder::compare(pair.left, pair.right,
                                          optionsFor(Folder::RecursionTier::Full));
        QCOMPARE(paths(full),
                 QStringList({QStringLiteral("a"), QStringLiteral("a/b"), QStringLiteral("a/b/c"),
                              QStringLiteral("a/b/c/leaf.txt"), QStringLiteral("a/inner.txt"),
                              QStringLiteral("top.txt")}));
        QVERIFY(full.complete);

        // 深度边界上的子目录节点仍然**在结果里**，状态既不可能是「相同」，
        // 也不可能是两个「只有一侧存在」——它两侧都在，只是没被枚举。
        const Folder::Entry *boundary = findEntry(direct, QStringLiteral("a"));
        QVERIFY(boundary);
        QCOMPARE(boundary->status, Folder::Status::Unknown);
        QVERIFY(boundary->left.exists());
        QVERIFY(boundary->right.exists());
        QVERIFY(boundary->isDirectory());
        QVERIFY(boundary->status != Folder::Status::Same);
        QVERIFY(boundary->status != Folder::Status::LeftOnly);
        QVERIFY(boundary->status != Folder::Status::RightOnly);
        // 第 1 条那条「不得仅改变显示」的另一半：没被枚举的层次不许出现在结果里。
        QVERIFY(!findEntry(direct, QStringLiteral("a/inner.txt")));
        QVERIFY(!findEntry(oneLevel, QStringLiteral("a/b/c/leaf.txt")));
        QVERIFY(!oneLevel.complete);
        QVERIFY(!direct.complete);

        // 第 2、4 条的「明确提示」：档位不递归时点出是**档位**决定的；
        // 达到上限时印出**实际生效的上限数值**。文案只有一份实现，
        // 引擎写的必须逐字等于共用函数给的那一句。
        QVERIFY2(boundary->explanation
                     == Folder::recursionBoundaryExplanation(optionsFor(Folder::RecursionTier::DirectChildren)),
                 qPrintable(boundary->explanation));
        QVERIFY2(boundary->explanation.contains(
                     Folder::recursionTierLabel(Folder::RecursionTier::DirectChildren)),
                 qPrintable(boundary->explanation));
        Folder::Options shallow;
        shallow.recursive = true;
        shallow.maximumDepth = 2;
        const auto limited = Folder::compare(pair.left, pair.right, shallow);
        const Folder::Entry *depthBoundary = findEntry(limited, QStringLiteral("a/b/c"));
        QVERIFY(depthBoundary);
        QCOMPARE(depthBoundary->status, Folder::Status::Unknown);
        QCOMPARE(depthBoundary->explanation, Folder::recursionBoundaryExplanation(shallow));
        QVERIFY2(depthBoundary->explanation.contains(QStringLiteral("2")),
                 qPrintable(depthBoundary->explanation));
        QVERIFY(!limited.complete);

        // 提示里印的必须是**实际生效**的上限：调用方传一个超界值（300）时引擎夹到
        // 256，照着原值印会让提示里的数字与真正拦住扫描的那个数不是一回事。
        Folder::Options over;
        over.recursive = true;
        over.maximumDepth = 300;
        const QString overText = Folder::recursionBoundaryExplanation(over);
        QVERIFY2(overText.contains(QString::number(Folder::kMaximumRecursionDepth)),
                 qPrintable(overText));
        QVERIFY2(!overText.contains(QStringLiteral("300")), qPrintable(overText));

        // 完全递归下同一个节点真的被读过了：状态变成「相同」而不是「未知」。
        QCOMPARE(findEntry(full, QStringLiteral("a/b/c"))->status, Folder::Status::Same);
    }

    void cyclicLinkTargetsAreDetected()
    {
        // 纯函数先按形状铺一张表：判据是「解析出来的目标等于链接自身、
        // 或是链接自身的严格上级」——它同时覆盖三种表面不同、实质相同的情形。
        const QString root = QStringLiteral("/scan/root");
        struct Row
        {
            const char *relative;
            const char *target;
            bool cycle;
            const char *why;
        };
        const QVector<Row> rows = {
            {"a/cycle", ".", true, "指向自己所在目录"},
            {"cycle", ".", true, "根目录下的链接指向自己所在目录"},
            {"a/cycle", "..", true, "指向上级（扫描根）"},
            {"a/b/cycle", "../..", true, "指向上两级"},
            {"a/cycle", "../..", true, "越过扫描根：根的上级仍然含这个链接"},
            {"a/cycle", "self/../cycle", true, "绕一圈回到自己"},
            {"cycle", "/scan/root", true, "绝对目标恰好是扫描根"},
            {"cycle", "/scan", true, "绝对目标是扫描根的上级"},
            {"cycle", "/", true, "绝对目标恰好是文件系统根"},
            {"a/cycle", "sibling", false, "指向自己的下级子树，不构成环"},
            {"a/cycle", "../sibling", false, "指向兄弟子树，不构成环"},
            {"a/cycle", "/elsewhere", false, "指向扫描范围之外且互不相干的位置"},
            {"a/cycle", "missing", false, "悬空目标"},
            {"a/cycle", "", false, "空目标（读不到就是读不到，不是循环）"},
        };
        QStringList failures;
        for (const auto &row : rows) {
            const bool detected = Folder::linkTargetReentersAncestor(
                root, QString::fromUtf8(row.relative), QString::fromUtf8(row.target));
            if (detected != row.cycle) {
                failures << QStringLiteral("%1 → 「%2」（%3）：期望 %4，实际 %5")
                                .arg(QString::fromUtf8(row.relative),
                                     QString::fromUtf8(row.target),
                                     QString::fromUtf8(row.why),
                                     row.cycle ? QStringLiteral("循环") : QStringLiteral("不循环"),
                                     detected ? QStringLiteral("循环") : QStringLiteral("不循环"));
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("\n"))));

#ifndef Q_OS_WIN
        // 引擎侧：真的造出循环链接，它必须是一条**错误条目**，而且整次比较
        // 自报不完整；而指向自己下级子树的链接必须照常比较、不被误伤。
        Pair pair;
        // 先把目录建出来，再建链接：`QFile::link` 不会替你造父目录。
        QVERIFY(writeFile(pair.left + "/a/keep.txt", "keep"));
        QVERIFY(writeFile(pair.right + "/a/keep.txt", "keep"));
        QVERIFY(writeFile(pair.left + "/sub/file.txt", "x"));
        QVERIFY(writeFile(pair.right + "/sub/file.txt", "x"));
        QVERIFY(QFile::link(QStringLiteral("."), pair.left + "/self"));
        QVERIFY(QFile::link(QStringLiteral("."), pair.right + "/self"));
        QVERIFY(QFile::link(QStringLiteral(".."), pair.left + "/a/up"));
        QVERIFY(QFile::link(QStringLiteral(".."), pair.right + "/a/up"));
        QVERIFY(QFile::link(QStringLiteral("sub"), pair.left + "/down"));
        QVERIFY(QFile::link(QStringLiteral("sub"), pair.right + "/down"));
        const auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(findEntry(result, "self")->status, Folder::Status::Error);
        QCOMPARE(findEntry(result, "a/up")->status, Folder::Status::Error);
        QVERIFY2(findEntry(result, "a/up")->explanation
                     == Folder::linkCycleExplanation(QStringLiteral("a/up"), QStringLiteral("..")),
                 qPrintable(findEntry(result, "a/up")->explanation));
        QCOMPARE(findEntry(result, "down")->status, Folder::Status::Same);
        QCOMPARE(findEntry(result, "a/keep.txt")->status, Folder::Status::Same);
        QVERIFY(!result.complete);
#endif
    }

    void recursionControlsDriveOptionsAndRescan()
    {
        FolderCompareView view;
        auto *tier = view.findChild<QComboBox *>(QStringLiteral("folderRecursionTier"));
        auto *depth = view.findChild<QSpinBox *>(QStringLiteral("folderMaximumDepth"));
        QVERIFY(tier);
        QVERIFY(depth);

        // 第一条链（用户看到的）：下拉的铺法**逐行**来自档位表，顺序也一样。
        // 只钉「选中项 → options() 的值」时，「把下拉铺法改成倒序」这种变异全绿
        // （handoff §6 里记过这条），所以显示这一侧必须单独断言。
        QCOMPARE(tier->count(), Folder::recursionTierTable().size());
        for (int i = 0; i < tier->count(); ++i) {
            const auto &row = Folder::recursionTierTable().at(i);
            QCOMPARE(tier->itemText(i), Folder::recursionTierLabel(row.tier));
            QCOMPARE(tier->itemData(i).toInt(), int(row.tier));
            QCOMPARE(tier->itemData(i, Qt::ToolTipRole).toString(),
                     Folder::recursionTierDescription(row.tier));
        }
        // 出厂状态就是缺省选项，不是另写一份初值。
        QCOMPARE(Folder::recursionTierIdentifier(view.recursionTier()),
                 Folder::recursionTierIdentifier(Folder::recursionTierOf(Folder::Options())));
        QCOMPARE(depth->value(), Folder::Options().maximumDepth);
        QVERIFY(depth->isEnabled());

        // 第二条链（实际生效的）：档位选「递归深度 1」时那两个字段就是它。
        tier->setCurrentIndex(tier->findData(int(Folder::RecursionTier::OneLevel)));
        QVERIFY(view.options().recursive);
        QCOMPARE(view.options().maximumDepth, 1);
        QCOMPARE(depth->value(), 1);
        QVERIFY(!depth->isEnabled()); // 这一档的深度由档位决定，不该可改
        tier->setCurrentIndex(tier->findData(int(Folder::RecursionTier::DirectChildren)));
        QVERIFY(!view.options().recursive);
        QCOMPARE(view.options().maximumDepth, 0);

        // 第三条链（第 3 条）：换档必须**请求重扫**，而不是只改显示。
        QSignalSpy rescan(&view, &FolderCompareView::rescanRequested);
        tier->setCurrentIndex(tier->findData(int(Folder::RecursionTier::Full)));
        QCOMPARE(rescan.count(), 1);
        QCOMPARE(depth->value(), Folder::kDefaultFullDepth);
        QVERIFY(depth->isEnabled());
        // 第 4 条：完全递归档下深度上限可另设，改它也请求重扫。
        depth->setValue(4);
        QCOMPARE(rescan.count(), 2);
        QVERIFY(view.options().recursive);
        QCOMPARE(view.options().maximumDepth, 4);
        // 把上限改成 1：档位自己落到「递归深度 1」——在引擎里那正是它的含义。
        // 于是控件与档位永远不会同时给出两个互相矛盾的档。
        depth->setValue(1);
        QCOMPARE(rescan.count(), 3);
        QCOMPARE(Folder::recursionTierIdentifier(view.recursionTier()),
                 Folder::recursionTierIdentifier(Folder::RecursionTier::OneLevel));
        QVERIFY(view.options().recursive);
        QCOMPARE(view.options().maximumDepth, 1);

        // 程序性回填**不得**触发重扫：恢复存档或会话里改设置时用户什么都没做。
        Folder::Options restored;
        restored.recursive = false;
        restored.maximumDepth = 7;
        view.setOptions(restored);
        QCOMPARE(rescan.count(), 3);
        // 而「不递归 + 深度 7」必须原样留在 options() 里：归一化成 0 会让
        // `.lqc` 存档的往返静默丢值（`savedFolderOptionsRoundTripThroughLqcAndActuallyScan`
        // 断言的就是这件事，这里把它单独钉一次，好让失败点直接指到档位重写）。
        QVERIFY(!view.options().recursive);
        QCOMPARE(view.options().maximumDepth, 7);
        QCOMPARE(Folder::recursionTierIdentifier(view.recursionTier()),
                 Folder::recursionTierIdentifier(Folder::RecursionTier::DirectChildren));
    }

    void switchingTheRecursionTierRescansWithTheNewScope()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/a/b/deep.txt", "same"));
        QVERIFY(writeFile(pair.right + "/a/b/deep.txt", "same"));
        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        auto *tier = session.view()->findChild<QComboBox *>(QStringLiteral("folderRecursionTier"));
        QVERIFY(tier);
        // 出厂档位是「完全递归」，先降到「仅根目录直属条目」再开始比较——
        // 该信号在 `Closed` 的会话上只是把档位记下来，不会开始扫描。
        tier->setCurrentIndex(tier->findData(int(Folder::RecursionTier::DirectChildren)));
        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QVERIFY(session.open());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(!findEntry(session.result(), QStringLiteral("a/b/deep.txt")));
        QCOMPARE(findEntry(session.result(), QStringLiteral("a"))->status, Folder::Status::Unknown);
        QVERIFY(!session.result().complete);

        // 换档改的是**扫描范围**，因此会话必须用新范围重扫一次——不能只改显示。
        tier->setCurrentIndex(tier->findData(int(Folder::RecursionTier::Full)));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        QVERIFY(findEntry(session.result(), QStringLiteral("a/b/deep.txt")));
        QCOMPARE(findEntry(session.result(), QStringLiteral("a/b/deep.txt"))->status,
                 Folder::Status::Same);
        QCOMPARE(findEntry(session.result(), QStringLiteral("a"))->status, Folder::Status::Same);
        QVERIFY(session.result().complete);
        // 重扫用的是新档位，且新档位已被写进会话设置（下一次打开也是它）。
        QVERIFY(session.comparisonOptions().recursive);
        QCOMPARE(session.comparisonOptions().maximumDepth, Folder::kDefaultFullDepth);

        // 档位在**没有开始过比较**的会话里只是被记下来：不该弹一句「请选择文件夹」。
        FolderCompareSession pending;
        std::unique_ptr<QWidget> pendingWidget(pending.createWidget());
        auto *pendingTier = pending.view()->findChild<QComboBox *>(QStringLiteral("folderRecursionTier"));
        QVERIFY(pendingTier);
        QSignalSpy pendingFinished(&pending, &FolderCompareSession::scanFinished);
        pendingTier->setCurrentIndex(pendingTier->findData(int(Folder::RecursionTier::OneLevel)));
        QCoreApplication::processEvents();
        QCOMPARE(pendingFinished.count(), 0);
        // 档位被记下来了，但既没开始扫描，也没弹一句「请选择文件夹」。
        QCOMPARE(Folder::recursionTierIdentifier(pending.view()->recursionTier()),
                 Folder::recursionTierIdentifier(Folder::RecursionTier::OneLevel));
        QVERIFY(!pending.statusText().contains(QStringLiteral("请选择")));
    }

    void cancellationRetainsCompletedEntries()
    {
        Pair pair;
        for (int i = 0; i < 20; ++i) {
            QVERIFY(writeFile(pair.left + QString("/%1.txt").arg(i), "same"));
            QVERIFY(writeFile(pair.right + QString("/%1.txt").arg(i), "same"));
        }
        std::atomic_bool cancelled{false};
        const auto result = Folder::compare(pair.left, pair.right, {}, &cancelled,
            [&cancelled](int count, const QString &) { if (count == 3) cancelled = true; });
        QVERIFY(result.cancelled);
        QVERIFY(!result.complete);
        QCOMPARE(result.entries.size(), 3);
        QCOMPARE(result.entries.first().status, Folder::Status::Same);
    }

    void inaccessibleDirectoryDoesNotCreateFalseOrphans()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/sub/file", "same"));
        QVERIFY(writeFile(pair.right + "/sub/file", "same"));
        FaultFileSystem fs;
        fs.unreadableDirectory = pair.right + "/sub";
        const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
        QCOMPARE(findEntry(result, "sub")->status, Folder::Status::Error);
        QCOMPARE(findEntry(result, "sub/file")->status, Folder::Status::Unknown);
        QVERIFY(!findEntry(result, "sub/file")->canCompareAsText());
        QVERIFY(!result.complete);
    }

    void inaccessibleMetadataIsError()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/file", "same"));
        QVERIFY(writeFile(pair.right + "/file", "same"));
        FaultFileSystem fs;
        fs.unreadableFile = pair.left + "/file";
        const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
        QCOMPARE(result.entries.first().status, Folder::Status::Error);
        QVERIFY(!result.entries.first().explanation.isEmpty());
    }

    void fileChangingDuringComparisonIsError()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/file", "same"));
        QVERIFY(writeFile(pair.right + "/file", "same"));
        FaultFileSystem fs;
        fs.changingFile = pair.left + "/file";
        const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
        QCOMPARE(result.entries.first().status, Folder::Status::Error);
        QVERIFY(result.entries.first().explanation.contains(QStringLiteral("变化")));
    }

    void deterministicOrderingAndHiddenFiles()
    {
        Pair pair;
        for (const QString &name : {QStringLiteral("z.txt"), QStringLiteral(".hidden"), QStringLiteral("中文.txt")}) {
            QVERIFY(writeFile(pair.left + "/" + name, "same"));
            QVERIFY(writeFile(pair.right + "/" + name, "same"));
        }
        const auto a = Folder::compare(pair.left, pair.right);
        const auto b = Folder::compare(pair.left, pair.right);
        QCOMPARE(a.entries.size(), 3);
        for (int i = 0; i < a.entries.size(); ++i)
            QCOMPARE(a.entries.at(i).relativePath, b.entries.at(i).relativePath);
        QCOMPARE(a.entries.first().relativePath, QStringLiteral(".hidden"));
    }

    void invalidAndOverlappingRoots()
    {
        Pair pair;
        auto result = Folder::compare(pair.left, pair.temp.path() + "/missing");
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!result.complete);
        result = Folder::compare(pair.left, pair.left);
        QCOMPARE(result.warnings.size(), 1);
        QVERIFY(QDir().mkpath(pair.left + "/child"));
        result = Folder::compare(pair.left, pair.left + "/child");
        QCOMPARE(result.warnings.size(), 1);
        QVERIFY(result.entries.size() < 5);
    }

    void sessionViewFilteringRefreshAndActivation()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/sub/same", "same"));
        QVERIFY(writeFile(pair.right + "/sub/same", "same"));
        QVERIFY(writeFile(pair.left + "/different", "aaa"));
        QVERIFY(writeFile(pair.right + "/different", "bbb"));
        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QSignalSpy activated(&session, &FolderCompareSession::compareFilesRequested);
        QVERIFY(session.open());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(session.state(), CompareSession::State::Open);
        QVERIFY(!session.canSave());
        auto *view = session.view();
        QVERIFY(view);
        widget->resize(1380, 650);
        widget->show();
        QTest::qWait(20);
        const QString capture = qEnvironmentVariable("LQCOMPARE_FOLDER_CAPTURE");
        if (!capture.isEmpty())
            QVERIFY(widget->grab().save(capture));
        auto *tree = view->leftTree();
        auto *model = tree->model();
        QModelIndex sub = indexNamed(model, "sub");
        QVERIFY(sub.isValid());
        tree->expand(sub);
        QVERIFY(view->rightTree()->isExpanded(sub));
        QModelIndex same = indexNamed(model, "same");
        QVERIFY(same.isValid());
        QVERIFY(QMetaObject::invokeMethod(tree, "activated", Q_ARG(QModelIndex, same)));
        QCOMPARE(activated.count(), 1);
        QCOMPARE(activated.first().first().toString(), pair.left + "/sub/same");
        auto *filter = view->findChild<QComboBox *>(QStringLiteral("folderStatusFilter"));
        QVERIFY(filter);
        filter->setCurrentIndex(filter->findData(int(Folder::Status::Different)));
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("different"));
        filter->setCurrentIndex(0);
        tree->expand(indexNamed(model, "sub"));
        QVERIFY(writeFile(pair.right + "/different", "aaa"));
        QVERIFY(session.reload());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        QCOMPARE(findEntry(session.result(), "different")->status, Folder::Status::Same);
        QVERIFY(tree->isExpanded(indexNamed(model, "sub")));
        session.close();
        QVERIFY(!view->isEnabled());
    }

    void unsubmittedPathEditsDoNotChangeSessionSource()
    {
        Pair pair;
        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        auto *edit = widget->findChild<QLineEdit *>(QStringLiteral("folderLeftPath"));
        edit->setText(pair.temp.path());
        QCOMPARE(session.leftPath(), pair.left);
        QSignalSpy paths(&session, &FolderCompareSession::pathsChanged);
        session.setPaths(pair.temp.path(), pair.right);
        QCOMPARE(paths.count(), 1);
        QCOMPARE(session.leftPath(), pair.temp.path());
    }

    void scannerRejectsConcurrentStartsAndClosesCleanly()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/file", "same"));
        QVERIFY(writeFile(pair.right + "/file", "same"));
        Folder::Scanner scanner;
        QSignalSpy finished(&scanner, &Folder::Scanner::finished);
        QVERIFY(scanner.start(pair.left, pair.right));
        QVERIFY(!scanner.start(pair.left, pair.right));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(!scanner.isRunning());
        FolderCompareSession session(pair.left, pair.right);
        QSignalSpy sessionFinished(&session, &FolderCompareSession::scanFinished);
        QVERIFY(session.open());
        session.close();
        QTRY_VERIFY_WITH_TIMEOUT(!session.isScanning(), 5000);
        QCOMPARE(sessionFinished.count(), 0);
    }

    void scanMasksNeverPruneIncludedDescendants()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/src/nested/keep.txt", "same"));
        QVERIFY(writeFile(pair.right + "/src/nested/keep.txt", "same"));
        QVERIFY(writeFile(pair.left + "/src/nested/ignored.bin", "aaa"));
        QVERIFY(writeFile(pair.right + "/src/nested/ignored.bin", "bbb"));
        Folder::Options options;
        options.scanMaskDeclaration = QStringLiteral("src/nested/keep.txt\n-src\n-src/nested");
        const auto result = Folder::compare(pair.left, pair.right, options);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.complete);
        QCOMPARE(result.entries.size(), 4);
        QCOMPARE(result.excludedCount, 1);
        const auto *src = findEntry(result, "src");
        QVERIFY(src->excludedByMask);
        QVERIFY(src->hasIncludedDescendants);
        QVERIFY(src->inComparison());
        QCOMPARE(src->status, Folder::Status::Same);
        QCOMPARE(findEntry(result, "src/nested/keep.txt")->status, Folder::Status::Same);
        const auto *ignored = findEntry(result, "src/nested/ignored.bin");
        QVERIFY(!ignored->inComparison());
        QCOMPARE(ignored->status, Folder::Status::Unknown);
        QCOMPARE(ignored->firstDifference, qint64(-1));
        QVERIFY(!ignored->canCompareAsText());
        QVERIFY(!ignored->filterReason.isEmpty());
        options.maximumDepth = 0;
        const auto limited = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(limited.entries.size(), 1);
        QVERIFY(findEntry(limited, "src")->inComparison());
        QCOMPARE(findEntry(limited, "src")->status, Folder::Status::Unknown);
        QVERIFY(!limited.complete);
    }

    void maskExclusionPriorityAndInvalidInput()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/keep.txt", "same"));
        QVERIFY(writeFile(pair.right + "/keep.txt", "same"));
        QVERIFY(writeFile(pair.left + "/ignored.txt", "aaa"));
        QVERIFY(writeFile(pair.right + "/ignored.txt", "bbb"));
        Folder::Options options;
        options.scanMaskDeclaration = QStringLiteral("*.txt\n-ignored.txt");
        auto result = Folder::compare(pair.left, pair.right, options);
        QVERIFY(findEntry(result, "ignored.txt")->excludedByMask);
        QCOMPARE(findEntry(result, "keep.txt")->status, Folder::Status::Same);
        options.scanMaskDeclaration = QStringLiteral("[");
        result = Folder::compare(pair.left, pair.right, options);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(!result.complete);
        QVERIFY(result.entries.isEmpty());
    }

    void maskedUnreadableDirectoryStaysVisibleAsError()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/src/keep.txt", "same"));
        QVERIFY(writeFile(pair.right + "/src/keep.txt", "same"));
        FaultFileSystem fs;
        fs.unreadableDirectory = pair.right + "/src";
        Folder::Options options;
        options.scanMaskDeclaration = QStringLiteral("*.txt\n-src");
        const auto result = Folder::compare(pair.left, pair.right, options, nullptr, {}, &fs);
        QCOMPARE(findEntry(result, "src")->status, Folder::Status::Error);
        QVERIFY(findEntry(result, "src")->inComparison());
        QVERIFY(!result.complete);
        FolderCompareView view;
        view.setResult(result);
        QVERIFY(indexNamed(view.leftTree()->model(), "src").isValid());
        fs.unreadableDirectory.clear();
        fs.unreadableFile = pair.right + "/src";
        const auto metadataFailure = Folder::compare(pair.left, pair.right, options, nullptr, {}, &fs);
        QCOMPARE(findEntry(metadataFailure, "src")->status, Folder::Status::Error);
        QVERIFY(findEntry(metadataFailure, "src")->inComparison());
        QVERIFY(!metadataFailure.complete);
    }

    void caseInsensitivePairsPreserveActualPaths()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/SRC/ReadMe.txt", "same"));
        QVERIFY(writeFile(pair.right + "/src/readme.txt", "same"));
        auto result = Folder::compare(pair.left, pair.right);
        QCOMPARE(findEntry(result, "SRC")->status, Folder::Status::LeftOnly);
        QCOMPARE(findEntry(result, "src")->status, Folder::Status::RightOnly);
        Folder::Options options;
        options.nameCaseSensitivity = Qt::CaseInsensitive;
        options.scanMaskDeclaration = QStringLiteral("SRC/README.TXT");
        result = Folder::compare(pair.left, pair.right, options);
        QCOMPARE(result.entries.size(), 2);
        const auto *file = findEntry(result, "SRC/ReadMe.txt");
        QVERIFY(file);
        QCOMPARE(file->status, Folder::Status::Same);
        QVERIFY(file->nameCaseDifference);
        QCOMPARE(file->left.info.path, pair.left + "/SRC/ReadMe.txt");
        QCOMPARE(file->right.info.path, pair.right + "/src/readme.txt");
        QVERIFY(findEntry(result, "SRC")->hasIncludedDescendants);
    }

    void ambiguousCaseFoldNamesNeverGuessOrLoseRows()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/lower-fixture", "same"));
        QVERIFY(writeFile(pair.left + "/upper-fixture", "other"));
        QVERIFY(writeFile(pair.right + "/other-fixture", "same"));
        // Simulate names from a case-sensitive volume even when the machine's
        // temporary directory is on a case-insensitive filesystem.
        FaultFileSystem fs;
        fs.aliases.insert(pair.left + "/lower-fixture", "name.txt");
        fs.aliases.insert(pair.left + "/upper-fixture", "NAME.txt");
        fs.aliases.insert(pair.right + "/other-fixture", "name.txt");
        Folder::Options options;
        options.nameCaseSensitivity = Qt::CaseInsensitive;
        auto result = Folder::compare(pair.left, pair.right, options, nullptr, {}, &fs);
        QCOMPARE(result.entries.size(), 2);
        for (const auto &entry : result.entries) {
            QCOMPARE(entry.status, Folder::Status::Error);
            QVERIFY(entry.explanation.contains(QStringLiteral("歧义")));
            QVERIFY(!entry.canCompareAsText());
        }
        QVERIFY(!result.complete);
        options.nameCaseSensitivity = Qt::CaseSensitive;
        result = Folder::compare(pair.left, pair.right, options, nullptr, {}, &fs);
        QCOMPARE(findEntry(result, "NAME.txt")->status, Folder::Status::LeftOnly);
        QCOMPARE(findEntry(result, "name.txt")->status, Folder::Status::Same);
    }

    void displayFiltersDoNotChangeComparisonScope()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/src/keep.txt", "same"));
        QVERIFY(writeFile(pair.right + "/src/keep.txt", "same"));
        QVERIFY(writeFile(pair.left + "/src/ignored.bin", "aaa"));
        QVERIFY(writeFile(pair.right + "/src/ignored.bin", "bbb"));
        Folder::Options options;
        options.scanMaskDeclaration = QStringLiteral("*.txt");
        FolderCompareSession session(pair.left, pair.right);
        QVERIFY(session.setComparisonOptions(options));
        std::unique_ptr<QWidget> widget(session.createWidget());
        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QVERIFY(session.open());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        auto *view = session.view();
        auto *model = view->leftTree()->model();
        QVERIFY(indexNamed(model, "src").isValid());
        QVERIFY(indexNamed(model, "keep.txt").isValid());
        QVERIFY(!indexNamed(model, "ignored.bin").isValid());
        QCOMPARE(session.result().entries.size(), 3);
        view->resetDisplayFilters();
        QVERIFY(indexNamed(model, "ignored.bin").isValid());
        QCOMPARE(session.result().entries.size(), 3);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(view->options().scanMaskDeclaration, options.scanMaskDeclaration);
        QCOMPARE(view->visibleDifferenceCount(), 0); // Excluded content isn't a difference.
        QVERIFY(widget->findChild<QPushButton *>(QStringLiteral("folderRefresh"))->shortcut().isEmpty());
        view->findChild<QPlainTextEdit *>(QStringLiteral("folderScanMask"))->setPlainText(QStringLiteral("["));
        QString error;
        QVERIFY(!session.reload(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(session.result().entries.size(), 3);
    }

    void differenceNavigationAndSelectionRespectDisplayFilters()
    {
        Pair pair;
        for (const QString &name : {QStringLiteral("a.txt"), QStringLiteral("b.txt"),
                                   QStringLiteral("sub/c.txt"), QStringLiteral("sub/d.txt")}) {
            QVERIFY(writeFile(pair.left + "/" + name, "same"));
            QVERIFY(writeFile(pair.right + "/" + name,
                              name == "b.txt" || name == "sub/c.txt" ? "DIFF" : "same"));
        }
        QVERIFY(writeFile(pair.left + "/z.txt", "left only"));
        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        widget->resize(1380, 650);
        widget->show();
        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QVERIFY(session.open());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        auto *view = session.view();
        auto *tree = view->leftTree();
        tree->setFocus();
        QTest::qWait(10);
        QCOMPARE(view->visibleDifferenceCount(), 3);
        session.firstDifference();
        QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("b.txt"));
        session.nextDifference();
        QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("c.txt"));
        QVERIFY(tree->isExpanded(tree->currentIndex().parent()));
        session.nextDifference();
        QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("z.txt"));
        session.nextDifference();
        QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("z.txt"));
        session.previousDifference();
        QCOMPARE(tree->currentIndex().data().toString(), QStringLiteral("c.txt"));
        auto *filter = view->findChild<QComboBox *>(QStringLiteral("folderStatusFilter"));
        filter->setCurrentIndex(filter->findData(int(Folder::Status::Different)));
        QCOMPARE(view->visibleDifferenceCount(), 2);
        view->rightTree()->setFocus();
        QTest::qWait(10);
        session.selectAllDifferences();
        QCOMPARE(view->rightTree()->selectionModel()->selectedRows().size(), 2);
        QCOMPARE(tree->selectionModel()->selectedRows().size(), 1); // Independent side selections.
        session.lastDifference();
        QCOMPARE(view->rightTree()->currentIndex().data().toString(), QStringLiteral("c.txt"));
        filter->setCurrentIndex(filter->findData(int(Folder::Status::Same)));
        QCOMPARE(view->visibleDifferenceCount(), 0);
        session.nextDifference();
        QVERIFY(session.statusText().contains(QStringLiteral("没有差异")));
        QCOMPARE(finished.count(), 1);
    }

    void hideEmptyFoldersKeepsReadErrorsVisible()
    {
        Pair pair;
        for (const auto &root : {pair.left, pair.right}) {
            QVERIFY(QDir().mkpath(root + "/empty"));
            QVERIFY(QDir().mkpath(root + "/denied"));
        }
        FaultFileSystem fs;
        fs.unreadableDirectory = pair.right + "/denied";
        const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
        FolderCompareView view;
        view.setResult(result);
        view.findChild<QCheckBox *>(QStringLiteral("folderHideEmpty"))->setChecked(true);
        auto *model = view.leftTree()->model();
        QVERIFY(!indexNamed(model, "empty").isValid());
        QVERIFY(indexNamed(model, "denied").isValid());
    }

    void contextualErrorParentsAreNotNavigationStopsForSameFilter()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/sub/error.txt", "same"));
        QVERIFY(writeFile(pair.right + "/sub/error.txt", "same"));
        QVERIFY(writeFile(pair.left + "/sub/same.txt", "same"));
        QVERIFY(writeFile(pair.right + "/sub/same.txt", "same"));
        FaultFileSystem fs;
        fs.unreadableFile = pair.right + "/sub/error.txt";
        const auto result = Folder::compare(pair.left, pair.right, {}, nullptr, {}, &fs);
        QCOMPARE(findEntry(result, "sub")->status, Folder::Status::Error);
        FolderCompareView view;
        view.setResult(result);
        auto *filter = view.findChild<QComboBox *>(QStringLiteral("folderStatusFilter"));
        filter->setCurrentIndex(filter->findData(int(Folder::Status::Same)));
        QVERIFY(indexNamed(view.leftTree()->model(), "sub").isValid());
        QVERIFY(indexNamed(view.leftTree()->model(), "same.txt").isValid());
        QCOMPARE(view.visibleDifferenceCount(), 0);
    }

    void savedFolderOptionsRoundTripThroughLqcAndActuallyScan()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/Report.TXT", "aaa"));
        QVERIFY(writeFile(pair.right + "/report.txt", "bbb"));
        QVERIFY(writeFile(pair.left + "/ignored.txt", "aaa"));
        QVERIFY(writeFile(pair.right + "/ignored.txt", "bbb"));
        QVERIFY(writeFile(pair.left + "/sub/deep.txt", "same"));
        QVERIFY(writeFile(pair.right + "/sub/deep.txt", "same"));
        Folder::Options chosen;
        chosen.scanMaskDeclaration = QStringLiteral("*.txt\n-ignored.txt");
        chosen.nameCaseSensitivity = Qt::CaseInsensitive;
        chosen.recursive = false;
        chosen.compareContent = false;
        chosen.maximumDepth = 7;
        chosen.compareFirstBytes = 4096;

        FolderCompareSession original(pair.left, pair.right);
        std::unique_ptr<QWidget> originalWidget(original.createWidget());
        // Simulate the form being edited and applied through Compare/open.
        original.view()->setOptions(chosen);
        QSignalSpy originalFinished(&original, &FolderCompareSession::scanFinished);
        QVERIFY(original.open());
        QTRY_COMPARE_WITH_TIMEOUT(originalFinished.count(), 1, 5000);
        QVERIFY(sameOptions(original.comparisonOptions(), chosen));
        const QVariantMap values = savedSettings(original.sessionSettings());
        QCOMPARE(values.size(), 6);
        QCOMPARE(values.value(QStringLiteral("folder.scanMaskDeclaration")).toString(), chosen.scanMaskDeclaration);
        QCOMPARE(values.value(QStringLiteral("folder.nameCaseSensitivity")).toInt(), int(Qt::CaseInsensitive));
        QCOMPARE(values.value(QStringLiteral("folder.compareFirstBytes")).toLongLong(), qint64(4096));

        original.view()->findChild<QCheckBox *>(QStringLiteral("folderHideEmpty"))->setChecked(true);
        original.view()->findChild<QCheckBox *>(QStringLiteral("folderHideExcluded"))->setChecked(false);
        auto *filter = original.view()->findChild<QComboBox *>(QStringLiteral("folderStatusFilter"));
        filter->setCurrentIndex(filter->findData(int(Folder::Status::Different)));
        QCOMPARE(savedSettings(original.sessionSettings()), values); // Display state is temporary.

        SessionDocument document;
        document.typeId = QStringLiteral("folder");
        document.leftPath = original.leftPath();
        document.rightPath = original.rightPath();
        document.settings = values;
        const QString filename = pair.temp.path() + QStringLiteral("/saved-folder.lqc");
        QString error;
        QVERIFY2(document.save(filename, &error), qPrintable(error));
        SessionDocument loaded;
        QVERIFY2(SessionDocument::load(filename, &loaded, &error), qPrintable(error));
        FolderCompareSession restored(loaded.leftPath, loaded.rightPath);
        std::unique_ptr<QWidget> restoredWidget(restored.createWidget());
        for (auto it = loaded.settings.cbegin(); it != loaded.settings.cend(); ++it)
            QVERIFY(restored.sessionSettings()->setValue(it.key(), it.value()));
        QVERIFY(!restored.isScanning());
        QVERIFY(sameOptions(restored.comparisonOptions(), chosen));
        QVERIFY(sameOptions(restored.view()->options(), chosen));
        QVERIFY(!restored.view()->findChild<QCheckBox *>(QStringLiteral("folderHideEmpty"))->isChecked());
        QVERIFY(restored.view()->findChild<QCheckBox *>(QStringLiteral("folderHideExcluded"))->isChecked());
        QCOMPARE(restored.view()->findChild<QComboBox *>(QStringLiteral("folderStatusFilter"))->currentData().toInt(), -1);

        QSignalSpy finished(&restored, &FolderCompareSession::scanFinished);
        QVERIFY2(restored.open(&error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        const auto *report = findEntry(restored.result(), QStringLiteral("Report.TXT"));
        QVERIFY(report);
        QCOMPARE(report->right.info.path, pair.right + "/report.txt"); // Restored ignore-case pairing.
        QCOMPARE(report->status, Folder::Status::Unknown); // Restored content comparison disabled.
        QVERIFY(!findEntry(restored.result(), "ignored.txt")->inComparison()); // Restored mask.
        QVERIFY(!findEntry(restored.result(), "sub/deep.txt")); // Restored nonrecursive scope.

        // The opposite direction remains live after restore: settings changes
        // update the existing form and become effective at the next reload.
        QVERIFY(restored.sessionSettings()->setValue(QStringLiteral("folder.compareContent"), true));
        QVERIFY(restored.view()->options().compareContent);
        QVERIFY(restored.reload(&error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        QCOMPARE(findEntry(restored.result(), "Report.TXT")->status, Folder::Status::Different);
    }

    void rejectedOptionsAtomicallyPreserveSettingsOptionsAndView()
    {
        FolderCompareSession session;
        Folder::Options accepted;
        accepted.scanMaskDeclaration = QStringLiteral("*.txt");
        accepted.nameCaseSensitivity = Qt::CaseInsensitive;
        accepted.maximumDepth = 9;
        QVERIFY(session.setComparisonOptions(accepted));
        std::unique_ptr<QWidget> widget(session.createWidget());
        const QVariantMap before = savedSettings(session.sessionSettings());
        QSignalSpy changed(session.sessionSettings(), &SessionSettings::changed);
        QVector<Folder::Options> invalid;
        Folder::Options next = accepted;
        next.recursive = false;
        next.compareContent = false;
        next.nameCaseSensitivity = Qt::CaseSensitive;
        next.scanMaskDeclaration = QStringLiteral("[");
        invalid.append(next);
        next = accepted;
        next.nameCaseSensitivity = static_cast<Qt::CaseSensitivity>(99);
        invalid.append(next);
        next = accepted;
        next.maximumDepth = -1;
        invalid.append(next);
        next.maximumDepth = 257;
        invalid.append(next);
        for (const auto &options : invalid) {
            QString error;
            QVERIFY(!session.setComparisonOptions(options, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(savedSettings(session.sessionSettings()), before);
            QVERIFY(sameOptions(session.comparisonOptions(), accepted));
            QVERIFY(sameOptions(session.view()->options(), accepted));
        }
        QCOMPARE(changed.count(), 0);
    }

    void invalidRestoredSettingsBlockScanUntilExplicitlyRepaired()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/same", "same"));
        QVERIFY(writeFile(pair.right + "/same", "same"));
        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        const auto previous = session.comparisonOptions();
        QVERIFY(session.sessionSettings()->setValue(QStringLiteral("folder.nameCaseSensitivity"), 0.5));
        QVERIFY(sameOptions(session.comparisonOptions(), previous));
        QVERIFY(sameOptions(session.view()->options(), previous));
        QString error;
        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QVERIFY(!session.open(&error));
        QVERIFY(error.contains(QStringLiteral("folder.nameCaseSensitivity")));
        QVERIFY(!session.isScanning());
        QCOMPARE(finished.count(), 0);
        QVERIFY(session.sessionSettings()->setValue(QStringLiteral("folder.nameCaseSensitivity"), int(Qt::CaseSensitive)));
        QVERIFY2(session.open(&error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QCOMPARE(session.result().entries.first().status, Folder::Status::Same);

        QVERIFY(session.sessionSettings()->setValue(QStringLiteral("folder.recursive"), QStringLiteral("false")));
        QVERIFY(!session.reload(&error));
        QVERIFY(error.contains(QStringLiteral("folder.recursive")));
        QVERIFY(sameOptions(session.comparisonOptions(), previous));
        QVERIFY(session.setComparisonOptions(previous, &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(session.sessionSettings()->value(QStringLiteral("folder.recursive")).type(), QVariant::Bool);
        session.sessionSettings()->clear();
        QVERIFY(sameOptions(session.comparisonOptions(), Folder::Options()));
        QVERIFY(sameOptions(session.view()->options(), Folder::Options()));
    }

    // -------------------------------------------------------------------------
    // DIR-011 条目状态判定与语义
    //
    // 这一族用例刻意**不**复述 `Tests/EntryStatus` 里那张纯函数真值表，
    // 而是回答另一个问题：表里的每一条，在**真的目录树**、真的引擎、
    // 真的视图上是不是也成立。两边的用例数不重要，重要的是同一条判据
    // 既有脱离文件系统的部分，也有穿过文件系统的部分。
    // -------------------------------------------------------------------------

    // DIR-011 第 1 条：内容证据与主状态是两维。
    // 判据不是「字段存不存在」，而是**主状态相同的两个条目可以带着不同的证据**：
    // 同为「不同」的两对文件，一对是逐字节读出来的，另一对只要大小不等就定案，
    // 后者一个字节都没读过。压成一维（哪怕只是压成一个布尔）迟早会让某个
    // 消费点把「没读」当成「读过」。
    void contentEvidenceSeparatesProofFromNeverReading()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/size.bin", "abc"));
        QVERIFY(writeFile(pair.right + "/size.bin", "abcdef"));
        QVERIFY(writeFile(pair.left + "/bytes.bin", "abc"));
        QVERIFY(writeFile(pair.right + "/bytes.bin", "abd"));
        QVERIFY(writeFile(pair.left + "/same.bin", "abc"));
        QVERIFY(writeFile(pair.right + "/same.bin", "abc"));

        const auto result = Folder::compare(pair.left, pair.right);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const auto *size = findEntry(result, "size.bin");
        const auto *bytes = findEntry(result, "bytes.bin");
        const auto *same = findEntry(result, "same.bin");
        QVERIFY(size && bytes && same);

        // 大小不同就足以定案：内容这一维因此停在「未比较」。
        // 如果这里被写成「字节不同」，报表就会声称读过内容——而它没有。
        QCOMPARE(size->status, Folder::Status::Different);
        QCOMPARE(size->contentEvidence, Folder::ContentEvidence::NotCompared);
        QCOMPARE(bytes->status, Folder::Status::Different);
        QCOMPARE(bytes->contentEvidence, Folder::ContentEvidence::ByteDifferent);
        QCOMPARE(same->status, Folder::Status::Same);
        QCOMPARE(same->contentEvidence, Folder::ContentEvidence::ByteIdentical);

        // 「这份证据能不能证明内容相同」必须由证据自己回答。主状态为「不同」的
        // 条目也带着一份证据——靠主状态去反推就把两维重新粘回一维了。
        QVERIFY(Folder::contentEvidenceProvesIdentity(Folder::ContentEvidence::ByteIdentical));
        QVERIFY(!Folder::contentEvidenceProvesIdentity(Folder::ContentEvidence::ByteDifferent));
        QVERIFY(!Folder::contentEvidenceProvesIdentity(Folder::ContentEvidence::NotCompared));
    }

    // DIR-011 第 1 条：部分比较是**第三种**证据，既不是「相同」也不是「未比较」。
    // 「一个字节都没读」和「读了前 N 个字节、后面不知道」在同步场景里
    // 触发的动作完全不同，所以证据这一维必须能把它们分开。
    void partialComparisonIsItsOwnEvidence()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/tail.bin", "ABCD-one"));
        QVERIFY(writeFile(pair.right + "/tail.bin", "ABCD-two"));

        Folder::Options options;
        options.compareFirstBytes = 4;
        const auto result = Folder::compare(pair.left, pair.right, options);
        const auto *entry = findEntry(result, "tail.bin");
        QVERIFY(entry);
        QCOMPARE(entry->contentEvidence, Folder::ContentEvidence::Partial);
        QVERIFY(entry->partialComparison());
        QVERIFY(entry->contentEvidence != Folder::ContentEvidence::NotCompared);
        QVERIFY(!Folder::contentEvidenceProvesIdentity(entry->contentEvidence));
        QVERIFY(!Folder::contentEvidenceCoversWholeContent(entry->contentEvidence));
        QVERIFY(!result.complete);
    }

    // DIR-011 第 1 条：内容比对被关掉时，证据是「未比较」而不是「相同」。
    // 这是第 3 条那句「内容未读完不得归为相同」在开关上的样子。
    void disabledContentComparisonReportsNotCompared()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/same.txt", "same"));
        QVERIFY(writeFile(pair.right + "/same.txt", "same"));
        Folder::Options options;
        options.compareContent = false;
        const auto result = Folder::compare(pair.left, pair.right, options);
        const auto *entry = findEntry(result, "same.txt");
        QVERIFY(entry);
        QVERIFY(entry->status != Folder::Status::Same);
        QCOMPARE(entry->contentEvidence, Folder::ContentEvidence::NotCompared);
        QVERIFY(!result.complete);
    }

    // DIR-011 第 2 条：时间关系是**独立**的一维，不参与主状态。
    // 两侧内容完全相同的两个文件只因为修改时间不同，主状态必须仍是「相同」；
    // 同时「哪一侧较新」也要说得出来。做成第四种主状态就得在两者之间二选一，
    // 而它们其实同时成立。
    void timeRelationIsSeparateFromMainStatus()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/left-newer.txt", "same"));
        QVERIFY(writeFile(pair.right + "/left-newer.txt", "same"));
        QVERIFY(writeFile(pair.left + "/right-newer.txt", "same"));
        QVERIFY(writeFile(pair.right + "/right-newer.txt", "same"));
        QVERIFY(writeFile(pair.left + "/orphan.txt", "same"));

        std::unique_ptr<Files::FileSystem> fs(Files::createNativeFileSystem());
        Files::ErrorCode error;
        const qint64 base = 1600000000; // 固定时刻：结论不随运行时间漂移。
        const auto stamp = [&](const QString &path, qint64 seconds) {
            return fs->setTimes(path, Files::FileTime::fromUnixTime(seconds, 0), {}, &error);
        };
        QVERIFY(stamp(pair.left + "/left-newer.txt", base + 600));
        QVERIFY(stamp(pair.right + "/left-newer.txt", base));
        QVERIFY(stamp(pair.left + "/right-newer.txt", base));
        QVERIFY(stamp(pair.right + "/right-newer.txt", base + 600));
        QVERIFY(stamp(pair.left + "/orphan.txt", base));

        const auto result = Folder::compare(pair.left, pair.right);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        const auto *leftNewer = findEntry(result, "left-newer.txt");
        const auto *rightNewer = findEntry(result, "right-newer.txt");
        const auto *orphan = findEntry(result, "orphan.txt");
        QVERIFY(leftNewer && rightNewer && orphan);

        QCOMPARE(leftNewer->status, Folder::Status::Same);
        QCOMPARE(leftNewer->timeRelation, Folder::TimeRelation::LeftNewer);
        QCOMPARE(rightNewer->status, Folder::Status::Same);
        QCOMPARE(rightNewer->timeRelation, Folder::TimeRelation::RightNewer);
        // 孤儿项没有对侧时间可比：这里必须是「未知」。塌成「两侧相同」是错的，
        // 把缺失读成「零纳秒」再和真实时间比出「右侧较新」更是错的——
        // 后者会凭空给出一个方向，而方向是同步动作的依据。
        QCOMPARE(orphan->status, Folder::Status::LeftOnly);
        QCOMPARE(orphan->timeRelation, Folder::TimeRelation::Unknown);
    }

    // DIR-011 第 3 条：内容未读完不得归为相同。
    // 「没展开」和「展开后确实是空的」在结果上长得一模一样（都是「没有子条目」），
    // 而结论相反：前者什么都不能说，后者是货真价实的「相同」。
    void unreadDirectoryIsNeverSummarisedAsSame()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/sub/same.txt", "same"));
        QVERIFY(writeFile(pair.right + "/sub/same.txt", "same"));
        QVERIFY(QDir().mkpath(pair.left + "/blank"));
        QVERIFY(QDir().mkpath(pair.right + "/blank"));

        // 先证明这两个目录真的读过之后是「相同」——否则下面那条
        //「未读 ≠ 相同」可能只是因为它们本来就是不同的东西。
        const auto deep = Folder::compare(pair.left, pair.right);
        QVERIFY2(deep.error.isEmpty(), qPrintable(deep.error));
        QCOMPARE(findEntry(deep, "sub")->status, Folder::Status::Same);
        QCOMPARE(findEntry(deep, "blank")->status, Folder::Status::Same);
        QVERIFY(deep.complete);

        Folder::Options shallow;
        shallow.recursive = false;
        const auto notRead = Folder::compare(pair.left, pair.right, shallow);
        for (const char *name : {"sub", "blank"}) {
            const auto *entry = findEntry(notRead, QString::fromLatin1(name));
            QVERIFY2(entry, name);
            QCOMPARE(entry->status, Folder::Status::Unknown);
            // 未知不等于被排除：它仍在比较范围内，只是没读完。
            QVERIFY(entry->inComparison());
        }
        QVERIFY(findEntry(notRead, "blank")->explanation.contains(QStringLiteral("未展开")));
        QVERIFY(!notRead.complete);
    }

    // DIR-011 第 3 条：两侧均改 / 冲突只能由**有效基线**推导。
    // 两份当前文件永远推不出「谁先动的手」，所以没有基线时这两档一个都不许出现；
    // 传进来一份坏基线也不能凑合着用——那会让「无效」和「没有」得出不同结论。
    void baselineDerivedStatesNeedAValidBaseline()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/retyped.txt", "SAME-V2"));
        QVERIFY(writeFile(pair.right + "/retyped.txt", "SAME-V2"));
        QVERIFY(writeFile(pair.left + "/clash.txt", "LEFT"));
        QVERIFY(writeFile(pair.right + "/clash.txt", "RIGHT"));

        const auto withoutBaseline = Folder::compare(pair.left, pair.right);
        QVERIFY(!withoutBaseline.baselineApplied);
        for (const auto &entry : withoutBaseline.entries) {
            QVERIFY(entry.status != Folder::Status::BothChanged);
            QVERIFY(entry.status != Folder::Status::Conflict);
        }

        const auto ancestor = [] {
            Folder::AncestorRecord record;
            // 尺寸与时间都和两侧当前值不同 →「这一侧相对祖先动过」成立。
            record.size = 1;
            record.modified = Files::FileTime::fromUnixTime(1000, 0);
            return record;
        };
        Folder::BaselineView baseline;
        baseline.valid = true;
        baseline.leftRoot = pair.left;
        baseline.rightRoot = pair.right;
        baseline.ancestors.insert(QStringLiteral("retyped.txt"), ancestor());
        baseline.ancestors.insert(QStringLiteral("clash.txt"), ancestor());
        QVERIFY(Folder::validateBaselineView(baseline).isEmpty());

        const auto withBaseline = Folder::compare(pair.left, pair.right, Folder::Options(),
                                                  nullptr, Folder::Progress(), nullptr, &baseline);
        QVERIFY(withBaseline.baselineApplied);
        // 两侧都动过、且改法一致（当前内容仍然相同）→ 同步无害。
        QCOMPARE(findEntry(withBaseline, "retyped.txt")->status, Folder::Status::BothChanged);
        // 两侧都动过、但改出了不同内容 → 必须由人决定。
        QCOMPARE(findEntry(withBaseline, "clash.txt")->status, Folder::Status::Conflict);

        Folder::BaselineView broken = baseline;
        broken.rightRoot.clear();
        QVERIFY(!Folder::validateBaselineView(broken).isEmpty());
        const auto rejected = Folder::compare(pair.left, pair.right, Folder::Options(),
                                             nullptr, Folder::Progress(), nullptr, &broken);
        QVERIFY(!rejected.baselineApplied);
        for (const auto &entry : rejected.entries) {
            QVERIFY(entry.status != Folder::Status::BothChanged);
            QVERIFY(entry.status != Folder::Status::Conflict);
        }
        // 基线只**细化**结论，不推翻结论：同一对文件在坏基线下仍是「不同」，
        // 不该退化成一团「未知」。
        QCOMPARE(findEntry(rejected, "clash.txt")->status, Folder::Status::Different);
    }

    // DIR-011 第 5 条：父子一致。父目录的结论**就是**子条目汇总结论本身，
    // 不是「两套口径今天碰巧相同」。所以这里把引擎产出的子条目喂回同一个
    // 汇总函数，再和引擎写在父目录上的那一格逐字比对。
    void parentConclusionIsTheSharedAggregateOfItsChildren()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/mixed/same.txt", "same"));
        QVERIFY(writeFile(pair.right + "/mixed/same.txt", "same"));
        QVERIFY(writeFile(pair.left + "/mixed/only-left.txt", "x"));
        QVERIFY(writeFile(pair.left + "/mixed/nested/inner.txt", "same"));
        QVERIFY(writeFile(pair.right + "/mixed/nested/inner.txt", "same"));
        QVERIFY(writeFile(pair.left + "/identical/a.txt", "same"));
        QVERIFY(writeFile(pair.right + "/identical/a.txt", "same"));
        QVERIFY(QDir().mkpath(pair.left + "/void"));
        QVERIFY(QDir().mkpath(pair.right + "/void"));

        const auto result = Folder::compare(pair.left, pair.right);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(result.complete);
        QCOMPARE(findEntry(result, "mixed")->status, Folder::Status::Different);
        QCOMPARE(findEntry(result, "mixed/nested")->status, Folder::Status::Same);
        QCOMPARE(findEntry(result, "identical")->status, Folder::Status::Same);
        QCOMPARE(findEntry(result, "void")->status, Folder::Status::Same);

        for (const char *name : {"mixed", "mixed/nested", "identical", "void"}) {
            const QString parentPath = QString::fromLatin1(name);
            const auto *parentEntry = findEntry(result, parentPath);
            QVERIFY2(parentEntry, name);
            QVector<Folder::ChildStatus> children;
            for (const auto &entry : result.entries) {
                if (entry.relativePath.startsWith(parentPath + QLatin1Char('/')))
                    children.append({entry.status, entry.inComparison()});
            }
            const auto aggregate = Folder::aggregateChildren(children);
            QCOMPARE(aggregate.status, parentEntry->status);
            QCOMPARE(aggregate.hasIncludedDescendants, parentEntry->hasIncludedDescendants);
            // 汇总不许依赖遍历顺序：把子条目反过来喂一遍必须还是同一个结论。
            // （真值表里「错误压过不同」「未知不压过不同」这两条最容易写成
            //   依赖顺序的短路，而目录的枚举顺序来自文件系统，不归我们决定。）
            std::reverse(children.begin(), children.end());
            QCOMPARE(Folder::aggregateChildren(children).status, parentEntry->status);
        }
    }

    // DIR-011 第 4 条：颜色之外还要有图标与文字，且「为什么是这个状态」够得着。
    // 灰度打印与色觉障碍下颜色是第一个失效的信息，所以三者缺一不可。
    void everyStatusRowCarriesAnIconAndExplainsItself()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/same.txt", "same"));
        QVERIFY(writeFile(pair.right + "/same.txt", "same"));
        QVERIFY(writeFile(pair.left + "/diff.txt", "aaa"));
        QVERIFY(writeFile(pair.right + "/diff.txt", "bbb"));
        QVERIFY(writeFile(pair.left + "/only-left.txt", "x"));

        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QVERIFY(session.open());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        auto *view = session.view();
        QVERIFY(view);
        auto *model = view->leftTree()->model();
        QVERIFY(model);

        const int stateColumn = 3; // FolderTreeModel::State
        for (const char *name : {"same.txt", "diff.txt", "only-left.txt"}) {
            const QString fileName = QString::fromLatin1(name);
            const QModelIndex name0 = indexNamed(model, fileName);
            QVERIFY2(name0.isValid(), name);
            const QModelIndex state = name0.sibling(name0.row(), stateColumn);
            QVERIFY(state.isValid());
            QVERIFY(!state.data(Qt::DisplayRole).toString().isEmpty());
            const QVariant decoration = state.data(Qt::DecorationRole);
            QVERIFY(decoration.isValid());
            QVERIFY(!decoration.value<QIcon>().isNull());

            std::unique_ptr<QMenu> menu(view->createStatusMenu(name0));
            QVERIFY(menu);
            auto *why = menu->findChild<QAction *>(QStringLiteral("folderWhyStatusAction"));
            QVERIFY(why);
            QVERIFY(why->isEnabled());
            const QStringList sections = {QStringLiteral("准则"), QStringLiteral("覆盖策略"),
                                         QStringLiteral("最终结论")};
            const QString reason = view->statusExplanation(name0);
            for (const QString &section : sections)
                QVERIFY2(reason.contains(section), qPrintable(reason));
            const auto *entry = findEntry(session.result(), fileName);
            QVERIFY(entry);
            QVERIFY2(reason.contains(Folder::statusLabel(entry->status)), qPrintable(reason));

            // 点开之后不许卡住：弹窗必须是**非模态**的，否则它会在 exec() 里
            // 开一个嵌套事件循环，离屏测试与自动化会一起僵在这一行。
            why->trigger();
            auto *box = view->findChild<QMessageBox *>(QStringLiteral("folderStatusReasonBox"));
            QVERIFY(box);
            QVERIFY(!box->isModal());
            QVERIFY(box->text().contains(Folder::statusLabel(entry->status)));
            box->close();
            box->deleteLater();
            // 逐个删干净再进下一轮：`findChild` 返回第一个同名对象，
            // 留着上一轮的弹窗会让下一轮的断言读到上一轮的文本。
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }

        // 图标键必须九档各不相同：全部指向同一个文件等于没有图标。
        QSet<QString> keys;
        for (const auto &row : Folder::mainStatusTable())
            keys.insert(Folder::statusIconKey(row.value));
        QCOMPARE(keys.size(), Folder::mainStatusTable().size());
        session.close();
    }

    // DIR-011 第 1 条的消费点守卫。状态整数要经「模型角色 → 下拉数据 → 筛选」三跳，
    // 任一跳拿到越界值都不许把列表清空——「一整屏空白」是最难归因的一种失败，
    // 用户会以为自己选中的是某个状态。
    void statusFilterRejectsIntegersOutsideTheMainStatusTable()
    {
        Pair pair;
        QVERIFY(writeFile(pair.left + "/same.txt", "same"));
        QVERIFY(writeFile(pair.right + "/same.txt", "same"));
        QVERIFY(writeFile(pair.left + "/diff.txt", "aaa"));
        QVERIFY(writeFile(pair.right + "/diff.txt", "bbb"));

        FolderCompareSession session(pair.left, pair.right);
        std::unique_ptr<QWidget> widget(session.createWidget());
        QSignalSpy finished(&session, &FolderCompareSession::scanFinished);
        QVERIFY(session.open());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        auto *view = session.view();
        QVERIFY(view);
        auto *model = view->leftTree()->model();
        auto *filter = view->findChild<QComboBox *>(QStringLiteral("folderStatusFilter"));
        QVERIFY(filter);
        const int all = model->rowCount();
        QCOMPARE(all, 2);

        // 下拉里的每一档都来自主状态表：界面不另写一份清单，
        // 否则新增一档状态时界面会静默落后一格。
        for (const auto &row : Folder::mainStatusTable())
            QVERIFY2(filter->findData(int(row.value)) >= 0, row.identifier);
        QVERIFY(filter->findData(-1) >= 0);
        QVERIFY(filter->findData(-2) >= 0);

        view->setStatusFilter(9999);
        QCOMPARE(model->rowCount(), all);
        QCOMPARE(filter->currentData().toInt(), -1);
        view->setStatusFilter(-7);
        QCOMPARE(model->rowCount(), all);
        QCOMPARE(filter->currentData().toInt(), -1);

        view->setStatusFilter(int(Folder::Status::Different));
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data().toString(), QStringLiteral("diff.txt"));
        // 两个哨兵值是本类自己的语义，不能被「越界就回落到全部」这条规则顺手吃掉。
        view->setStatusFilter(-2);
        QCOMPARE(model->rowCount(), 1);
        view->setStatusFilter(-1);
        QCOMPARE(model->rowCount(), all);
        session.close();
    }
};

QTEST_MAIN(FolderTests)
#include "tst_folder.moc"
