#include "tst_filesystem.h"

#include "fakefilesystem.h"
#include "filesystem.h"
#include "pathutils.h"

#include <QDir>
#include <QTemporaryDir>

#include <cerrno>
#include <memory>

using namespace LqCompare::Files;

namespace QTest {

/// 让失败信息里直接显示错误标识（"busy"、"permission-denied"），
/// 而不是「Compared values are not the same」。
/// 断言失败时看得懂原因，是这些护栏能否被信任的前提。
template <>
char *toString(const FileSystemError &error)
{
    return qstrdup(errorIdentifier(error));
}

/// ErrorCode 的显示：分类 + 原始系统码。
///
/// 只显示分类是不够的：PLAT-008 要区分的正是「同一分类下的不同原始码」
/// （EPERM 与 EACCES 都归 PermissionDenied，但排查方向完全不同）。
/// 断言失败时看不到原始码，就等于看不到区别。
template <>
char *toString(const ErrorCode &error)
{
    const QString text = errorReport(error);
    return qstrdup(qPrintable(text.isEmpty() ? QStringLiteral("none") : text));
}

} // namespace QTest

namespace {

const PathUtils::Style kPosix = PathUtils::Style::posix();
const PathUtils::Style kWindows = PathUtils::Style::windows();

/// 构造一段必然超过长路径阈值的 Windows 路径。
QString makeLongWindowsPath()
{
    QString path = QStringLiteral("C:\\");
    for (int i = 0; i < 40; ++i)
        path += QStringLiteral("abcdefgh\\");
    return path;
}

} // namespace

// -----------------------------------------------------------------------------
// 路径规则
// -----------------------------------------------------------------------------

void TstFileSystem::normalizeCollapsesRedundantSeparators()
{
    QCOMPARE(PathUtils::normalize(QStringLiteral("/a//b"), kPosix), QStringLiteral("/a/b"));
    QCOMPARE(PathUtils::normalize(QStringLiteral("/a/b/"), kPosix), QStringLiteral("/a/b"));
    QCOMPARE(PathUtils::normalize(QStringLiteral("/a/b/./"), kPosix), QStringLiteral("/a/b"));
    // 根目录本身的结尾分隔符必须保留，否则 "/" 会变成空串。
    QCOMPARE(PathUtils::normalize(QStringLiteral("/"), kPosix), QStringLiteral("/"));
    QCOMPARE(PathUtils::normalize(QStringLiteral("///"), kPosix), QStringLiteral("/"));
}

void TstFileSystem::normalizeResolvesDotAndDotDot()
{
    QCOMPARE(PathUtils::normalize(QStringLiteral("/a/b/../c"), kPosix), QStringLiteral("/a/c"));
    QCOMPARE(PathUtils::normalize(QStringLiteral("/a/b/c/../../d"), kPosix),
             QStringLiteral("/a/d"));
}

void TstFileSystem::normalizeKeepsRelativeDotDot()
{
    // "../a" 退无可退时必须保留 ".."。若在这里把它丢掉变成 "a"，
    // 指向的位置就完全不同了——这是路径处理里最容易被静默做错的一处。
    QCOMPARE(PathUtils::normalize(QStringLiteral("../a"), kPosix), QStringLiteral("../a"));
    QCOMPARE(PathUtils::normalize(QStringLiteral("../../a"), kPosix), QStringLiteral("../../a"));
    // 相对路径的 "." 是「当前目录」，保留。
    QCOMPARE(PathUtils::normalize(QStringLiteral("."), kPosix), QStringLiteral("."));
}

void TstFileSystem::normalizeDoesNotEscapeRoot()
{
    // 绝对路径上的 ".." 越界即被丢弃：根之上没有东西。
    QCOMPARE(PathUtils::normalize(QStringLiteral("/../a"), kPosix), QStringLiteral("/a"));
    QCOMPARE(PathUtils::normalize(QStringLiteral("/.."), kPosix), QStringLiteral("/"));
}

void TstFileSystem::normalizeHandlesWindowsSeparators()
{
    // 正反斜杠混写要统一，且连续分隔符要合并。
    QCOMPARE(PathUtils::normalize(QStringLiteral("C:/a\\b//c"), kWindows),
             QStringLiteral("C:\\a\\b\\c"));
    QCOMPARE(PathUtils::normalize(QStringLiteral("C:\\a\\..\\b"), kWindows),
             QStringLiteral("C:\\b"));
}

void TstFileSystem::normalizeHandlesWindowsDriveRoot()
{
    QCOMPARE(PathUtils::normalize(QStringLiteral("C:\\"), kWindows), QStringLiteral("C:\\"));
    QCOMPARE(PathUtils::normalize(QStringLiteral("C:\\..\\a"), kWindows), QStringLiteral("C:\\a"));
    // "C:a" 是「C 盘当前目录下的 a」，属相对路径，不能被补成分隔符。
    QCOMPARE(PathUtils::normalize(QStringLiteral("C:a"), kWindows), QStringLiteral("C:a"));
}

