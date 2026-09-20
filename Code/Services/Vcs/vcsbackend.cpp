#include "vcsbackend.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QHash>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextCodec>
#include <QSet>
#include <algorithm>

Q_LOGGING_CATEGORY(vcsLog, "lqcompare.vcs")
namespace LqCompare { namespace Vcs {
namespace {
Error fail(ErrorCode code, const QString &message, const QString &detail = {}) { return {code, message, detail}; }
bool cancelled(const std::atomic_bool *flag) { return flag && flag->load(); }
QString trimLine(QByteArray bytes) {
    if (bytes.endsWith('\n')) bytes.chop(1);
    if (bytes.endsWith('\r')) bytes.chop(1);
    return QString::fromUtf8(bytes);
}
Result<QString> relativePath(const Repository &repo, const QString &path) {
    Result<QString> result;
    if (path.isEmpty() || path.contains(QChar::Null)) {
        result.error = fail(ErrorCode::InvalidPath, QStringLiteral("文件路径为空或无效")); return result;
    }
    // Qt treats leading ':' as a resource URL; Git treats it as a legal filename.
    const bool absolute = !path.startsWith(QLatin1Char(':')) && QDir::isAbsolutePath(path);
    QString relative = absolute ? QDir(repo.root).relativeFilePath(path) : path;
    relative = QDir::cleanPath(relative);
    if ((!relative.startsWith(QLatin1Char(':')) && QDir::isAbsolutePath(relative)) || relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))
        || relative == QStringLiteral(".")) {
        result.error = fail(ErrorCode::InvalidPath, QStringLiteral("路径必须位于当前仓库内"), path); return result;
    }
    result.value = relative;
    return result;
}
bool inside(const QString &root, const QString &path) {
    const QString relative = QDir(root).relativeFilePath(path);
    return !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../"));
}
bool validUtf8(const QByteArray &bytes) {
    QTextCodec::ConverterState state;
    QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
    return state.invalidChars == 0 && state.remainingChars == 0;
}
Result<QString> blameFilename(const QByteArray &encoded) {
    Result<QString> result;
    QByteArray decoded;
    if (!encoded.startsWith('"')) decoded = encoded;
    else {
        if (encoded.size() < 2 || !encoded.endsWith('"')) {
            result.error = fail(ErrorCode::Process, QStringLiteral("Git 追溯文件名的引号不完整")); return result;
        }
        const int end = encoded.size() - 1;
        for (int i = 1; i < end; ++i) {
            const char character = encoded[i];
            if (character == '"') {
                result.error = fail(ErrorCode::Process, QStringLiteral("Git 追溯文件名含有未转义引号")); return result;
            }
            if (character != '\\') { decoded += character; continue; }
            if (++i >= end) {
                result.error = fail(ErrorCode::Process, QStringLiteral("Git 追溯文件名的转义不完整")); return result;
            }
            switch (encoded[i]) {
            case 'a': decoded += '\a'; break;
            case 'b': decoded += '\b'; break;
            case 't': decoded += '\t'; break;
            case 'n': decoded += '\n'; break;
            case 'v': decoded += '\v'; break;
            case 'f': decoded += '\f'; break;
            case 'r': decoded += '\r'; break;
            case '\\': decoded += '\\'; break;
            case '"': decoded += '"'; break;
            default:
                // Git's C-style filename quoting uses exactly three octal
                // digits for one byte; reject overflow instead of truncating.
                if (encoded[i] < '0' || encoded[i] > '3' || i + 2 >= end
                    || encoded[i + 1] < '0' || encoded[i + 1] > '7'
                    || encoded[i + 2] < '0' || encoded[i + 2] > '7') {
                    result.error = fail(ErrorCode::Process, QStringLiteral("Git 追溯文件名含有无效转义")); return result;
                }
                decoded += char((encoded[i] - '0') * 64 + (encoded[i + 1] - '0') * 8 + (encoded[i + 2] - '0'));
                i += 2;
            }
        }
    }
    if (decoded.isEmpty() || decoded.contains('\0')) {
        result.error = fail(ErrorCode::Process, QStringLiteral("Git 追溯文件名为空或包含空字符")); return result;
    }
    if (!validUtf8(decoded)) {
        result.error = fail(ErrorCode::Unsupported, QStringLiteral("追溯中的历史文件名不是 UTF-8，无法安全定位该文件")); return result;
    }
    result.value = QString::fromUtf8(decoded);
    return result;
}
Result<QVector<Change>> parseNameStatus(const QByteArray &data) {
    Result<QVector<Change>> result;
    const auto fields = data.split('\0');
    QHash<QString, int> byPath;
    for (int i = 0; i < fields.size() && !fields[i].isEmpty();) {
        Change change;
        change.status = QString::fromLatin1(fields[i++]);
        if (i >= fields.size() || fields[i].isEmpty()) {
            result.error = fail(ErrorCode::Process, QStringLiteral("Git 返回了不完整的变更记录")); return result;
        }
        change.path = QString::fromUtf8(fields[i++]);
        if (change.status.startsWith('R') || change.status.startsWith('C')) {
            change.oldPath = change.path;
            if (i >= fields.size() || fields[i].isEmpty()) {
                result.error = fail(ErrorCode::Process, QStringLiteral("Git 返回了不完整的重命名记录")); return result;
            }
            change.path = QString::fromUtf8(fields[i++]);
        }
        change.conflict = change.status.startsWith('U');
        const int existing = byPath.value(change.path, -1);
        if (existing < 0) { byPath.insert(change.path, result.value.size()); result.value.append(change); }
        else if (change.conflict) result.value[existing] = change; // Git may report both U and M for one conflicted path.
    }
    return result;
}
}

