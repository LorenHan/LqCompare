#include "blameview.h"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextCodec>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <functional>

namespace LqCompare {
namespace {
using namespace Vcs;
constexpr int MarkerRole = Qt::UserRole + 1;
constexpr int BlockStartRole = Qt::UserRole + 2;

bool validCommit(const QString &commit)
{
    return QRegularExpression(QStringLiteral("^[0-9a-f]{40,64}$")).match(commit).hasMatch()
        && commit.count(QLatin1Char('0')) != commit.size();
}
QString errorText(const Error &error)
{
    if (error.code == ErrorCode::Unavailable) return QStringLiteral("未检测到 git 可执行文件，请安装 Git 后重新打开追溯视图。");
    if (error.code == ErrorCode::NoHead) return QStringLiteral("仓库尚无 HEAD 提交，未提交的内容无法追溯。");
    if (error.code == ErrorCode::Cancelled) return QStringLiteral("追溯查询已取消。");
    if (error.code == ErrorCode::Timeout) return QStringLiteral("追溯查询超时，请重试或增加 Git 超时设置。");
    return error.message.isEmpty() ? QStringLiteral("无法读取追溯结果。") : error.message;
}
Result<QString> scopedPath(const Repository &repo, const QString &path)
{
    Result<QString> result;
    const QFileInfo info(path);
    if (info.isDir()) {
        result.error = {ErrorCode::InvalidPath, QStringLiteral("追溯需要一个文件，请选择仓库内的文件。"), path};
        return result;
    }
    QString absolute = info.absoluteFilePath();
    QString ancestor = info.absolutePath();
    while (!QFileInfo::exists(ancestor)) {
        const QString next = QFileInfo(ancestor).absolutePath();
        if (next == ancestor) break;
        ancestor = next;
    }
    const QString canonical = QFileInfo(ancestor).canonicalFilePath();
    if (!canonical.isEmpty()) absolute = canonical + QLatin1Char('/') + QDir(ancestor).relativeFilePath(absolute);
    const auto canonicalRoot = QFileInfo(repo.root).canonicalFilePath();
    result.value = QDir(canonicalRoot.isEmpty() ? repo.root : canonicalRoot).relativeFilePath(absolute);
    if (result.value.isEmpty() || result.value == QStringLiteral(".") || result.value == QStringLiteral("..")
        || result.value.startsWith(QStringLiteral("../")) || result.value.contains(QChar::Null)
        || (!result.value.startsWith(QLatin1Char(':')) && QDir::isAbsolutePath(result.value)))
        result.error = {ErrorCode::InvalidPath, QStringLiteral("文件必须位于所选 Git 仓库内。"), path};
    return result;
}
uint stableHash(const QString &value)
{
    uint hash = 2166136261u;
    for (const QChar ch : value) { hash ^= ch.unicode(); hash *= 16777619u; }
    return hash;
}

// Only visible row spans are indexed; no item widgets or duplicated line text
// are created, even for a large file. Blocks always follow original adjacency.
class BlameModel final : public QAbstractTableModel {
public:
    struct Span { int first = 0, count = 1, block = 0; };
    explicit BlameModel(QObject *parent) : QAbstractTableModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : rows.size(); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 6; }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        const QStringList labels{QString(), QStringLiteral("行"), QStringLiteral("修订"), QStringLiteral("作者"), QStringLiteral("日期"), QStringLiteral("内容（只读）")};
        return labels.value(section);
    }
    Qt::ItemFlags flags(const QModelIndex &index) const override
    { return index.isValid() ? Qt::ItemIsSelectable | Qt::ItemIsEnabled : Qt::NoItemFlags; }
    const BlameLine *lineAt(int row) const
    { return row >= 0 && row < rows.size() ? &lines.at(rows.at(row).first) : nullptr; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        const auto *line = lineAt(index.row());
        if (!index.isValid() || !line) return {};
        const auto &span = rows.at(index.row());
        if (role == Qt::UserRole) return line->finalLine;
        if (role == BlockStartRole) return index.row() == 0 || rows.at(index.row() - 1).block != span.block;
        if (role == MarkerRole) return QColor::fromHsv(int(stableHash(line->commit) % 360), 150, 165);
        if (role == Qt::ForegroundRole) return QColor(QStringLiteral("#172b3a"));
        if (role == Qt::BackgroundRole) {
            if (mode == BlameView::Age) {
                const qint64 date = line->date.toSecsSinceEpoch();
                const double age = newest > oldest && line->date.isValid() ? std::clamp(double(newest - date) / double(newest - oldest), 0.0, 1.0) : 0.0;
                return QColor(int(246 - age * 64), int(250 - age * 39), int(253 - age * 20));
            }
            static const QColor palette[] = {QColor("#e8f1fc"), QColor("#eaf5e8"), QColor("#fff1d9"), QColor("#f3eafa"), QColor("#dff4f3"), QColor("#fbe9e8")};
            return palette[(mode == BlameView::Author ? stableHash(line->author) : uint(span.block)) % 6];
        }
        if (role == Qt::FontRole) return QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (role == Qt::ToolTipRole) {
            return QStringLiteral("修订：%1\n作者：%2 <%3>\n日期：%4\n原始路径：%5\n原始行：%6\n%7%8")
                .arg(line->commit, line->author, line->email, line->date.toLocalTime().toString(Qt::ISODate),
                     line->originalPath.isEmpty() ? QStringLiteral("未知（不可打开历史差异）") : line->originalPath)
                .arg(line->originalLine).arg(line->summary, span.count > 1 ? QStringLiteral("\n此块折叠了 %1 行。").arg(span.count) : QString());
        }
        if (role == Qt::TextAlignmentRole && index.column() == 1) return int(Qt::AlignRight | Qt::AlignVCenter);
        if (role != Qt::DisplayRole) return {};
        switch (index.column()) {
        // 首列是窄栏：颜色由委托按作者/日期龄/提交块绘制，文字只标出「这一行
        // 开启了一个新的提交块」。留空会让首列在无选中时看起来像坏了，
        // 也让「块从哪里开始」只能靠颜色深浅去猜。
        case 0: return index.data(BlockStartRole).toBool() ? QStringLiteral("▌") : QStringLiteral("│");
        case 1: return span.count > 1 ? QStringLiteral("%1–%2").arg(line->finalLine).arg(lines.at(span.first + span.count - 1).finalLine) : QString::number(line->finalLine);
        case 2: return validCommit(line->commit) ? line->commit.left(12) : QStringLiteral("未提交");
        case 3: return line->author;
        case 4: return line->date.isValid() ? line->date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd")) : QStringLiteral("未知");
        case 5: return line->text + (span.count > 1 ? QStringLiteral("  …（%1 行；取消折叠查看全文）").arg(span.count) : QString());
        }
        return {};
    }
    void setLines(QVector<BlameLine> value)
    {
        lines = std::move(value);
        oldest = newest = 0;
        for (const auto &line : lines) if (line.date.isValid()) {
            const auto seconds = line.date.toSecsSinceEpoch();
            if (!oldest || seconds < oldest) oldest = seconds;
            if (!newest || seconds > newest) newest = seconds;
        }
        rebuild();
    }
    void setMode(BlameView::ColorMode value)
    {
        mode = value;
        if (!rows.isEmpty()) emit dataChanged(index(0, 0), index(rows.size() - 1, 5), {Qt::BackgroundRole});
    }
    void rebuild()
    {
        beginResetModel();
        rows.clear();
        int block = 0;
        for (int first = 0; first < lines.size(); ++block) {
            int end = first + 1;
            while (end < lines.size() && lines[end].commit == lines[first].commit
                   && lines[end].originalPath == lines[first].originalPath
                   && lines[end].finalLine == lines[end - 1].finalLine + 1) ++end;
            const bool visible = (author.isEmpty() || lines[first].author == author)
                              && (commitPrefix.isEmpty() || lines[first].commit.startsWith(commitPrefix, Qt::CaseInsensitive));
            if (visible) {
                if (fold) rows.append({first, end - first, block});
                else for (int i = first; i < end; ++i) rows.append({i, 1, block});
            }
            first = end;
        }
        endResetModel();
    }
    QVector<BlameLine> lines;
    QVector<Span> rows;
    BlameView::ColorMode mode = BlameView::Author;
    QString author, commitPrefix;
    bool fold = false;
    qint64 oldest = 0, newest = 0;
};

