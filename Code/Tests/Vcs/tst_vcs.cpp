#include "vcsbackend.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <cstdio>
#include <thread>

using namespace LqCompare::Vcs;

namespace {

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString errorText(const Error &error)
{
    return error.message + QStringLiteral(" / ") + error.detail;
}

// All fixture mutations are confined to a caller-owned QTemporaryDir. The
// application backend is never used for init/add/commit/checkout/merge.
class GitFixture {
public:
    explicit GitFixture(QString directory) : root(std::move(directory)) {}

    bool init()
    {
        QDir().mkpath(root);
        return command({"init", "-q"})
            && command({"symbolic-ref", "HEAD", "refs/heads/main"})
            && command({"config", "user.name", "VCS Test Author"})
            && command({"config", "user.email", "vcs-test@example.invalid"})
            && command({"config", "commit.gpgSign", "false"})
            && command({"config", "core.autocrlf", "false"});
    }

    QByteArray run(const QStringList &arguments, int expectedExit = 0)
    {
        QProcess process;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString &key : env.keys()) {
            if (key.startsWith(QStringLiteral("GIT_")))
                env.remove(key);
        }
        env.insert("GIT_CONFIG_NOSYSTEM", "1");
        env.insert("GIT_CONFIG_GLOBAL", QDir(root).filePath(".fixture-empty-config"));
        env.insert("GIT_AUTHOR_DATE", "2020-01-02T03:04:05+00:00");
        env.insert("GIT_COMMITTER_DATE", "2020-01-02T03:04:05+00:00");
        env.insert("LC_ALL", "C");
        process.setProcessEnvironment(env);
        process.setWorkingDirectory(root);
        process.start(QStandardPaths::findExecutable("git"), arguments);
        const bool completed = process.waitForStarted(5000) && process.waitForFinished(15000);
        if (!completed) {
            process.kill();
            process.waitForFinished(5000);
        }
        const QByteArray out = process.readAllStandardOutput();
        const QByteArray err = process.readAllStandardError();
        success = completed && process.exitStatus() == QProcess::NormalExit
            && process.exitCode() == expectedExit;
        error = success ? QString() : arguments.join(' ') + ": " + QString::fromUtf8(err);
        return out;
    }

    bool command(const QStringList &arguments, int expectedExit = 0)
    {
        run(arguments, expectedExit);
        return success;
    }

    bool write(const QString &path, const QByteArray &bytes)
    {
        // QDir::filePath treats a leading ':' as a Qt resource path even
        // though it is a legal literal filename on Unix filesystems.
        const QString full = root + '/' + path;
        if (!QDir().mkpath(QFileInfo(full).absolutePath()))
            return false;
        QFile file(full);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            error = file.errorString();
            return false;
        }
        return file.write(bytes) == bytes.size();
    }

    QString commit(const QString &message)
    {
        if (!command({"add", "-A", "--", "."}) || !command({"commit", "-q", "-m", message}))
            return {};
        return QString::fromLatin1(run({"rev-parse", "HEAD"})).trimmed();
    }

    QString root, error;
    bool success = false;
};

const Change *findChange(const QVector<Change> &changes, const QString &path)
{
    for (const Change &change : changes) {
        if (change.path == path)
            return &change;
    }
    return nullptr;
}

class FakeBackend final : public Backend {
public:
    Error availability() const override { return {}; }
    Result<Repository> detectRepo(const QString &, const std::atomic_bool *) const override { return {}; }
    Result<QVector<Change>> status(const Repository &, const std::atomic_bool *) const override { return {}; }
    Result<QVector<Commit>> log(const Repository &, const LogQuery &, const std::atomic_bool *) const override { return {}; }
    Result<QVector<Change>> diff(const Repository &, const Source &, const Source &, const std::atomic_bool *) const override { return {}; }
    Result<QVector<Reference>> references(const Repository &, const std::atomic_bool *) const override { return {}; }
    Result<QVector<BlameLine>> blame(const Repository &, const QString &, const QString &, const std::atomic_bool *) const override { return {}; }
    Result<FileContent> catFile(const Repository &, const QString &, const Source &source,
                                const std::atomic_bool *cancel) const override
    {
        if (cancel && cancel->load())
            return {{}, {ErrorCode::Cancelled, "Cancelled", {}}};
        if (source.kind == SourceKind::Empty)
            return {FileContent{{}, "Empty", {}, false, false}, {}};
        const bool historical = source.kind == SourceKind::Revision;
        return {FileContent{historical ? QByteArray("old\n") : QByteArray("new\n"),
                            historical ? QString("HEAD") : QString("Working tree"), {}, true, false}, {}};
    }
};

class EnvironmentGuard {
public:
    EnvironmentGuard(const char *name, const QByteArray &value)
        : key(name), old(qgetenv(name)), existed(qEnvironmentVariableIsSet(name))
    { qputenv(key.constData(), value); }
    ~EnvironmentGuard()
    {
        if (existed)
            qputenv(key.constData(), old);
        else
            qunsetenv(key.constData());
    }
private:
    QByteArray key, old;
    bool existed;
};

// A subprocess mode of this same executable provides deterministic process
// failures on every platform. Backend still uses QProcess directly, no shell.
int runProcessHelper(const QByteArray &mode, const QStringList &arguments)
{
    if (arguments.contains("--version")) {
        std::fputs("git version 2.40.0\n", stdout);
        return 0;
    }
    if (mode == "blame") {
        QByteArray hash = qgetenv("LQCOMPARE_VCS_BLAME_HASH");
        if (hash.isEmpty()) hash = QByteArray(40, 'a');
        QByteArray output;
        if (arguments.contains("rev-parse")) output = hash + '\n';
        else if (arguments.contains("ls-tree")) output = "100644 blob " + hash + "\ta.txt" + '\0';
        else if (arguments.contains("cat-file")) output = "alpha\n";
        else if (arguments.contains("blame")) output = qgetenv("LQCOMPARE_VCS_BLAME_PAYLOAD");
        else return 23;
        std::fwrite(output.constData(), 1, size_t(output.size()), stdout);
        return 0;
    }
    if (mode == "slow") {
        QThread::msleep(10000);
        return 0;
    }
    if (mode == "output") {
        const QByteArray block(64 * 1024, 'x');
        for (int i = 0; i < 1024; ++i) {
            if (std::fwrite(block.constData(), 1, size_t(block.size()), stdout) != size_t(block.size()))
                break;
            std::fflush(stdout);
        }
        return 0;
    }
    if (mode == "filter") {
        QFile marker(QString::fromUtf8(qgetenv("LQCOMPARE_VCS_FILTER_MARKER")));
        if (marker.open(QIODevice::WriteOnly))
            marker.write("Unexpected external filter execution");
        std::fputs("filtered\n", stdout);
        return 0;
    }
    return 23;
}

} // namespace

