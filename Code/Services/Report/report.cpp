#include "report.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSaveFile>

namespace LqCompare { namespace Report {
namespace {
QString yesNo(bool value) { return value ? QStringLiteral("是") : QStringLiteral("否"); }
QString escaped(const QString &value) { return value.toHtmlEscaped(); }
QString source(const QString &value) { return value.isEmpty() ? QStringLiteral("（内存文档）") : value; }

bool selected(const Row &row, const Options &options)
{
    if (!row.visible) return false;
    if (row.state == State::Equal && !options.includeEqual) return false;
    if (row.state == State::Ignored && !options.includeIgnored) return false;
    if ((row.state == State::LeftOnly || row.state == State::RightOnly) && !options.includeOrphans) return false;
    return true;
}

QString stateKey(State state)
{
    switch (state) {
    case State::Equal: return QStringLiteral("equal");
    case State::Changed: return QStringLiteral("changed");
    case State::LeftOnly: return QStringLiteral("left");
    case State::RightOnly: return QStringLiteral("right");
    case State::Ignored: return QStringLiteral("ignored");
    case State::Conflict: return QStringLiteral("conflict");
    case State::Error: return QStringLiteral("error");
    case State::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString layoutLabel(Layout layout)
{
    switch (layout) {
    case Layout::SideBySide: return QStringLiteral("并排");
    case Layout::Interleaved: return QStringLiteral("交错");
    case Layout::Summary: return QStringLiteral("摘要");
    case Layout::Statistics: return QStringLiteral("统计");
    }
    return {};
}

QString conclusion(const Statistics &stats)
{
    if (!stats.complete) return QStringLiteral("结果不完整 / 存在错误或未知项");
    return stats.hasDifferences() ? QStringLiteral("有差异") : QStringLiteral("无差异（按当前比较规则）");
}

QString countText(const Statistics &s, Kind kind)
{
    return QStringLiteral("统计单位：%1；总计 %2；相同 %3；修改 %4；仅左 %5；仅右 %6；已忽略 %7；冲突 %8；错误 %9；未知 %10；目录 %11；范围内条目 %12")
        .arg(kind == Kind::Text ? QStringLiteral("对齐行") : QStringLiteral("目录条目"))
        .arg(s.total).arg(s.equal).arg(s.changed).arg(s.leftOnly).arg(s.rightOnly)
        .arg(s.ignored).arg(s.conflicts).arg(s.errors).arg(s.unknown).arg(s.directories).arg(s.exported);
}

QString optionText(const Options &o)
{
    return QStringLiteral("布局：%1；包含相同项：%2；包含已忽略项：%3；包含仅单侧项：%4；显示行号：%5；遵循条目可见性过滤；编码：UTF-8%6")
        .arg(layoutLabel(o.layout), yesNo(o.includeEqual), yesNo(o.includeIgnored),
             yesNo(o.includeOrphans), yesNo(o.showLineNumbers), o.utf8Bom ? QStringLiteral(" with BOM") : QString());
}

QString position(const Row &row, bool numbers)
{
    if (!row.path.isEmpty()) return row.path;
    if (!numbers) return QString();
    return QStringLiteral("左 %1 / 右 %2")
        .arg(row.leftLine ? QString::number(row.leftLine) : QStringLiteral("—"),
             row.rightLine ? QString::number(row.rightLine) : QStringLiteral("—"));
}

QString sideText(const Folder::Side &side)
{
    if (side.kind == Folder::Kind::Missing && !side.info.exists && side.error.isEmpty()) return QStringLiteral("不存在");
    QString kind;
    switch (side.kind) {
    case Folder::Kind::Missing: kind = QStringLiteral("未知"); break;
    case Folder::Kind::File: kind = QStringLiteral("文件"); break;
    case Folder::Kind::Directory: kind = QStringLiteral("目录"); break;
    case Folder::Kind::SymbolicLink: kind = QStringLiteral("符号链接"); break;
    case Folder::Kind::Other: kind = QStringLiteral("其他"); break;
    }
    if (side.kind == Folder::Kind::File) kind += QStringLiteral("，%1 字节").arg(side.info.size);
    if (!side.error.isEmpty()) kind += QStringLiteral("，错误：") + side.error;
    return kind;
}

class Writer {
public:
    Writer(QIODevice *device, QString *error, const std::atomic_bool *cancelled)
        : m_device(device), m_error(error), m_cancelled(cancelled) {}
    bool check()
    {
        if (m_cancelled && m_cancelled->load()) return fail(QStringLiteral("报表生成已取消"));
        return m_good;
    }
    bool fail(const QString &message)
    {
        if (m_good && m_error) *m_error = message;
        m_good = false;
        return false;
    }
    bool bytes(const QByteArray &bytes)
    {
        if (!check()) return false;
        qint64 offset = 0;
        while (offset < bytes.size()) {
            if (!check()) return false;
            const qint64 count = m_device->write(bytes.constData() + offset, qMin<qint64>(65536, bytes.size() - offset));
            if (count <= 0) return fail(QStringLiteral("报表写入失败：") + m_device->errorString());
            offset += count;
        }
        return true;
    }
    bool text(const QString &value) { return bytes(value.toUtf8()); }
private:
    QIODevice *m_device;
    QString *m_error;
    const std::atomic_bool *m_cancelled;
    bool m_good = true;
};

bool sameFile(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty()) return false;
    const QFileInfo first(a), second(b);
    if (first.absoluteFilePath() == second.absoluteFilePath()) return true;
    return !first.canonicalFilePath().isEmpty() && first.canonicalFilePath() == second.canonicalFilePath();
}
} // namespace

QString stateLabel(State state)
{
    switch (state) {
    case State::Equal: return QStringLiteral("相同");
    case State::Changed: return QStringLiteral("修改");
    case State::LeftOnly: return QStringLiteral("仅左");
    case State::RightOnly: return QStringLiteral("仅右");
    case State::Ignored: return QStringLiteral("已忽略");
    case State::Conflict: return QStringLiteral("冲突");
    case State::Error: return QStringLiteral("错误");
    case State::Unknown: return QStringLiteral("未知");
    }
    return QStringLiteral("未知");
}

Model fromText(const Text::Document &left, const Text::Document &right,
               const Text::Result &result, const Text::CompareOptions &compareOptions,
               const Metadata &metadata)
{
    Model model;
    model.kind = Kind::Text;
    model.metadata = metadata;
    if (model.metadata.title.isEmpty()) model.metadata.title = QStringLiteral("文本差异报表");
    if (model.metadata.leftSource.isEmpty()) model.metadata.leftSource = left.path();
    if (model.metadata.rightSource.isEmpty()) model.metadata.rightSource = right.path();
    const QString whitespace = compareOptions.whitespace == Text::Whitespace::Exact ? QStringLiteral("精确") :
        compareOptions.whitespace == Text::Whitespace::IgnoreChanges ? QStringLiteral("忽略空白变化") : QStringLiteral("忽略全部空白");
    model.metadata.settings << QStringLiteral("忽略大小写：%1；空白：%2；忽略换行类型：%3；忽略末尾换行：%4")
        .arg(yesNo(compareOptions.ignoreCase), whitespace, yesNo(compareOptions.ignoreEol), yesNo(compareOptions.ignoreFinalNewline));
    model.metadata.settings << QStringLiteral("左编码：%1；BOM：%2；换行：%3；右编码：%4；BOM：%5；换行：%6")
        .arg(QString::fromLatin1(left.codecName()), yesNo(left.hasBom()), left.eolDescription(),
             QString::fromLatin1(right.codecName()), yesNo(right.hasBom()), right.eolDescription());
    if (result.alignmentLimited) model.warnings << QStringLiteral("对齐达到工作量上限，部分范围以整体替换表示，行对齐可能较粗略。");
    if (left.decodingErrors() || right.decodingErrors()) {
        model.complete = false;
        model.warnings << QStringLiteral("源文件存在解码错误，报表中的文字可能不完整。");
    }
    model.rows.reserve(result.rows.size());
    for (const Text::Row &input : result.rows) {
        Row row;
        const bool validLeft = input.leftLine >= 0 && input.leftLine < left.lines().size();
        const bool validRight = input.rightLine >= 0 && input.rightLine < right.lines().size();
        if (validLeft) { row.leftLine = input.leftLine + 1; row.left = left.lines()[input.leftLine].text; }
        if (validRight) { row.rightLine = input.rightLine + 1; row.right = right.lines()[input.rightLine].text; }
        switch (input.change) {
        case Text::Change::Equal: row.state = State::Equal; break;
        case Text::Change::Ignored: row.state = State::Ignored; break;
        case Text::Change::Insert: row.state = State::RightOnly; break;
        case Text::Change::Delete: row.state = State::LeftOnly; break;
        case Text::Change::Replace: row.state = State::Changed; break;
        }
        if ((input.leftLine != -1 && !validLeft) || (input.rightLine != -1 && !validRight) || (!validLeft && !validRight)) {
            row.state = State::Error;
            row.detail = QStringLiteral("比较模型包含无效行号");
            model.complete = false;
        } else {
            QStringList endings;
            const auto ending = [](Text::Eol eol) {
                switch (eol) {
                case Text::Eol::LF: return QStringLiteral("LF");
                case Text::Eol::CRLF: return QStringLiteral("CRLF");
                case Text::Eol::CR: return QStringLiteral("CR");
                case Text::Eol::None: return QStringLiteral("无末尾换行");
                }
                return QString();
            };
            if (validLeft) endings << QStringLiteral("左行尾：") + ending(left.lines()[input.leftLine].eol);
            if (validRight) endings << QStringLiteral("右行尾：") + ending(right.lines()[input.rightLine].eol);
            row.detail = endings.join(QStringLiteral("；"));
        }
        model.rows.append(row);
    }
    return model;
}

Model fromFolder(const Folder::Result &result, const Folder::Options &compareOptions, const Metadata &metadata)
{
    Model model;
    model.kind = Kind::Folder;
    model.metadata = metadata;
    if (model.metadata.title.isEmpty()) model.metadata.title = QStringLiteral("目录差异报表");
    if (model.metadata.leftSource.isEmpty()) model.metadata.leftSource = result.leftRoot;
    if (model.metadata.rightSource.isEmpty()) model.metadata.rightSource = result.rightRoot;
    model.metadata.settings << QStringLiteral("递归：%1；内容比较：%2；最大深度：%3")
        .arg(yesNo(compareOptions.recursive), yesNo(compareOptions.compareContent)).arg(compareOptions.maximumDepth);
    model.complete = result.complete && !result.cancelled && result.error.isEmpty();
    model.warnings = result.warnings;
    if (result.cancelled) model.warnings << QStringLiteral("目录比较已取消，结果不完整。");
    if (!result.complete) model.warnings << QStringLiteral("目录扫描未完成，结果不完整。");
    if (!result.error.isEmpty()) model.warnings << result.error;
    model.rows.reserve(result.entries.size());
    for (const Folder::Entry &entry : result.entries) {
        Row row;
        row.path = entry.relativePath;
        row.left = sideText(entry.left);
        row.right = sideText(entry.right);
        row.detail = entry.explanation;
        if (entry.firstDifference >= 0) row.detail += QStringLiteral("；首个差异字节偏移：%1").arg(entry.firstDifference);
        row.directory = entry.left.kind == Folder::Kind::Directory || entry.right.kind == Folder::Kind::Directory;
        switch (entry.status) {
        case Folder::Status::Same: row.state = State::Equal; break;
        case Folder::Status::Different: row.state = State::Changed; break;
        case Folder::Status::LeftOnly: row.state = State::LeftOnly; break;
        case Folder::Status::RightOnly: row.state = State::RightOnly; break;
        case Folder::Status::TypeConflict: row.state = State::Conflict; break;
        case Folder::Status::Error: row.state = State::Error; break;
        case Folder::Status::Unknown: row.state = State::Unknown; break;
        }
        model.rows.append(row);
    }
    return model;
}

Statistics statistics(const Model &model, const Options &options)
{
    Statistics stats;
    stats.complete = model.complete;
    for (const Row &row : model.rows) {
        ++stats.total;
        if (row.directory) ++stats.directories;
        if (selected(row, options)) ++stats.exported;
        switch (row.state) {
        case State::Equal: ++stats.equal; break;
        case State::Changed: ++stats.changed; break;
        case State::LeftOnly: ++stats.leftOnly; break;
        case State::RightOnly: ++stats.rightOnly; break;
        case State::Ignored: ++stats.ignored; break;
        case State::Conflict: ++stats.conflicts; break;
        case State::Error: ++stats.errors; stats.complete = false; break;
        case State::Unknown: ++stats.unknown; stats.complete = false; break;
        }
    }
    return stats;
}

bool write(QIODevice *device, const Model &model, const Options &options, QString *error,
           const std::atomic_bool *cancelled, const Progress &progress)
{
    if (error) error->clear();
    Writer writer(device, error, cancelled);
    if (!device || !device->isWritable()) return writer.fail(QStringLiteral("输出设备未打开或不可写"));
    if (options.rowsPerGroup < 1 || options.rowsPerGroup > 10000 || layoutLabel(options.layout).isEmpty())
        return writer.fail(QStringLiteral("报表布局或分组大小无效（每组应为 1–10000 项）"));
    if (options.format != Format::Html && options.format != Format::PlainText)
        return writer.fail(QStringLiteral("不支持的报表格式"));
    if (!writer.check()) return false;
    const Statistics stats = statistics(model, options);
    const bool html = options.format == Format::Html;
    if (options.utf8Bom && !writer.bytes(QByteArray::fromHex("efbbbf"))) return false;
    if (progress) progress(0, model.rows.size());
    const QString title = model.metadata.title.isEmpty() ? QStringLiteral("差异报表") : model.metadata.title;
    const QStringList metadata = {
        QStringLiteral("左数据源：") + source(model.metadata.leftSource),
        QStringLiteral("右数据源：") + source(model.metadata.rightSource),
        QStringLiteral("生成时间：") + model.metadata.generatedAt.toString(Qt::ISODateWithMs),
        QStringLiteral("工具版本：") + model.metadata.toolVersion
            + (model.metadata.toolVersion == QStringLiteral("LqCompare")
               ? (QCoreApplication::applicationVersion().isEmpty() ? QStringLiteral("（版本未提供）")
                  : QStringLiteral(" ") + QCoreApplication::applicationVersion()) : QString()),
        QStringLiteral("结论：") + conclusion(stats),
        countText(stats, model.kind),
        QStringLiteral("统计范围：全部比较结果；范围内条目遵循输出选项与可见性过滤。"),
        optionText(options)
    };
    if (html) {
        writer.text(QStringLiteral("<!doctype html>\n<html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'\">"
            "<title>") + escaped(title) + QStringLiteral("</title><style>"
            "body{font:14px system-ui,sans-serif;color:#202630;background:#fff;margin:24px}h1{font-size:24px}"
            ".meta p{margin:6px 0;white-space:pre-wrap;overflow-wrap:anywhere}.warning{color:#923800}"
            "table{width:100%;border-collapse:collapse;table-layout:fixed;margin:12px 0}td,th{border:1px solid #cbd1d9;padding:7px;text-align:left;vertical-align:top;white-space:pre-wrap;overflow-wrap:anywhere}"
            "th{background:#edf0f4}.pos{width:18%}.state{width:8%}.content{font-family:ui-monospace,monospace}"
            "tr[data-state=changed]{background:#fff1c7}tr[data-state=left]{background:#ffe3e3}tr[data-state=right]{background:#dff4e7}"
            "tr[data-state=ignored]{color:#68717c}tr[data-state=error],tr[data-state=unknown],tr[data-state=conflict]{background:#f5def4}"
            "details{margin:12px 0}summary{cursor:pointer;font-weight:600}input,select{padding:6px;margin:6px}"
            ".detail{display:block;color:#5f6570;font:12px system-ui,sans-serif;margin-top:5px}"
            "@media print{.controls{display:none}body{margin:0}tr,th{background:#fff!important;color:#000!important}table{font-size:10pt}}"
            "</style></head><body><h1>") + escaped(title) + QStringLiteral("</h1><section class=\"meta\">\n"));
        for (const QString &item : metadata) writer.text(QStringLiteral("<p>") + escaped(item) + QStringLiteral("</p>\n"));
        for (const QString &setting : model.metadata.settings) writer.text(QStringLiteral("<p>比较设置：") + escaped(setting) + QStringLiteral("</p>\n"));
        for (const QString &warning : model.warnings) writer.text(QStringLiteral("<p class=\"warning\">提示：") + escaped(warning) + QStringLiteral("</p>\n"));
        writer.text(QStringLiteral("</section>\n"));
        if (options.layout != Layout::Statistics) {
            writer.text(QStringLiteral("<div class=\"controls\"><label>搜索 <input id=\"search\" type=\"search\"></label><label>状态 <select id=\"state\"><option value=\"\">全部</option>"));
            for (State state : {State::Equal, State::Changed, State::LeftOnly, State::RightOnly, State::Ignored, State::Conflict, State::Error, State::Unknown})
                writer.text(QStringLiteral("<option value=\"") + stateKey(state) + QStringLiteral("\">") + stateLabel(state) + QStringLiteral("</option>"));
            writer.text(QStringLiteral("</select></label><button id=\"expand\">展开全部</button><button id=\"collapse\">折叠全部</button></div>\n"));
        }
    } else {
        writer.text(title + QStringLiteral("\n"));
        for (const QString &item : metadata) writer.text(item + QStringLiteral("\n"));
        for (const QString &setting : model.metadata.settings) writer.text(QStringLiteral("比较设置：") + setting + QStringLiteral("\n"));
        for (const QString &warning : model.warnings) writer.text(QStringLiteral("提示：") + warning + QStringLiteral("\n"));
        writer.text(QStringLiteral("\n"));
    }
    qint64 emitted = 0;
    bool groupOpen = false;
    for (int i = 0; i < model.rows.size(); ++i) {
        if (!writer.check()) return false;
        const Row &row = model.rows[i];
        if (options.layout != Layout::Statistics && selected(row, options)) {
            const QString pos = position(row, options.showLineNumbers);
            const bool equal = row.state == State::Equal;
            const bool ignored = row.state == State::Ignored;
            const QString leftPrefix = equal ? QStringLiteral("  ") : ignored ? QStringLiteral("≈ ") : QStringLiteral("- ");
            const QString rightPrefix = ignored ? QStringLiteral("≈ ") : QStringLiteral("+ ");
            QStringList interleaved;
            if (row.state != State::RightOnly)
                interleaved << leftPrefix + row.left;
            if (!equal && row.state != State::LeftOnly)
                interleaved << rightPrefix + row.right;
            if (html) {
                if (emitted % options.rowsPerGroup == 0) {
                    if (groupOpen) writer.text(QStringLiteral("</tbody></table></details>\n"));
                    writer.text(QStringLiteral("<details%1><summary>条目 %2–%3</summary><table><thead><tr><th class=\"state\">状态</th><th class=\"pos\">%4</th>")
                        .arg(emitted == 0 ? QStringLiteral(" open") : QString()).arg(emitted + 1)
                        .arg(qMin(stats.exported, emitted + options.rowsPerGroup))
                        .arg(model.kind == Kind::Folder ? QStringLiteral("相对路径") : QStringLiteral("位置")));
                    if (options.layout == Layout::SideBySide) writer.text(QStringLiteral("<th>左</th><th>右</th>"));
                    else writer.text(QStringLiteral("<th>内容</th>"));
                    writer.text(QStringLiteral("</tr></thead><tbody>\n"));
                    groupOpen = true;
                }
                writer.text(QStringLiteral("<tr data-state=\"") + stateKey(row.state) + QStringLiteral("\"><td>") + stateLabel(row.state) + QStringLiteral("</td><td>") + escaped(pos) + QStringLiteral("</td>"));
                const QString detail = QStringLiteral("<span class=\"detail\">") + escaped(row.detail) + QStringLiteral("</span>");
                if (options.layout == Layout::SideBySide)
                    writer.text(QStringLiteral("<td class=\"content\">") + escaped(row.left) + QStringLiteral("</td><td class=\"content\">") + escaped(row.right) + detail + QStringLiteral("</td>"));
                else if (options.layout == Layout::Interleaved)
                    writer.text(QStringLiteral("<td class=\"content\">") + escaped(interleaved.join(QStringLiteral("\n"))) + detail + QStringLiteral("</td>"));
                else writer.text(QStringLiteral("<td>") + escaped(row.directory ? QStringLiteral("目录；") + row.detail : row.detail) + QStringLiteral("</td>"));
                writer.text(QStringLiteral("</tr>\n"));
            } else {
                writer.text(QStringLiteral("[%1] %2\n").arg(stateLabel(row.state), pos));
                if (options.layout == Layout::SideBySide)
                    writer.text(QStringLiteral("左 | ") + row.left + QStringLiteral("\n右 | ") + row.right + QStringLiteral("\n"));
                else if (options.layout == Layout::Interleaved)
                    writer.text(interleaved.join(QStringLiteral("\n")) + QStringLiteral("\n"));
                if (!row.detail.isEmpty()) writer.text(QStringLiteral("  ") + row.detail + QStringLiteral("\n"));
                writer.text(QStringLiteral("\n"));
            }
            ++emitted;
        }
        if (progress && ((i + 1) % 128 == 0 || i + 1 == model.rows.size())) progress(i + 1, model.rows.size());
    }
    if (html) {
        if (groupOpen) writer.text(QStringLiteral("</tbody></table></details>\n"));
        if (!emitted && options.layout != Layout::Statistics) writer.text(QStringLiteral("<p>当前输出范围内没有条目。</p>\n"));
        // This script is fixed application text. User values are only escaped text
        // nodes, never JavaScript, URLs, CSS, attribute names, or event handlers.
        writer.text(QStringLiteral("<script>const q=document.getElementById('search'),s=document.getElementById('state');"
            "function filter(){const v=q.value.toLocaleLowerCase();document.querySelectorAll('tbody tr').forEach(r=>{r.hidden=!!((s.value&&r.dataset.state!==s.value)||(v&&!r.textContent.toLocaleLowerCase().includes(v)));});"
            "document.querySelectorAll('details').forEach(d=>{d.hidden=!Array.from(d.querySelectorAll('tbody tr')).some(r=>!r.hidden);if(v||s.value)d.open=true;});}"
            "if(q){q.addEventListener('input',filter);s.addEventListener('change',filter);document.getElementById('expand').addEventListener('click',()=>document.querySelectorAll('details').forEach(d=>d.open=true));"
            "document.getElementById('collapse').addEventListener('click',()=>document.querySelectorAll('details').forEach(d=>d.open=false));}"
            "let printState=[];window.addEventListener('beforeprint',()=>{printState=Array.from(document.querySelectorAll('details')).map(d=>{const o=d.open;d.open=true;return o;});});"
            "window.addEventListener('afterprint',()=>document.querySelectorAll('details').forEach((d,i)=>d.open=printState[i]));"
            "</script></body></html>\n"));
    } else if (!emitted && options.layout != Layout::Statistics) writer.text(QStringLiteral("当前输出范围内没有条目。\n"));
    return writer.check();
}

bool writeFile(const QString &path, const Model &model, const Options &options, QString *error,
               const std::atomic_bool *cancelled, const Progress &progress, bool overwrite)
{
    if (error) error->clear();
    const auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (path.isEmpty()) return fail(QStringLiteral("报表输出路径不能为空"));
    if (sameFile(path, model.metadata.leftSource) || sameFile(path, model.metadata.rightSource))
        return fail(QStringLiteral("报表不能覆盖比较源文件"));
    const QFileInfo destination(path);
    if ((destination.exists() || destination.isSymLink()) && !overwrite)
        return fail(QStringLiteral("报表目标已存在，需要明确选择覆盖"));
    if (destination.isSymLink()) return fail(QStringLiteral("报表目标不能是符号链接"));
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return fail(QStringLiteral("无法创建报表：") + file.errorString());
    if (!write(&file, model, options, error, cancelled, progress)) { file.cancelWriting(); return false; }
    if (cancelled && cancelled->load()) { file.cancelWriting(); return fail(QStringLiteral("报表生成已取消")); }
    if (!file.commit()) return fail(QStringLiteral("报表原子保存失败：") + file.errorString());
    return true;
}

QString render(const Model &model, const Options &options, QString *error)
{
    QByteArray output;
    QBuffer buffer(&output);
    buffer.open(QIODevice::WriteOnly);
    if (!write(&buffer, model, options, error)) return {};
    if (output.startsWith(QByteArray::fromHex("efbbbf"))) output.remove(0, 3);
    return QString::fromUtf8(output);
}

} }
