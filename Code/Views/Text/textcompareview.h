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
    ///
    /// \brief 号码槽里当前铺的原文行号（0 起；-1 = 这一行在这一侧不存在）。
    ///
    /// 为什么要把这份数据变成公开接口：**正文与号码是两条链**——正文由
    /// `setPlainText()` 铺，号码由 `setLineNumbers()` 铺，两者取自同一个数组却
    /// 各走各的路。只断言正文的话，「号码传成了另一侧那份数组」（号码与正文整体
    /// 错位一格）或「号码根本没传下去」（号码槽全空）都不会让任何用例变红，
    /// 而这两件事恰恰是「行号与行高严格对齐」要防的东西——用户按行号读出来的
    /// 结论会与屏幕上真正显示的内容对不上。
    ///
    /// 这与 TXT-009 那条纪律是同一个来源：**UI 值与 UI 文案同源不同路时，
    /// 两条链都要有断言**（见 handoff §6）。
    ///
    QVector<int> lineNumbers() const { return m_numbers; }
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
    /// 保存侧的 BOM 策略（TXT-015 第 3 条）。**按侧**存：两侧的保存目的可以不同，
    /// 而比较规则那一排（`m_bomPolicy`）是成对的，两者不能共用一个控件。
    QComboBox *m_bomSaves[2] = {};
    QPushButton *m_copyToRight = nullptr;
    QPushButton *m_copyToLeft = nullptr;
    QLabel *m_summary = nullptr;
    QCheckBox *m_ignoreCase = nullptr;
    QCheckBox *m_ignoreEol = nullptr;
    QCheckBox *m_ignoreFinal = nullptr;
    QComboBox *m_whitespace = nullptr;
    /// BOM 处理策略（TXT-015 第 1 条）。与其余比较规则同排，因为它就是一条比较规则：
    /// 规格写的入口（会话设置 → 格式）所在的那张页属 `OPT-007`，尚未落地，
    /// 与 `DIR-003` 的档位下拉同一处置。
    QComboBox *m_bomPolicy = nullptr;
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