class VcsTests : public QObject {
    Q_OBJECT
private slots:
    void missingGitDegradesAllQueries();
    void snapshotRetainsLifetimeAndIsReadOnly();
    void snapshotSupportsEmptySource();
    void processTimeout();
    void processCancellation();
    void processOutputLimit();
    void detectRepositoryAndUnbornHead();
    void detectWorktreeAndNestedRepository();
    void statusSeparatesIndexWorktreeAndRenames();
    void revisionIndexAndWorkingTreeContents();
    void specialPaths_data();
    void specialPaths();
    void revisionsAndInjectionRejection();
    void ambiguousRevision();
    void revisionDiffAndRootCommit();
    void stagedDeletionRetainsPhysicalWorkingFile();
    void conflictsExposeCorrectStages();
    void logPagingFiltersReferencesAndBlame();
    void queriesDoNotChangeRepository();
    void invalidPathsAndBinaryContent();
    void workingTreeDoesNotFollowExternalSymlinks();
    void externalFiltersAreNotExecuted_data();
    void externalFiltersAreNotExecuted();
    void blamePreservesRenamedOriginalPaths_data();
    void blamePreservesRenamedOriginalPaths();
    void blameInputBoundaries();
    void blameProtocolValid_data();
    void blameProtocolValid();
    void blameRejectsMalformedOutput_data();
    void blameRejectsMalformedOutput();
};

#define REQUIRE_GIT() do { if (QStandardPaths::findExecutable("git").isEmpty()) QSKIP("Real Git fixture skipped: git executable unavailable"); } while (false)
#define VERIFY_RESULT(result) QVERIFY2((result).ok(), qPrintable(errorText((result).error)))
#define VERIFY_FIXTURE(expression) QVERIFY2((expression), qPrintable(fixture.error))

void VcsTests::missingGitDegradesAllQueries()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    Options options;
    options.gitExecutable = temporary.filePath("does-not-exist/git");
    GitBackend backend(options);
    const Repository repo{temporary.path(), {}, {}, {}, {}, false};
    QCOMPARE(backend.availability().code, ErrorCode::Unavailable);
    QVERIFY(!backend.availability().message.isEmpty());
    QCOMPARE(backend.detectRepo(temporary.path()).error.code, ErrorCode::Unavailable);
    QCOMPARE(backend.status(repo).error.code, ErrorCode::Unavailable);
    QCOMPARE(backend.log(repo).error.code, ErrorCode::Unavailable);
    QCOMPARE(backend.references(repo).error.code, ErrorCode::Unavailable);
    QCOMPARE(backend.blame(repo, "a.txt").error.code, ErrorCode::Unavailable);
    QCOMPARE(backend.diff(repo, Source::at("HEAD"), Source::workingTree()).error.code, ErrorCode::Unavailable);
    QCOMPARE(backend.catFile(repo, "a.txt", Source::at("HEAD")).error.code, ErrorCode::Unavailable);
}

void VcsTests::snapshotRetainsLifetimeAndIsReadOnly()
{
    FakeBackend backend;
    QString left, right, directory;
    Comparison retained;
    {
        auto result = backend.compare({}, "a.txt", Source::at("HEAD"), "a.txt", Source::workingTree());
        VERIFY_RESULT(result);
        left = result.value.leftPath;
        right = result.value.rightPath;
        QVERIFY(left != right);
        QCOMPARE(readBytes(left), QByteArray("old\n"));
        QCOMPARE(readBytes(right), QByteArray("new\n"));
        QVERIFY(result.value.lifetime);
        directory = result.value.lifetime->path();
        QVERIFY(QFileInfo(left).isAbsolute());
        const auto writable = QFileDevice::WriteOwner | QFileDevice::WriteGroup | QFileDevice::WriteOther;
        QVERIFY(!(QFileInfo(left).permissions() & writable));
        QVERIFY(!(QFileInfo(right).permissions() & writable));
        QVERIFY(!result.value.leftLabel.isEmpty());
        QVERIFY(!result.value.rightLabel.isEmpty());
        retained = result.value;
    }
    QVERIFY(QFileInfo::exists(left));
    QVERIFY(QFileInfo::exists(right));
    retained = {};
    QVERIFY(!QFileInfo::exists(directory));
}

void VcsTests::snapshotSupportsEmptySource()
{
    FakeBackend backend;
    auto result = backend.compare({}, "a.txt", Source::empty(), "a.txt", Source::at("HEAD"));
    VERIFY_RESULT(result);
    QCOMPARE(QFileInfo(result.value.leftPath).size(), qint64(0));
    QCOMPARE(readBytes(result.value.rightPath), QByteArray("old\n"));
    std::atomic_bool cancelled{true};
    QCOMPARE(backend.compare({}, "a", Source::at("HEAD"), "a", Source::workingTree(), &cancelled).error.code,
             ErrorCode::Cancelled);
}

void VcsTests::processTimeout()
{
    EnvironmentGuard helper("LQCOMPARE_VCS_PROCESS_HELPER", "slow");
    Options options;
    options.gitExecutable = QCoreApplication::applicationFilePath();
    options.timeoutMs = 100;
    GitBackend backend(options);
    QElapsedTimer elapsed;
    elapsed.start();
    const auto result = backend.detectRepo(QDir::tempPath());
    QCOMPARE(result.error.code, ErrorCode::Timeout);
    QVERIFY2(elapsed.elapsed() < 3000, "Timed out child process was not stopped promptly");
}

void VcsTests::processCancellation()
{
    EnvironmentGuard helper("LQCOMPARE_VCS_PROCESS_HELPER", "slow");
    Options options;
    options.gitExecutable = QCoreApplication::applicationFilePath();
    options.timeoutMs = 10000;
    GitBackend backend(options);
    std::atomic_bool cancelled{true};
    QCOMPARE(backend.detectRepo(QDir::tempPath(), &cancelled).error.code, ErrorCode::Cancelled);
    cancelled.store(false);
    std::thread cancellation([&cancelled] { QThread::msleep(150); cancelled.store(true); });
    QElapsedTimer elapsed;
    elapsed.start();
    const auto result = backend.detectRepo(QDir::tempPath(), &cancelled);
    cancellation.join();
    QCOMPARE(result.error.code, ErrorCode::Cancelled);
    QVERIFY2(elapsed.elapsed() < 3000, "In-flight process cancellation was not observed promptly");
}

void VcsTests::processOutputLimit()
{
    EnvironmentGuard helper("LQCOMPARE_VCS_PROCESS_HELPER", "output");
    Options options;
    QCOMPARE(options.maximumOutputBytes, qint64(32 * 1024 * 1024));
    options.gitExecutable = QCoreApplication::applicationFilePath();
    options.maximumOutputBytes = 128 * 1024;
    GitBackend backend(options);
    const auto result = backend.detectRepo(QDir::tempPath());
    QCOMPARE(result.error.code, ErrorCode::TooLarge);
}

