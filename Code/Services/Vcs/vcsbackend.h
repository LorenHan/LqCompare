#ifndef LQCOMPARE_VCSBACKEND_H
#define LQCOMPARE_VCSBACKEND_H
#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QSharedPointer>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>
#include <atomic>

namespace LqCompare { namespace Vcs {
enum class ErrorCode { None, Unavailable, NotRepository, NoHead, InvalidRevision, AmbiguousRevision,
                       InvalidPath, Conflict, Unsupported, Process, Timeout, Cancelled, TooLarge, Io };
struct Error {
    ErrorCode code = ErrorCode::None;
    QString message;
    QString detail;
    bool isError() const { return code != ErrorCode::None; }
};
template<class T> struct Result {
    T value{};
    Error error;
    bool ok() const { return !error.isError(); }
};
struct Repository { QString root, gitDirectory, commonDirectory, head, branch; bool hasHead = false; };
struct Change {
    QString path, oldPath;
    QString status; // A/M/D/R/C/T/U/?/!, or the two porcelain index/worktree columns.
    bool conflict = false;
};
struct Commit { QString id; QStringList parents; QString author, email; QDateTime date; QString subject, message, references; };
struct LogQuery {
    QString revision = QStringLiteral("HEAD"), path, author, message;
    QDateTime since, until;
    int skip = 0, limit = 100;
};
enum class SourceKind { WorkingTree, Index, Revision, Empty };
struct Source {
    SourceKind kind = SourceKind::WorkingTree;
    QString revision;
    int stage = 0; // Index conflict stages: 1=base, 2=ours, 3=theirs.
    static Source workingTree() { return {}; }
    static Source index(int stage = 0) { return {SourceKind::Index, {}, stage}; }
    static Source at(const QString &revision) { return {SourceKind::Revision, revision, 0}; }
    static Source empty() { return {SourceKind::Empty, {}, 0}; }
};
struct FileContent { QByteArray bytes; QString label, objectId; bool exists = false; bool binary = false; };
// A copy of this value retains the private snapshot directory. Keep it on the
// receiving compare session for its entire lifetime; both inputs are read-only.
struct Comparison {
    QString leftPath, rightPath, leftLabel, rightLabel, title;
    bool binary = false;
    QSharedPointer<QTemporaryDir> lifetime;
};
struct Reference { QString name, objectId, kind; };
struct BlameLine {
    QString commit, author, text;
    int originalLine = 0, finalLine = 0;
    QDateTime date;
    // Path in the attributed commit, which can differ after a rename.
    QString originalPath, email, summary;
};
struct Options { QString gitExecutable; int timeoutMs = 15000; qint64 maximumOutputBytes = 32 * 1024 * 1024; };

class Backend {
public:
    virtual ~Backend() = default;
    virtual Error availability() const = 0;
    virtual Result<Repository> detectRepo(const QString &path, const std::atomic_bool *cancel = nullptr) const = 0;
    virtual Result<QVector<Change>> status(const Repository &, const std::atomic_bool *cancel = nullptr) const = 0;
    virtual Result<QVector<Commit>> log(const Repository &, const LogQuery & = {}, const std::atomic_bool *cancel = nullptr) const = 0;
    virtual Result<QVector<Change>> diff(const Repository &, const Source &left, const Source &right,
                                        const std::atomic_bool *cancel = nullptr) const = 0;
    virtual Result<FileContent> catFile(const Repository &, const QString &path, const Source &,
                                      const std::atomic_bool *cancel = nullptr) const = 0;
    virtual Result<QVector<Reference>> references(const Repository &, const std::atomic_bool *cancel = nullptr) const = 0;
    virtual Result<QVector<BlameLine>> blame(const Repository &, const QString &path, const QString &revision = QStringLiteral("HEAD"),
                                          const std::atomic_bool *cancel = nullptr) const = 0;
    virtual Result<QVector<Commit>> revisionGraph(const Repository &repo, const LogQuery &query = {},
                                                 const std::atomic_bool *cancel = nullptr) const { return log(repo, query, cancel); }
    Result<Comparison> compare(const Repository &, const QString &leftPath, const Source &left,
                               const QString &rightPath, const Source &right, const std::atomic_bool *cancel = nullptr) const;
};
class GitBackend final : public Backend {
public:
    explicit GitBackend(Options options = {});
    Error availability() const override;
    Result<Repository> detectRepo(const QString &, const std::atomic_bool * = nullptr) const override;
    Result<QVector<Change>> status(const Repository &, const std::atomic_bool * = nullptr) const override;
    Result<QVector<Commit>> log(const Repository &, const LogQuery & = {}, const std::atomic_bool * = nullptr) const override;
    Result<QVector<Change>> diff(const Repository &, const Source &, const Source &, const std::atomic_bool * = nullptr) const override;
    Result<FileContent> catFile(const Repository &, const QString &, const Source &, const std::atomic_bool * = nullptr) const override;
    Result<QVector<Reference>> references(const Repository &, const std::atomic_bool * = nullptr) const override;
    Result<QVector<BlameLine>> blame(const Repository &, const QString &, const QString & = QStringLiteral("HEAD"),
                                  const std::atomic_bool * = nullptr) const override;
    Result<QString> resolveRevision(const Repository &, const QString &, const std::atomic_bool * = nullptr) const;
    Options options() const { return m_options; }
private:
    struct Output { QByteArray out, err; int exitCode = -1; };
    Result<Output> run(const QString &directory, const QStringList &arguments, const std::atomic_bool *, const QByteArray &input = {}) const;
    Error checkWorktreeFilters(const Repository &, const std::atomic_bool *) const;
    Options m_options;
    QString m_executable;
};
QString sourceLabel(const Source &source);
QString changeLabel(const Change &change);
} }
Q_DECLARE_METATYPE(LqCompare::Vcs::Comparison)
#endif