void TstFileSystem::normalizeHandlesUncPath()
{
    QCOMPARE(PathUtils::normalize(QStringLiteral("\\\\server\\share\\a\\..\\b"), kWindows),
             QStringLiteral("\\\\server\\share\\b"));
    // UNC 的 server 与 share 是不可消解的头部：share 之上的 ".." 不能把
    // share 退掉，否则 \\server\share 会被吃成 \\server。
    QCOMPARE(PathUtils::normalize(QStringLiteral("\\\\server\\share\\..\\..\\a"), kWindows),
             QStringLiteral("\\\\server\\share\\a"));
}

void TstFileSystem::normalizeUncRootKeepsTrailingSeparator()
{
    QCOMPARE(PathUtils::normalize(QStringLiteral("\\\\server\\share"), kWindows),
             QStringLiteral("\\\\server\\share\\"));
}

void TstFileSystem::unifySeparatorsDoesNotCollapse()
{
    // 刻意不合并连续分隔符：UNC 开头就是两个分隔符，
    // 合并会把 "\\server\share" 破坏成 "\server\share"。
    QCOMPARE(PathUtils::unifySeparators(QStringLiteral("C:/a\\b//c"), kWindows),
             QStringLiteral("C:\\a\\b\\\\c"));
    // POSIX 上的 '\' 是合法文件名字符，绝不能替换。
    QCOMPARE(PathUtils::unifySeparators(QStringLiteral("a\\b"), kPosix), QStringLiteral("a\\b"));
}

void TstFileSystem::splitDropsEmptySegments()
{
    QCOMPARE(PathUtils::split(QStringLiteral("/a//b/"), kPosix),
             QStringList({QStringLiteral("a"), QStringLiteral("b")}));
    QCOMPARE(PathUtils::split(QStringLiteral("\\\\server\\share"), kWindows),
             QStringList({QStringLiteral("server"), QStringLiteral("share")}));
    QCOMPARE(PathUtils::split(QStringLiteral("/"), kPosix), QStringList());
}

void TstFileSystem::joinAvoidsDoubleSeparator()
{
    QCOMPARE(PathUtils::join(QStringLiteral("/a"), QStringLiteral("b"), kPosix),
             QStringLiteral("/a/b"));
    QCOMPARE(PathUtils::join(QStringLiteral("/a/"), QStringLiteral("b"), kPosix),
             QStringLiteral("/a/b"));
    // 根目录 + 名字不应该出现双分隔符
    QCOMPARE(PathUtils::join(QStringLiteral("/"), QStringLiteral("b"), kPosix),
             QStringLiteral("/b"));
}

void TstFileSystem::joinKeepsDriveRelativePrefix()
{
    // "C:" + "a" 必须是 "C:a"（C 盘当前目录下的 a）。
    // 若写成 "C:\a" 就指向了另一个位置。
    QCOMPARE(PathUtils::join(QStringLiteral("C:"), QStringLiteral("a"), kWindows),
             QStringLiteral("C:a"));
}

void TstFileSystem::fileNameAndParentPath()
{
    QCOMPARE(PathUtils::fileName(QStringLiteral("/a/b.txt"), kPosix), QStringLiteral("b.txt"));
    QCOMPARE(PathUtils::fileName(QStringLiteral("/"), kPosix), QString());
    QCOMPARE(PathUtils::parentPath(QStringLiteral("/a/b"), kPosix), QStringLiteral("/a"));
    QCOMPARE(PathUtils::parentPath(QStringLiteral("/a"), kPosix), QStringLiteral("/"));
    // 相对路径只有一段时，父目录是「当前目录」。
    QCOMPARE(PathUtils::parentPath(QStringLiteral("a"), kPosix), QStringLiteral("."));
}

void TstFileSystem::parentPathStopsAtRoot()
{
    // 根没有父目录，返回自身——上层向上递归（如删除、创建）靠这个停下来。
    QCOMPARE(PathUtils::parentPath(QStringLiteral("/"), kPosix), QStringLiteral("/"));
    QCOMPARE(PathUtils::parentPath(QStringLiteral("C:\\"), kWindows), QStringLiteral("C:\\"));
    QCOMPARE(PathUtils::parentPath(QStringLiteral("\\\\server\\share\\"), kWindows),
             QStringLiteral("\\\\server\\share\\"));
    // 盘符根之下的第一层，父目录是盘符根而不是 "C:"
    QCOMPARE(PathUtils::parentPath(QStringLiteral("C:\\a"), kWindows), QStringLiteral("C:\\"));
}

void TstFileSystem::isAbsoluteDistinguishesDriveRelative()
{
    QVERIFY(PathUtils::isAbsolute(QStringLiteral("/a"), kPosix));
    QVERIFY(!PathUtils::isAbsolute(QStringLiteral("a"), kPosix));
    QVERIFY(!PathUtils::isAbsolute(QStringLiteral("./a"), kPosix));

    QVERIFY(PathUtils::isAbsolute(QStringLiteral("C:\\a"), kWindows));
    QVERIFY(PathUtils::isAbsolute(QStringLiteral("\\\\server\\share\\a"), kWindows));
    // 关键区别："C:a" 不是绝对路径。
    QVERIFY(!PathUtils::isAbsolute(QStringLiteral("C:a"), kWindows));
}