void VcsTests::detectRepositoryAndUnbornHead()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
#ifdef Q_OS_WIN
    GitFixture fixture(temporary.filePath("repository with spaces"));
#else
    GitFixture fixture(temporary.filePath("repository\nwith spaces"));
#endif
    VERIFY_FIXTURE(fixture.init());
    GitBackend backend;
    auto detected = backend.detectRepo(fixture.root);
    VERIFY_RESULT(detected);
    QCOMPARE(detected.value.root, QFileInfo(fixture.root).canonicalFilePath());
    QVERIFY(!detected.value.hasHead);
    QCOMPARE(backend.catFile(detected.value, "a.txt", Source::at("HEAD")).error.code, ErrorCode::NoHead);
    VERIFY_FIXTURE(fixture.write("sub/directory/a.txt", "first\n"));
    QVERIFY(!fixture.commit("Initial commit").isEmpty());
    detected = backend.detectRepo(fixture.root + "/sub/directory/a.txt");
    VERIFY_RESULT(detected);
    QVERIFY(detected.value.hasHead);
    QCOMPARE(detected.value.branch, QString("main"));
    QCOMPARE(detected.value.root, QFileInfo(fixture.root).canonicalFilePath());
    auto child = backend.detectRepo(fixture.root + "/sub/directory");
    VERIFY_RESULT(child);
    QCOMPARE(child.value.root, detected.value.root);
    QCOMPARE(backend.detectRepo(temporary.path()).error.code, ErrorCode::NotRepository);
}

void VcsTests::detectWorktreeAndNestedRepository()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.filePath("main-repository"));
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("a.txt", "main\n"));
    QVERIFY(!fixture.commit("Root").isEmpty());
    const QString worktree = temporary.filePath("linked worktree");
    VERIFY_FIXTURE(fixture.command({"worktree", "add", "--detach", worktree, "HEAD"}));
    GitBackend backend;
    auto original = backend.detectRepo(fixture.root);
    auto linked = backend.detectRepo(worktree + "/a.txt");
    VERIFY_RESULT(original);
    VERIFY_RESULT(linked);
    QCOMPARE(linked.value.root, QFileInfo(worktree).canonicalFilePath());
    QVERIFY(linked.value.gitDirectory != original.value.gitDirectory);
    QCOMPARE(QFileInfo(linked.value.commonDirectory).canonicalFilePath(),
             QFileInfo(original.value.commonDirectory).canonicalFilePath());
    auto contents = backend.catFile(linked.value, "a.txt", Source::at("HEAD"));
    VERIFY_RESULT(contents);
    QCOMPARE(contents.value.bytes, QByteArray("main\n"));
    GitFixture nested(fixture.root + "/nested");
    QVERIFY2(nested.init(), qPrintable(nested.error));
    QVERIFY(nested.write("inside/a.txt", "nested\n"));
    QVERIFY(!nested.commit("Nested root").isEmpty());
    auto inner = backend.detectRepo(nested.root + "/inside");
    VERIFY_RESULT(inner);
    QCOMPARE(inner.value.root, QFileInfo(nested.root).canonicalFilePath());
    QVERIFY(inner.value.root != original.value.root);
}

void VcsTests::statusSeparatesIndexWorktreeAndRenames()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    for (const QString &path : {QString("work.txt"), QString("index.txt"), QString("both.txt"), QString("deleted.txt"), QString("old name.txt")})
        VERIFY_FIXTURE(fixture.write(path, "base\n"));
    VERIFY_FIXTURE(fixture.write(".gitignore", "ignored.dat\n"));
    QVERIFY(!fixture.commit("Baseline").isEmpty());
    VERIFY_FIXTURE(fixture.write("work.txt", "working\n"));
    VERIFY_FIXTURE(fixture.write("index.txt", "staged\n"));
    VERIFY_FIXTURE(fixture.write("both.txt", "staged\n"));
    VERIFY_FIXTURE(fixture.command({"add", "--", "index.txt", "both.txt"}));
    VERIFY_FIXTURE(fixture.write("both.txt", "unstaged\n"));
    VERIFY_FIXTURE(fixture.command({"mv", "--", "old name.txt", "new name.txt"}));
    QVERIFY(QFile::remove(temporary.filePath("deleted.txt")));
    VERIFY_FIXTURE(fixture.write("untracked.txt", "untracked\n"));
    VERIFY_FIXTURE(fixture.write("ignored.dat", "ignored\n"));
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    auto result = backend.status(repo.value);
    VERIFY_RESULT(result);
    const QMap<QString, QString> expected{{"work.txt", " M"}, {"index.txt", "M "}, {"both.txt", "MM"},
                                         {"deleted.txt", " D"}, {"new name.txt", "R "}, {"untracked.txt", "??"}, {"ignored.dat", "!!"}};
    for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
        const Change *change = findChange(result.value, it.key());
        QVERIFY2(change, qPrintable("Missing change: " + it.key()));
        QCOMPARE(change->status, it.value());
        QVERIFY(!change->conflict);
    }
    QCOMPARE(findChange(result.value, "new name.txt")->oldPath, QString("old name.txt"));
}

void VcsTests::revisionIndexAndWorkingTreeContents()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("a.txt", "first\n"));
    const QString first = fixture.commit("First");
    QVERIFY(!first.isEmpty());
    VERIFY_FIXTURE(fixture.write("a.txt", "second\n"));
    const QString second = fixture.commit("Second");
    QVERIFY(!second.isEmpty());
    VERIFY_FIXTURE(fixture.write("a.txt", "index\n"));
    VERIFY_FIXTURE(fixture.command({"add", "--", "a.txt"}));
    VERIFY_FIXTURE(fixture.write("a.txt", "working\n"));
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    const QVector<QPair<Source, QByteArray>> versions{{Source::at(first), "first\n"}, {Source::at("HEAD"), "second\n"},
        {Source::index(), "index\n"}, {Source::workingTree(), "working\n"}};
    for (const auto &version : versions) {
        auto content = backend.catFile(repo.value, "a.txt", version.first);
        VERIFY_RESULT(content);
        QVERIFY(content.value.exists);
        QCOMPARE(content.value.bytes, version.second);
    }
    const auto comparison = backend.compare(repo.value, "a.txt", Source::at(first), "a.txt", Source::at(second));
    VERIFY_RESULT(comparison);
    QCOMPARE(readBytes(comparison.value.leftPath), QByteArray("first\n"));
    QCOMPARE(readBytes(comparison.value.rightPath), QByteArray("second\n"));
    QVERIFY(comparison.value.leftPath != temporary.filePath("a.txt"));
    QCOMPARE(readBytes(temporary.filePath("a.txt")), QByteArray("working\n"));
    auto missing = backend.catFile(repo.value, "absent.txt", Source::at(first));
    VERIFY_RESULT(missing);
    QVERIFY(!missing.value.exists);
}