class BlameDelegate final : public QStyledItemDelegate {
public:
    explicit BlameDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        painter->save();
        if (index.column() == 0) {
            painter->fillRect(QRect(option.rect.left() + 4, option.rect.top() + 2, 5, option.rect.height() - 4), index.data(MarkerRole).value<QColor>());
        }
        if (index.data(BlockStartRole).toBool()) {
            painter->setPen(QColor(QStringLiteral("#667986")));
            painter->drawLine(option.rect.topLeft(), option.rect.topRight());
        }
        painter->restore();
    }
};
struct WorkResult {
    Error error;
    Repository repository;
    QString relativePath, resolvedRevision;
    QVector<BlameLine> lines;
    Comparison comparison;
};
}

class BlameView::Private {
public:
    Private(BlameView *owner, QSharedPointer<Backend> value) : q(owner), backend(std::move(value)) {}
    ~Private() { if (token) token->store(true); }
    BlameView *q;
    QSharedPointer<Backend> backend;
    QSharedPointer<std::atomic_bool> token;
    quint64 generation = 0;
    bool busy = false, started = false, available = true, ready = false;
    Repository repository;
    QString relativePath, resolvedRevision;
    QLineEdit *path = nullptr, *revision = nullptr, *commitFilter = nullptr;
    QComboBox *color = nullptr, *author = nullptr;
    QCheckBox *fold = nullptr;
    QPushButton *refreshButton = nullptr, *cancelButton = nullptr, *openButton = nullptr, *compareButton = nullptr;
    QLabel *status = nullptr, *description = nullptr;
    QProgressBar *progress = nullptr;
    QPlainTextEdit *details = nullptr;
    QTableView *table = nullptr;
    BlameModel *model = nullptr;

