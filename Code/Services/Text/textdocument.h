#ifndef LQCOMPARE_TEXTDOCUMENT_H
#define LQCOMPARE_TEXTDOCUMENT_H

#include <QByteArray>
#include <QString>
#include <QVector>

namespace LqCompare { namespace Text {

enum class Eol { None, LF, CRLF, CR };
struct Line {
    QString text;
    Eol eol = Eol::None;
    bool operator==(const Line &other) const { return text == other.text && eol == other.eol; }
};

// A lossless decoded document. Display normalization never changes stored endings.
class Document {
public:
    static constexpr qint64 MaximumFileBytes = 32 * 1024 * 1024;
    static constexpr int MaximumLines = 500000;
    static bool load(const QString &path, Document *result, QString *error = nullptr,
                     const QByteArray &codecName = QByteArray());
    static bool decode(const QByteArray &bytes, Document *result, QString *error = nullptr,
                       const QByteArray &codecName = QByteArray());
    static QVector<Line> splitLines(const QString &text);
    const QVector<Line> &lines() const { return m_lines; }
    QString path() const { return m_path; }
    QByteArray codecName() const { return m_codec; }
    bool hasBom() const { return m_bom; }
    int decodingErrors() const { return m_decodeErrors; }
    QString warning() const;
    QString eolDescription() const;
    Eol preferredEol() const;
    QString normalizedText() const;
    QByteArray bytes(QString *error = nullptr) const;
    bool isModified() const;
    bool canEdit() const { return m_decodeErrors == 0 && !m_binary && !m_paragraphSeparators; }
    // Text from an editor uses LF. Preserve existing endings for unchanged lines.
    bool setNormalizedText(const QString &text, QString *error = nullptr);
    bool replaceLines(int start, int count, const QVector<Line> &replacement,
                      QString *error = nullptr);
    void setEol(Eol eol);
    bool checkUnchangedOnDisk(QString *error = nullptr) const;
    // Includes original file identity, so aliases remain protected after a
    // source path is replaced or a symbolic link is redirected.
    bool refersToPath(const QString &path) const;
    bool save(QString *error = nullptr);
    // Existing unrelated destinations require an explicit overwrite choice.
    bool saveAs(const QString &path, bool overwrite, QString *error = nullptr);

private:
    bool write(const QString &path, QString *error, bool protectOriginal = false);
    QVector<Line> m_lines;
    QString m_path;
    QByteArray m_codec = "UTF-8";
    QByteArray m_originalBytes;
    QByteArray m_originalIdentity;
    QString m_originalCanonicalPath;
    bool m_bom = false;
    bool m_binary = false;
    bool m_paragraphSeparators = false;
    int m_decodeErrors = 0;
};

QString eolText(Eol eol);

} }
#endif