void VcsTests::specialPaths_data()
{
    QTest::addColumn<QString>("path");
    QTest::newRow("spaces") << QString("dir with spaces/file name.txt");
    QTest::newRow("tab") << QString("tab\tname.txt");
    QTest::newRow("newline") << QString("line\nbreak.txt");
    QTest::newRow("unicode") << QString::fromUtf8("目录/数据😀.txt");
    QTest::newRow("colon") << QString("name:part.txt");
    QTest::newRow("glob") << QString("literal[*]?.txt");
    QTest::newRow("leading-dash") << QString("--output=elsewhere.txt");
    QTest::newRow("pathspec-magic") << QString(":(glob)*.txt");
}

void VcsTests::specialPaths()
{
    REQUIRE_GIT();
    QFETCH(QString, path);
#ifdef Q_OS_WIN
    if (path.contains(':') || path.contains('*') || path.contains('?') || path.contains('\n') || path.contains('\t'))
        QSKIP("Filename is not representable on Windows filesystems");
#endif
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write(path, "first exact file\n"));
    VERIFY_FIXTURE(fixture.write("decoy.txt", "decoy\n"));
    const QString first = fixture.commit("Target first");
    QVERIFY(!first.isEmpty());
    VERIFY_FIXTURE(fixture.write(path, "second exact file\n"));
    const QString second = fixture.commit("Target second");
    QVERIFY(!second.isEmpty());
    VERIFY_FIXTURE(fixture.write("decoy.txt", "changed decoy\n"));
    QVERIFY(!fixture.commit("Decoy only").isEmpty());
    VERIFY_FIXTURE(fixture.write(path, "index exact file\n"));
    VERIFY_FIXTURE(fixture.command({"--literal-pathspecs", "add", "--", path}));
    VERIFY_FIXTURE(fixture.write(path, "working exact file\n"));
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    auto historical = backend.catFile(repo.value, path, Source::at(first));
    auto indexed = backend.catFile(repo.value, path, Source::index());
    auto working = backend.catFile(repo.value, path, Source::workingTree());
    VERIFY_RESULT(historical);
    VERIFY_RESULT(indexed);
    VERIFY_RESULT(working);
    QCOMPARE(historical.value.bytes, QByteArray("first exact file\n"));
    QCOMPARE(indexed.value.bytes, QByteArray("index exact file\n"));
    QCOMPARE(working.value.bytes, QByteArray("working exact file\n"));
    auto status = backend.status(repo.value);
    VERIFY_RESULT(status);
    QVERIFY2(findChange(status.value, path), qPrintable("Exact path lost in status: " + path));
    auto difference = backend.diff(repo.value, Source::at(first), Source::at(second));
    VERIFY_RESULT(difference);
    QCOMPARE(difference.value.size(), 1);
    QCOMPARE(difference.value.first().path, path);
    LogQuery query;
    query.path = path;
    auto log = backend.log(repo.value, query);
    VERIFY_RESULT(log);
    QCOMPARE(log.value.size(), 2);
    QCOMPARE(log.value.first().id, second);
}

void VcsTests::revisionsAndInjectionRejection()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("a.txt", "first\n"));
    const QString first = fixture.commit("First");
    QVERIFY(!first.isEmpty());
    VERIFY_FIXTURE(fixture.command({"tag", "-a", "release", "-m", "Annotated tag"}));
    VERIFY_FIXTURE(fixture.write("a.txt", "second\n"));
    QVERIFY(!fixture.commit("Second").isEmpty());
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    for (const QString &revision : {QString("HEAD~1"), first.left(9), QString("release"), QString("release^{}")}) {
        auto resolved = backend.resolveRevision(repo.value, revision);
        VERIFY_RESULT(resolved);
        QCOMPARE(resolved.value, first);
    }
    for (const QString &revision : {QString("--output=unsafe"), QString("-C"), QString("HEAD; touch marker"),
                                   QString("HEAD$(touch marker)"), QString("missing-ref"), QString("HEAD:a.txt")}) {
        auto resolved = backend.resolveRevision(repo.value, revision);
        QVERIFY2(!resolved.ok(), qPrintable("Unsafe or non-commit revision accepted: " + revision));
        QVERIFY(resolved.error.code == ErrorCode::InvalidRevision || resolved.error.code == ErrorCode::AmbiguousRevision);
    }
    QVERIFY(!QFileInfo::exists(temporary.filePath("marker")));
}

void VcsTests::ambiguousRevision()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("a.txt", "first\n"));
    QVERIFY(!fixture.commit("First").isEmpty());
    VERIFY_FIXTURE(fixture.command({"tag", "collision"}));
    VERIFY_FIXTURE(fixture.command({"branch", "collision"}));
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    QCOMPARE(backend.resolveRevision(repo.value, "collision").error.code, ErrorCode::AmbiguousRevision);
    auto fullyQualified = backend.resolveRevision(repo.value, "refs/heads/collision");
    VERIFY_RESULT(fullyQualified);
}

void VcsTests::revisionDiffAndRootCommit()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("a.txt", "base\n"));
    VERIFY_FIXTURE(fixture.write("before.txt", "unchanged rename content\n"));
    const QString first = fixture.commit("Root");
    QVERIFY(!first.isEmpty());
    VERIFY_FIXTURE(fixture.write("a.txt", "changed\n"));
    VERIFY_FIXTURE(fixture.command({"mv", "--", "before.txt", "after.txt"}));
    VERIFY_FIXTURE(fixture.write("added.txt", "added\n"));
    const QString second = fixture.commit("Rename and modify");
    QVERIFY(!second.isEmpty());
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    auto rootDiff = backend.diff(repo.value, Source::empty(), Source::at(first));
    VERIFY_RESULT(rootDiff);
    QCOMPARE(rootDiff.value.size(), 2);
    for (const auto &change : rootDiff.value)
        QVERIFY(change.status.startsWith('A'));
    auto changes = backend.diff(repo.value, Source::at(first), Source::at(second));
    VERIFY_RESULT(changes);
    QCOMPARE(changes.value.size(), 3);
    const auto rename = findChange(changes.value, "after.txt");
    QVERIFY(rename);
    QCOMPARE(rename->oldPath, QString("before.txt"));
    QVERIFY(rename->status.startsWith('R'));
    const auto modification = findChange(changes.value, "a.txt");
    QVERIFY(modification);
    QVERIFY(modification->status.startsWith('M'));
    VERIFY_FIXTURE(fixture.write("a.txt", "indexed\n"));
    VERIFY_FIXTURE(fixture.command({"add", "--", "a.txt"}));
    VERIFY_FIXTURE(fixture.write("a.txt", "working\n"));
    auto staged = backend.diff(repo.value, Source::at("HEAD"), Source::index());
    auto unstaged = backend.diff(repo.value, Source::index(), Source::workingTree());
    VERIFY_RESULT(staged);
    VERIFY_RESULT(unstaged);
    QVERIFY(findChange(staged.value, "a.txt"));
    QVERIFY(findChange(unstaged.value, "a.txt"));
}