void TstFileSystem::isUncAndUncPrefix()
{
    QVERIFY(PathUtils::isUnc(QStringLiteral("\\\\server\\share\\a"), kWindows));
    QVERIFY(!PathUtils::isUnc(QStringLiteral("C:\\a"), kWindows));
    // POSIX 风格下恒为 false：只有 Windows 有 UNC 概念。
    QVERIFY(!PathUtils::isUnc(QStringLiteral("//server/share/a"), kPosix));

    QCOMPARE(PathUtils::uncPrefix(QStringLiteral("\\\\server\\share\\a\\b"), kWindows),
             QStringLiteral("\\\\server\\share"));
    QCOMPARE(PathUtils::uncPrefix(QStringLiteral("C:\\a"), kWindows), QString());
}

void TstFileSystem::drivePrefixOnlyForWindowsStyle()
{
    QCOMPARE(PathUtils::drivePrefix(QStringLiteral("C:\\a"), kWindows), QStringLiteral("C:"));
    QCOMPARE(PathUtils::drivePrefix(QStringLiteral("C:a"), kWindows), QStringLiteral("C:"));
    QCOMPARE(PathUtils::drivePrefix(QStringLiteral("/a"), kWindows), QString());
    // POSIX 风格下 "C:" 只是一个普通文件名，不是盘符。
    QCOMPARE(PathUtils::drivePrefix(QStringLiteral("C:\\a"), kPosix), QString());
}

void TstFileSystem::isRootPathRecognisesAllRootForms()
{
    QVERIFY(PathUtils::isRootPath(QStringLiteral("/"), kPosix));
    QVERIFY(!PathUtils::isRootPath(QStringLiteral("/a"), kPosix));

    QVERIFY(PathUtils::isRootPath(QStringLiteral("C:\\"), kWindows));
    // "C:" 不是根，而是「C 盘的当前目录」——一个驱动器相对路径。
    // 这一点很容易写错（最初这条断言就是写成 QVERIFY 的，被用例纠正过来了）：
    // 根必须是 "C:\"。把 "C:" 当成根会让「向上递归到根」的循环提前停下，
    // 于是删除/创建操作作用在一个含义完全不同的位置上。
    QVERIFY(!PathUtils::isRootPath(QStringLiteral("C:"), kWindows));
    QVERIFY(!PathUtils::isRootPath(QStringLiteral("C:\\a"), kWindows));

    QVERIFY(PathUtils::isRootPath(QStringLiteral("\\\\server\\share\\"), kWindows));
    // share 之下的路径不是根
    QVERIFY(!PathUtils::isRootPath(QStringLiteral("\\\\server\\share\\a"), kWindows));
}

void TstFileSystem::toExtendedPathAddsPrefixOnlyForLongAbsoluteWindowsPaths()
{
    const QString shortPath = QStringLiteral("C:\\a\\b.txt");
    QCOMPARE(PathUtils::toExtendedPath(shortPath, kWindows), shortPath);

    const QString longPath = makeLongWindowsPath();
    QVERIFY(longPath.length() >= PathUtils::extendedPathThreshold());
    QVERIFY(PathUtils::toExtendedPath(longPath, kWindows).startsWith(QStringLiteral("\\\\?\\")));

    // 相对路径即使很长也不加前缀：\\?\ 语法要求绝对路径。
    QString longRelative;
    for (int i = 0; i < 40; ++i)
        longRelative += QStringLiteral("abcdefgh\\");
    QVERIFY(longRelative.length() >= PathUtils::extendedPathThreshold());
    QCOMPARE(PathUtils::toExtendedPath(longRelative, kWindows), longRelative);

    // 非 Windows 平台没有这个概念，必须原样返回，
    // 否则调用方就要到处写平台判断。
    QCOMPARE(PathUtils::toExtendedPath(longPath, kPosix), longPath);
}

void TstFileSystem::toExtendedPathHandlesUncForm()
{
    // UNC 必须写成 \\?\UNC\server\share\...，
    // 直接拼成 \\?\\\server\share 是非法路径。
    QString uncPath = QStringLiteral("\\\\server\\share\\");
    for (int i = 0; i < 40; ++i)
        uncPath += QStringLiteral("abcdefgh\\");

    const QString extended = PathUtils::toExtendedPath(uncPath, kWindows);
    QVERIFY(extended.startsWith(QStringLiteral("\\\\?\\UNC\\")));
    QVERIFY(!extended.contains(QStringLiteral("\\\\?\\\\\\")));
    QCOMPARE(extended.mid(8), uncPath.mid(2));
}

void TstFileSystem::toExtendedPathIsIdempotent()
{
    const QString longPath = makeLongWindowsPath();
    const QString once = PathUtils::toExtendedPath(longPath, kWindows);
    const QString twice = PathUtils::toExtendedPath(once, kWindows);
    // 重复加前缀会得到非法路径，因此第二次必须是恒等变换。
    QCOMPARE(twice, once);
}

void TstFileSystem::comparePathsHonoursCaseSensitivity()
{
    // 同一个程序在 Windows 与 Linux 上对「a.txt 与 A.TXT 是否同一个文件」
    // 的答案必须不同，否则文件夹比对会给出错误结论。
    QVERIFY(PathUtils::comparePaths(QStringLiteral("/a/B.txt"), QStringLiteral("/a/b.txt"),
                                    kPosix, Qt::CaseSensitive)
            == false);
    QVERIFY(PathUtils::comparePaths(QStringLiteral("/a/B.txt"), QStringLiteral("/a/b.txt"),
                                    kPosix, Qt::CaseInsensitive));
    // 先规范化再比较，因此冗余分隔符不影响判定。
    QVERIFY(PathUtils::comparePaths(QStringLiteral("/a//b/"), QStringLiteral("/a/b"),
                                    kPosix, Qt::CaseSensitive));
}

