#include "patch.h"
#include "textdiff.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <limits>

namespace LqCompare { namespace Patch {
namespace {
constexpr int MaximumBytes = 32 * 1024 * 1024;
constexpr int MaximumLines = 1000000;

QVector<QByteArray> splitLines(const QByteArray &bytes)
{
    QVector<QByteArray> lines;
    int start = 0;
    for (int i = 0; i < bytes.size(); ++i) {
        if (bytes.at(i) == '\n') {
            lines.append(bytes.mid(start, i - start + 1));
            start = i + 1;
        }
    }
    if (start < bytes.size()) lines.append(bytes.mid(start));
    return lines;
}

QByteArray withoutLf(QByteArray line)
{
    if (line.endsWith('\n')) line.chop(1);
    return line;
}
QByteArray controlLine(const QByteArray &line)
{
    QByteArray result = withoutLf(line);
    if (result.endsWith('\r')) result.chop(1);
    return result;
}

bool relativePath(const QString &path, QString *error)
{
    if (path.isEmpty() || path.startsWith('/') || path.contains('\\') || path.contains(':')) {
        *error = QStringLiteral("路径必须是相对路径，且不得含绝对路径、反斜线或盘符: %1").arg(path);
        return false;
    }
    for (const QChar c : path) {
        if (c.unicode() < 32 || c.unicode() == 127) {
            *error = QStringLiteral("路径不得含控制字符");
            return false;
        }
    }
    const QStringList parts = path.split('/');
    for (const QString &part : parts) {
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith(' ') || part.endsWith('.')) {
            *error = QStringLiteral("路径包含空段、遍历段或不安全的末尾字符: %1").arg(path);
            return false;
        }
        // These Win32 device aliases are unsafe even when developing on Unix.
        const QString base = part.section('.', 0, 0).toUpper();
        static const QRegularExpression device(QStringLiteral("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$"));
        if (device.match(base).hasMatch()) {
            *error = QStringLiteral("路径包含系统设备名称: %1").arg(path);
            return false;
        }
    }
    return true;
}

QByteArray unifiedHeaderPath(const QString &path)
{
    // A TAB delimits the filename from the optional timestamp. This standard
    // unified spelling is accepted by both git apply and BSD/GNU patch, whereas
    // Git's octal-quoted UTF-8 names are not understood by BSD patch.
    return path.toUtf8() + '\t';
}

bool parsePath(const QByteArray &header, QString *path, QString *error)
{
    const QByteArray value = header.mid(4);
    QByteArray decoded;
    if (value.startsWith('"')) {
        bool closed = false;
        int i = 1;
        for (; i < value.size(); ++i) {
            unsigned char c = value.at(i);
            if (c == '"') { closed = true; ++i; break; }
            if (c != '\\') { decoded += char(c); continue; }
            if (++i >= value.size()) break;
            c = value.at(i);
            if (c >= '0' && c <= '7') {
                int number = c - '0';
                int digits = 1;
                while (digits < 3 && i + 1 < value.size() && value.at(i + 1) >= '0' && value.at(i + 1) <= '7') {
                    number = number * 8 + value.at(++i) - '0';
                    ++digits;
                }
                if (number > 255) { *error = QStringLiteral("路径八进制转义超出字节范围"); return false; }
                decoded += char(number);
            } else {
                switch (c) {
                case '\\': case '"': decoded += char(c); break;
                case 't': decoded += '\t'; break;
                case 'n': decoded += '\n'; break;
                case 'r': decoded += '\r'; break;
                default: *error = QStringLiteral("路径包含不支持的转义"); return false;
                }
            }
        }
        if (!closed || (i < value.size() && value.at(i) != '\t')) {
            *error = QStringLiteral("引号路径未闭合或路径后存在非法字符"); return false;
        }
    } else {
        decoded = value.left(value.indexOf('\t') < 0 ? value.size() : value.indexOf('\t'));
    }
    *path = QString::fromUtf8(decoded);
    if (path->toUtf8() != decoded || path->isEmpty()) {
        *error = QStringLiteral("路径为空或不是有效 UTF-8"); return false;
    }
    if (*path != "/dev/null" && !relativePath(*path, error)) return false;
    return true;
}

QVector<Text::Line> textLines(const QVector<QByteArray> &lines)
{
    QVector<Text::Line> result;
    result.reserve(lines.size());
    for (const auto &line : lines)
        result.append({QString::fromLatin1(withoutLf(line)), line.endsWith('\n') ? Text::Eol::LF : Text::Eol::None});
    return result;
}

bool resolvePath(const QString &root, const QString &raw, int strip, QString *relative,
                 QString *absolute, QString *error)
{
    if (!relativePath(raw, error)) return false; // Validate BEFORE stripping.
    const QStringList parts = raw.split('/');
    if (strip < 0 || strip >= parts.size()) {
        *error = QStringLiteral("路径剥离层数 %1 对 %2 无效").arg(strip).arg(raw); return false;
    }
    *relative = parts.mid(strip).join('/');
    QString current = root;
    for (int i = strip; i < parts.size(); ++i) {
        current = QDir(current).filePath(parts.at(i));
        const QFileInfo info(current);
        if (info.isSymLink()) {
            *error = QStringLiteral("拒绝符号链接路径组件: %1").arg(current); return false;
        }
        if (info.exists()) {
            if (i + 1 < parts.size() && !info.isDir()) {
                *error = QStringLiteral("路径父组件不是目录: %1").arg(current); return false;
            }
            const QString canonical = info.canonicalFilePath();
            if (canonical.isEmpty() || !canonical.startsWith(root == "/" ? root : root + '/')) {
                *error = QStringLiteral("路径逃逸出目标根目录: %1").arg(current); return false;
            }
        }
    }
    *absolute = current;
    return true;
}

bool validHunk(const Hunk &h, QString *error)
{
    if (h.oldStart < 0 || h.newStart < 0 || h.oldCount < 0 || h.newCount < 0 ||
        (h.oldCount > 0 && h.oldStart == 0) || (h.newCount > 0 && h.newStart == 0)) {
        *error = QStringLiteral("hunk 行号或行数无效"); return false;
    }
    qint64 oldCount = 0, newCount = 0;
    for (const auto &line : h.lines) {
        if (line.kind != ' ' && line.kind != '+' && line.kind != '-') {
            *error = QStringLiteral("hunk 行前缀无效"); return false;
        }
        const int newline = line.bytes.indexOf('\n');
        if (newline >= 0 && newline + 1 != line.bytes.size()) {
            *error = QStringLiteral("hunk 正文单行包含嵌入换行"); return false;
        }
        if (line.kind != '+') ++oldCount;
        if (line.kind != '-') ++newCount;
    }
    if (oldCount != h.oldCount || newCount != h.newCount || h.lines.isEmpty()) {
        *error = QStringLiteral("hunk 正文行数与头部不一致"); return false;
    }
    return true;
}
}

GenerateResult generate(const QVector<FileInput> &files, const GenerateOptions &options)
{
    GenerateResult result;
    auto fail = [&](const QString &message) {
        result.bytes.clear(); result.diagnostics.append({0, message}); return result;
    };
    if (options.contextLines < 0 || options.contextLines > MaximumLines)
        return fail(QStringLiteral("上下文行数必须在 0 至 %1 之间").arg(MaximumLines));
    for (const FileInput &file : files) {
        QString error;
        if (!file.oldExists && !file.newExists) return fail(QStringLiteral("补丁两侧都不存在"));
        if ((!file.oldExists && !file.oldBytes.isEmpty()) || (!file.newExists && !file.newBytes.isEmpty()))
            return fail(QStringLiteral("不存在的一侧不能提供文件内容"));
        if ((file.oldExists && (!relativePath(file.oldPath, &error) || !relativePath(options.oldPrefix + file.oldPath, &error))) ||
            (file.newExists && (!relativePath(file.newPath, &error) || !relativePath(options.newPrefix + file.newPath, &error))))
            return fail(error);
        if ((file.oldExists && (options.oldPrefix + file.oldPath).startsWith('"')) ||
            (file.newExists && (options.newPrefix + file.newPath).startsWith('"')))
            return fail(QStringLiteral("以引号开头的文件名需要非空相对路径前缀"));
        if (file.oldBytes.size() > MaximumBytes || file.newBytes.size() > MaximumBytes)
            return fail(QStringLiteral("单侧内容超过 32 MiB 补丁生成限制"));
        if (file.oldBytes.contains('\0') || file.newBytes.contains('\0'))
            return fail(QStringLiteral("含 NUL 的二进制或 UTF-16 内容不能生成为文本 unified diff"));
        if (file.oldBytes.count('\n') >= MaximumLines || file.newBytes.count('\n') >= MaximumLines)
            return fail(QStringLiteral("输入超过每侧一百万行限制"));
        if (file.oldBytes == file.newBytes) {
            if (file.oldExists != file.newExists)
                return fail(QStringLiteral("纯 unified diff 无法可移植地表示空文件创建或删除"));
            continue;
        }
        const auto left = splitLines(file.oldBytes), right = splitLines(file.newBytes);
        if (left.size() > MaximumLines || right.size() > MaximumLines)
            return fail(QStringLiteral("输入超过每侧一百万行限制"));
        Text::CompareOptions exact;
        exact.ignoreEol = false;
        const auto comparison = Text::compare(textLines(left), textLines(right), exact);
        result.alignmentLimited = result.alignmentLimited || comparison.alignmentLimited;
        struct Operation { char kind; QByteArray bytes; };
        QVector<Operation> operations;
        for (const auto &block : comparison.blocks) {
            if (block.change == Text::Change::Equal) {
                for (int i = 0; i < block.leftCount; ++i) operations.append({' ', left.at(block.leftStart + i)});
            } else {
                for (int i = 0; i < block.leftCount; ++i) { operations.append({'-', left.at(block.leftStart + i)}); ++result.removedLines; }
                for (int i = 0; i < block.rightCount; ++i) { operations.append({'+', right.at(block.rightStart + i)}); ++result.addedLines; }
            }
        }
        QVector<QPair<int, int>> ranges;
        for (int i = 0; i < operations.size();) {
            if (operations.at(i).kind == ' ') { ++i; continue; }
            int begin = i;
            while (i < operations.size() && operations.at(i).kind != ' ') ++i;
            int end = i;
            for (int n = 0; n < options.contextLines && begin > 0 && operations.at(begin - 1).kind == ' '; ++n) --begin;
            for (int n = 0; n < options.contextLines && end < operations.size() && operations.at(end).kind == ' '; ++n) ++end;
            if (!ranges.isEmpty() && begin <= ranges.last().second) ranges.last().second = qMax(ranges.last().second, end);
            else ranges.append({begin, end});
        }
        QVector<int> oldBefore(operations.size() + 1), newBefore(operations.size() + 1);
        for (int i = 0; i < operations.size(); ++i) {
            oldBefore[i + 1] = oldBefore.at(i) + (operations.at(i).kind != '+');
            newBefore[i + 1] = newBefore.at(i) + (operations.at(i).kind != '-');
        }
        result.bytes += "--- " + unifiedHeaderPath(file.oldExists ? options.oldPrefix + file.oldPath : QStringLiteral("/dev/null")) + '\n';
        result.bytes += "+++ " + unifiedHeaderPath(file.newExists ? options.newPrefix + file.newPath : QStringLiteral("/dev/null")) + '\n';
        ++result.fileCount;
        for (const auto &range : ranges) {
            const int oldCount = oldBefore.at(range.second) - oldBefore.at(range.first);
            const int newCount = newBefore.at(range.second) - newBefore.at(range.first);
            result.bytes += "@@ -" + QByteArray::number(oldBefore.at(range.first) + (oldCount > 0)) + ',' + QByteArray::number(oldCount) +
                            " +" + QByteArray::number(newBefore.at(range.first) + (newCount > 0)) + ',' + QByteArray::number(newCount) + " @@\n";
            for (int i = range.first; i < range.second; ++i) {
                const auto &op = operations.at(i);
                result.bytes += op.kind;
                result.bytes += op.bytes;
                if (!op.bytes.endsWith('\n')) result.bytes += "\n\\ No newline at end of file\n";
            }
            ++result.hunkCount;
        }
        if (result.bytes.size() > MaximumBytes * 4)
            return fail(QStringLiteral("生成结果超过 128 MiB 限制"));
    }
    result.ok = true;
    return result;
}

bool writeFile(const QString &path, const QByteArray &patchBytes, QString *error, bool overwrite)
{
    if (error) error->clear();
    auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    const QFileInfo destination(path);
    if (path.isEmpty()) return fail(QStringLiteral("补丁导出路径为空"));
    if (destination.isSymLink() || (destination.exists() && !destination.isFile()))
        return fail(QStringLiteral("补丁导出目标是符号链接或不是普通文件"));
    if (destination.exists() && !overwrite)
        return fail(QStringLiteral("目标文件已经存在，覆盖需要显式选择"));
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) return fail(output.errorString());
    if (output.write(patchBytes) != patchBytes.size()) {
        const QString message = output.errorString();
        output.cancelWriting();
        return fail(message);
    }
    if (!output.commit()) return fail(output.errorString());
    return true;
}

