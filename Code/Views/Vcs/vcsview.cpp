#include "vcsview.h"
#include "vcsavailability.h"

#include <QComboBox>
#include <QColor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>

namespace LqCompare {
namespace {
using namespace Vcs;
using CancelToken = QSharedPointer<std::atomic_bool>;
constexpr int PageSize = 100;

struct Scope {
    QString path;
    bool directory = false;
};

Scope pathScope(const Repository &repo, const QString &path)
{
    const QFileInfo info(path);
    QString absolute = info.absoluteFilePath();
    if (info.isDir() && !info.canonicalFilePath().isEmpty()) {
        absolute = info.canonicalFilePath();
    } else {
        // Canonicalize directory aliases, but retain the leaf (including a
        // symlink or deleted file) so the backend can enforce its file policy.
        QString ancestor = info.absolutePath();
        while (!QFileInfo::exists(ancestor)) {
            const QString parent = QFileInfo(ancestor).absolutePath();
            if (parent == ancestor) break;
            ancestor = parent;
        }
        const QString canonical = QFileInfo(ancestor).canonicalFilePath();
        if (!canonical.isEmpty()) absolute = QDir(canonical).filePath(QDir(ancestor).relativeFilePath(absolute));
    }
    const QString canonicalRoot = QFileInfo(repo.root).canonicalFilePath();
    QString relative = QDir::fromNativeSeparators(QDir(canonicalRoot.isEmpty() ? repo.root : canonicalRoot).relativeFilePath(absolute));
    if (relative == QStringLiteral(".")) relative.clear();
    return {relative, info.isDir() || path.endsWith('/') || path.endsWith('\\')};
}

QVector<Change> scopedChanges(const QVector<Change> &changes, const Scope &scope)
{
    if (scope.path.isEmpty()) return changes;
    QVector<Change> selected;
    const auto matches = [&scope](const QString &path) {
        return path == scope.path || (scope.directory && path.startsWith(scope.path + '/'));
    };
    for (const auto &change : changes)
        if (matches(change.path) || (!change.oldPath.isEmpty() && matches(change.oldPath))) selected.append(change);
    return selected;
}

QString errorText(const Error &error)
{
    switch (error.code) {
    case ErrorCode::Unavailable: return QStringLiteral("未检测到 git 可执行文件。请安装 Git 后重新打开此视图。");
    case ErrorCode::NoHead: return QStringLiteral("此仓库尚无 HEAD（还没有提交）。可以选择“与暂存区比对”审阅工作副本。");
    case ErrorCode::NotRepository: return QStringLiteral("所选路径不属于 Git 仓库，请选择仓库内的文件或目录。");
    case ErrorCode::Cancelled: return QStringLiteral("已取消 Git 查询。");
    case ErrorCode::Timeout: return QStringLiteral("Git 查询超时，请重试或提高后端超时设置。");
    case ErrorCode::AmbiguousRevision: return QStringLiteral("修订引用存在歧义，请输入更完整的提交哈希或引用名称。") + error.message;
    case ErrorCode::InvalidRevision: return QStringLiteral("无法解析修订引用：") + error.message;
    default: return error.message.isEmpty() ? QStringLiteral("Git 查询失败。请查看详细错误后重试。") : error.message;
    }
}

struct WorkResult {
    Error error;
    Repository repository;
    Scope scope;
    QVector<Change> changes;
    QVector<Commit> commits;
    Source left, right;
    QString leftLabel, rightLabel;
    Comparison comparison;
};

Result<QString> resolveCommit(const Backend &backend, const Repository &repo, const QString &revision,
                              const std::atomic_bool *cancel)
{
    LogQuery query;
    query.revision = revision;
    query.limit = 1;
    const auto result = backend.log(repo, query, cancel);
    if (!result.ok()) return {{}, result.error};
    if (result.value.isEmpty())
        return {{}, {ErrorCode::InvalidRevision, QStringLiteral("“%1” 没有可比较的提交。").arg(revision), {}}};
    return {result.value.first().id, {}};
}
}

class VcsView::Private {
public:
    explicit Private(VcsView *owner, QSharedPointer<Backend> backend)
        : q(owner), backend(std::move(backend)),
          // 探测结果按路径缓存（VCS-001 第 4 条）。缓存与后端一起**按值**持有：
          // 工作线程里只允许出现值、后端与取消令牌（见 run()），缓存同理。
          cache(QSharedPointer<RepositoryCache>::create(this->backend)) {}
    ~Private() { if (token) token->store(true); }