    void setStatus(const QString &message) { status->setText(message); emit q->statusChanged(message); }
    const BlameLine *selectedLine() const { return model->lineAt(table->currentIndex().row()); }
    void controls()
    {
        const auto *line = selectedLine();
        const bool navigable = line && validCommit(line->commit) && !line->originalPath.isEmpty();
        refreshButton->setEnabled(available);
        cancelButton->setEnabled(busy);
        progress->setVisible(busy);
        openButton->setEnabled(available && ready && !busy && navigable);
        compareButton->setEnabled(available && ready && !busy && navigable);
        color->setEnabled(available && ready);
        author->setEnabled(available && ready);
        commitFilter->setEnabled(available && ready);
        fold->setEnabled(available && ready);
        const QString tooltip = available ? QString() : QStringLiteral("未检测到 git 可执行文件");
        refreshButton->setToolTip(tooltip); openButton->setToolTip(tooltip); compareButton->setToolTip(tooltip);
    }
    void showError(const Error &error)
    {
        if (error.code == ErrorCode::Unavailable) available = false;
        status->setToolTip(error.detail);
        const auto message = errorText(error);
        setStatus(message); controls();
        if (error.code != ErrorCode::Cancelled) emit q->errorOccurred(message);
    }
    void stop(bool report)
    {
        if (token) token->store(true);
        ++generation;
        const bool previous = busy;
        busy = false;
        controls();
        if (previous && report) setStatus(QStringLiteral("追溯查询已取消。"));
    }
    void run(const QString &message, std::function<WorkResult(const Backend &, const std::atomic_bool *)> work,
             std::function<void(const WorkResult &)> complete)
    {
        stop(false);
        token = QSharedPointer<std::atomic_bool>::create(false);
        const auto currentToken = token;
        const auto currentGeneration = generation;
        const auto backendCopy = backend;
        busy = true;
        status->setToolTip({});
        setStatus(message); controls();
        auto *watcher = new QFutureWatcher<WorkResult>(q);
        QObject::connect(watcher, &QFutureWatcher<WorkResult>::finished, q,
                         [this, watcher, currentToken, currentGeneration, complete] {
            const auto result = watcher->result();
            watcher->deleteLater();
            if (currentGeneration != generation || currentToken->load()) return;
            busy = false; controls();
            if (result.error.isError()) { showError(result.error); return; }
            complete(result);
        });
        watcher->setFuture(QtConcurrent::run([backendCopy, currentToken, work] {
            if (currentToken->load()) { WorkResult result; result.error = {ErrorCode::Cancelled, {}, {}}; return result; }
            return work(*backendCopy, currentToken.data());
        }));
    }
    void selectionChanged()
    {
        const auto *line = selectedLine();
        if (!line) details->clear();
        else details->setPlainText(QStringLiteral("修订：%1\n作者：%2 <%3>\n日期：%4\n历史路径：%5 · 原始行：%6\n%7\n\n%8")
            .arg(line->commit, line->author, line->email, line->date.toLocalTime().toString(Qt::ISODate), line->originalPath)
            .arg(line->originalLine).arg(line->summary,
                !validCommit(line->commit) ? QStringLiteral("此行未提交，无法打开修订或父提交差异。")
                : line->originalPath.isEmpty() ? QStringLiteral("缺少历史路径，无法安全识别重命名前的文件。")
                : QStringLiteral("点击修订号或“打开所选行修订”跳转日志；父差异以该归因提交的第一个父提交为基线。")));
        controls();
    }
    void filterChanged()
    {
        const auto *selected = selectedLine();
        const int selectedNumber = selected ? selected->finalLine : -1;
        model->author = author->currentData().toString();
        model->commitPrefix = commitFilter->text().trimmed();
        model->fold = fold->isChecked();
        model->rebuild();
        int target = 0;
        for (int row = 0; row < model->rowCount(); ++row) {
            if (model->lineAt(row)->finalLine == selectedNumber) { target = row; break; }
        }
        if (model->rowCount()) table->setCurrentIndex(model->index(target, 1));
        selectionChanged();
        if (ready) setStatus(QStringLiteral("共 %1 行 · 当前显示 %2 %3").arg(model->lines.size()).arg(model->rowCount()).arg(model->fold ? QStringLiteral("个提交块") : QStringLiteral("行")));
    }
    void refresh()
    {
        started = true; available = true; ready = false;
        model->setLines({}); details->clear();
        const QString requestedPath = path->text();
        const QString requestedRevision = revision->text().trimmed();
        if (requestedPath.isEmpty() || requestedRevision.isEmpty()) {
            stop(false); setStatus(QStringLiteral("请输入文件路径和终点修订（例如 HEAD）。")); return;
        }
        run(QStringLiteral("正在读取文件追溯…"),
            [requestedPath, requestedRevision](const Backend &backend, const std::atomic_bool *cancel) {
                WorkResult result;
                result.error = backend.availability();
                if (result.error.isError()) return result;
                const auto detected = backend.detectRepo(requestedPath, cancel);
                if (!detected.ok()) { result.error = detected.error; return result; }
                result.repository = detected.value;
                if (!detected.value.hasHead && requestedRevision == QStringLiteral("HEAD")) {
                    result.error = {ErrorCode::NoHead, {}, {}}; return result;
                }
                const auto relative = scopedPath(detected.value, requestedPath);
                if (!relative.ok()) { result.error = relative.error; return result; }
                result.relativePath = relative.value;
                LogQuery query; query.revision = requestedRevision; query.limit = 1;
                const auto log = backend.log(detected.value, query, cancel);
                if (!log.ok()) { result.error = log.error; return result; }
                if (log.value.isEmpty()) { result.error = {ErrorCode::InvalidRevision, QStringLiteral("所选修订没有提交记录。"), requestedRevision}; return result; }
                result.resolvedRevision = log.value.first().id;
                const auto content = backend.catFile(detected.value, relative.value, Source::at(result.resolvedRevision), cancel);
                if (!content.ok()) { result.error = content.error; return result; }
                if (!content.value.exists) {
                    result.error = {ErrorCode::InvalidPath, QStringLiteral("文件在所选修订中不存在：可能尚未提交、已删除或使用了重命名后的路径；请选择该修订中的文件路径。"), relative.value}; return result;
                }
                if (content.value.binary) { result.error = {ErrorCode::Unsupported, QStringLiteral("二进制文件无法逐行追溯，请使用十六进制比较。"), relative.value}; return result; }
                // 追溯的行号来自 git 按字节切分的 blob 行；若界面显示的是一份
                // 「坏字节被替换成 U+FFFD」的文本，行号与用户看到的行就会错位，
                // 于是「跳到第 N 行的提交」会跳到别的行。宁可在入口拒绝。
                // QTextCodec 的 ConverterState 是 Qt 5.15 上唯一能区分
                // 「无效字节」与「真的是 U+FFFD」的途径（见 §6 的坑表）。
                QTextCodec::ConverterState state;
                QTextCodec::codecForName("UTF-8")
                    ->toUnicode(content.value.bytes.constData(), content.value.bytes.size(), &state);
                if (state.invalidChars > 0) {
                    result.error = {ErrorCode::Unsupported,
                                    QStringLiteral("文件不是有效的 UTF-8 文本（%1 个无效字节），无法逐行追溯。").arg(state.invalidChars),
                                    relative.value};
                    return result;
                }
                if (content.value.bytes.isEmpty()) return result;
                const auto blame = backend.blame(detected.value, relative.value, result.resolvedRevision, cancel);
                result.error = blame.error;
                result.lines = blame.value;
                return result;
            }, [this, requestedRevision](const WorkResult &result) {
                repository = result.repository; relativePath = result.relativePath; resolvedRevision = result.resolvedRevision; ready = true;
                description->setText(QStringLiteral("%1 · %2 (%3) · 历史快照，不包含未提交改动。重命名使用 Git 返回的历史路径。")
                                     .arg(relativePath, requestedRevision, resolvedRevision.left(12)));
                const auto oldAuthor = author->currentData().toString();
                {
                    QSignalBlocker blocker(author);
                    author->clear(); author->addItem(QStringLiteral("全部作者"), QString());
                    QStringList authors;
                    for (const auto &line : result.lines) if (!authors.contains(line.author)) authors.append(line.author);
                    authors.sort(Qt::CaseInsensitive);
                    for (const auto &name : authors) author->addItem(name, name);
                    const int previous = author->findData(oldAuthor);
                    if (previous >= 0) author->setCurrentIndex(previous);
                }
                model->setLines(result.lines); filterChanged();
                if (result.lines.isEmpty()) setStatus(QStringLiteral("所选修订中的文件为空，没有可追溯的行。"));
                controls();
            });
    }
    bool selected(BlameLine *line)
    {
        if (busy || !ready) return false;
        const auto *value = selectedLine();
        if (!value) { showError({ErrorCode::InvalidPath, QStringLiteral("请选择一个可追溯的行。"), {}}); return false; }
        if (!validCommit(value->commit)) { showError({ErrorCode::InvalidRevision, QStringLiteral("此行尚未提交，无法打开历史修订。"), {}}); return false; }
        if (value->originalPath.isEmpty()) { showError({ErrorCode::InvalidPath, QStringLiteral("缺少该行的历史路径，无法安全映射重命名文件。"), {}}); return false; }
        *line = *value; return true;
    }
    void openRevision()
    {
        BlameLine line;
        if (!selected(&line)) return;
        emit q->revisionRequested(repository.root, line.commit, line.originalPath, line.originalLine);
    }
    void compareParent()
    {
        BlameLine line;
        if (!selected(&line)) return;
        const auto repo = repository;
        run(QStringLiteral("正在读取所选行的父提交差异…"),
            [repo, line](const Backend &backend, const std::atomic_bool *cancel) {
                WorkResult result;
                LogQuery query; query.revision = line.commit; query.limit = 1;
                const auto log = backend.log(repo, query, cancel);
                if (!log.ok()) { result.error = log.error; return result; }
                if (log.value.isEmpty() || log.value.first().id != line.commit) {
                    result.error = {ErrorCode::InvalidRevision, QStringLiteral("无法确认所选行的归因提交。"), line.commit}; return result;
                }
                const auto commit = log.value.first();
                Source left = commit.parents.isEmpty() ? Source::empty() : Source::at(commit.parents.first());
                const auto right = Source::at(commit.id);
                const auto changes = backend.diff(repo, left, right, cancel);
                if (!changes.ok()) { result.error = changes.error; return result; }
                QVector<Change> matches;
                for (const auto &change : changes.value) if (change.path == line.originalPath) matches.append(change);
                if (matches.size() != 1 || matches.first().status.startsWith('D')) {
                    result.error = {ErrorCode::InvalidPath, QStringLiteral("归因提交的变更清单无法唯一匹配历史路径，不能安全猜测重命名映射。请打开该修订检查。"), line.originalPath}; return result;
                }
                const auto &change = matches.first();
                const QString leftPath = change.oldPath.isEmpty() ? line.originalPath : change.oldPath;
                if (change.status.startsWith('A')) left = Source::empty();
                // 不再单独 catFile 探测右侧是否存在：Backend::compare 本身就把
                // 「不存在的一侧」写成空快照并在标签里标 [不存在]，再探一次只是
                // 多一次 Git 进程，而且会把「归因提交里确实没有这个路径」变成
                // 一个硬错误——那属于要靠标签 + 用户判断的情况，不是可判定错误。
                const auto comparison = backend.compare(repo, leftPath, left, line.originalPath, right, cancel);
                result.error = comparison.error; result.comparison = comparison.value;
                if (comparison.ok()) result.comparison.title = QStringLiteral("%1 · %2 → %3 · 归因行 %4")
                    .arg(line.originalPath, left.kind == SourceKind::Empty ? QStringLiteral("空版本") : QStringLiteral("第一个父提交 ") + left.revision.left(12), commit.id.left(12)).arg(line.originalLine);
                return result;
            }, [this](const WorkResult &result) {
                setStatus(QStringLiteral("只读父提交差异已就绪：%1").arg(result.comparison.title));
                emit q->compareRequested(result.comparison);
            });
    }