void TstFileSystem::findInvalidFileNameCharacterReportsPosition()
{
    // 返回位置而不是 bool，是为了界面能把光标定位到出错处。
    QCOMPARE(PathUtils::findInvalidFileNameCharacter(QStringLiteral("a<b.txt")), 1);
    QCOMPARE(PathUtils::findInvalidFileNameCharacter(QStringLiteral("a/b")), 1);
    QCOMPARE(PathUtils::findInvalidFileNameCharacter(QStringLiteral("a\\b")), 1);
    QCOMPARE(PathUtils::findInvalidFileNameCharacter(QStringLiteral("a:b")), 1);
    QCOMPARE(PathUtils::findInvalidFileNameCharacter(QStringLiteral("tab\there")), 3);
    QCOMPARE(PathUtils::findInvalidFileNameCharacter(QStringLiteral("正常名称.txt")), -1);
    QCOMPARE(PathUtils::findInvalidFileNameCharacter(QStringLiteral("name with space")), -1);

    // "." 与 ".." 长度合法但语义非法，从第 0 个字符起就不合法。
    QCOMPARE(PathUtils::findInvalidFileNameCharacter(QStringLiteral(".")), 0);
    QCOMPARE(PathUtils::findInvalidFileNameCharacter(QStringLiteral("..")), 0);
    QCOMPARE(PathUtils::findInvalidFileNameCharacter(QString()), 0);
}

void TstFileSystem::isValidFileNameRejectsReservedNames()
{
    // 保留设备名不含非法字符，字符检查发现不了，只能整名判定。
    QVERIFY(PathUtils::isReservedName(QStringLiteral("con")));
    QVERIFY(PathUtils::isReservedName(QStringLiteral("CON")));
    QVERIFY(PathUtils::isReservedName(QStringLiteral("com1")));
    // Windows 上 "con.txt" 同样非法——保留名匹配忽略扩展名。
    QVERIFY(PathUtils::isReservedName(QStringLiteral("con.txt")));
    // "console" 不是保留名，不能被前缀匹配误伤。
    QVERIFY(!PathUtils::isReservedName(QStringLiteral("console")));
    QVERIFY(!PathUtils::isReservedName(QStringLiteral("com10")));

    QVERIFY(!PathUtils::isValidFileName(QStringLiteral("con")));
    QVERIFY(!PathUtils::isValidFileName(QStringLiteral("a:b")));
    QVERIFY(PathUtils::isValidFileName(QStringLiteral("report.txt")));
    QVERIFY(PathUtils::isValidFileName(QStringLiteral("报告-2026.txt")));
}

// -----------------------------------------------------------------------------
// 时间戳
// -----------------------------------------------------------------------------

void TstFileSystem::fileTimeRoundTripsThroughUtc()
{
    const QDateTime utc(QDate(2026, 1, 2), QTime(3, 4, 5), Qt::UTC);
    const FileTime time = FileTime::fromDateTime(utc);

    QVERIFY(time.isValid());
    QCOMPARE(time.toUtcDateTime().toSecsSinceEpoch(), utc.toSecsSinceEpoch());
    QCOMPARE(time.toUtcDateTime().timeSpec(), Qt::UTC);
}

void TstFileSystem::fileTimeTreatsLocalAndUtcInputEqually()
{
    // 同一时刻用 UTC 表示和用本地时间表示，必须得到同一个内部值。
    // 若这里出错，比对结果会随时区变化——而用户完全看不出原因。
    const QDateTime utc(QDate(2026, 1, 2), QTime(3, 4, 5), Qt::UTC);
    const QDateTime local = utc.toLocalTime();

    QCOMPARE(FileTime::fromDateTime(utc), FileTime::fromDateTime(local));

    // 显示层转本地时间应与原始本地时间一致。
    QCOMPARE(FileTime::fromDateTime(utc).toLocalDateTime().toSecsSinceEpoch(),
             local.toSecsSinceEpoch());
}

void TstFileSystem::fileTimeInvalidIsNotEpoch()
{
    // 「拿不到创建时间」必须是无效值，不能是 0。
    // 0 表示 1970-01-01，会被当成真实时间参与比较，
    // 从而让「创建时间不同」这种判定凭空成立。
    const FileTime invalid;
    QVERIFY(!invalid.isValid());
    QVERIFY(!invalid.toUtcDateTime().isValid());

    const FileTime epoch = FileTime::fromNanosecondsSinceEpoch(0);
    QVERIFY(epoch.isValid()); // 纪元时刻是**有效**时间
    QVERIFY(invalid != epoch);
}

