#ifndef LQCOMPARE_TEXTCOMPARESESSION_H
#define LQCOMPARE_TEXTCOMPARESESSION_H

#include "comparesession.h"
#include "compareconclusion.h"
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
    ///
    /// \brief 「一处改动」的条数（TXT-005 起与 `comparison().differences.size()` 不再相等）。
    ///
    /// 引擎从 TXT-005 起会把一段改动铺成**若干个相邻的块**：够像的行配成一对
    /// （替换块），不够像的行各自成块（删除块 / 新增块）。于是「一处改动」
    /// 在引擎里可以是两三个块。界面上用户按一次「下一处」应当跳过**一整处**
    /// 而不是停在半途，复制一处改动也必须把这一整处搬过去——所以导航、状态栏
    /// 与复制都走这里，而不是直接数引擎块。
    ///
    /// 判据：`comparison().differences` 里**下标连续**的一串块属于同一处改动。
    /// 之所以用「下标连续」而不是别的规则：两种算法产出的区间本来就是
    /// 「相同段 / 非相同段」交替的，两处独立改动之间必然隔着一个相同段，
    /// 因此各自成块的两个非相同段**不可能**下标连续——这条判据不需要再引入
    /// 「行号是否相邻」之类的第二套口径。
    ///
    int differenceCount() const { return m_differenceRuns.size(); }
    /// 第 `index` 处改动在 `comparison().blocks` 里的下标区间；越界返回 -1。
    int differenceFirstBlock(int index) const;
    int differenceLastBlock(int index) const;
    /// `block` 落在第几处改动里；不属于任何一处时返回 -1。
    /// 光标移动时要用它把「当前块」翻回「第几处」，不能拿块下标去 `differences` 里
    /// 找位置——一处改动的第二块在那个列表里同样有位置，于是光标一进第二块
    /// 就会跳到另一处去。
    int differenceIndexOfBlock(int block) const;

    bool setPaths(const QString &left, const QString &right, QString *error = nullptr);
    void setComparisonOptions(const Text::CompareOptions &options);
    bool setText(bool left, const QString &text, QString *error = nullptr);
    bool saveSide(bool left, QString *error = nullptr);
    bool saveSideAs(bool left, const QString &path, bool overwrite, QString *error = nullptr);
    bool setEncoding(bool left, const QByteArray &codec, QString *error = nullptr);
    void setLineEnding(bool left, Text::Eol eol);

    // -----------------------------------------------------------------------
    // BOM（TXT-015）
    // -----------------------------------------------------------------------

    ///
    /// \brief BOM 这一维的判定（TXT-015 第 1、2 条）。
    ///
    /// 每次调用重新算，不做缓存：输入只有两侧文档的 BOM 事实与一个策略，
    /// 判定本身是常数时间，而缓存要跟着「换文件 / 改规则 / 重新加载」三件事
    /// 一起失效——漏掉一处就会在状态栏留下一句关于**上一对文件**的话。
    ///
    /// 会话未打开时返回 `BomConclusion::None`（不下结论）。
    ///
    Text::BomVerdict bomVerdict() const;

    ///
    /// \brief 本次比较的结论档（TXT-015 第 2 条）。
    ///
    /// 与 `differenceCount()` 的关系：后者数的是**行级差异块**，因此
    /// 「两侧只差 BOM、而策略要求把它算成差异」时它仍然是 0。要回答
    /// 「这两份文件到底算不算相同」必须问这里，这也是规格为什么要求
    /// 结论分「相同 / 规则相同 / 不同」三档而不是一个差异计数。
    ///
    Text::Conclusion conclusion() const;

    /// 保存侧的 BOM 策略（TXT-015 第 3 条）。按侧设置：两侧可以是两种保存目的。
    void setBomSavePolicy(bool left, Text::BomSavePolicy policy);
    Text::BomSavePolicy bomSavePolicy(bool left) const;

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
    /// 保存侧的 BOM 策略（TXT-015 第 3 条）。单独存而不塞进 `Document` 是因为
    /// `Document` 会在每次 `loadPair()` 时被整体替换——策略是会话级的偏好，
    /// 重新加载不该把它忘掉（忘了的表现是「改了策略、一重新加载就回到默认」）。
    Text::BomSavePolicy m_leftBomSavePolicy = Text::BomSavePolicy::Preserve;
    Text::BomSavePolicy m_rightBomSavePolicy = Text::BomSavePolicy::Preserve;
    int m_currentDifference = -1;
    /// 每一处改动对应的块下标区间（闭区间），见 `differenceCount()`。
    QVector<Text::DifferenceRun> m_differenceRuns;
    QVector<BufferState> m_undo;
    QVector<BufferState> m_redo;
    bool m_leftReadOnly = false;
    bool m_rightReadOnly = false;
    bool m_useLocalShortcuts = true;
};

}
#endif