    VcsView *q;
    QSharedPointer<Backend> backend;
    QSharedPointer<RepositoryCache> cache;
    CancelToken token;
    quint64 generation = 0;
    bool busy = false, started = false, available = true, repositoryReady = false, hasMore = false;
    Mode currentMode = Head;
    Repository repository;
    Scope scope;
    Source left, right;
    QString leftLabel, rightLabel;
    QVector<Change> changes;
    QVector<Commit> commits;
    QLineEdit *path = nullptr, *leftRevision = nullptr, *rightRevision = nullptr;
    QComboBox *mode = nullptr;
    QPushButton *refreshButton = nullptr, *cancelButton = nullptr, *compareButton = nullptr, *loadMoreButton = nullptr;
    QTreeWidget *files = nullptr, *history = nullptr;
    QPlainTextEdit *details = nullptr;
    QLabel *description = nullptr, *status = nullptr;
    QProgressBar *progress = nullptr;
    QWidget *revisions = nullptr, *historyPanel = nullptr;

    void setStatus(const QString &message)
    {
        status->setText(message);
        emit q->statusChanged(message);
    }

    void updateControls()
    {
        const QString unavailable = QStringLiteral("未检测到 git 可执行文件");
        mode->setEnabled(available);
        revisions->setEnabled(available);
        refreshButton->setEnabled(available);
        cancelButton->setEnabled(busy);
        progress->setVisible(busy);
        files->setEnabled(available && repositoryReady);
        history->setEnabled(available && repositoryReady);
        compareButton->setEnabled(available && repositoryReady && !busy && files->currentItem());
        loadMoreButton->setEnabled(available && repositoryReady && !busy && hasMore);
        for (QWidget *widget : QList<QWidget *>{mode, refreshButton, compareButton, loadMoreButton})
            widget->setToolTip(available ? QString() : unavailable);
    }

    void showError(const Error &error)
    {
        if (error.code == ErrorCode::Unavailable) available = false;
        const QString message = errorText(error);
        status->setToolTip(error.detail);
        setStatus(message);
        updateControls();
        if (error.code != ErrorCode::Cancelled) emit q->errorOccurred(message);
    }

    void stop(bool report)
    {
        if (token) token->store(true);
        ++generation;
        const bool wasBusy = busy;
        busy = false;
        updateControls();
        if (wasBusy && report) setStatus(QStringLiteral("已取消 Git 查询。"));
    }

    // 仓库探测缓存按引用传进来，而不是让每个 lambda 自己捕获：工作线程里只允许出现
    // 值、后端、缓存与取消令牌（捕获 `this`/`d` 会在标签被关掉之后指向已析构的对象）。
    // 四个 lambda 必须共用同一个签名，因此只有一个真的用它——其余三个把参数名留空，
    // 因为本仓按 `-Wextra` 编译，命名了却不用的形参会直接报 `-Wunused-parameter`。
    void run(const QString &message,
             std::function<WorkResult(const Backend &, RepositoryCache &, const std::atomic_bool *)> work,
             std::function<void(const WorkResult &)> complete)
    {
        stop(false);
        token = CancelToken::create(false);
        const auto thisToken = token;
        const auto thisGeneration = generation;
        const auto backendCopy = backend;
        const auto cacheCopy = cache;
        busy = true;
        status->setToolTip({});
        setStatus(message);
        updateControls();
        auto *watcher = new QFutureWatcher<WorkResult>(q);
        // The connection has a QObject context, so closing a tab disconnects it.
        // Only values, the backend, the cache and the cancellation token enter the worker.
        QObject::connect(watcher, &QFutureWatcher<WorkResult>::finished, q,
                         [this, watcher, thisGeneration, thisToken, complete] {
            const WorkResult result = watcher->result();
            watcher->deleteLater();
            if (thisGeneration != generation || thisToken->load()) return;
            busy = false;
            updateControls();
            if (result.error.isError()) { showError(result.error); return; }
            complete(result);
        });
        watcher->setFuture(QtConcurrent::run([backendCopy, cacheCopy, thisToken, work] {
            if (thisToken->load()) {
                WorkResult cancelled;
                cancelled.error = {ErrorCode::Cancelled, {}, {}};
                return cancelled;
            }
            return work(*backendCopy, *cacheCopy, thisToken.data());
        }));
    }