void TstFileSystem::fileTimeComparisonOperators()
{
    const FileTime earlier = FileTime::fromSecondsSinceEpoch(1000);
    const FileTime later = FileTime::fromSecondsSinceEpoch(2000);

    QVERIFY(earlier < later);
    QVERIFY(later > earlier);
    QVERIFY(earlier <= earlier);
    QVERIFY(earlier >= earlier);
    QVERIFY(earlier == FileTime::fromSecondsSinceEpoch(1000));
    QVERIFY(earlier != later);
}

void TstFileSystem::fileTimeKeepsNanosecondPrecision()
{
    // 纳秒精度是内部约定的要求：文件系统能给的精度不能在这里被削掉。
    const FileTime time = FileTime::fromUnixTime(1600000000, 123456789);
    QCOMPARE(time.nanosecondsSinceEpoch(), 1600000000LL * 1000000000LL + 123456789LL);
    QCOMPARE(time.millisecondsSinceEpoch(), 1600000000123LL);
}

// -----------------------------------------------------------------------------
// 错误分类
// -----------------------------------------------------------------------------

void TstFileSystem::classifyPosixErrors()
{
    QCOMPARE(classifySystemError(0), FileSystemError::None);
    QCOMPARE(classifySystemError(ENOENT), FileSystemError::NotFound);
    QCOMPARE(classifySystemError(EACCES), FileSystemError::PermissionDenied);
    QCOMPARE(classifySystemError(EPERM), FileSystemError::PermissionDenied);
    QCOMPARE(classifySystemError(EBUSY), FileSystemError::Busy);
    QCOMPARE(classifySystemError(EROFS), FileSystemError::ReadOnlyFileSystem);
    QCOMPARE(classifySystemError(EEXIST), FileSystemError::AlreadyExists);
    QCOMPARE(classifySystemError(ENAMETOOLONG), FileSystemError::InvalidName);
    QCOMPARE(classifySystemError(ENOTDIR), FileSystemError::NotDirectory);
    QCOMPARE(classifySystemError(ENOSPC), FileSystemError::NoSpace);
    // 无法归类的必须是 Unknown，而不是硬塞进某个「看起来差不多」的分类——
    // 分类错了，界面就会给出错误的处置建议。
    QCOMPARE(classifySystemError(999999), FileSystemError::Unknown);
}

void TstFileSystem::classifyWindowsAccessDeniedIsNotBusy()
{
    // 这是整个错误映射里最关键的一条：
    // Windows 上「文件被占用」返回 ERROR_SHARING_VIOLATION(32)，
    // 而标准库会把它映射成 EACCES，看起来像「权限不足」。
    // 如果按 errno 归类，用户会拿到「请以管理员身份运行」这种完全无效的建议，
    // 而真正该做的是关掉占用文件的程序。
    QCOMPARE(classifyWindowsErrorCode(Win32Error::SharingViolation), FileSystemError::Busy);
    QCOMPARE(classifyWindowsErrorCode(Win32Error::AccessDenied),
             FileSystemError::PermissionDenied);
    QVERIFY(classifyWindowsErrorCode(Win32Error::SharingViolation)
            != classifyWindowsErrorCode(Win32Error::AccessDenied));
}

void TstFileSystem::classifyWindowsErrorCodes()
{
    QCOMPARE(classifyWindowsErrorCode(0), FileSystemError::None);
    QCOMPARE(classifyWindowsErrorCode(Win32Error::FileNotFound), FileSystemError::NotFound);
    QCOMPARE(classifyWindowsErrorCode(Win32Error::PathNotFound), FileSystemError::NotFound);
    QCOMPARE(classifyWindowsErrorCode(Win32Error::WriteProtect), FileSystemError::ReadOnly);
    QCOMPARE(classifyWindowsErrorCode(Win32Error::LockViolation), FileSystemError::Busy);
    QCOMPARE(classifyWindowsErrorCode(Win32Error::FileExists), FileSystemError::AlreadyExists);
    QCOMPARE(classifyWindowsErrorCode(Win32Error::FilenameExceededRange),
             FileSystemError::InvalidName);
    QCOMPARE(classifyWindowsErrorCode(Win32Error::DiskFull), FileSystemError::NoSpace);
    QCOMPARE(classifyWindowsErrorCode(Win32Error::NotSupported), FileSystemError::NotSupported);

    // 「目录非空」不能归为 Busy：重试不会改变结果，
    // 界面若把它当成可重试的占用问题，用户会一直重试到放弃。
    QCOMPARE(classifyWindowsErrorCode(Win32Error::DirectoryNotEmpty), FileSystemError::Unknown);
}

void TstFileSystem::errorIdentifierIsStable()
{
    // 标识用于日志与测试断言，必须稳定且不含中文、不含空格，
    // 否则日志检索与断言会随文案调整而失效。
    QCOMPARE(QString::fromLatin1(errorIdentifier(FileSystemError::None)), QStringLiteral("none"));
    QCOMPARE(QString::fromLatin1(errorIdentifier(FileSystemError::PermissionDenied)),
             QStringLiteral("permission-denied"));
    QCOMPARE(QString::fromLatin1(errorIdentifier(FileSystemError::ReadOnlyFileSystem)),
             QStringLiteral("read-only-filesystem"));

    // 每个取值都必须有非空标识，不能被漏掉。
    const FileSystemError all[] = {
        FileSystemError::None,        FileSystemError::NotFound,
        FileSystemError::PermissionDenied, FileSystemError::ReadOnly,
        FileSystemError::Busy,        FileSystemError::ReadOnlyFileSystem,
        FileSystemError::NoSpace,     FileSystemError::InvalidName,
        FileSystemError::NotDirectory, FileSystemError::AlreadyExists,
        FileSystemError::NotSupported, FileSystemError::Unknown,
    };
    for (FileSystemError error : all) {
        const QString identifier = QString::fromLatin1(errorIdentifier(error));
        QVERIFY2(!identifier.isEmpty(), qPrintable(identifier));
        QVERIFY(!identifier.contains(QLatin1Char(' ')));
    }
}

