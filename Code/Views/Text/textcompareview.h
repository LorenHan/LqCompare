#ifndef LQCOMPARE_TEXTCOMPAREVIEW_H
#define LQCOMPARE_TEXTCOMPAREVIEW_H

#include <QPlainTextEdit>
#include <QWidget>
#include <QVector>
#include "textdiff.h"

class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QCheckBox;
class QShortcut;
class QSpinBox;

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
    /// 单个空白模式的界面文案。
    ///
    /// 之所以放在公开接口上而不是留在 .cpp 的匿名命名空间里：**下拉里第 i 行显示的
    /// 文案必须就是模式表第 i 项的文案**，而这件事只有从外面才验得到。原本的
    /// 「写死三行文案 + `static_cast<Whitespace>(currentIndex())`」烂掉的方式，
    /// 正是文案与序号脱钩——把下拉的铺法改成倒序，值那条路照样对得上，
    /// 只有用户看到的字变了，任何用例都不会红（TXT-009 第一版实测漏检过）。
    /// 断言要靠这个函数取「期望文案」，所以它必须是可见的。
    static QString whitespaceLabel(Text::Whitespace mode);
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
    // TXT-005：相似行对齐的总开关与阈值。两者是一组——阈值在开关关掉时
    // 不生效，因此界面上必须把阈值一起禁用，否则用户会以为调了没用。
    QCheckBox *m_alignSimilar = nullptr;
    QSpinBox *m_similarity = nullptr;
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
