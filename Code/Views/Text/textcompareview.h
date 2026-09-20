#ifndef LQCOMPARE_TEXTCOMPAREVIEW_H
#define LQCOMPARE_TEXTCOMPAREVIEW_H

#include <QPlainTextEdit>
#include <QWidget>
#include <QVector>

class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QCheckBox;
class QShortcut;

namespace LqCompare {
class TextCompareSession;

class TextPane : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit TextPane(QWidget *parent = nullptr);
    void setLineNumbers(const QVector<int> &numbers);
    void paintGutter(QPaintEvent *event);
signals:
    void activated();
protected:
    void resizeEvent(QResizeEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
private:
    void updateGutter();
    QWidget *m_gutter;
    QVector<int> m_numbers;
    int m_gutterWidth = 44;
};

class TextCompareView : public QWidget {
    Q_OBJECT
public:
    explicit TextCompareView(TextCompareSession *session, QWidget *parent = nullptr);
    bool findText(const QString &text, bool backward = false, bool caseSensitive = false);
    bool goToLine(int oneBasedLine, bool left = true);
    void promptFind();
    void promptGoToLine();
    void edit(bool left);
    void setUseLocalShortcuts(bool enabled);
private:
    void refresh();
    void refreshActions();
    void highlight();
    void locate(int difference);
    void save(bool left, bool saveAs);
    void showError(const QString &message);
    QWidget *makeSide(bool left);
    TextCompareSession *m_session;
    TextPane *m_panes[2] = {};
    QLineEdit *m_paths[2] = {};
    QLabel *m_metadata[2] = {};
    QPushButton *m_edit[2] = {};
    QPushButton *m_save[2] = {};
    QPushButton *m_saveAs[2] = {};
    QComboBox *m_encodings[2] = {};
    QComboBox *m_endings[2] = {};
    QPushButton *m_copyToRight = nullptr;
    QPushButton *m_copyToLeft = nullptr;
    QLabel *m_summary = nullptr;
    QCheckBox *m_ignoreCase = nullptr;
    QCheckBox *m_ignoreEol = nullptr;
    QCheckBox *m_ignoreFinal = nullptr;
    QComboBox *m_whitespace = nullptr;
    QPushButton *m_undo = nullptr;
    QPushButton *m_redo = nullptr;
    bool m_scrolling = false;
    bool m_selectingText = false;
    int m_activeSide = 0;
    QString m_searchText;
    QVector<QShortcut *> m_shortcuts;
};
}
#endif
