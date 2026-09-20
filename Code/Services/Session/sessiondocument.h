#ifndef LQCOMPARE_SESSIONDOCUMENT_H
#define LQCOMPARE_SESSIONDOCUMENT_H
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace LqCompare {
struct SessionDocument {
    QString typeId = QStringLiteral("text");
    QString leftPath;
    QString rightPath;
    QString basePath;
    QString outputPath;
    QString title;
    QString notes;
    QVariantMap settings;
    QJsonObject original; // Preserve fields this version does not understand.

    static bool load(const QString &path, SessionDocument *document, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const;
    QJsonObject toJson(const QString &baseDirectory = QString()) const;
    static bool fromJson(const QJsonObject &json, const QString &baseDirectory,
                         SessionDocument *document, QString *error = nullptr);
};
}
#endif