ParseResult parse(const QByteArray &bytes, const ParseOptions &options)
{
    ParseResult result;
    auto fail = [&](int line, const QString &message) {
        result.document.files.clear(); result.diagnostics.append({line, message}); return result;
    };
    if (bytes.size() > MaximumBytes * 4) return fail(0, QStringLiteral("补丁超过 128 MiB 解析限制"));
    if (bytes.contains('\0')) return fail(0, QStringLiteral("不支持含 NUL 的二进制补丁"));
    if (bytes.count('\n') > MaximumLines * 4) return fail(0, QStringLiteral("补丁超过四百万行解析限制"));
    const auto lines = splitLines(bytes);
    const QRegularExpression header(QStringLiteral("^@@ -(\\d+)(?:,(\\d+))? \\+(\\d+)(?:,(\\d+))? @@(?:.*)$"));
    int i = 0;
    int pendingMetadataLine = 0;
    while (i < lines.size()) {
        const QByteArray control = controlLine(lines.at(i));
        if (control.isEmpty()) { ++i; continue; }
        if (control.startsWith("diff --git ") || control.startsWith("diff -r ") ||
            control.startsWith("diff -u ") || control.startsWith("diff -N ") || control.startsWith("Index: ") ||
            control.startsWith("====") || control.startsWith("index ") || control == "new file mode 100644" ||
            control == "deleted file mode 100644") {
            if ((control.startsWith("diff ") || control.startsWith("Index: ")) && pendingMetadataLine)
                return fail(pendingMetadataLine, QStringLiteral("补丁元数据记录缺少 unified diff 正文"));
            if (!pendingMetadataLine) pendingMetadataLine = i + 1;
            ++i;
            continue;
        }
        if (!control.startsWith("--- "))
            return fail(i + 1, QStringLiteral("不支持的补丁记录或格式（需要 unified diff；二进制、权限、重命名及 context diff 尚不支持）"));
        FilePatch file;
        file.patchLine = i + 1;
        QString error;
        if (!parsePath(control, &file.oldPath, &error)) return fail(i + 1, error);
        if (++i >= lines.size() || !controlLine(lines.at(i)).startsWith("+++ "))
            return fail(i + 1, QStringLiteral("缺少 +++ 文件头"));
        if (!parsePath(controlLine(lines.at(i)), &file.newPath, &error)) return fail(i + 1, error);
        if (file.oldPath == "/dev/null" && file.newPath == "/dev/null")
            return fail(i + 1, QStringLiteral("文件头两侧不能同时是 /dev/null"));
        ++i;
        qint64 previousOldEnd = 0, previousNewEnd = 0;
        bool oldEnded = false, newEnded = false;
        while (i < lines.size() && controlLine(lines.at(i)).startsWith("@@")) {
            Hunk hunk;
            hunk.patchLine = i + 1;
            const auto match = header.match(QString::fromLatin1(controlLine(lines.at(i))));
            if (!match.hasMatch()) return fail(i + 1, QStringLiteral("hunk 头部格式无效"));
            int *values[] = {&hunk.oldStart, &hunk.oldCount, &hunk.newStart, &hunk.newCount};
            for (int field = 0; field < 4; ++field) {
                const QString captured = match.captured(field + 1);
                bool ok = false;
                *values[field] = captured.isEmpty() ? 1 : captured.toInt(&ok);
                if (!captured.isEmpty() && !ok) return fail(i + 1, QStringLiteral("hunk 行号或行数超出整数范围"));
            }
            if ((hunk.oldCount > 0 && hunk.oldStart == 0) || (hunk.newCount > 0 && hunk.newStart == 0))
                return fail(i + 1, QStringLiteral("非空 hunk 的起始行必须大于零"));
            const qint64 oldBegin = qint64(hunk.oldStart) - (hunk.oldCount > 0);
            const qint64 newBegin = qint64(hunk.newStart) - (hunk.newCount > 0);
            if (oldBegin < previousOldEnd || newBegin < previousNewEnd)
                return fail(i + 1, QStringLiteral("hunk 顺序错误或相互重叠"));
            if (oldBegin - previousOldEnd != newBegin - previousNewEnd)
                return fail(i + 1, QStringLiteral("hunk 两侧未修改行数不一致"));
            previousOldEnd = oldBegin + hunk.oldCount;
            previousNewEnd = newBegin + hunk.newCount;
            int oldCount = 0, newCount = 0;
            ++i;
            while (i < lines.size()) {
                const QByteArray bodyControl = controlLine(lines.at(i));
                if (bodyControl == "\\ No newline at end of file") {
                    if (hunk.lines.isEmpty() || !hunk.lines.last().bytes.endsWith('\n'))
                        return fail(i + 1, QStringLiteral("无末尾换行标记没有对应的正文行"));
                    auto &last = hunk.lines.last();
                    last.bytes.chop(1);
                    if (last.kind != '+') oldEnded = true;
                    if (last.kind != '-') newEnded = true;
                    ++i;
                    continue;
                }
                if (oldCount == hunk.oldCount && newCount == hunk.newCount) break;
                QByteArray raw = lines.at(i);
                if (raw.isEmpty() || (raw.at(0) != ' ' && raw.at(0) != '+' && raw.at(0) != '-'))
                    return fail(i + 1, QStringLiteral("hunk 正文提前结束或行前缀无效"));
                if (!raw.endsWith('\n')) return fail(i + 1, QStringLiteral("补丁正文缺少换行终止；原文件无换行需使用标准标记"));
                const char kind = raw.at(0);
                if ((kind != '+' && oldEnded) || (kind != '-' && newEnded))
                    return fail(i + 1, QStringLiteral("无末尾换行标记之后仍有同侧正文"));
                QByteArray content = raw.mid(1);
                if (options.stripTransportCr && content.endsWith("\r\n")) content.remove(content.size() - 2, 1);
                hunk.lines.append({kind, content, i + 1});
                if (kind != '+') ++oldCount;
                if (kind != '-') ++newCount;
                if (oldCount > hunk.oldCount || newCount > hunk.newCount)
                    return fail(i + 1, QStringLiteral("hunk 正文超过声明行数"));
                ++i;
            }
            if (!validHunk(hunk, &error)) return fail(hunk.patchLine, error);
            if (file.oldPath == "/dev/null" && hunk.oldCount != 0)
                return fail(hunk.patchLine, QStringLiteral("创建文件的旧侧行数必须是零"));
            if (file.newPath == "/dev/null" && hunk.newCount != 0)
                return fail(hunk.patchLine, QStringLiteral("删除文件的新侧行数必须是零"));
            file.hunks.append(hunk);
        }
        if (file.hunks.isEmpty()) return fail(file.patchLine, QStringLiteral("文件记录没有 unified diff hunk"));
        result.document.files.append(file);
        pendingMetadataLine = 0;
    }
    if (pendingMetadataLine) return fail(pendingMetadataLine, QStringLiteral("补丁元数据记录缺少 unified diff 正文"));
    result.ok = true;
    return result;
}