void VcsTests::conflictsExposeCorrectStages()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("conflict.txt", "base\n"));
    QVERIFY(!fixture.commit("Base").isEmpty());
    VERIFY_FIXTURE(fixture.command({"branch", "feature"}));
    VERIFY_FIXTURE(fixture.write("conflict.txt", "ours\n"));
    QVERIFY(!fixture.commit("Ours").isEmpty());
    VERIFY_FIXTURE(fixture.command({"checkout", "-q", "feature"}));
    VERIFY_FIXTURE(fixture.write("conflict.txt", "theirs\n"));
    QVERIFY(!fixture.commit("Theirs").isEmpty());
    VERIFY_FIXTURE(fixture.command({"checkout", "-q", "main"}));
    VERIFY_FIXTURE(fixture.command({"merge", "--no-edit", "feature"}, 1));
    const QByteArray indexBefore = readBytes(temporary.filePath(".git/index"));
    const QByteArray workingBefore = readBytes(temporary.filePath("conflict.txt"));
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    auto status = backend.status(repo.value);
    VERIFY_RESULT(status);
    const Change *conflict = findChange(status.value, "conflict.txt");
    QVERIFY(conflict);
    QVERIFY(conflict->conflict);
    QCOMPARE(conflict->status, QString("UU"));
    auto difference = backend.diff(repo.value, Source::index(), Source::workingTree());
    VERIFY_RESULT(difference);
    int conflictEntries = 0;
    for (const auto &change : difference.value) {
        if (change.path == "conflict.txt") {
            ++conflictEntries;
            QVERIFY(change.conflict);
            QVERIFY(change.status.startsWith('U'));
        }
    }
    QCOMPARE(conflictEntries, 1);
    const QVector<QByteArray> expected{"base\n", "ours\n", "theirs\n"};
    for (int stage = 1; stage <= 3; ++stage) {
        auto content = backend.catFile(repo.value, "conflict.txt", Source::index(stage));
        VERIFY_RESULT(content);
        QCOMPARE(content.value.bytes, expected[stage - 1]);
    }
    QCOMPARE(backend.catFile(repo.value, "conflict.txt", Source::index()).error.code, ErrorCode::Conflict);
    auto comparison = backend.compare(repo.value, "conflict.txt", Source::index(2), "conflict.txt", Source::index(3));
    VERIFY_RESULT(comparison);
    QCOMPARE(readBytes(comparison.value.leftPath), QByteArray("ours\n"));
    QCOMPARE(readBytes(comparison.value.rightPath), QByteArray("theirs\n"));
    QCOMPARE(readBytes(temporary.filePath(".git/index")), indexBefore);
    QCOMPARE(readBytes(temporary.filePath("conflict.txt")), workingBefore);
}

void VcsTests::stagedDeletionRetainsPhysicalWorkingFile()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("retained.txt", "committed\n"));
    QVERIFY(!fixture.commit("Initial").isEmpty());
    VERIFY_FIXTURE(fixture.command({"rm", "--cached", "--", "retained.txt"}));
    VERIFY_FIXTURE(fixture.write("retained.txt", "retained working contents\n"));
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    auto againstHead = backend.diff(repo.value, Source::at("HEAD"), Source::workingTree());
    VERIFY_RESULT(againstHead);
    QCOMPARE(againstHead.value.size(), 1);
    QCOMPARE(againstHead.value.first().path, QString("retained.txt"));
    QVERIFY(!againstHead.value.first().status.startsWith('D'));
    auto comparison = backend.compare(repo.value, "retained.txt", Source::at("HEAD"),
                                      "retained.txt", Source::workingTree());
    VERIFY_RESULT(comparison);
    QCOMPARE(readBytes(comparison.value.leftPath), QByteArray("committed\n"));
    QCOMPARE(readBytes(comparison.value.rightPath), QByteArray("retained working contents\n"));
    auto againstIndex = backend.diff(repo.value, Source::index(), Source::workingTree());
    VERIFY_RESULT(againstIndex);
    QCOMPARE(againstIndex.value.size(), 1);
    QCOMPARE(againstIndex.value.first().path, QString("retained.txt"));
    QCOMPARE(againstIndex.value.first().status, QString("??"));
}

void VcsTests::logPagingFiltersReferencesAndBlame()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("a.txt", "alpha\nbase\n"));
    const QString first = fixture.commit(QString::fromUtf8("First 提交\n\nFull body kept."));
    QVERIFY(!first.isEmpty());
    VERIFY_FIXTURE(fixture.command({"tag", "v1"}));
    VERIFY_FIXTURE(fixture.write("a.txt", "alpha\nchanged\n"));
    const QString second = fixture.commit("Second target");
    QVERIFY(!second.isEmpty());
    VERIFY_FIXTURE(fixture.write("other.txt", "other\n"));
    const QString third = fixture.commit("Third unrelated");
    QVERIFY(!third.isEmpty());
    VERIFY_FIXTURE(fixture.command({"update-ref", "refs/remotes/origin/main", third}));
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    LogQuery query;
    query.limit = 1;
    auto page1 = backend.log(repo.value, query);
    VERIFY_RESULT(page1);
    QCOMPARE(page1.value.size(), 1);
    QCOMPARE(page1.value.first().id, third);
    query.skip = 1;
    auto page2 = backend.log(repo.value, query);
    VERIFY_RESULT(page2);
    QCOMPARE(page2.value.size(), 1);
    QCOMPARE(page2.value.first().id, second);
    QCOMPARE(page2.value.first().parents, QStringList{first});
    query = {};
    query.path = "a.txt";
    query.author = "VCS Test Author";
    auto filtered = backend.log(repo.value, query);
    VERIFY_RESULT(filtered);
    QCOMPARE(filtered.value.size(), 2);
    QCOMPARE(filtered.value.last().subject, QString::fromUtf8("First 提交"));
    QVERIFY(filtered.value.last().message.contains("Full body kept."));
    QCOMPARE(filtered.value.last().author, QString("VCS Test Author"));
    QCOMPARE(filtered.value.last().email, QString("vcs-test@example.invalid"));
    QVERIFY(filtered.value.last().date.isValid());
    query = {};
    query.message = "Second target";
    auto byMessage = backend.log(repo.value, query);
    VERIFY_RESULT(byMessage);
    QCOMPARE(byMessage.value.size(), 1);
    QCOMPARE(byMessage.value.first().id, second);
    auto references = backend.references(repo.value);
    VERIFY_RESULT(references);
    QStringList names;
    for (const auto &reference : references.value)
        names << reference.name;
    QVERIFY(names.contains("refs/heads/main") || names.contains("main"));
    QVERIFY(names.contains("refs/tags/v1") || names.contains("v1"));
    QVERIFY(names.contains("refs/remotes/origin/main") || names.contains("origin/main"));
    auto blame = backend.blame(repo.value, "a.txt", "HEAD");
    VERIFY_RESULT(blame);
    QCOMPARE(blame.value.size(), 2);
    QCOMPARE(blame.value[0].commit, first);
    QCOMPARE(blame.value[0].text, QString("alpha"));
    QCOMPARE(blame.value[0].finalLine, 1);
    QCOMPARE(blame.value[1].commit, second);
    QCOMPARE(blame.value[1].text, QString("changed"));
    QCOMPARE(blame.value[1].finalLine, 2);
    QCOMPARE(blame.value[1].author, QString("VCS Test Author"));
    QVERIFY(blame.value[1].date.isValid());
    auto graph = backend.revisionGraph(repo.value);
    VERIFY_RESULT(graph);
    QCOMPARE(graph.value.size(), 3);
    QCOMPARE(graph.value.first().parents, QStringList{second});
}