QString sourceLabel(const Source &source) {
    switch (source.kind) {
    case SourceKind::WorkingTree: return QStringLiteral("工作副本");
    case SourceKind::Index:
        return source.stage == 1 ? QStringLiteral("索引：基线") : source.stage == 2 ? QStringLiteral("索引：我方") :
               source.stage == 3 ? QStringLiteral("索引：他方") : QStringLiteral("暂存区");
    case SourceKind::Revision: return source.revision;
    case SourceKind::Empty: return QStringLiteral("不存在（空版本）");
    }
    return {};
}
QString changeLabel(const Change &change) {
    if (change.conflict) return QStringLiteral("冲突");
    if (change.status == QStringLiteral("??")) return QStringLiteral("未跟踪");
    if (change.status == QStringLiteral("!!")) return QStringLiteral("忽略");
    if (change.status.contains('R')) return QStringLiteral("重命名");
    if (change.status.contains('C')) return QStringLiteral("复制");
    if (change.status.contains('A')) return QStringLiteral("新增");
    if (change.status.contains('D')) return QStringLiteral("删除");
    if (change.status.contains('T')) return QStringLiteral("类型变更");
    return QStringLiteral("修改");
}

GitBackend::GitBackend(Options options) : m_options(std::move(options)) {
    m_executable = m_options.gitExecutable.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("git"))
                   : QStandardPaths::findExecutable(m_options.gitExecutable);
    m_options.timeoutMs = std::max(1, m_options.timeoutMs);
    m_options.maximumOutputBytes = std::max<qint64>(1, m_options.maximumOutputBytes);
}
Error GitBackend::availability() const {
    return m_executable.isEmpty() ? fail(ErrorCode::Unavailable, QStringLiteral("未检测到 git 可执行文件，请在版本控制设置中配置 Git 路径")) : Error{};
}
Result<GitBackend::Output> GitBackend::run(const QString &directory, const QStringList &arguments,
                                         const std::atomic_bool *cancel, const QByteArray &input) const {
    Result<Output> result;
    result.error = availability();
    if (!result.ok()) return result;
    if (cancelled(cancel)) { result.error = fail(ErrorCode::Cancelled, QStringLiteral("Git 查询已取消")); return result; }
    QProcess process;
    process.setWorkingDirectory(directory);
    auto environment = QProcessEnvironment::systemEnvironment();
    // Git environment from an IDE/terminal must not silently select another
    // repository, index, object store, config file or external helper.
    for (const auto &key : environment.keys()) if (key.startsWith(QStringLiteral("GIT_"))) environment.remove(key);
    environment.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_PAGER"), QStringLiteral("cat"));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(environment);
    for (const auto &argument : arguments) {
        if (argument.contains(QChar::Null)) {
            result.error = fail(ErrorCode::Process, QStringLiteral("Git 查询参数含有无效的空字符")); return result;
        }
    }
    QStringList args{QStringLiteral("--no-pager"), QStringLiteral("--literal-pathspecs"),
                     QStringLiteral("-c"), QStringLiteral("core.fsmonitor=false"),
                     QStringLiteral("-c"), QStringLiteral("color.ui=false"),
                     QStringLiteral("-c"), QStringLiteral("core.quotePath=false")};
    args.append(arguments);
    QElapsedTimer timer; timer.start();
    process.start(m_executable, args, QIODevice::ReadWrite);
    while (process.state() == QProcess::Starting) {
        process.waitForStarted(20);
        if (cancelled(cancel) || timer.elapsed() >= m_options.timeoutMs) break;
    }
    if (process.state() == QProcess::NotRunning && process.error() == QProcess::FailedToStart) {
        result.error = fail(ErrorCode::Unavailable, QStringLiteral("无法启动 git 可执行文件"), process.errorString()); return result;
    }
    if (!input.isEmpty()) process.write(input);
    process.closeWriteChannel();
    for (;;) {
        result.value.out += process.readAllStandardOutput();
        result.value.err += process.readAllStandardError();
        if (cancelled(cancel)) result.error = fail(ErrorCode::Cancelled, QStringLiteral("Git 查询已取消"));
        else if (timer.elapsed() >= m_options.timeoutMs) result.error = fail(ErrorCode::Timeout, QStringLiteral("Git 查询超时"));
        else if (result.value.out.size() + qint64(result.value.err.size()) > m_options.maximumOutputBytes)
            result.error = fail(ErrorCode::TooLarge, QStringLiteral("Git 输出超过允许的大小"));
        if (!result.ok()) { process.kill(); process.waitForFinished(1000); break; }
        if (process.state() == QProcess::NotRunning) break;
        process.waitForFinished(20);
    }
    result.value.exitCode = process.exitCode();
    if (result.ok() && (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0))
        result.error = fail(ErrorCode::Process, QStringLiteral("Git 查询失败"), QString::fromUtf8(result.value.err).trimmed());
    // Do not log file content or commit messages.
    qCDebug(vcsLog).noquote() << arguments.value(0) << timer.elapsed() << "ms" << "exit" << result.value.exitCode;
    return result;
}

