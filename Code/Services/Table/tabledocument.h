#ifndef LQCOMPARE_TABLEDOCUMENT_H
#define LQCOMPARE_TABLEDOCUMENT_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace LqCompare { namespace Table {

struct ParseOptions {
    // Empty selects comma/tab/semicolon/pipe automatically. Otherwise literal.
    QString delimiter;
    // Empty detects a Unicode BOM, falling back to strict UTF-8.
    QByteArray encoding;
    bool firstRowHeader = true;
};

struct Document {
    static constexpr qint64 MaximumFileBytes = 32 * 1024 * 1024;
    QString path;
    QStringList headers;
    // Never padded: a missing trailing field is different from an empty field.
    QVector<QStringList> rows;
    QByteArray encoding;
    QString delimiter;
    bool hasHeader = true;
    QStringList warnings;

    int columnCount() const;
    // These operations replace the output only on success. Error is cleared on success.
    static bool parse(const QByteArray &bytes, Document *result, QString *error = nullptr,
                      const ParseOptions &options = {}, const QString &sourceName = {});
    // An empty path represents an empty side of a comparison.
    static bool load(const QString &path, Document *result, QString *error = nullptr,
                     const ParseOptions &options = {});
};

} }
#endif