BytesPreview previewBytes(const FilePatch &file, const QByteArray &original, const ApplyOptions &options, int fileIndex)
{
    BytesPreview result;
    if (file.hunks.isEmpty()) {
        result.diagnostics.append({file.patchLine, QStringLiteral("文件记录没有 unified diff hunk")}); return result;
    }
    if (original.size() > MaximumBytes || original.count('\n') >= MaximumLines || options.maximumOffset < 0 || options.maximumOffset > MaximumLines) {
        result.diagnostics.append({0, QStringLiteral("输入超过 32 MiB 或偏移搜索范围无效")}); return result;
    }
    const auto source = splitLines(original);
    QVector<QByteArray> output;
    int cursor = 0;
    int previousOffset = 0;
    bool failed = false;
    qint64 matchBudget = 12000000;
    qint64 previousOldEnd = 0, previousNewEnd = 0;
    for (const auto &hunk : file.hunks) {
        QString error;
        const qint64 oldBegin = qint64(hunk.oldStart) - (hunk.oldCount > 0);
        const qint64 newBegin = qint64(hunk.newStart) - (hunk.newCount > 0);
        if (!validHunk(hunk, &error)) {
            result.diagnostics.append({hunk.patchLine, error}); return result;
        }
        if (oldBegin < previousOldEnd || newBegin < previousNewEnd ||
            oldBegin - previousOldEnd != newBegin - previousNewEnd) {
            result.diagnostics.append({hunk.patchLine, QStringLiteral("hunk 顺序、范围或两侧未修改行数不一致")}); return result;
        }
        previousOldEnd = oldBegin + hunk.oldCount;
        previousNewEnd = newBegin + hunk.newCount;
    }
    const auto selection = options.selectedHunks.constFind(fileIndex);
    if (selection != options.selectedHunks.cend()) {
        for (int index : selection.value()) {
            if (index < 0 || index >= file.hunks.size()) {
                result.diagnostics.append({file.patchLine, QStringLiteral("所选 hunk 索引超出范围")}); return result;
            }
        }
    }
    for (int index = 0; index < file.hunks.size(); ++index) {
        const Hunk &hunk = file.hunks.at(index);
        HunkPreview item;
        item.index = index;
        item.patchLine = hunk.patchLine;
        item.selected = selection == options.selectedHunks.cend() || selection.value().contains(index);
        if (!item.selected) { result.hunks.append(item); continue; }
        QString error;
        if (!validHunk(hunk, &error)) {
            item.error = error; failed = true; result.diagnostics.append({hunk.patchLine, error}); result.hunks.append(item); continue;
        }
        QVector<QByteArray> before, after;
        for (const auto &line : hunk.lines) {
            const char kind = options.reverse ? (line.kind == '+' ? '-' : line.kind == '-' ? '+' : ' ') : line.kind;
            if (kind != '+') before.append(line.bytes);
            if (kind != '-') after.append(line.bytes);
        }
        const int start = options.reverse ? hunk.newStart : hunk.oldStart;
        const int count = options.reverse ? hunk.newCount : hunk.oldCount;
        const qint64 declared = qint64(start) - (count > 0);
        const qint64 expected = declared + previousOffset;
        auto matches = [&](qint64 pos) {
            if (--matchBudget <= 0) return false;
            if (pos < cursor || pos < 0 || pos > source.size() || pos + before.size() > source.size()) return false;
            for (int n = 0; n < before.size(); ++n) {
                if (--matchBudget <= 0 || source.at(int(pos) + n) != before.at(n)) return false;
            }
            return true;
        };
        qint64 position = -1;
        bool ambiguous = false;
        if (matches(expected)) position = expected;
        else if (!before.isEmpty()) {
            const qint64 low = qMax<qint64>(cursor, expected - options.maximumOffset);
            const qint64 high = qMin<qint64>(source.size() - before.size(), expected + options.maximumOffset);
            // A shifted hunk needs one unique full-context match. No fuzz or
            // whitespace normalization is used, even if several matches are near.
            for (qint64 candidate = low; candidate <= high; ++candidate) {
                if (matchBudget <= 0) break;
                if (matches(candidate)) {
                    if (position >= 0) { ambiguous = true; break; }
                    position = candidate;
                }
            }
        }
        if (position < 0 || ambiguous || matchBudget <= 0) {
            item.error = matchBudget <= 0 ? QStringLiteral("完整上下文搜索超过工作量限制") :
                         ambiguous ? QStringLiteral("上下文存在多个偏移匹配，拒绝猜测目标") : QStringLiteral("原文件内容与完整上下文不匹配");
            failed = true;
            result.diagnostics.append({hunk.patchLine, item.error});
            result.hunks.append(item);
            continue;
        }
        while (cursor < position) output.append(source.at(cursor++));
        for (const auto &line : after) output.append(line);
        cursor = int(position) + before.size();
        item.applicable = true;
        item.offset = int(position - declared);
        previousOffset = item.offset;
        result.hunks.append(item);
    }
    while (cursor < source.size()) output.append(source.at(cursor++));
    if (failed) return result;
    for (int i = 0; i < output.size(); ++i) {
        if (i + 1 < output.size() && !output.at(i).endsWith('\n')) {
            result.resultBytes.clear();
            result.diagnostics.append({file.patchLine, QStringLiteral("补丁会在文件中间产生无换行行，拒绝不一致的结果")}); return result;
        }
        result.resultBytes += output.at(i);
        if (result.resultBytes.size() > MaximumBytes) {
            result.resultBytes.clear(); result.diagnostics.append({file.patchLine, QStringLiteral("预演结果超过 32 MiB 限制")}); return result;
        }
    }
    result.ok = true;
    return result;
}

