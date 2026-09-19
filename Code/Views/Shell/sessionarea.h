#ifndef LQCOMPARE_SESSIONAREA_H
#define LQCOMPARE_SESSIONAREA_H

#include <QTabWidget>

namespace LqCompare {

class HomePage;

///
/// \brief The SessionArea class
/// 会话标签容器（PRD: SESS-010、SESS-003）。
///
/// 第 0 个标签固定是 Home 页且不可关闭；其余标签各持有一个会话。
/// 关闭最后一个会话后回到 Home 页，而不是留下空白窗口。
///
class SessionArea : public QTabWidget
{
    Q_OBJECT

public:
    explicit SessionArea(QWidget *parent = nullptr);

    /// Home 页指针，供主窗口连接 sessionTypeRequested。
    HomePage *homePage() const { return m_home; }

    /// 新建一个会话标签。返回该标签的索引。
    ///
    /// 目前只创建占位页：真正的会话视图按 SESS-001 / SESS-002 的契约在后续 issue 中接入。
    int addSession(const QString &title, const QString &typeId);

    /// 当前是否停留在 Home 页。
    bool isHomeCurrent() const;

    /// 会话标签数量（不含 Home 页）。
    int sessionCount() const;

public slots:
    /// 关闭当前标签；Home 页不可关闭。
    void closeCurrentSession();

signals:
    /// 会话标签数量变化（供状态栏与标题栏使用）。
    void sessionCountChanged(int count);

private:
    HomePage *m_home = nullptr;
};

} // namespace LqCompare

#endif // LQCOMPARE_SESSIONAREA_H
