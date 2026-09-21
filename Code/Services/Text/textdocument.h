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
    ///
    /// \brief 文件里是否**同时**存在两种及以上行尾风格（LF / CRLF / CR）。
    ///
    /// 为什么单独做一个判定而不是让调用方去嗅 `eolDescription()` 的文本：
    /// 状态栏要靠它决定「要不要亮警告图标」（TXT-010 第 4 条），而文案是给
    /// 用户看的、随时可能改词或加计数，判定不该跟着一起动。这里是唯一的事实来源。
    ///
    /// **末尾无换行不算一种风格**：`"a\r\nb"` 的末行是 `Eol::None`，它表示
    /// 「文件没有以换行收尾」，与 LF/CRLF/CR 的选择是两条独立的事（同 `TXT-010`
    /// 第 2 条）。若把 `None` 也算进去，任何不以换行收尾的文件都会被报成「混合」，
    /// 警告图标就会一直亮着——叫狼来了的提示等于没有提示。
    ///
    bool hasMixedEndings() const;
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
    /// 按 `static_cast<int>(Eol)` 把行尾次数统计到 `counts[0..3]`。
    /// `eolDescription()` / `preferredEol()` / `hasMixedEndings()` 共用它，
    /// 免得三处各写一遍循环、其中一处哪天忘了把 `None` 排除掉。
    void countEndings(int *counts) const;
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
