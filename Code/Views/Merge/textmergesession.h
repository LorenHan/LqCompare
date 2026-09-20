#ifndef LQCOMPARE_TEXTMERGESESSION_H
#define LQCOMPARE_TEXTMERGESESSION_H

#include "comparesession.h"
#include "mergeengine.h"
#include "mergeoutput.h"

namespace LqCompare {

// Character offsets use normalized editor text; original EOLs live in Document.
struct MergeOutputRange {
    int start = 0;
    int length = 0;
    Merge::Resolution resolution = Merge::Resolution::Unresolved;
    bool manuallyEdited = false;
    // A manual replacement joined content from several source blocks. The
    // user can edit/mark it resolved, or undo, but a source choice is ambiguous.
    bool manualGroup = false;
};

class TextMergeSession : public CompareSession {
    Q_OBJECT
public:
    explicit TextMergeSession(QObject *parent = nullptr);
    TextMergeSession(const QString &basePath, const QString &leftPath,
                     const QString &rightPath, const QString &outputPath,
                     QObject *parent = nullptr);
    QString basePath() const { return m_basePath; }
    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    QString outputPath() const { return m_file.path(); }
    bool hasBase() const { return !m_basePath.isEmpty(); }
    const Text::Document &baseDocument() const { return m_base; }
    const Text::Document &leftDocument() const { return m_left; }
    const Text::Document &rightDocument() const { return m_right; }
    const Text::Document &outputDocument() const { return m_snapshot.output; }
    QString outputText() const { return m_snapshot.output.normalizedText(); }
    const Merge::Result &mergeResult() const { return m_result; }
    const QVector<MergeOutputRange> &outputRanges() const { return m_snapshot.ranges; }
    int currentBlock() const { return m_currentBlock; }
    int unresolvedCount() const;
    bool hasSavedOutput() const { return m_file.hasSaved() && !isDirty() && unresolvedCount() == 0; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    // The application window owns shortcuts when this is false. Toolbar
    // actions remain usable; standalone views keep local bindings by default.
    void setUseLocalShortcuts(bool enabled);
    bool usesLocalShortcuts() const { return m_useLocalShortcuts; }

    bool setOutputPath(const QString &path, bool overwrite = false, QString *error = nullptr);
    bool setOutputText(const QString &text, QString *error = nullptr);
    bool resolveBlock(int block, Merge::Resolution choice, QString *error = nullptr);
    bool resolveCurrent(Merge::Resolution choice, QString *error = nullptr);
    bool markCurrentResolved(bool resolved, QString *error = nullptr);
    bool selectBlock(int block);
    void selectOutputPosition(int position);

public slots:
    void previousConflict();
    void nextConflict();
    void undo();
    void redo();

signals:
    void mergeChanged();
    void currentBlockChanged(int block);
    void outputPathChanged();
    void localShortcutsChanged(bool enabled);

protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    bool doSave(QString *error) override;
    bool canSaveNow() const override;

private:
    struct Snapshot {
        Text::Document output;
        QVector<MergeOutputRange> ranges;
    };
    void pushUndo();
    void changed();
    void updateStatus();
    void adjustRanges(int position, int removed, int added, int owner, bool manual);
    int blockAt(int position) const;
    QString m_basePath, m_leftPath, m_rightPath, m_initialOutputPath;
    Text::Document m_base, m_left, m_right;
    Merge::OutputFile m_file;
    Merge::Result m_result;
    Snapshot m_snapshot;
    QVector<Snapshot> m_undo, m_redo;
    int m_currentBlock = -1;
    bool m_useLocalShortcuts = true;
};

}
#endif