PreviewResult preview(const Document &document, const QString &root, const ApplyOptions &options)
{
    PreviewResult result;
    QFileInfo rootInfo(root);
    result.root = rootInfo.canonicalFilePath();
    if (!rootInfo.isDir() || rootInfo.isSymLink() || result.root.isEmpty() || options.stripComponents < 0) {
        result.diagnostics.append({0, QStringLiteral("目标根目录不存在、不是普通目录、是符号链接或路径剥离参数无效")}); return result;
    }
    for (auto it = options.selectedHunks.cbegin(); it != options.selectedHunks.cend(); ++it) {
        if (it.key() < 0 || it.key() >= document.files.size()) {
            result.diagnostics.append({0, QStringLiteral("所选文件索引超出范围")}); return result;
        }
    }
    QSet<QString> targets;
    bool failed = false;
    for (int index = 0; index < document.files.size(); ++index) {
        const auto &file = document.files.at(index);
        FilePreview item;
        const QString oldPath = options.reverse ? file.newPath : file.oldPath;
        const QString newPath = options.reverse ? file.oldPath : file.newPath;
        item.createsFile = oldPath == "/dev/null";
        item.deletesFile = newPath == "/dev/null";
        item.selected = !options.selectedHunks.contains(index) || !options.selectedHunks.value(index).isEmpty();
        QString oldRelative, oldAbsolute, newRelative, newAbsolute, error;
        if ((item.createsFile && item.deletesFile) ||
            (!item.createsFile && !resolvePath(result.root, oldPath, options.stripComponents, &oldRelative, &oldAbsolute, &error)) ||
            (!item.deletesFile && !resolvePath(result.root, newPath, options.stripComponents, &newRelative, &newAbsolute, &error))) {
            item.diagnostics.append({file.patchLine, error.isEmpty() ? QStringLiteral("无效的补丁路径") : error});
        } else if (!item.createsFile && !item.deletesFile && oldRelative != newRelative) {
            item.diagnostics.append({file.patchLine, QStringLiteral("预演不支持重命名；两侧剥离后路径必须一致")});
        } else {
            item.relativePath = item.deletesFile ? oldRelative : newRelative;
            item.absolutePath = item.deletesFile ? oldAbsolute : newAbsolute;
            const QString key = item.relativePath.normalized(QString::NormalizationForm_C).toCaseFolded();
            const QFileInfo info(item.absolutePath);
            if (targets.contains(key)) item.diagnostics.append({file.patchLine, QStringLiteral("补丁含重复或大小写/Unicode 别名目标")});
            targets.insert(key);
            if (item.selected && item.diagnostics.isEmpty()) {
                if (item.createsFile && info.exists()) item.diagnostics.append({file.patchLine, QStringLiteral("创建目标已经存在")});
                else if (!item.createsFile && (!info.exists() || !info.isFile())) item.diagnostics.append({file.patchLine, QStringLiteral("原文件不存在或不是普通文件")});
                else if (!item.createsFile) {
                    QFile input(item.absolutePath);
                    if (!input.open(QIODevice::ReadOnly)) item.diagnostics.append({file.patchLine, input.errorString()});
                    else if (input.size() > MaximumBytes) item.diagnostics.append({file.patchLine, QStringLiteral("原文件超过 32 MiB 预演限制")});
                    else {
                        item.originalBytes = input.read(MaximumBytes + 1);
                        if (input.error() != QFile::NoError || item.originalBytes.size() > MaximumBytes)
                            item.diagnostics.append({file.patchLine, QStringLiteral("读取原文件失败或文件在读取时超出限制")});
                    }
                }
                if (item.diagnostics.isEmpty()) {
                    const BytesPreview bytes = previewBytes(file, item.originalBytes, options, index);
                    item.applicable = bytes.ok;
                    item.resultBytes = bytes.resultBytes;
                    item.hunks = bytes.hunks;
                    item.diagnostics = bytes.diagnostics;
                    if (item.deletesFile && bytes.ok && !item.resultBytes.isEmpty()) {
                        item.applicable = false;
                        item.resultBytes.clear();
                        item.diagnostics.append({file.patchLine, QStringLiteral("删除文件的补丁未删除全部内容")});
                    }
                }
            }
        }
        if (!item.diagnostics.isEmpty() || (item.selected && !item.applicable)) failed = true;
        result.files.append(item);
    }
    result.ok = !failed;
    return result;
}

} }
