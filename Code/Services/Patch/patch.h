#ifndef LQCOMPARE_PATCH_H
#define LQCOMPARE_PATCH_H

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

namespace LqCompare { namespace Patch {

struct Diagnostic {
    int line = 0; // One-based patch line, or zero for an input/filesystem error.
    QString message;
};

struct FileInput {
    QString oldPath;
    QString newPath;
    QByteArray oldBytes;
    QByteArray newBytes;
    bool oldExists = true;
    bool newExists = true;
};
struct GenerateOptions {
    int contextLines = 3;
    QString oldPrefix = QStringLiteral("a/");
    QString newPrefix = QStringLiteral("b/");
};
struct GenerateResult {
    bool ok = false;
    QByteArray bytes;
    QVector<Diagnostic> diagnostics;
    int fileCount = 0;
    int hunkCount = 0;
    int addedLines = 0;
    int removedLines = 0;
    bool alignmentLimited = false;
};

// Paths are repository-relative. Raw bytes are never display-normalized. NUL
// data (including UTF-16) is rejected; git binary patches are not synthesized.
GenerateResult generate(const QVector<FileInput> &files,
                        const GenerateOptions &options = GenerateOptions());
// Explicit patch export only, never application to source files. QSaveFile
// replaces the destination atomically; existing destinations require overwrite.
bool writeFile(const QString &path, const QByteArray &patchBytes,
               QString *error = nullptr, bool overwrite = false);

struct Line {
    char kind = ' '; // ' ', '+', '-'
    QByteArray bytes; // Original content plus LF, unless no-final-newline marker.
    int patchLine = 0;
};
struct Hunk {
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
    int patchLine = 0;
    QVector<Line> lines;
};
struct FilePatch {
    QString oldPath; // /dev/null denotes a file creation.
    QString newPath; // /dev/null denotes a file deletion.
    int patchLine = 0;
    QVector<Hunk> hunks;
};
struct Document { QVector<FilePatch> files; };
struct ParseOptions {
    // Normally CR in a hunk belongs to the original file. Set this only when
    // an entire patch was converted to CRLF in transit; inference is ambiguous.
    bool stripTransportCr = false;
};
struct ParseResult {
    bool ok = false;
    Document document;
    QVector<Diagnostic> diagnostics;
};
ParseResult parse(const QByteArray &bytes, const ParseOptions &options = ParseOptions());

struct ApplyOptions {
    bool reverse = false;
    int stripComponents = 1; // git a/ and b/ paths; use zero for svn/GNU names.
    int maximumOffset = 1000; // Exact complete context only; never discard context.
    // Missing file index selects all hunks. Present empty set selects none.
    QHash<int, QSet<int>> selectedHunks;
};
struct HunkPreview {
    int index = 0;
    int patchLine = 0;
    bool selected = true;
    bool applicable = false;
    int offset = 0;
    QString error;
};
struct BytesPreview {
    bool ok = false;
    QByteArray resultBytes; // Empty on failure, never a partially applied result.
    QVector<HunkPreview> hunks;
    QVector<Diagnostic> diagnostics;
};
BytesPreview previewBytes(const FilePatch &file, const QByteArray &original,
                          const ApplyOptions &options = ApplyOptions(), int fileIndex = 0);

struct FilePreview {
    QString relativePath;
    QString absolutePath;
    bool createsFile = false;
    bool deletesFile = false;
    bool selected = true;
    bool applicable = false;
    QByteArray originalBytes;
    QByteArray resultBytes;
    QVector<HunkPreview> hunks;
    QVector<Diagnostic> diagnostics;
};
struct PreviewResult {
    bool ok = false;
    QString root;
    QVector<FilePreview> files;
    QVector<Diagnostic> diagnostics;
};
// Read-only plan. Rejects absolute/traversal paths before stripping, symlink
// components, nonregular targets, duplicate targets, and renames. This API does
// not write files or promise a transaction; callers must never auto-apply it.
PreviewResult preview(const Document &document, const QString &root,
                      const ApplyOptions &options = ApplyOptions());

} }
#endif