void TstFileSystem::adviceIsDistinctForActionableErrors()
{
    // PLAT-008 的硬要求：四类可处置错误必须给出**不同**的建议。
    // 统一写成「操作失败，请重试」等于没给建议。
    const FileSystemError actionable[] = {
        FileSystemError::PermissionDenied,
        FileSystemError::ReadOnly,
        FileSystemError::Busy,
        FileSystemError::ReadOnlyFileSystem,
    };

    QStringList advices;
    for (FileSystemError error : actionable) {
        const QString advice = errorAdvice(error);
        QVERIFY2(!advice.isEmpty(), errorIdentifier(error));
        QVERIFY2(!advices.contains(advice),
                 qPrintable(QStringLiteral("两类错误给出了相同建议：%1").arg(advice)));
        advices.append(advice);
    }
    QCOMPARE(advices.size(), 4);

    // 说明文字也要各不相同，否则界面上的错误标题就失去区分度。
    QVERIFY(errorMessage(FileSystemError::Busy) != errorMessage(FileSystemError::PermissionDenied));
    QVERIFY(errorMessage(FileSystemError::ReadOnlyFileSystem,
                         QStringLiteral("/a"))
            != errorMessage(FileSystemError::PermissionDenied, QStringLiteral("/a")));
}

void TstFileSystem::onlyBusyIsRetryable()
{
    // 只有「被占用」是重试可能自行消失的。
    // 界面不应该给「权限不足」也配一个「重试」按钮——那注定无效。
    QVERIFY(isRetryable(FileSystemError::Busy));
    QVERIFY(!isRetryable(FileSystemError::PermissionDenied));
    QVERIFY(!isRetryable(FileSystemError::ReadOnly));
    QVERIFY(!isRetryable(FileSystemError::ReadOnlyFileSystem));
    QVERIFY(!isRetryable(FileSystemError::NoSpace));
    QVERIFY(!isRetryable(FileSystemError::NotFound));
}

// -----------------------------------------------------------------------------
// 异常路径（内存文件系统注入故障）
// -----------------------------------------------------------------------------

void TstFileSystem::fakeStatReportsInjectedError()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/data/report.txt"), 128);

    ErrorCode error;
    const FileInfo ok = fileSystem.stat(QStringLiteral("/data/report.txt"), &error);
    QVERIFY(ok.exists);
    QCOMPARE(ok.size, quint64(128));
    QCOMPARE(error, FileSystemError::None);

    // 注入「被占用」——这类错误在真实文件系统上无法稳定复现。
    fileSystem.fail(Test::FakeFileSystem::Operation::Stat, QStringLiteral("/data/report.txt"),
                    FileSystemError::Busy);
    const FileInfo busy = fileSystem.stat(QStringLiteral("/data/report.txt"), &error);
    QVERIFY(!busy.exists);
    QCOMPARE(error, FileSystemError::Busy);
}

void TstFileSystem::fakeStatOnMissingPathIsNotFound()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/a/b.txt"));

    ErrorCode error;
    const FileInfo missing = fileSystem.stat(QStringLiteral("/a/does-not-exist.txt"), &error);
    QVERIFY(!missing.exists);
    QCOMPARE(error, FileSystemError::NotFound);
}

void TstFileSystem::fakeEnumerateInjectedBusyStopsEnumeration()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/locked/a.txt"));
    fileSystem.addFile(QStringLiteral("/locked/b.txt"));

    ErrorCode error;
    QCOMPARE(fileSystem.enumerateDirectory(QStringLiteral("/locked"), &error).size(), 2);
    QCOMPARE(error, FileSystemError::None);

    fileSystem.fail(Test::FakeFileSystem::Operation::Enumerate, QStringLiteral("/locked"),
                    FileSystemError::Busy);
    const QVector<FileInfo> entries = fileSystem.enumerateDirectory(QStringLiteral("/locked"), &error);
    QVERIFY(entries.isEmpty());
    // 必须是明确的错误，不能被当成「空目录」——在文件夹比对里
    // 「空目录」意味着「目标需要被清空」，后果很严重。
    QCOMPARE(error, FileSystemError::Busy);
}

void TstFileSystem::fakeEnumerateOnFileIsNotDirectory()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/a/b.txt"));

    ErrorCode error;
    const QVector<FileInfo> entries = fileSystem.enumerateDirectory(QStringLiteral("/a/b.txt"), &error);
    QVERIFY(entries.isEmpty());
    QCOMPARE(error, FileSystemError::NotDirectory);
}