void VcsTests::queriesDoNotChangeRepository()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("a.txt", "committed\n"));
    QVERIFY(!fixture.commit("Initial").isEmpty());
    VERIFY_FIXTURE(fixture.write("a.txt", "staged\n"));
    VERIFY_FIXTURE(fixture.command({"add", "--", "a.txt"}));
    VERIFY_FIXTURE(fixture.write("a.txt", "unstaged\n"));
    const QByteArray indexBefore = readBytes(temporary.filePath(".git/index"));
    const QDateTime indexModified = QFileInfo(temporary.filePath(".git/index")).lastModified();
    const QByteArray refsBefore = fixture.run({"show-ref"});
    const QByteArray headBefore = readBytes(temporary.filePath(".git/HEAD"));
    const QByteArray workBefore = readBytes(temporary.filePath("a.txt"));
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    VERIFY_RESULT(backend.status(repo.value));
    VERIFY_RESULT(backend.log(repo.value));
    VERIFY_RESULT(backend.references(repo.value));
    VERIFY_RESULT(backend.blame(repo.value, "a.txt"));
    VERIFY_RESULT(backend.diff(repo.value, Source::at("HEAD"), Source::workingTree()));
    VERIFY_RESULT(backend.diff(repo.value, Source::at("HEAD"), Source::index()));
    VERIFY_RESULT(backend.catFile(repo.value, "a.txt", Source::index()));
    VERIFY_RESULT(backend.compare(repo.value, "a.txt", Source::at("HEAD"), "a.txt", Source::workingTree()));
    QCOMPARE(readBytes(temporary.filePath(".git/index")), indexBefore);
    QCOMPARE(QFileInfo(temporary.filePath(".git/index")).lastModified(), indexModified);
    QCOMPARE(fixture.run({"show-ref"}), refsBefore);
    QCOMPARE(readBytes(temporary.filePath(".git/HEAD")), headBefore);
    QCOMPARE(readBytes(temporary.filePath("a.txt")), workBefore);
    QVERIFY(!QFileInfo::exists(temporary.filePath(".git/index.lock")));
}

void VcsTests::invalidPathsAndBinaryContent()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.filePath("repo"));
    VERIFY_FIXTURE(fixture.init());
    const QByteArray binary("prefix\0suffix\xff", 14);
    const QByteArray utf16 = QByteArray::fromHex("fffe61000a00");
    VERIFY_FIXTURE(fixture.write("binary.dat", binary));
    VERIFY_FIXTURE(fixture.write("utf16.txt", utf16));
    QVERIFY(!fixture.commit("Binary").isEmpty());
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    for (const Source &source : {Source::workingTree(), Source::index(), Source::at("HEAD")}) {
        auto content = backend.catFile(repo.value, "binary.dat", source);
        VERIFY_RESULT(content);
        QCOMPARE(content.value.bytes, binary);
        QVERIFY(content.value.binary);
        auto textContent = backend.catFile(repo.value, "utf16.txt", source);
        VERIFY_RESULT(textContent);
        QCOMPARE(textContent.value.bytes, utf16);
        QVERIFY(!textContent.value.binary);
        QCOMPARE(backend.catFile(repo.value, "../outside.txt", source).error.code, ErrorCode::InvalidPath);
        QCOMPARE(backend.catFile(repo.value, temporary.filePath("outside.txt"), source).error.code, ErrorCode::InvalidPath);
    }
    auto comparison = backend.compare(repo.value, "binary.dat", Source::at("HEAD"), "binary.dat", Source::workingTree());
    VERIFY_RESULT(comparison);
    QVERIFY(comparison.value.binary);
    QCOMPARE(readBytes(comparison.value.leftPath), binary);
}

void VcsTests::workingTreeDoesNotFollowExternalSymlinks()
{
#ifdef Q_OS_WIN
    QSKIP("QFile::link creates shortcuts on Windows, not filesystem symlinks");
#else
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.filePath("repository"));
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("ordinary.txt", "inside\n"));
    QVERIFY(!fixture.commit("Initial").isEmpty());
    QFile outside(temporary.filePath("outside.txt"));
    QVERIFY(outside.open(QIODevice::WriteOnly));
    QCOMPARE(outside.write("outside secret\n"), qint64(15));
    outside.close();
    QVERIFY(QFile::link(outside.fileName(), fixture.root + "/link.txt"));
    QVERIFY(QFile::link(temporary.path(), fixture.root + "/directory-link"));
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    const auto link = backend.catFile(repo.value, "link.txt", Source::workingTree());
    QCOMPARE(link.error.code, ErrorCode::Unsupported);
    QVERIFY(link.value.bytes.isEmpty());
    const auto throughDirectory = backend.catFile(repo.value, "directory-link/outside.txt", Source::workingTree());
    QCOMPARE(throughDirectory.error.code, ErrorCode::InvalidPath);
    QVERIFY(throughDirectory.value.bytes.isEmpty());
    const auto absentThroughDirectory = backend.catFile(repo.value, "directory-link/absent/file.txt", Source::workingTree());
    QCOMPARE(absentThroughDirectory.error.code, ErrorCode::InvalidPath);
#endif
}

void VcsTests::externalFiltersAreNotExecuted_data()
{
    QTest::addColumn<QString>("filterKind");
    QTest::newRow("clean") << QString("clean");
    QTest::newRow("process") << QString("process");
}