    void populateFiles(const QVector<Change> &value)
    {
        changes = value;
        const QSignalBlocker blocker(files);
        files->clear();
        for (int i = 0; i < changes.size(); ++i) {
            const auto &change = changes.at(i);
            auto *item = new QTreeWidgetItem(files, {changeLabel(change), change.oldPath, change.path});
            item->setData(0, Qt::UserRole, i);
            item->setToolTip(0, change.status);
            item->setToolTip(1, change.oldPath);
            item->setToolTip(2, change.path);
            if (change.conflict) item->setForeground(0, QColor(Qt::darkRed));
        }
        if (files->topLevelItemCount()) files->setCurrentItem(files->topLevelItem(0));
        updateControls();
    }

    void setDirection(const QString &a, const QString &b)
    {
        leftLabel = a;
        rightLabel = b;
        description->setText(a + QStringLiteral(" → ") + b + QStringLiteral(" · 只读比较，双击文件打开"));
    }

    void populateHistory(const QVector<Commit> &page, bool append)
    {
        const QSignalBlocker blocker(history);
        if (!append) { commits.clear(); history->clear(); }
        const int offset = commits.size();
        commits += page;
        for (int i = 0; i < page.size(); ++i) {
            const auto &commit = page.at(i);
            auto *item = new QTreeWidgetItem(history, {commit.id.left(12), commit.author,
                commit.date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")), commit.subject});
            item->setData(0, Qt::UserRole, offset + i);
            item->setToolTip(0, commit.id);
            item->setToolTip(1, QStringLiteral("%1 <%2>").arg(commit.author, commit.email));
            item->setToolTip(2, commit.date.toLocalTime().toString(Qt::ISODate));
            item->setToolTip(3, commit.message);
        }
        hasMore = page.size() == PageSize;
        loadMoreButton->setText(hasMore ? QStringLiteral("加载更多（已加载 %1 条）").arg(commits.size())
                                        : QStringLiteral("已加载全部 %1 条提交").arg(commits.size()));
        updateControls();
    }

    void refresh()
    {
        started = true;
        available = true; // An explicit refresh also retries unavailable backends.
        repositoryReady = false;
        hasMore = false;
        changes.clear();
        files->clear();
        { const QSignalBlocker blocker(history); history->clear(); commits.clear(); }
        details->clear();
        const QString requestedPath = path->text();
        const QString revisionA = leftRevision->text().trimmed(), revisionB = rightRevision->text().trimmed();
        const Mode requestedMode = currentMode;
        if (requestedPath.isEmpty()) {
            stop(false);
            setStatus(QStringLiteral("请输入 Git 仓库内的文件或目录路径。"));
            return;
        }
        run(QStringLiteral("正在读取 Git 仓库…"),
            [requestedPath, requestedMode, revisionA, revisionB](const Backend &backend, RepositoryCache &cache, const std::atomic_bool *cancel) {
                WorkResult result;
                result.error = backend.availability();
                if (result.error.isError()) return result;
                // 走缓存而不是直接问后端：切模式 / 刷新都会重来一遍，而同一个路径
                // 的仓库探测要起一次 git 进程（VCS-001 第 4 条）。
                const auto detected = cache.detect(requestedPath, cancel);
                if (!detected.ok()) { result.error = detected.error; return result; }
                result.repository = detected.value;
                result.scope = pathScope(result.repository, requestedPath);
                if ((requestedMode == Head || requestedMode == History) && !result.repository.hasHead) {
                    result.error = {ErrorCode::NoHead, {}, {}};
                    return result;
                }
                if (requestedMode == History) {
                    LogQuery query;
                    query.revision = result.repository.head.isEmpty() ? QStringLiteral("HEAD") : result.repository.head;
                    query.path = result.scope.path;
                    query.limit = PageSize;
                    const auto page = backend.log(result.repository, query, cancel);
                    result.error = page.error;
                    result.commits = page.value;
                    return result;
                }
                result.right = Source::workingTree();
                result.rightLabel = QStringLiteral("工作副本");
                if (requestedMode == Head) {
                    result.left = Source::at(result.repository.head.isEmpty() ? QStringLiteral("HEAD") : result.repository.head);
                    result.leftLabel = QStringLiteral("HEAD %1").arg(result.repository.head.left(12));
                } else if (requestedMode == Index) {
                    result.left = Source::index();
                    result.leftLabel = QStringLiteral("暂存区（Index）");
                } else {
                    if (revisionA.isEmpty() || revisionB.isEmpty()) {
                        result.error = {ErrorCode::InvalidRevision, QStringLiteral("请输入左右两个修订引用。"), {}};
                        return result;
                    }
                    const auto resolvedA = resolveCommit(backend, result.repository, revisionA, cancel);
                    if (!resolvedA.ok()) { result.error = resolvedA.error; return result; }
                    const auto resolvedB = resolveCommit(backend, result.repository, revisionB, cancel);
                    if (!resolvedB.ok()) { result.error = resolvedB.error; return result; }
                    result.left = Source::at(resolvedA.value);
                    result.right = Source::at(resolvedB.value);
                    result.leftLabel = QStringLiteral("%1 (%2)").arg(revisionA, resolvedA.value.left(12));
                    result.rightLabel = QStringLiteral("%1 (%2)").arg(revisionB, resolvedB.value.left(12));
                }
                const auto diff = backend.diff(result.repository, result.left, result.right, cancel);
                result.error = diff.error;
                result.changes = scopedChanges(diff.value, result.scope);
                return result;
            }, [this, requestedMode](const WorkResult &result) {
                repository = result.repository;
                scope = result.scope;
                repositoryReady = true;
                if (requestedMode == History) {
                    populateHistory(result.commits, false);
                    if (history->topLevelItemCount()) history->setCurrentItem(history->topLevelItem(0));
                    else setStatus(QStringLiteral("当前路径没有提交记录。"));
                } else {
                    left = result.left;
                    right = result.right;
                    setDirection(result.leftLabel, result.rightLabel);
                    populateFiles(result.changes);
                    setStatus(QStringLiteral("%1 · %2 个变更文件").arg(repository.root).arg(changes.size()));
                }
                updateControls();
            });
    }

    void selectCommit()
    {
        if (currentMode != History || !repositoryReady || !history->currentItem()) return;
        const int index = history->currentItem()->data(0, Qt::UserRole).toInt();
        if (index < 0 || index >= commits.size()) return;
        const Commit commit = commits.at(index);
        const QString parent = commit.parents.isEmpty() ? QString() : commit.parents.first();
        left = parent.isEmpty() ? Source::empty() : Source::at(parent);
        right = Source::at(commit.id);
        const QString parentLabel = parent.isEmpty() ? QStringLiteral("空版本（根提交）")
                                                   : QStringLiteral("第一个父提交 %1").arg(parent.left(12));
        setDirection(parentLabel, QStringLiteral("提交 %1").arg(commit.id.left(12)));
        details->setPlainText(QStringLiteral("提交：%1\n作者：%2 <%3>\n日期：%4\n父提交：%5\n引用：%6\n\n%7\n\n%8")
            .arg(commit.id, commit.author, commit.email, commit.date.toLocalTime().toString(Qt::ISODate),
                 commit.parents.isEmpty() ? QStringLiteral("无（根提交）") : commit.parents.join(QStringLiteral("\n         ")),
                 commit.references, commit.message.isEmpty() ? commit.subject : commit.message,
                 QStringLiteral("下方清单显示此提交相对第一个父提交的变更；根提交相对空版本。")));
        populateFiles({});
        const Repository repo = repository;
        const Source sourceA = left, sourceB = right;
        const Scope queryScope = scope;
        run(QStringLiteral("正在读取提交 %1 的变更…").arg(commit.id.left(12)),
            [repo, sourceA, sourceB, queryScope](const Backend &backend, RepositoryCache &, const std::atomic_bool *cancel) {
                WorkResult result;
                const auto diff = backend.diff(repo, sourceA, sourceB, cancel);
                result.error = diff.error;
                result.changes = scopedChanges(diff.value, queryScope);
                return result;
            }, [this](const WorkResult &result) {
                populateFiles(result.changes);
                setStatus(QStringLiteral("已加载 %1 条提交 · 当前提交 %2 个变更文件（相对第一个父提交）")
                          .arg(commits.size()).arg(changes.size()));
            });
    }

    void loadMore()
    {
        if (busy || currentMode != History || !repositoryReady || !hasMore) return;
        const Repository repo = repository;
        LogQuery query;
        query.revision = repo.head.isEmpty() ? QStringLiteral("HEAD") : repo.head;
        query.path = scope.path;
        query.skip = commits.size();
        query.limit = PageSize;
        run(QStringLiteral("正在加载更多提交…"),
            [repo, query](const Backend &backend, RepositoryCache &, const std::atomic_bool *cancel) {
                WorkResult result;
                const auto page = backend.log(repo, query, cancel);
                result.error = page.error;
                result.commits = page.value;
                return result;
            }, [this](const WorkResult &result) {
                populateHistory(result.commits, true);
                setStatus(QStringLiteral("已加载 %1 条提交；变更清单仍对应选中的提交。").arg(commits.size()));
            });
    }

    void compare()
    {
        if (busy || !repositoryReady || !files->currentItem()) return;
        const int index = files->currentItem()->data(0, Qt::UserRole).toInt();
        if (index < 0 || index >= changes.size()) return;
        const Change change = changes.at(index);
        if (change.conflict && left.kind == SourceKind::Index) {
            showError({ErrorCode::Conflict, QStringLiteral("此文件存在未解决的暂存区冲突，无法读取 stage 0。可与 HEAD 比对，或使用三方合并处理冲突。"), {}});
            return;
        }
        Source sourceA = left, sourceB = right;
        const QString state = change.status.trimmed();
        if (state.startsWith('A') || state.startsWith('?')) sourceA = Source::empty();
        if (state.startsWith('D')) sourceB = Source::empty();
        const QString pathA = change.oldPath.isEmpty() ? change.path : change.oldPath;
        const QString pathB = change.path;
        const Repository repo = repository;
        const QString labelA = sourceA.kind == SourceKind::Empty ? QStringLiteral("空版本（新增文件）") : leftLabel;
        const QString labelB = sourceB.kind == SourceKind::Empty ? QStringLiteral("空版本（已删除文件）") : rightLabel;
        run(QStringLiteral("正在读取只读比较快照…"),
            [repo, pathA, pathB, sourceA, sourceB, labelA, labelB](const Backend &backend, RepositoryCache &, const std::atomic_bool *cancel) {
                WorkResult result;
                const auto comparison = backend.compare(repo, pathA, sourceA, pathB, sourceB, cancel);
                result.error = comparison.error;
                result.comparison = comparison.value;
                if (comparison.ok()) {
                    result.comparison.leftLabel = labelA + QStringLiteral(" · ") + pathA;
                    result.comparison.rightLabel = labelB + QStringLiteral(" · ") + pathB;
                    result.comparison.title = pathB + QStringLiteral(" · ") + labelA + QStringLiteral(" → ") + labelB;
                }
                return result;
            }, [this](const WorkResult &result) {
                setStatus(QStringLiteral("只读比较已就绪：%1").arg(result.comparison.title));
                emit q->compareRequested(result.comparison);
            });
    }

    void configureMode()
    {
        revisions->setVisible(currentMode == Revisions);
        historyPanel->setVisible(currentMode == History);
        details->setVisible(currentMode == History);
        if (currentMode == History)
            description->setText(QStringLiteral("选择提交查看相对第一个父提交的变更；根提交相对空版本。"));
        else if (currentMode == Head) description->setText(QStringLiteral("HEAD → 工作副本 · 只读比较"));
        else if (currentMode == Index) description->setText(QStringLiteral("暂存区（Index）→ 工作副本 · 只读比较"));
        else description->setText(QStringLiteral("左侧修订 → 右侧修订 · 直接比较两个提交，只读"));
    }

    void buildUi(const QString &initialPath)
    {
        auto *layout = new QVBoxLayout(q);
        auto *pathRow = new QHBoxLayout;
        pathRow->addWidget(new QLabel(QStringLiteral("仓库路径"), q));
        path = new QLineEdit(initialPath, q);
        path->setObjectName(QStringLiteral("vcsPath"));
        path->setPlaceholderText(QStringLiteral("Git 仓库内的文件或目录"));
        pathRow->addWidget(path, 1);
        auto *browse = new QPushButton(QStringLiteral("选择目录…"), q);
        pathRow->addWidget(browse);
        refreshButton = new QPushButton(QStringLiteral("刷新"), q);
        refreshButton->setObjectName(QStringLiteral("vcsRefresh"));
        pathRow->addWidget(refreshButton);
        cancelButton = new QPushButton(QStringLiteral("取消"), q);
        cancelButton->setObjectName(QStringLiteral("vcsCancel"));
        pathRow->addWidget(cancelButton);
        layout->addLayout(pathRow);

        auto *modeRow = new QHBoxLayout;
        mode = new QComboBox(q);
        mode->setObjectName(QStringLiteral("vcsMode"));
        mode->addItems({QStringLiteral("与 HEAD 比对"), QStringLiteral("与暂存区比对"),
                        QStringLiteral("比较两个修订"), QStringLiteral("提交日志")});
        modeRow->addWidget(mode);
        revisions = new QWidget(q);
        auto *revisionRow = new QHBoxLayout(revisions);
        revisionRow->setContentsMargins(0, 0, 0, 0);
        revisionRow->addWidget(new QLabel(QStringLiteral("左修订"), revisions));
        leftRevision = new QLineEdit(QStringLiteral("HEAD~1"), revisions);
        leftRevision->setObjectName(QStringLiteral("vcsLeftRevision"));
        leftRevision->setPlaceholderText(QStringLiteral("提交哈希、分支、标签或 HEAD~2"));
        revisionRow->addWidget(leftRevision, 1);
        revisionRow->addWidget(new QLabel(QStringLiteral("右修订"), revisions));
        rightRevision = new QLineEdit(QStringLiteral("HEAD"), revisions);
        rightRevision->setObjectName(QStringLiteral("vcsRightRevision"));
        revisionRow->addWidget(rightRevision, 1);
        modeRow->addWidget(revisions, 1);
        modeRow->addStretch();
        layout->addLayout(modeRow);
        description = new QLabel(q);
        description->setObjectName(QStringLiteral("vcsDirection"));
        description->setTextFormat(Qt::PlainText);
        description->setWordWrap(true);
        layout->addWidget(description);

        auto *splitter = new QSplitter(Qt::Horizontal, q);
        historyPanel = new QWidget(splitter);
        auto *historyLayout = new QVBoxLayout(historyPanel);
        historyLayout->setContentsMargins(0, 0, 0, 0);
        history = new QTreeWidget(historyPanel);
        history->setObjectName(QStringLiteral("vcsCommits"));
        history->setHeaderLabels({QStringLiteral("修订"), QStringLiteral("作者"), QStringLiteral("日期"), QStringLiteral("消息")});
        history->setRootIsDecorated(false);
        history->setUniformRowHeights(true);
        history->setColumnWidth(0, 110);
        history->setColumnWidth(1, 120);
        history->setColumnWidth(2, 165);
        historyLayout->addWidget(history);
        loadMoreButton = new QPushButton(QStringLiteral("加载更多"), historyPanel);
        loadMoreButton->setObjectName(QStringLiteral("vcsLoadMore"));
        historyLayout->addWidget(loadMoreButton);

        auto *rightPanel = new QWidget(splitter);
        auto *rightLayout = new QVBoxLayout(rightPanel);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        details = new QPlainTextEdit(rightPanel);
        details->setObjectName(QStringLiteral("vcsCommitDetails"));
        details->setReadOnly(true);
        details->setMaximumHeight(220);
        rightLayout->addWidget(details);
        files = new QTreeWidget(rightPanel);
        files->setObjectName(QStringLiteral("vcsChanges"));
        files->setHeaderLabels({QStringLiteral("状态"), QStringLiteral("旧路径"), QStringLiteral("路径")});
        files->setRootIsDecorated(false);
        files->setUniformRowHeights(true);
        files->setColumnWidth(0, 90);
        files->setColumnWidth(1, 190);
        files->header()->setStretchLastSection(true);
        rightLayout->addWidget(files, 1);
        compareButton = new QPushButton(QStringLiteral("打开所选文件的只读比较"), rightPanel);
        compareButton->setObjectName(QStringLiteral("vcsCompare"));
        rightLayout->addWidget(compareButton);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        layout->addWidget(splitter, 1);

        auto *statusRow = new QHBoxLayout;
        status = new QLabel(q);
        status->setObjectName(QStringLiteral("vcsStatus"));
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        statusRow->addWidget(status, 1);
        progress = new QProgressBar(q);
        progress->setRange(0, 0);
        progress->setMaximumWidth(130);
        statusRow->addWidget(progress);
        layout->addLayout(statusRow);

        QObject::connect(refreshButton, &QPushButton::clicked, q, &VcsView::refresh);
        QObject::connect(cancelButton, &QPushButton::clicked, q, &VcsView::cancel);
        QObject::connect(path, &QLineEdit::returnPressed, q, &VcsView::refresh);
        QObject::connect(leftRevision, &QLineEdit::returnPressed, q, &VcsView::refresh);
        QObject::connect(rightRevision, &QLineEdit::returnPressed, q, &VcsView::refresh);
        QObject::connect(browse, &QPushButton::clicked, q, [this] {
            const QString directory = QFileDialog::getExistingDirectory(q, QStringLiteral("选择 Git 仓库内的目录"), path->text());
            if (!directory.isEmpty()) q->setPath(directory);
        });
        QObject::connect(mode, QOverload<int>::of(&QComboBox::currentIndexChanged), q, [this](int index) {
            q->setMode(static_cast<Mode>(index));
        });
        QObject::connect(files, &QTreeWidget::itemSelectionChanged, q, [this] { updateControls(); });
        QObject::connect(files, &QTreeWidget::itemDoubleClicked, q, [this] { compare(); });
        QObject::connect(compareButton, &QPushButton::clicked, q, [this] { compare(); });
        QObject::connect(history, &QTreeWidget::itemSelectionChanged, q, [this] { selectCommit(); });
        QObject::connect(loadMoreButton, &QPushButton::clicked, q, [this] { loadMore(); });
        QObject::connect(history->verticalScrollBar(), &QScrollBar::valueChanged, q, [this](int value) {
            if (value > 0 && value == history->verticalScrollBar()->maximum()) loadMore();
        });
        configureMode();
        updateControls();
    }
};

VcsView::VcsView(const QString &path, QWidget *parent)
    : VcsView(path, QSharedPointer<Vcs::GitBackend>::create(), parent)
{}

VcsView::VcsView(const QString &path, QSharedPointer<Vcs::Backend> backend, QWidget *parent)
    : QWidget(parent), d(new Private(this, backend ? std::move(backend) : QSharedPointer<Vcs::GitBackend>::create()))
{
    qRegisterMetaType<Vcs::Comparison>();
    d->buildUi(path);
    QTimer::singleShot(0, this, [this] { if (!d->started) refresh(); });
}

VcsView::~VcsView() = default;
VcsView::Mode VcsView::mode() const { return d->currentMode; }
QString VcsView::path() const { return d->path->text(); }
bool VcsView::isBusy() const { return d->busy; }

void VcsView::setMode(Mode mode)
{
    if (mode < Head || mode > History || d->currentMode == mode) return;
    d->currentMode = mode;
    const QSignalBlocker blocker(d->mode);
    d->mode->setCurrentIndex(mode);
    d->configureMode();
    // 切模式**不丢**探测缓存：同一个仓库、同一个路径，只是在问它另一个问题，
    // 重探一次要起一个 git 进程（VCS-001 第 4 条）。用户显式按「重新查询」
    // 才丢缓存，见 `VcsView::refresh()`。走 `d->refresh()` 而不是 `refresh()`，
    // 差的就是这一处失效。
    d->refresh();
}

void VcsView::setPath(const QString &path)
{
    if (d->path->text() == path) return;
    d->path->setText(path);
    refresh();
}

void VcsView::refresh()
{
    // 显式「重新查询」= 我不相信上一次的结论。这就是 VCS-001 第 4 条里
    // 「路径变化时失效」的另一半：缓存必须有一条**被用户按下去的**失效路径，
    // 否则用户在刚 `git init` 的目录里点刷新，会一直拿到那条被缓存起来的
    // 「这不是仓库」——而且看不出是缓存，只会以为刷新按钮坏了。
    //
    // 只失效当前路径，不整表清空：同一个视图里换过路径之后，别的路径的结论仍然成立。
    d->cache->invalidate(d->path->text());
    d->refresh();
}
void VcsView::cancel() { d->stop(true); }
}