void TstFileSystem::fakeExistsDistinguishesMissingFromDenied()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/secret/keys.txt"));

    // 「不存在」是确定的答案，不是失败。
    ErrorCode error;
    QVERIFY(!fileSystem.exists(QStringLiteral("/secret/nope.txt"), &error));
    QCOMPARE(error, FileSystemError::NotFound);

    // 而「没权限查」是失败，两者必须能区分开：
    // 前者说明「目标需要新建」，后者说明「读不到，先解决权限」。
    fileSystem.fail(Test::FakeFileSystem::Operation::Exists, QStringLiteral("/secret/keys.txt"),
                    FileSystemError::PermissionDenied);
    QVERIFY(!fileSystem.exists(QStringLiteral("/secret/keys.txt"), &error));
    QCOMPARE(error, FileSystemError::PermissionDenied);
}

void TstFileSystem::fakeSetTimesHonoursInjectedReadOnly()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/a/b.txt"));
    const FileTime original = FileTime::fromSecondsSinceEpoch(1000);
    fileSystem.setModifiedTime(QStringLiteral("/a/b.txt"), original);

    // 只读文件的时间戳改不了：应报 ReadOnly 而不是「失败」。
    fileSystem.setAttributesFor(QStringLiteral("/a/b.txt"), FileAttribute::ReadOnly);
    fileSystem.fail(Test::FakeFileSystem::Operation::SetTimes, QStringLiteral("/a/b.txt"),
                    FileSystemError::ReadOnly);

    ErrorCode error;
    const bool ok = fileSystem.setTimes(QStringLiteral("/a/b.txt"),
                                        FileTime::fromSecondsSinceEpoch(2000), FileTime(), &error);
    QVERIFY(!ok);
    QCOMPARE(error, FileSystemError::ReadOnly);

    // 失败后原值必须保持不变——不能出现「报错了但其实改了一半」。
    fileSystem.clearFailures();
    ErrorCode readError;
    QCOMPARE(fileSystem.stat(QStringLiteral("/a/b.txt"), &readError).lastModified, original);
}

void TstFileSystem::fakeSetTimesLeavesUnspecifiedTimestampAlone()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/a/b.txt"));

    const FileTime accessed = FileTime::fromSecondsSinceEpoch(500);
    fileSystem.setModifiedTime(QStringLiteral("/a/b.txt"), FileTime::fromSecondsSinceEpoch(1000));

    ErrorCode error;
    // 只传「修改时间」，访问时间传无效值表示「不要动它」。
    QVERIFY(fileSystem.setTimes(QStringLiteral("/a/b.txt"), FileTime::fromSecondsSinceEpoch(2000),
                               FileTime(), &error));
    QCOMPARE(error, FileSystemError::None);

    const FileInfo after = fileSystem.stat(QStringLiteral("/a/b.txt"), &error);
    QCOMPARE(after.lastModified, FileTime::fromSecondsSinceEpoch(2000));
    // 访问时间没有被顺手改成当前时间。
    QVERIFY(after.lastAccessed != accessed); // 本来就是无效值，仍应保持无效
    QVERIFY(!after.lastAccessed.isValid());
}

void TstFileSystem::fakeChecksCaseInsensitivityUnderWindowsSemantics()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.useWindowsSemantics();
    fileSystem.addFile(QStringLiteral("C:\\Data\\Report.TXT"), 10);

    ErrorCode error;
    // Windows 语义下不同大小写指向同一个文件。
    QVERIFY(fileSystem.stat(QStringLiteral("c:\\data\\report.txt"), &error).exists);
    QCOMPARE(error, FileSystemError::None);

    // 换成 POSIX 语义后，同一个查询应当找不到——
    // 这正是文件夹比对在两个平台上必须给出不同结论的原因。
    Test::FakeFileSystem posixFileSystem;
    posixFileSystem.addFile(QStringLiteral("/Data/Report.TXT"), 10);
    QVERIFY(!posixFileSystem.stat(QStringLiteral("/data/report.txt"), &error).exists);
    QCOMPARE(error, FileSystemError::NotFound);
}

void TstFileSystem::fakeReproducesHeldFileScenario()
{
    // 这个用例本身没什么复杂度，价值在于演示：真实文件系统上无法稳定
    // 复现的「文件被占用」，在假实现里只是一行注入。
    Test::FakeFileSystem fileSystem;
    fileSystem.addFile(QStringLiteral("/work/output.bin"), 4096);
    fileSystem.fail(Test::FakeFileSystem::Operation::SetAttributes,
                    QStringLiteral("/work/output.bin"), FileSystemError::Busy);

    ErrorCode error;
    const bool ok = fileSystem.setAttributes(QStringLiteral("/work/output.bin"),
                                             FileAttribute::ReadOnly, &error);
    QVERIFY(!ok);
    QCOMPARE(error, FileSystemError::Busy);

    // 而且用户拿到的建议必须是「关掉占用程序」，不是「提高权限」。
    QVERIFY(errorAdvice(error).contains(QStringLiteral("关闭")));
    QVERIFY(isRetryable(error));
}