void VcsTests::externalFiltersAreNotExecuted()
{
    REQUIRE_GIT();
    QFETCH(QString, filterKind);
    QTemporaryDir temporary;
    GitFixture fixture(temporary.filePath("repository"));
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("a.txt", "first\n"));
    VERIFY_FIXTURE(fixture.write(".gitattributes", "a.txt filter=probe\n"));
    const QString first = fixture.commit("First without filter configured");
    QVERIFY(!first.isEmpty());
    VERIFY_FIXTURE(fixture.write("a.txt", "second\n"));
    const QString second = fixture.commit("Second without filter configured");
    QVERIFY(!second.isEmpty());
    const QString command = '"' + QCoreApplication::applicationFilePath() + '"';
    VERIFY_FIXTURE(fixture.command({"config", "filter.probe." + filterKind, command}));
    VERIFY_FIXTURE(fixture.write("a.txt", "working change\n"));
    const QString marker = temporary.filePath("filter-executed");
    EnvironmentGuard helper("LQCOMPARE_VCS_PROCESS_HELPER", "filter");
    EnvironmentGuard markerPath("LQCOMPARE_VCS_FILTER_MARKER", marker.toUtf8());
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    QCOMPARE(backend.status(repo.value).error.code, ErrorCode::Unsupported);
    QCOMPARE(backend.diff(repo.value, Source::at("HEAD"), Source::workingTree()).error.code, ErrorCode::Unsupported);
    QCOMPARE(backend.diff(repo.value, Source::index(), Source::workingTree()).error.code, ErrorCode::Unsupported);
    QVERIFY(!QFileInfo::exists(marker));
    auto historical = backend.diff(repo.value, Source::at(first), Source::at(second));
    VERIFY_RESULT(historical);
    QCOMPARE(historical.value.size(), 1);
    auto bytes = backend.catFile(repo.value, "a.txt", Source::at(second));
    VERIFY_RESULT(bytes);
    QCOMPARE(bytes.value.bytes, QByteArray("second\n"));
    auto blame = backend.blame(repo.value, "a.txt", second);
    VERIFY_RESULT(blame);
    QCOMPARE(blame.value.size(), 1);
    QCOMPARE(blame.value.first().commit, second);
    QVERIFY(!QFileInfo::exists(marker));
    // Installed but unused drivers (for example global Git LFS config) must
    // not disable an otherwise ordinary repository.
    VERIFY_FIXTURE(fixture.write(".gitattributes", "*.unused filter=probe\n"));
    auto unused = backend.status(repo.value);
    VERIFY_RESULT(unused);
    QVERIFY(findChange(unused.value, "a.txt"));
    QVERIFY(!QFileInfo::exists(marker));
}

void VcsTests::blamePreservesRenamedOriginalPaths_data()
{
    QTest::addColumn<QString>("originalPath");
    QTest::newRow("ordinary") << QString("old/original.txt");
    QTest::newRow("spaces-preserved") << QString(" leading and trailing ");
    QTest::newRow("quoted-control-unicode") << QString::fromUtf8("旧目录/tab\tline\nback\\quote\"\001.txt");
}

void VcsTests::blamePreservesRenamedOriginalPaths()
{
    REQUIRE_GIT();
    QFETCH(QString, originalPath);
#ifdef Q_OS_WIN
    if (originalPath.endsWith(' ') || originalPath.contains('\t'))
        QSKIP("Filename is not representable on Windows filesystems");
#endif
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    const QByteArray original("first stable line\r\n\nlast stable line");
    VERIFY_FIXTURE(fixture.write(originalPath, original));
    const QString first = fixture.commit(QString::fromUtf8("Original 摘要\n\nOriginal body"));
    QVERIFY(!first.isEmpty());
    VERIFY_FIXTURE(fixture.command({"--literal-pathspecs", "mv", "--", originalPath, "renamed.txt"}));
    VERIFY_FIXTURE(fixture.write("renamed.txt", "first stable line\r\nchanged middle\nlast stable line"));
    const QString second = fixture.commit("Rename and edit middle");
    QVERIFY(!second.isEmpty());
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    auto result = backend.blame(repo.value, "renamed.txt", second);
    VERIFY_RESULT(result);
    QCOMPARE(result.value.size(), 3);
    QCOMPARE(result.value[0].commit, first);
    QCOMPARE(result.value[0].originalPath, originalPath);
    QCOMPARE(result.value[0].originalLine, 1);
    QCOMPARE(result.value[0].text, QString("first stable line\r"));
    QCOMPARE(result.value[0].email, QString("vcs-test@example.invalid"));
    QCOMPARE(result.value[0].summary, QString::fromUtf8("Original 摘要"));
    QCOMPARE(result.value[1].commit, second);
    QCOMPARE(result.value[1].originalPath, QString("renamed.txt"));
    QCOMPARE(result.value[1].originalLine, 2);
    QCOMPARE(result.value[1].summary, QString("Rename and edit middle"));
    QCOMPARE(result.value[2].commit, first);
    QCOMPARE(result.value[2].originalPath, originalPath);
    QCOMPARE(result.value[2].originalLine, 3);
    QCOMPARE(result.value[2].text, QString("last stable line"));
    auto beforeRename = backend.blame(repo.value, originalPath, first);
    VERIFY_RESULT(beforeRename);
    QCOMPARE(beforeRename.value.size(), 3);
    QCOMPARE(beforeRename.value[1].text, QString());
    auto navigated = backend.catFile(repo.value, result.value[0].originalPath, Source::at(result.value[0].commit));
    VERIFY_RESULT(navigated);
    QCOMPARE(navigated.value.bytes, original);
}

void VcsTests::blameInputBoundaries()
{
    REQUIRE_GIT();
    QTemporaryDir temporary;
    GitFixture fixture(temporary.path());
    VERIFY_FIXTURE(fixture.init());
    VERIFY_FIXTURE(fixture.write("empty.txt", {}));
    VERIFY_FIXTURE(fixture.write("binary.dat", QByteArray("a\0b", 3)));
    VERIFY_FIXTURE(fixture.write("utf16.txt", QByteArray::fromHex("fffe61000a00")));
    VERIFY_FIXTURE(fixture.write("latin1.txt", QByteArray::fromHex("636166e90a")));
    VERIFY_FIXTURE(fixture.write("incomplete-utf8.txt", QByteArray::fromHex("6162e4b8")));
    VERIFY_FIXTURE(fixture.write("large.txt", QByteArray(4096, 'a')));
    VERIFY_FIXTURE(fixture.write("newlines.txt", "\n\n"));
    QVERIFY(!fixture.commit("Boundary files").isEmpty());
    VERIFY_FIXTURE(fixture.write("untracked.txt", "not committed\n"));
    GitBackend backend;
    auto repo = backend.detectRepo(fixture.root);
    VERIFY_RESULT(repo);
    auto empty = backend.blame(repo.value, "empty.txt");
    VERIFY_RESULT(empty);
    QVERIFY(empty.value.isEmpty());
    auto newlines = backend.blame(repo.value, "newlines.txt");
    VERIFY_RESULT(newlines);
    QCOMPARE(newlines.value.size(), 2);
    QVERIFY(newlines.value[0].text.isEmpty());
    QVERIFY(newlines.value[1].text.isEmpty());
    for (const QString &path : {QString("binary.dat"), QString("utf16.txt"), QString("latin1.txt"), QString("incomplete-utf8.txt")}) {
        auto result = backend.blame(repo.value, path);
        QCOMPARE(result.error.code, ErrorCode::Unsupported);
        QVERIFY(!result.error.message.isEmpty());
        QVERIFY(result.value.isEmpty());
    }
    for (const QString &path : {QString("untracked.txt"), QString("missing.txt")}) {
        auto result = backend.blame(repo.value, path);
        QCOMPARE(result.error.code, ErrorCode::InvalidPath);
        QVERIFY(!result.error.message.isEmpty());
        QVERIFY(result.value.isEmpty());
    }
    std::atomic_bool cancelled{true};
    QCOMPARE(backend.blame(repo.value, "newlines.txt", "HEAD", &cancelled).error.code, ErrorCode::Cancelled);
    Options options;
    options.maximumOutputBytes = 512;
    GitBackend limited(options);
    QCOMPARE(limited.blame(repo.value, "large.txt").error.code, ErrorCode::TooLarge);
}

