#ifndef LQCOMPARE_REPORT_H
#define LQCOMPARE_REPORT_H

#include "textdiff.h"
#include "foldercompare.h"
#include <QDateTime>
#include <QIODevice>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>

namespace LqCompare { namespace Report {

enum class Format { Html, PlainText };
enum class Layout { SideBySide, Interleaved, Summary, Statistics };
enum class State { Equal, Changed, LeftOnly, RightOnly, Ignored, Conflict, Error, Unknown };
enum class Kind { Text, Folder };

struct Metadata {
    QString title;
    QString leftSource;
    QString rightSource;
    QString toolVersion = QStringLiteral("LqCompare");
    QDateTime generatedAt = QDateTime::currentDateTimeUtc();
    QStringList settings;
};

struct Row {
    State state = State::Equal;
    QString path;
    QString left;
    QString right;
    QString detail;
    int leftLine = 0; // One-based; zero means missing.
    int rightLine = 0;
    bool directory = false;
    bool visible = true; // Caller may apply the active view's display filter.
};

struct Model {
    Kind kind = Kind::Text;
    Metadata metadata;
    QVector<Row> rows;
    QStringList warnings;
    bool complete = true;
};

struct Options {
    Format format = Format::Html;
    Layout layout = Layout::SideBySide;
    bool includeEqual = false;
    bool includeIgnored = false;
    bool includeOrphans = true;
    bool showLineNumbers = true;
    bool utf8Bom = true;
    int rowsPerGroup = 250; // HTML's later groups are initially collapsed.
};

struct Statistics {
    qint64 total = 0;
    qint64 equal = 0;
    qint64 changed = 0;
    qint64 leftOnly = 0;
    qint64 rightOnly = 0;
    qint64 ignored = 0;
    qint64 conflicts = 0;
    qint64 errors = 0;
    qint64 unknown = 0;
    qint64 directories = 0;
    qint64 exported = 0;
    bool complete = true;
    bool hasDifferences() const { return changed + leftOnly + rightOnly + conflicts > 0; }
};

using Progress = std::function<void(qint64 processed, qint64 total)>;

Model fromText(const Text::Document &left, const Text::Document &right,
               const Text::Result &result, const Text::CompareOptions &compareOptions = {},
               const Metadata &metadata = {});
Model fromFolder(const Folder::Result &result, const Folder::Options &compareOptions = {},
                 const Metadata &metadata = {});
Statistics statistics(const Model &model, const Options &options = {});
QString stateLabel(State state);

// Stateless synchronous engine, suitable for a worker thread. Output is streamed;
// writeFile discards its temporary file on cancellation or any write failure.
// The caller owns its model snapshot and marshals progress callbacks to the UI.
bool write(QIODevice *device, const Model &model, const Options &options = {},
           QString *error = nullptr, const std::atomic_bool *cancelled = nullptr,
           const Progress &progress = {});
bool writeFile(const QString &path, const Model &model, const Options &options = {},
               QString *error = nullptr, const std::atomic_bool *cancelled = nullptr,
               const Progress &progress = {}, bool overwrite = false);
// Same renderer, intentionally buffered for clipboard / small previews.
// Returns decoded Unicode without BOM. File encoding is UTF-8, optionally BOM.
QString render(const Model &model, const Options &options = {}, QString *error = nullptr);

} }
#endif