Result<Repository> GitBackend::detectRepo(const QString &path, const std::atomic_bool *cancel) const {
    Result<Repository> result;
    QFileInfo info(path);
    QString directory = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    // Deleted files and descendants still belong to their nearest existing ancestor.
    while (!QFileInfo::exists(directory)) {
        const QString parent = QFileInfo(directory).absolutePath();
        if (parent == directory) break;
        directory = parent;
    }
    auto root = run(directory, {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")}, cancel);
    if (!root.ok()) {
        result.error = root.error;
        if (result.error.code == ErrorCode::Process) result.error.message = QStringLiteral("所选路径不属于可读取的 Git 工作树"), result.error.code = ErrorCode::NotRepository;
        return result;
    }
    result.value.root = trimLine(root.value.out);
    // Separate queries preserve metadata directories containing newlines.
    auto gitDirectory = run(result.value.root, {QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir")}, cancel);
    if (!gitDirectory.ok()) { result.error = gitDirectory.error; return result; }
    auto commonDirectory = run(result.value.root, {QStringLiteral("rev-parse"), QStringLiteral("--git-common-dir")}, cancel);
    if (!commonDirectory.ok()) { result.error = commonDirectory.error; return result; }
    result.value.gitDirectory = QDir::cleanPath(trimLine(gitDirectory.value.out));
    result.value.commonDirectory = QDir::cleanPath(QDir(result.value.root).absoluteFilePath(trimLine(commonDirectory.value.out)));
    auto head = resolveRevision(result.value, QStringLiteral("HEAD"), cancel);
    if (!head.ok() && head.error.code != ErrorCode::NoHead && head.error.code != ErrorCode::InvalidRevision) { result.error = head.error; return result; }
    result.value.hasHead = head.ok(); result.value.head = head.value;
    auto branch = run(result.value.root, {QStringLiteral("symbolic-ref"), QStringLiteral("--quiet"), QStringLiteral("--short"), QStringLiteral("HEAD")}, cancel);
    if (branch.ok()) result.value.branch = trimLine(branch.value.out);
    else if (branch.error.code != ErrorCode::Process) result.error = branch.error;
    return result;
}
Result<QString> GitBackend::resolveRevision(const Repository &repo, const QString &revision, const std::atomic_bool *cancel) const {
    Result<QString> result;
    if (revision.isEmpty() || revision.contains(QChar::Null)) {
        result.error = fail(ErrorCode::InvalidRevision, QStringLiteral("修订不能为空或包含空字符")); return result;
    }
    const auto output = run(repo.root, {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--end-of-options"), revision + QStringLiteral("^{commit}")}, cancel);
    if (!output.ok()) {
        result.error = output.error;
        if (result.error.code == ErrorCode::Process) {
            const bool ambiguous = output.value.err.contains("ambiguous");
            result.error.code = ambiguous ? ErrorCode::AmbiguousRevision : revision == QStringLiteral("HEAD") ? ErrorCode::NoHead : ErrorCode::InvalidRevision;
            result.error.message = ambiguous ? QStringLiteral("修订存在歧义，请使用完整引用或哈希") : revision == QStringLiteral("HEAD") ? QStringLiteral("仓库尚无 HEAD 提交") : QStringLiteral("修订不存在或不指向提交");
        }
        return result;
    }
    if (output.value.err.contains("ambiguous")) { result.error = fail(ErrorCode::AmbiguousRevision, QStringLiteral("修订存在歧义，请使用完整引用或哈希"), QString::fromUtf8(output.value.err)); return result; }
    result.value = trimLine(output.value.out);
    if (!QRegularExpression(QStringLiteral("^[0-9a-f]{40,64}$")).match(result.value).hasMatch())
        result.error = fail(ErrorCode::InvalidRevision, QStringLiteral("Git 返回的修订哈希无效"));
    return result;
}
Error GitBackend::checkWorktreeFilters(const Repository &repo, const std::atomic_bool *cancel) const {
    // Even --name-status and status can invoke a configured clean/process filter.
    // Such a helper is arbitrary code and can write to the user's worktree.
    // Fail explicitly instead of disabling conversions and reporting false changes.
    const auto output = run(repo.root, {QStringLiteral("config"), QStringLiteral("--null"), QStringLiteral("--get-regexp"),
                                       QStringLiteral("^filter\\..*\\.(clean|process)$")}, cancel);
    if (!output.ok()) {
        if (output.error.code == ErrorCode::Process && output.value.exitCode == 1 && output.value.err.isEmpty()) return {};
        return output.error;
    }
    QSet<QByteArray> drivers;
    for (const auto &record : output.value.out.split('\0')) {
        const int newline = record.indexOf('\n');
        if (newline < 0 || record.mid(newline + 1).trimmed().isEmpty()) continue;
        const auto key = record.left(newline);
        drivers.insert(key.mid(7, key.lastIndexOf('.') - 7));
    }
    if (drivers.isEmpty()) return {};
    // Git LFS commonly installs global filter definitions in every repository.
    // Only reject helpers used by tracked paths, so an unused global definition
    // does not disable ordinary repositories. Both plumbing queries are read-only.
    const auto paths = run(repo.root, {QStringLiteral("ls-files"), QStringLiteral("--cached"), QStringLiteral("-z")}, cancel);
    if (!paths.ok()) return paths.error;
    if (paths.value.out.isEmpty()) return {};
    const auto attributes = run(repo.root, {QStringLiteral("check-attr"), QStringLiteral("-z"), QStringLiteral("--stdin"), QStringLiteral("filter")}, cancel, paths.value.out);
    if (!attributes.ok()) return attributes.error;
    const auto fields = attributes.value.out.split('\0');
    for (int i = 0; i + 2 < fields.size(); i += 3) {
        if (drivers.contains(fields[i + 2]))
            return fail(ErrorCode::Unsupported, QStringLiteral("仓库文件使用外部内容过滤器，无法保证工作区查询只读；请使用外部 Git 客户端审阅工作区，或比较两个历史修订"),
                        QString::fromUtf8(fields[i]) + QStringLiteral(": ") + QString::fromUtf8(fields[i + 2]));
    }
    return {};
}
Result<QVector<Change>> GitBackend::status(const Repository &repo, const std::atomic_bool *cancel) const {
    Result<QVector<Change>> result;
    result.error = checkWorktreeFilters(repo, cancel);
    if (!result.ok()) return result;
    const auto output = run(repo.root, {QStringLiteral("status"), QStringLiteral("--porcelain=v1"), QStringLiteral("-z"), QStringLiteral("--untracked-files=all"), QStringLiteral("--ignored=matching")}, cancel);
    if (!output.ok()) { result.error = output.error; return result; }
    const auto fields = output.value.out.split('\0');
    for (int i = 0; i < fields.size() && !fields[i].isEmpty(); ++i) {
        const auto &field = fields[i];
        if (field.size() < 4 || field[2] != ' ') { result.error = fail(ErrorCode::Process, QStringLiteral("Git 返回了无效的状态记录")); return result; }
        Change change; change.status = QString::fromLatin1(field.left(2)); change.path = QString::fromUtf8(field.mid(3));
        change.conflict = change.status.contains('U') || change.status == QStringLiteral("AA") || change.status == QStringLiteral("DD");
        if (change.status.contains('R') || change.status.contains('C')) {
            if (++i >= fields.size() || fields[i].isEmpty()) { result.error = fail(ErrorCode::Process, QStringLiteral("Git 返回了不完整的重命名记录")); return result; }
            change.oldPath = QString::fromUtf8(fields[i]);
        }
        result.value.append(change);
    }
    return result;
}
Result<QVector<Commit>> GitBackend::log(const Repository &repo, const LogQuery &query, const std::atomic_bool *cancel) const {
    Result<QVector<Commit>> result;
    auto revision = resolveRevision(repo, query.revision, cancel);
    if (!revision.ok()) { result.error = revision.error; return result; }
    QStringList args{QStringLiteral("log"), QStringLiteral("-z"), QStringLiteral("--date-order"), QStringLiteral("--no-show-signature"),
                     QStringLiteral("--format=%H%x00%P%x00%an%x00%ae%x00%aI%x00%D%x00%B"),
                     QStringLiteral("--max-count=%1").arg(std::clamp(query.limit, 1, 1000)), QStringLiteral("--skip=%1").arg(std::max(0, query.skip)),
                     QStringLiteral("--fixed-strings")};
    if (!query.author.isEmpty()) args << QStringLiteral("--author=") + query.author;
    if (!query.message.isEmpty()) args << QStringLiteral("--grep=") + query.message;
    if (query.since.isValid()) args << QStringLiteral("--since=") + query.since.toString(Qt::ISODate);
    if (query.until.isValid()) args << QStringLiteral("--until=") + query.until.toString(Qt::ISODate);
    args << revision.value << QStringLiteral("--");
    if (!query.path.isEmpty()) {
        const auto path = relativePath(repo, query.path);
        if (!path.ok()) { result.error = path.error; return result; }
        args << path.value;
    }
    auto output = run(repo.root, args, cancel);
    if (!output.ok()) { result.error = output.error; return result; }
    const auto fields = output.value.out.split('\0');
    for (int i = 0; i < fields.size() && !fields[i].isEmpty(); i += 7) {
        if (i + 6 >= fields.size()) { result.error = fail(ErrorCode::Process, QStringLiteral("Git 返回了不完整的日志记录")); return result; }
        Commit commit;
        commit.id = QString::fromLatin1(fields[i]);
        commit.parents = QString::fromLatin1(fields[i + 1]).split(' ', Qt::SkipEmptyParts);
        commit.author = QString::fromUtf8(fields[i + 2]); commit.email = QString::fromUtf8(fields[i + 3]);
        commit.date = QDateTime::fromString(QString::fromLatin1(fields[i + 4]), Qt::ISODate);
        commit.references = QString::fromUtf8(fields[i + 5]); commit.message = QString::fromUtf8(fields[i + 6]);
        commit.subject = commit.message.section('\n', 0, 0);
        result.value.append(commit);
    }
    return result;
}

Result<QVector<Change>> GitBackend::diff(const Repository &repo, const Source &left, const Source &right,
                                         const std::atomic_bool *cancel) const {
    Result<QVector<Change>> result;
    if (left.kind == SourceKind::WorkingTree || (left.kind == SourceKind::Index && right.kind == SourceKind::Revision)
        || right.kind == SourceKind::Empty) {
        if (left.kind == right.kind) { result.error = fail(ErrorCode::Unsupported, QStringLiteral("请选择不同的比较来源")); return result; }
        result = diff(repo, right, left, cancel);
        if (!result.ok()) return result;
        for (auto &change : result.value) {
            if (change.status.startsWith('A')) change.status = QStringLiteral("D");
            else if (change.status.startsWith('D')) change.status = QStringLiteral("A");
            if (!change.oldPath.isEmpty()) std::swap(change.path, change.oldPath);
        }
        return result;
    }
    if (right.kind == SourceKind::WorkingTree) {
        result.error = checkWorktreeFilters(repo, cancel);
        if (!result.ok()) return result;
    }
    QString leftRevision, rightRevision;
    for (const auto &item : {qMakePair(left, &leftRevision), qMakePair(right, &rightRevision)}) {
        if (item.first.kind != SourceKind::Revision) continue;
        auto revision = resolveRevision(repo, item.first.revision, cancel);
        if (!revision.ok()) { result.error = revision.error; return result; }
        *item.second = revision.value;
    }
    if (left.kind == SourceKind::Empty && right.kind == SourceKind::Revision) {
        const auto output = run(repo.root, {QStringLiteral("ls-tree"), QStringLiteral("-r"), QStringLiteral("--name-only"), QStringLiteral("-z"), rightRevision}, cancel);
        if (!output.ok()) { result.error = output.error; return result; }
        for (const auto &path : output.value.out.split('\0')) if (!path.isEmpty()) result.value.append({QString::fromUtf8(path), {}, QStringLiteral("A"), false});
        return result;
    }
    QStringList args{QStringLiteral("diff"), QStringLiteral("--no-ext-diff"), QStringLiteral("--no-textconv"),
                     QStringLiteral("--name-status"), QStringLiteral("-z"), QStringLiteral("--find-renames"), QStringLiteral("--ignore-submodules=none")};
    if (left.kind == SourceKind::Revision && right.kind == SourceKind::Revision) args << leftRevision << rightRevision;
    else if (left.kind == SourceKind::Revision && right.kind == SourceKind::Index && right.stage == 0) args << QStringLiteral("--cached") << leftRevision;
    else if (left.kind == SourceKind::Revision && right.kind == SourceKind::WorkingTree) args << leftRevision;
    else if (left.kind == SourceKind::Index && left.stage == 0 && right.kind == SourceKind::WorkingTree) {}
    else { result.error = fail(ErrorCode::Unsupported, QStringLiteral("不支持这组比较来源；冲突请使用明确的索引阶段读取文件")); return result; }
    args << QStringLiteral("--");
    auto output = run(repo.root, args, cancel);
    if (!output.ok()) { result.error = output.error; return result; }
    result = parseNameStatus(output.value.out);
    if (!result.ok()) return result;
    if (right.kind == SourceKind::WorkingTree) {
        auto state = status(repo, cancel);
        if (!state.ok()) { result.error = state.error; return result; }
        QHash<QString, int> byPath;
        for (int i = 0; i < result.value.size(); ++i) byPath.insert(result.value[i].path, i);
        for (const auto &change : state.value) {
            if (change.status != QStringLiteral("??")) continue;
            const int existing = byPath.value(change.path, -1);
            if (existing < 0) { byPath.insert(change.path, result.value.size()); result.value.append(change); }
            else if (result.value[existing].status.startsWith('D')) {
                // A path removed from the index can still exist as an untracked
                // working file. Its physical content is the right-hand source;
                // treating it as an empty deletion would hide that content.
                result.value[existing].status = QStringLiteral("M");
            }
        }
    }
    return result;
}
Result<FileContent> GitBackend::catFile(const Repository &repo, const QString &path, const Source &source,
                                       const std::atomic_bool *cancel) const {
    Result<FileContent> result;
    result.value.label = sourceLabel(source);
    if (cancelled(cancel)) { result.error = fail(ErrorCode::Cancelled, QStringLiteral("内容读取已取消")); return result; }
    const auto relative = relativePath(repo, path);
    if (!relative.ok()) { result.error = relative.error; return result; }
    result.value.label += QStringLiteral(": ") + relative.value;
    if (source.kind == SourceKind::Empty) return result;
    if (source.kind == SourceKind::WorkingTree) {
        const QString fullPath = QDir::cleanPath(repo.root + QLatin1Char('/') + relative.value);
        const QFileInfo info(fullPath);
        if (info.isSymLink()) { result.error = fail(ErrorCode::Unsupported, QStringLiteral("符号链接请在文件夹视图中审阅；不会读取链接目标"), fullPath); return result; }
        // Canonicalize the nearest existing parent, including for deleted files.
        QString parentPath = info.absolutePath();
        while (!QFileInfo::exists(parentPath)) {
            const auto next = QFileInfo(parentPath).absolutePath();
            if (next == parentPath) break;
            parentPath = next;
        }
        if (!inside(QFileInfo(repo.root).canonicalFilePath(), QFileInfo(parentPath).canonicalFilePath())) {
            result.error = fail(ErrorCode::InvalidPath, QStringLiteral("路径经过了指向仓库外的链接"), fullPath); return result;
        }
        if (!info.exists()) return result;
        if (!info.isFile()) { result.error = fail(ErrorCode::Unsupported, QStringLiteral("所选条目不是普通文件（可能是目录或子模块）"), fullPath); return result; }
        if (info.size() > m_options.maximumOutputBytes) { result.error = fail(ErrorCode::TooLarge, QStringLiteral("文件超过允许的大小")); return result; }
        QFile file(fullPath);
        if (!file.open(QIODevice::ReadOnly)) { result.error = fail(ErrorCode::Io, QStringLiteral("无法读取工作副本文件"), file.errorString()); return result; }
        result.value.bytes = file.read(m_options.maximumOutputBytes + 1);
        if (file.error() != QFileDevice::NoError) { result.error = fail(ErrorCode::Io, QStringLiteral("读取工作副本失败"), file.errorString()); return result; }
        if (result.value.bytes.size() > m_options.maximumOutputBytes) { result.error = fail(ErrorCode::TooLarge, QStringLiteral("文件超过允许的大小")); return result; }
        result.value.exists = true;
    } else {
        QString objectId;
        if (source.kind == SourceKind::Revision) {
            auto revision = resolveRevision(repo, source.revision, cancel);
            if (!revision.ok()) { result.error = revision.error; return result; }
            result.value.label = source.revision + QStringLiteral(" (") + revision.value.left(12) + QStringLiteral("): ") + relative.value;
            auto output = run(repo.root, {QStringLiteral("ls-tree"), QStringLiteral("-z"), QStringLiteral("--full-tree"), revision.value, QStringLiteral("--"), relative.value}, cancel);
            if (!output.ok()) { result.error = output.error; return result; }
            for (const auto &entry : output.value.out.split('\0')) {
                const auto tab = entry.indexOf('\t');
                if (tab < 0 || entry.mid(tab + 1) != relative.value.toUtf8()) continue;
                const auto metadata = entry.left(tab).split(' ');
                if (metadata.size() != 3) { result.error = fail(ErrorCode::Process, QStringLiteral("Git 文件记录无效")); return result; }
                if (metadata[1] != "blob") { result.error = fail(ErrorCode::Unsupported, QStringLiteral("历史条目不是文件（可能是目录或子模块）")); return result; }
                objectId = QString::fromLatin1(metadata[2]);
            }
        } else if (source.kind == SourceKind::Index) {
            if (source.stage < 0 || source.stage > 3) { result.error = fail(ErrorCode::InvalidPath, QStringLiteral("索引阶段必须为 0 到 3")); return result; }
            auto output = run(repo.root, {QStringLiteral("ls-files"), QStringLiteral("--stage"), QStringLiteral("-z"), QStringLiteral("--"), relative.value}, cancel);
            if (!output.ok()) { result.error = output.error; return result; }
            bool hasConflict = false;
            for (const auto &entry : output.value.out.split('\0')) {
                const auto tab = entry.indexOf('\t');
                if (tab < 0 || entry.mid(tab + 1) != relative.value.toUtf8()) continue;
                const auto metadata = entry.left(tab).split(' ');
                if (metadata.size() != 3) { result.error = fail(ErrorCode::Process, QStringLiteral("Git 索引记录无效")); return result; }
                const int stage = metadata[2].toInt();
                hasConflict |= stage != 0;
                if (stage != source.stage) continue;
                if (metadata[0] == "160000") { result.error = fail(ErrorCode::Unsupported, QStringLiteral("所选索引条目是子模块")); return result; }
                objectId = QString::fromLatin1(metadata[1]);
            }
            if (source.stage == 0 && hasConflict) { result.error = fail(ErrorCode::Conflict, QStringLiteral("文件存在冲突，请选择基线、我方或他方索引版本")); return result; }
        } else { result.error = fail(ErrorCode::Unsupported, QStringLiteral("未知内容来源")); return result; }
        if (objectId.isEmpty()) return result; // Absent in a valid source: the empty side of add/delete.
        auto output = run(repo.root, {QStringLiteral("cat-file"), QStringLiteral("blob"), objectId}, cancel);
        if (!output.ok()) { result.error = output.error; return result; }
        result.value.objectId = objectId; result.value.bytes = output.value.out; result.value.exists = true;
    }
    if (cancelled(cancel)) { result.error = fail(ErrorCode::Cancelled, QStringLiteral("内容读取已取消")); return result; }
    const auto &bytes = result.value.bytes;
    const bool utf16Bom = (bytes.startsWith(QByteArray::fromHex("fffe")) || bytes.startsWith(QByteArray::fromHex("feff")))
                         && !bytes.startsWith(QByteArray::fromHex("fffe0000"));
    result.value.binary = bytes.contains('\0') && !utf16Bom;
    return result;
}
Result<Comparison> Backend::compare(const Repository &repo, const QString &leftPath, const Source &left,
                                    const QString &rightPath, const Source &right, const std::atomic_bool *cancel) const {
    Result<Comparison> result;
    auto a = catFile(repo, leftPath, left, cancel);
    if (!a.ok()) { result.error = a.error; return result; }
    auto b = catFile(repo, rightPath, right, cancel);
    if (!b.ok()) { result.error = b.error; return result; }
    if (cancelled(cancel)) { result.error = fail(ErrorCode::Cancelled, QStringLiteral("比较已取消")); return result; }
    auto lifetime = QSharedPointer<QTemporaryDir>::create(QDir::tempPath() + QStringLiteral("/lqcompare-vcs-XXXXXX"));
    if (!lifetime->isValid()) { result.error = fail(ErrorCode::Io, QStringLiteral("无法创建 Git 比较快照目录")); return result; }
    auto writeSnapshot = [&](const QString &name, const QByteArray &bytes, QString *destination) {
        *destination = lifetime->filePath(name);
        QFile file(*destination);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush()) {
            result.error = fail(ErrorCode::Io, QStringLiteral("无法写入比较快照"), file.errorString()); return false;
        }
        file.close();
        if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup | QFileDevice::ReadOther)) {
            result.error = fail(ErrorCode::Io, QStringLiteral("无法将比较快照设置为只读"), file.errorString()); return false;
        }
        return true;
    };
    auto snapshotName = [](const QString &prefix, const QString &path) {
        const auto suffix = QFileInfo(path).suffix();
        return prefix + (QRegularExpression(QStringLiteral("^[A-Za-z0-9]{1,16}$")).match(suffix).hasMatch() ? QStringLiteral(".") + suffix : QStringLiteral(".snapshot"));
    };
    if (!writeSnapshot(snapshotName(QStringLiteral("left"), leftPath), a.value.bytes, &result.value.leftPath)
        || !writeSnapshot(snapshotName(QStringLiteral("right"), rightPath), b.value.bytes, &result.value.rightPath)) return result;
    result.value.leftLabel = a.value.label + (a.value.exists ? QString() : QStringLiteral(" [不存在]"));
    result.value.rightLabel = b.value.label + (b.value.exists ? QString() : QStringLiteral(" [不存在]"));
    result.value.title = result.value.leftLabel + QStringLiteral(" ↔ ") + result.value.rightLabel;
    result.value.binary = a.value.binary || b.value.binary;
    result.value.lifetime = lifetime;
    return result;
}

Result<QVector<Reference>> GitBackend::references(const Repository &repo, const std::atomic_bool *cancel) const {
    Result<QVector<Reference>> result;
    const auto output = run(repo.root, {QStringLiteral("for-each-ref"), QStringLiteral("--format=%(refname)%00%(objectname)"),
                                      QStringLiteral("refs/heads"), QStringLiteral("refs/remotes"), QStringLiteral("refs/tags")}, cancel);
    if (!output.ok()) { result.error = output.error; return result; }
    for (const auto &line : output.value.out.split('\n')) {
        if (line.isEmpty()) continue;
        const auto fields = line.split('\0');
        if (fields.size() != 2) { result.error = fail(ErrorCode::Process, QStringLiteral("Git 引用记录无效")); return result; }
        Reference reference; reference.name = QString::fromUtf8(fields[0]); reference.objectId = QString::fromLatin1(fields[1]);
        reference.kind = reference.name.startsWith(QStringLiteral("refs/heads/")) ? QStringLiteral("branch") : reference.name.startsWith(QStringLiteral("refs/remotes/")) ? QStringLiteral("remote") : QStringLiteral("tag");
        result.value.append(reference);
    }
    return result;
}
Result<QVector<BlameLine>> GitBackend::blame(const Repository &repo, const QString &path, const QString &revision,
                                           const std::atomic_bool *cancel) const {
    Result<QVector<BlameLine>> result;
    auto ref = resolveRevision(repo, revision, cancel);
    if (!ref.ok()) { result.error = ref.error; return result; }
    const auto relative = relativePath(repo, path);
    if (!relative.ok()) { result.error = relative.error; return result; }
    // Pin both reads to the resolved commit so a moving branch cannot change
    // the file between validation and attribution. catFile preserves all
    // cancellation, blob-size and read-only process protections.
    const auto content = catFile(repo, relative.value, Source::at(ref.value), cancel);
    if (!content.ok()) { result.error = content.error; return result; }
    if (!content.value.exists) {
        result.error = fail(ErrorCode::InvalidPath, QStringLiteral("所选修订中不存在该文件；它可能尚未提交或在该修订中使用其他路径"), relative.value); return result;
    }
    if (content.value.binary) {
        result.error = fail(ErrorCode::Unsupported, QStringLiteral("二进制文件不支持逐行追溯")); return result;
    }
    const auto &sourceBytes = content.value.bytes;
    if (!validUtf8(sourceBytes)) {
        result.error = fail(ErrorCode::Unsupported, QStringLiteral("逐行追溯目前只支持 UTF-8 文本；该文件使用 UTF-16 或其他编码")); return result;
    }
    if (sourceBytes.isEmpty()) return result;
    const auto output = run(repo.root, {QStringLiteral("blame"), QStringLiteral("--line-porcelain"), QStringLiteral("--no-textconv"),
                                      QStringLiteral("--encoding=UTF-8"), ref.value, QStringLiteral("--"), relative.value}, cancel);
    if (!output.ok()) { result.error = output.error; return result; }
    BlameLine current;
    bool inRecord = false;
    enum Field { Author = 1, Email = 2, Date = 4, Summary = 8, Filename = 16 };
    int fields = 0, sourceOffset = 0;
    const int requiredFields = Author | Email | Date | Summary | Filename;
    const QRegularExpression header(QStringLiteral("^([0-9a-f]{40}|[0-9a-f]{64}) ([1-9][0-9]*) ([1-9][0-9]*)(?: ([1-9][0-9]*))?$"));
    const QRegularExpression metadata(QStringLiteral("^[a-z][a-z-]*(?: .*)?$"));
    auto invalid = [&](const QString &detail) {
        result.value.clear();
        result.error = fail(ErrorCode::Process, QStringLiteral("Git 返回了不完整或无效的逐行追溯记录"), detail);
        return result;
    };
    int offset = 0;
    while (offset < output.value.out.size()) {
        if (cancelled(cancel)) {
            result.value.clear(); result.error = fail(ErrorCode::Cancelled, QStringLiteral("逐行追溯已取消")); return result;
        }
        const int end = output.value.out.indexOf('\n', offset);
        if (end < 0) return invalid(QStringLiteral("记录缺少行终止符"));
        const auto line = output.value.out.mid(offset, end - offset);
        offset = end + 1;
        if (!inRecord) {
            const auto match = header.match(QString::fromLatin1(line));
            if (!match.hasMatch()) return invalid(QStringLiteral("缺少有效的提交与行号"));
            current = {};
            current.commit = match.captured(1);
            bool originalOk = false, finalOk = false, countOk = true;
            current.originalLine = match.captured(2).toInt(&originalOk);
            current.finalLine = match.captured(3).toInt(&finalOk);
            if (!match.captured(4).isEmpty()) match.captured(4).toInt(&countOk);
            if (!originalOk || !finalOk || !countOk || current.finalLine != result.value.size() + 1)
                return invalid(QStringLiteral("行号溢出、重复或顺序无效"));
            fields = 0;
            inRecord = true;
            continue;
        }
        if (line.startsWith('\t')) {
            if (fields != requiredFields) return invalid(QStringLiteral("记录缺少作者、邮箱、日期、摘要或文件名"));
            if (sourceOffset >= sourceBytes.size()) return invalid(QStringLiteral("追溯结果超过文件行数"));
            const int sourceEnd = sourceBytes.indexOf('\n', sourceOffset);
            const int sourceLength = (sourceEnd < 0 ? sourceBytes.size() : sourceEnd) - sourceOffset;
            const auto text = line.mid(1);
            if (text != sourceBytes.mid(sourceOffset, sourceLength))
                return invalid(QStringLiteral("追溯内容与所选修订的文件内容不一致"));
            // Preserve empty lines, CRLF carriage returns and a final line
            // without LF. Only the porcelain prefix is removed.
            current.text = QString::fromUtf8(text);
            sourceOffset += sourceLength + (sourceEnd < 0 ? 0 : 1);
            result.value.append(current);
            inRecord = false;
            continue;
        }
        if (!validUtf8(line) || !metadata.match(QString::fromUtf8(line)).hasMatch())
            return invalid(QStringLiteral("记录元数据格式无效"));
        int field = 0;
        if (line.startsWith("author ")) { field = Author; current.author = QString::fromUtf8(line.mid(7)); }
        else if (line.startsWith("author-mail ")) {
            field = Email;
            const auto email = line.mid(12);
            if (email.size() < 2 || !email.startsWith('<') || !email.endsWith('>'))
                return invalid(QStringLiteral("作者邮箱缺少边界符"));
            current.email = QString::fromUtf8(email.mid(1, email.size() - 2));
        } else if (line.startsWith("author-time ")) {
            field = Date;
            bool valid = false;
            const auto seconds = line.mid(12).toLongLong(&valid);
            current.date = QDateTime::fromSecsSinceEpoch(seconds, Qt::UTC);
            if (!valid || !current.date.isValid()) return invalid(QStringLiteral("作者日期无效"));
        } else if (line.startsWith("summary ")) { field = Summary; current.summary = QString::fromUtf8(line.mid(8)); }
        else if (line.startsWith("filename ")) {
            field = Filename;
            const auto filename = blameFilename(line.mid(9));
            if (!filename.ok()) { result.value.clear(); result.error = filename.error; return result; }
            const auto checkedPath = relativePath(repo, filename.value);
            if (!checkedPath.ok() || checkedPath.value != filename.value)
                return invalid(QStringLiteral("历史文件名不是有效的仓库相对路径"));
            current.originalPath = filename.value;
        }
        if (field && (fields & field)) return invalid(QStringLiteral("记录包含重复元数据"));
        fields |= field;
    }
    if (inRecord || sourceOffset != sourceBytes.size()) return invalid(QStringLiteral("追溯记录未覆盖文件全部内容"));
    return result;
}
} }