namespace {
QByteArray blamePayload(const QByteArray &filename = "a.txt", const QByteArray &hash = QByteArray(40, 'a'))
{
    return hash + " 1 1 1\nauthor Test Author\nauthor-mail <test@example.invalid>\nauthor-time 1577934245\n"
        "author-tz +0000\nsummary Fixture summary\nfilename " + filename + "\n\talpha\n";
}
}

void VcsTests::blameProtocolValid_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<QByteArray>("hash");
    QTest::addColumn<QString>("expectedPath");
    QTest::addColumn<QString>("expectedEmail");
    QTest::addColumn<QString>("expectedSummary");
    const QByteArray sha1(40, 'a'), sha256(64, 'b');
    QTest::newRow("sha256") << blamePayload("a.txt", sha256) << sha256 << QString("a.txt")
                            << QString("test@example.invalid") << QString("Fixture summary");
    const QByteArray quoted("\"dir/\\t\\n\\r\\a\\b\\v\\f\\\\\\\"\\001\\177\\344\\270\\255.txt\"");
    const QString decoded = QString::fromUtf8("dir/\t\n\r\a\b\v\f\\\"\001\177中.txt");
    QTest::newRow("quoted-octal-bytes") << blamePayload(quoted) << sha1 << decoded
                                       << QString("test@example.invalid") << QString("Fixture summary");
    QByteArray metadata = blamePayload(" leading trailing ");
    metadata.replace("<test@example.invalid>", "<>");
    metadata.replace("Fixture summary", " Leading\twith CR\r");
    QTest::newRow("empty-email-whitespace-summary") << metadata << sha1 << QString(" leading trailing ")
        << QString() << QString(" Leading\twith CR\r");
}

void VcsTests::blameProtocolValid()
{
    QFETCH(QByteArray, payload);
    QFETCH(QByteArray, hash);
    QFETCH(QString, expectedPath);
    QFETCH(QString, expectedEmail);
    QFETCH(QString, expectedSummary);
    QTemporaryDir temporary;
    EnvironmentGuard helper("LQCOMPARE_VCS_PROCESS_HELPER", "blame");
    EnvironmentGuard output("LQCOMPARE_VCS_BLAME_PAYLOAD", payload);
    EnvironmentGuard identifier("LQCOMPARE_VCS_BLAME_HASH", hash);
    Options options;
    options.gitExecutable = QCoreApplication::applicationFilePath();
    GitBackend backend(options);
    Repository repo;
    repo.root = temporary.path();
    auto result = backend.blame(repo, "a.txt", "HEAD");
    VERIFY_RESULT(result);
    QCOMPARE(result.value.size(), 1);
    QCOMPARE(result.value.first().commit, QString::fromLatin1(hash));
    QCOMPARE(result.value.first().originalPath, expectedPath);
    QCOMPARE(result.value.first().email, expectedEmail);
    QCOMPARE(result.value.first().summary, expectedSummary);
    QCOMPARE(result.value.first().text, QString("alpha"));
}

void VcsTests::blameRejectsMalformedOutput_data()
{
    QTest::addColumn<QByteArray>("payload");
    const QByteArray valid = blamePayload();
    QTest::newRow("empty-output") << QByteArray();
    QTest::newRow("missing-header") << QByteArray("\talpha\n");
    QTest::newRow("hash-length-41") << blamePayload("a.txt", QByteArray(41, 'a'));
    QTest::newRow("line-zero") << QByteArray(valid).replace(" 1 1 1\n", " 0 1 1\n");
    QTest::newRow("line-overflow") << QByteArray(valid).replace(" 1 1 1\n", " 99999999999999 1 1\n");
    QTest::newRow("wrong-final-line") << QByteArray(valid).replace(" 1 1 1\n", " 1 2 1\n");
    QTest::newRow("missing-filename") << QByteArray(valid).replace("filename a.txt\n", "");
    QTest::newRow("missing-content") << QByteArray(valid).replace("\talpha\n", "");
    QTest::newRow("different-content") << QByteArray(valid).replace("\talpha\n", "\twrong\n");
    QTest::newRow("duplicate-author") << QByteArray(valid).replace("author Test Author\n", "author Test Author\nauthor Duplicate\n");
    QTest::newRow("invalid-date") << QByteArray(valid).replace("1577934245", "not-a-date");
    QTest::newRow("invalid-email") << QByteArray(valid).replace("<test@example.invalid>", "test@example.invalid");
    QTest::newRow("unclosed-filename") << blamePayload("\"unclosed");
    QTest::newRow("unknown-escape") << blamePayload("\"bad\\q\"");
    QTest::newRow("octal-overflow") << blamePayload("\"bad\\777\"");
    QTest::newRow("short-octal") << blamePayload("\"bad\\01\"");
    QTest::newRow("null-filename-byte") << blamePayload("\"bad\\000\"");
    QTest::newRow("path-traversal") << blamePayload("../outside.txt");
    QTest::newRow("unterminated-output") << valid.left(valid.size() - 1);
    QTest::newRow("extra-empty-line") << (valid + '\n');
}

void VcsTests::blameRejectsMalformedOutput()
{
    QFETCH(QByteArray, payload);
    QTemporaryDir temporary;
    EnvironmentGuard helper("LQCOMPARE_VCS_PROCESS_HELPER", "blame");
    EnvironmentGuard output("LQCOMPARE_VCS_BLAME_PAYLOAD", payload);
    Options options;
    options.gitExecutable = QCoreApplication::applicationFilePath();
    GitBackend backend(options);
    Repository repo;
    repo.root = temporary.path();
    auto result = backend.blame(repo, "a.txt", "HEAD");
    QCOMPARE(result.error.code, ErrorCode::Process);
    QVERIFY(!result.error.message.isEmpty());
    QVERIFY(result.value.isEmpty());
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QByteArray helperMode = qgetenv("LQCOMPARE_VCS_PROCESS_HELPER");
    if (!helperMode.isEmpty())
        return runProcessHelper(helperMode, application.arguments());
    VcsTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "tst_vcs.moc"
