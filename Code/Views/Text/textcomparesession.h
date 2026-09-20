#ifndef LQCOMPARE_TEXTCOMPARESESSION_H
#define LQCOMPARE_TEXTCOMPARESESSION_H

#include "comparesession.h"
#include "textdiff.h"

namespace LqCompare {

class TextCompareSession : public CompareSession {
    Q_OBJECT
public:
    explicit TextCompareSession(QObject *parent = nullptr);
    TextCompareSession(const QString &leftPath, const QString &rightPath, QObject *parent = nullptr);
    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    const Text::Document &leftDocument() const { return m_left; }
    const Text::Document &rightDocument() const { return m_right; }
    const Text::Result &comparison() const { return m_result; }
    Text::CompareOptions comparisonOptions() const { return m_options; }
    int currentDifference() const { return m_currentDifference; }
    bool setPaths(const QString &left, const QString &right, QString *error = nullptr);
    void setComparisonOptions(const Text::CompareOptions &options);
    bool setText(bool left, const QString &text, QString *error = nullptr);
    bool saveSide(bool left, QString *error = nullptr);
    bool saveSideAs(bool left, const QString &path, bool overwrite, QString *error = nullptr);
    bool setEncoding(bool left, const QByteArray &codec, QString *error = nullptr);
    void setLineEnding(bool left, Text::Eol eol);
    bool copyDifference(bool leftToRight, QString *error = nullptr);
    void selectDifference(int index);
    // Read-only is enforced at every mutation API, independently of UI state.
    // Existing dirty buffers are retained; history cannot modify a locked side.
    void setReadOnly(bool left, bool right);
    bool isSideReadOnly(bool left) const { return left ? m_leftReadOnly : m_rightReadOnly; }
    // Shells with a command binder disable these before or after view creation.
    void setUseLocalShortcuts(bool enabled);
    bool usesLocalShortcuts() const { return m_useLocalShortcuts; }
    bool canUndo() const;
    bool canRedo() const;
    bool findText(const QString &text, bool backward = false, bool caseSensitive = false);
    bool goToLine(int oneBasedLine, bool left = true);

public slots:
    void previousDifference();
    void nextDifference();
    void firstDifference();
    void lastDifference();
    void undo();
    void redo();
    void findText();
    void goToLine();
    void editSide(bool left);

signals:
    void comparisonChanged();
    void currentDifferenceChanged(int index);
    void pathsChanged();
    void readOnlyChanged();
    void localShortcutsChanged(bool enabled);

protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    bool doSave(QString *error) override;
    bool canSaveNow() const override;

private:
    bool loadPair(const QString &left, const QString &right, QString *error);
    void recompute();
    void updateStatus();
    void updateTitle();
    struct BufferState {
        QVector<Text::Line> left;
        QVector<Text::Line> right;
    };
    BufferState bufferState() const { return {m_left.lines(), m_right.lines()}; }
    void recordChange(const BufferState &before);
    void restoreBuffers(const BufferState &buffers);
    bool canRestoreBuffers(const BufferState &buffers) const;
    static void appendHistory(QVector<BufferState> &history, const BufferState &buffers);
    Text::Document m_left;
    Text::Document m_right;
    Text::Result m_result;
    Text::CompareOptions m_options;
    QString m_leftPath;
    QString m_rightPath;
    QByteArray m_leftCodec;
    QByteArray m_rightCodec;
    int m_currentDifference = -1;
    QVector<BufferState> m_undo;
    QVector<BufferState> m_redo;
    bool m_leftReadOnly = false;
    bool m_rightReadOnly = false;
    bool m_useLocalShortcuts = true;
};

}
#endif