    void applyColorMode(BlameView::ColorMode value)
    {
        if (value < BlameView::Author || value > BlameView::CommitBlock) return;
        model->setMode(value);
        // 组合框的条目顺序与枚举同序，切换模式只影响呈现、不改变行集合，
        // 所以这里不需要 rebuild()——重建会让用户的选中行与滚动位置全部丢失。
        const QSignalBlocker blocker(color);
        color->setCurrentIndex(int(value));
    }

    void buildUi(const QString &initialPath)
    {
        auto *layout = new QVBoxLayout(q);

        auto *pathRow = new QHBoxLayout;
        pathRow->addWidget(new QLabel(QStringLiteral("文件"), q));
        path = new QLineEdit(initialPath, q);
        path->setObjectName(QStringLiteral("blamePath"));
        path->setPlaceholderText(QStringLiteral("仓库内要追溯的文件"));
        pathRow->addWidget(path, 1);
        auto *browse = new QPushButton(QStringLiteral("选择文件…"), q);
        pathRow->addWidget(browse);
        // 「追至修订」是 blame 的上界：git blame <rev> 只追到该提交为止，
        // 而不是「只显示该提交改过的行」。标签特意不写成「只看某提交」，
        // 免得用户把两个语义搞混（后者是下面的提交前缀过滤）。
        pathRow->addWidget(new QLabel(QStringLiteral("追至修订"), q));
        revision = new QLineEdit(QStringLiteral("HEAD"), q);
        revision->setObjectName(QStringLiteral("blameRevision"));
        revision->setPlaceholderText(QStringLiteral("HEAD、提交哈希、分支或标签"));
        revision->setMaximumWidth(220);
        pathRow->addWidget(revision);
        refreshButton = new QPushButton(QStringLiteral("刷新"), q);
        refreshButton->setObjectName(QStringLiteral("blameRefresh"));
        pathRow->addWidget(refreshButton);
        cancelButton = new QPushButton(QStringLiteral("取消"), q);
        cancelButton->setObjectName(QStringLiteral("blameCancel"));
        pathRow->addWidget(cancelButton);
        layout->addLayout(pathRow);

        auto *filterRow = new QHBoxLayout;
        filterRow->addWidget(new QLabel(QStringLiteral("着色"), q));
        color = new QComboBox(q);
        color->setObjectName(QStringLiteral("blameColorMode"));
        color->addItems({QStringLiteral("按作者"), QStringLiteral("按日期龄（越旧越深）"),
                         QStringLiteral("按提交块")});
        filterRow->addWidget(color);
        filterRow->addWidget(new QLabel(QStringLiteral("只看作者"), q));
        author = new QComboBox(q);
        author->setObjectName(QStringLiteral("blameAuthorFilter"));
        author->addItem(QStringLiteral("全部作者"), QString());
        filterRow->addWidget(author);
        filterRow->addWidget(new QLabel(QStringLiteral("只看提交前缀"), q));
        commitFilter = new QLineEdit(q);
        commitFilter->setObjectName(QStringLiteral("blameCommitFilter"));
        commitFilter->setPlaceholderText(QStringLiteral("提交哈希前缀"));
        commitFilter->setMaximumWidth(160);
        filterRow->addWidget(commitFilter);
        fold = new QCheckBox(QStringLiteral("合并同一提交的连续行"), q);
        fold->setObjectName(QStringLiteral("blameFoldBlocks"));
        filterRow->addWidget(fold);
        filterRow->addStretch();
        layout->addLayout(filterRow);

        description = new QLabel(q);
        description->setObjectName(QStringLiteral("blameDescription"));
        description->setTextFormat(Qt::PlainText);
        description->setWordWrap(true);
        layout->addWidget(description);

        model = new BlameModel(q);
        table = new QTableView(q);
        table->setObjectName(QStringLiteral("blameLines"));
        table->setModel(model);
        table->setItemDelegate(new BlameDelegate(table));
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setWordWrap(false);
        table->setAlternatingRowColors(false);
        table->setShowGrid(false);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setColumnWidth(0, 16);
        table->setColumnWidth(1, 74);
        table->setColumnWidth(2, 128);
        table->setColumnWidth(3, 130);
        table->setColumnWidth(4, 100);
        layout->addWidget(table, 1);

        details = new QPlainTextEdit(q);
        details->setObjectName(QStringLiteral("blameDetails"));
        details->setReadOnly(true);
        details->setMaximumHeight(150);
        layout->addWidget(details);

        auto *actionRow = new QHBoxLayout;
        openButton = new QPushButton(QStringLiteral("跳转到所选行的修订日志"), q);
        openButton->setObjectName(QStringLiteral("blameOpenRevision"));
        actionRow->addWidget(openButton);
        compareButton = new QPushButton(QStringLiteral("与所选行的父提交比较"), q);
        compareButton->setObjectName(QStringLiteral("blameCompareParent"));
        actionRow->addWidget(compareButton);
        actionRow->addStretch();
        layout->addLayout(actionRow);

        auto *statusRow = new QHBoxLayout;
        status = new QLabel(q);
        status->setObjectName(QStringLiteral("blameStatus"));
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        status->setTextInteractionFlags(Qt::TextSelectableByMouse);
        statusRow->addWidget(status, 1);
        progress = new QProgressBar(q);
        // blame 是单次 git 调用，拿不到中间进度；用不定长进度条表达「在跑」，
        // 而不是编一个百分比——一个假的 30% 比没有进度更误导。
        progress->setRange(0, 0);
        progress->setMaximumWidth(130);
        statusRow->addWidget(progress);
        layout->addLayout(statusRow);

        QObject::connect(refreshButton, &QPushButton::clicked, q, &BlameView::refresh);
        QObject::connect(cancelButton, &QPushButton::clicked, q, &BlameView::cancel);
        QObject::connect(path, &QLineEdit::returnPressed, q, &BlameView::refresh);
        QObject::connect(revision, &QLineEdit::returnPressed, q, &BlameView::refresh);
        QObject::connect(openButton, &QPushButton::clicked, q, [this] { openRevision(); });
        QObject::connect(compareButton, &QPushButton::clicked, q, [this] { compareParent(); });
        QObject::connect(browse, &QPushButton::clicked, q, [this] {
            const QString file = QFileDialog::getOpenFileName(q, QStringLiteral("选择要追溯的文件"), path->text());
            if (!file.isEmpty()) q->setPath(file);
        });
        QObject::connect(color, QOverload<int>::of(&QComboBox::currentIndexChanged), q,
                         [this](int index) { applyColorMode(static_cast<BlameView::ColorMode>(index)); });
        QObject::connect(author, QOverload<int>::of(&QComboBox::currentIndexChanged), q,
                         [this] { filterChanged(); });
        QObject::connect(commitFilter, &QLineEdit::textChanged, q, [this] { filterChanged(); });
        QObject::connect(fold, &QCheckBox::stateChanged, q, [this] { filterChanged(); });
        // 单击只更新下方详情，不自动跳转日志：浏览追溯表时按行读详情是高频动作，
        // 若每次点行都把应用切到日志视图，用户就没法连续看几行了。
        // 跳转保留两个显式入口——按钮与双击，语义上都是「我要去这个提交」。
        QObject::connect(table, &QTableView::doubleClicked, q, [this] { openRevision(); });
        if (table->selectionModel())
            QObject::connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, q,
                             [this] { selectionChanged(); });
        applyColorMode(BlameView::Author);
        controls();
    }
};