void TstFileSystem::fakeAddDirectoryBuildsAncestors()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addDirectory(QStringLiteral("/a/b/c"));

    ErrorCode error;
    QVERIFY(fileSystem.stat(QStringLiteral("/a"), &error).exists);
    QVERIFY(fileSystem.stat(QStringLiteral("/a/b"), &error).exists);
    const FileInfo leaf = fileSystem.stat(QStringLiteral("/a/b/c"), &error);
    QVERIFY(leaf.exists);
    QVERIFY(leaf.isDirectory);

    // Windows 盘符与 UNC 前缀也要能正确构建祖先链——
    // 这里的实现复用 parentPath()，因此 UNC 的 server\share 不会被误当成目录名。
    Test::FakeFileSystem windows;
    windows.useWindowsSemantics();
    windows.addDirectory(QStringLiteral("\\\\server\\share\\team\\src"));
    QVERIFY(windows.stat(QStringLiteral("\\\\server\\share\\team"), &error).exists);
    QVERIFY(windows.stat(QStringLiteral("\\\\server\\share\\"), &error).exists);
    // server\share 不能被当成分级目录拆开
    QVERIFY(!windows.stat(QStringLiteral("\\\\server\\"), &error).exists);
}

void TstFileSystem::fakeEnumerateListsOnlyDirectChildren()
{
    Test::FakeFileSystem fileSystem;
    fileSystem.addDirectory(QStringLiteral("/root/sub"));
    fileSystem.addFile(QStringLiteral("/root/a.txt"));
    fileSystem.addFile(QStringLiteral("/root/sub/deep.txt"));

    ErrorCode error;
    const QVector<FileInfo> entries = fileSystem.enumerateDirectory(QStringLiteral("/root"), &error);
    QCOMPARE(error, FileSystemError::None);

    // 不递归：只列直接子项。deep.txt 不该出现。
    QCOMPARE(entries.size(), 2);
    QStringList names;
    for (const FileInfo &entry : entries)
        names.append(entry.name);
    names.sort();
    QCOMPARE(names, QStringList({QStringLiteral("a.txt"), QStringLiteral("sub")}));
}

// -----------------------------------------------------------------------------
// 真实实现（本机平台）
// -----------------------------------------------------------------------------

void TstFileSystem::nativeFileSystemReportsItsPlatform()
{
    const std::unique_ptr<FileSystem> fileSystem(createNativeFileSystem());
    QVERIFY(fileSystem != nullptr);

    const QString platform = fileSystem->platformName();
#ifdef Q_OS_MACOS
    QCOMPARE(platform, QStringLiteral("macos"));
    QCOMPARE(fileSystem->caseSensitivity(), Qt::CaseInsensitive);
    QCOMPARE(fileSystem->separator(), QLatin1Char('/'));
#elif defined(Q_OS_WIN)
    QCOMPARE(platform, QStringLiteral("windows"));
    QCOMPARE(fileSystem->caseSensitivity(), Qt::CaseInsensitive);
    QCOMPARE(fileSystem->separator(), QLatin1Char('\\'));
#else
    QCOMPARE(platform, QStringLiteral("linux"));
    QCOMPARE(fileSystem->caseSensitivity(), Qt::CaseSensitive);
#endif
}

void TstFileSystem::nativeFileSystemReadsRealDirectory()
{
    QTemporaryDir temporaryDir;
    QVERIFY(temporaryDir.isValid());

    const QString root = temporaryDir.path();
    QVERIFY(QDir().mkpath(root + QStringLiteral("/sub")));

    QFile file(root + QStringLiteral("/hello.txt"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("hello lqcompare");
    file.close();

    const std::unique_ptr<FileSystem> fileSystem(createNativeFileSystem());

    ErrorCode error;
    QVERIFY(fileSystem->exists(root, &error));
    QCOMPARE(error, FileSystemError::None);

    const FileInfo fileInfo = fileSystem->stat(root + QStringLiteral("/hello.txt"), &error);
    QVERIFY(fileInfo.exists);
    QVERIFY(!fileInfo.isDirectory);
    QCOMPARE(fileInfo.size, quint64(15));
    QCOMPARE(fileInfo.name, QStringLiteral("hello.txt"));
    QVERIFY(fileInfo.lastModified.isValid());

    const FileInfo dirInfo = fileSystem->stat(root + QStringLiteral("/sub"), &error);
    QVERIFY(dirInfo.isDirectory);
    QVERIFY(!dirInfo.isSymLink);

    const QVector<FileInfo> entries = fileSystem->enumerateDirectory(root, &error);
    QCOMPARE(error, FileSystemError::None);
    QCOMPARE(entries.size(), 2);

    // 缺失路径必须是 NotFound 而不是 Unknown。
    const FileInfo missing = fileSystem->stat(root + QStringLiteral("/nope.txt"), &error);
    QVERIFY(!missing.exists);
    QCOMPARE(error, FileSystemError::NotFound);

    // 对文件调用枚举要明确报 NotDirectory。
    QCOMPARE(fileSystem->enumerateDirectory(root + QStringLiteral("/hello.txt"), &error).size(), 0);
    QCOMPARE(error, FileSystemError::NotDirectory);
}

// Q_OBJECT 声明在头文件里，因此这里不需要 #include "xxx.moc"：
// qmake 会对 HEADERS 中的 Q_OBJECT 头文件生成 moc_*.cpp 并单独编译。
// 曾经在这里加过一次 .moc include，报的是「No rule to make target」。
QTEST_MAIN(TstFileSystem)
