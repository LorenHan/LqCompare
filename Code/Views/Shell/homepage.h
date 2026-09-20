#ifndef LQCOMPARE_HOMEPAGE_H
#define LQCOMPARE_HOMEPAGE_H

#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace LqCompare {

///
/// \brief The HomePage class
/// 会话入口首屏（PRD: SESS-003）。
///
/// Home 页不参与比对，它只做三件事：按类别列出新建会话入口、展示最近会话、
/// 提供会话树浏览入口。前两项在本类实现，会话树留给 SESS-004。
///
class HomePage : public QWidget
{
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);

    /// 记录一次会话打开，用于「最近会话」区（SESS-009 的界面部分）。
    void setRecentSessions(const QList<QPair<QString, QString>> &entries);
    void setTypeAvailable(const QString &typeId, bool available, const QString &reason = QString());

signals:
    /// 用户选择了某个会话类型的新建入口。
    /// \param typeId 会话类型 ID，取值见 Services/Session/sessiontype.h
    void sessionTypeRequested(const QString &typeId);
    void recentSessionRequested(int index);

private:
    struct Section
    {
        QString title;
        QString hint;
        QList<QPair<QString, QString>> entries; ///< (类型 ID, 卡片标题)
    };

    void buildSections();
    QList<Section> sections() const;

    QVBoxLayout *m_body = nullptr;
    QVBoxLayout *m_recentLayout = nullptr;
    QLabel *m_recentEmptyHint = nullptr;
    int m_recentCount = 0;
};

} // namespace LqCompare

#endif // LQCOMPARE_HOMEPAGE_H