BlameView::BlameView(const QString &filePath, QWidget *parent)
    : BlameView(filePath, QSharedPointer<Vcs::GitBackend>::create(), parent)
{}

BlameView::BlameView(const QString &filePath, QSharedPointer<Vcs::Backend> backend, QWidget *parent)
    : QWidget(parent)
    , d(new Private(this, backend ? std::move(backend) : QSharedPointer<Vcs::GitBackend>::create()))
{
    qRegisterMetaType<Vcs::Comparison>();
    d->buildUi(filePath);
    QTimer::singleShot(0, this, [this] { if (!d->started) refresh(); });
}

BlameView::~BlameView() = default;

QString BlameView::path() const { return d->path->text(); }
QString BlameView::revision() const { return d->revision->text().trimmed(); }
bool BlameView::isBusy() const { return d->busy; }
BlameView::ColorMode BlameView::colorMode() const { return d->model->mode; }

void BlameView::setPath(const QString &filePath)
{
    if (d->path->text() == filePath) return;
    d->path->setText(filePath);
    refresh();
}

void BlameView::setRevision(const QString &value)
{
    if (d->revision->text().trimmed() == value.trimmed()) return;
    d->revision->setText(value);
    refresh();
}

void BlameView::setColorMode(ColorMode mode) { d->applyColorMode(mode); }
void BlameView::refresh() { d->refresh(); }
void BlameView::cancel() { d->stop(true); }
void BlameView::showSelectedRevision() { d->openRevision(); }
void BlameView::compareSelectedWithParent() { d->compareParent(); }
}
