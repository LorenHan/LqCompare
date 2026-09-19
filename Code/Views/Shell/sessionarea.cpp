#include "sessionarea.h"

#include "homepage.h"
#include "logging.h"

#include <QLabel>
#include <QPalette>
#include <QTabBar>
#include <QVBoxLayout>

namespace LqCompare {

SessionArea::SessionArea(QWidget *parent) : QTabWidget(parent)
{
    setDocumentMode(true);
    setTabsClosable(true);
    setMovable(true);

    m_home = new HomePage(this);
    const int homeIndex = addTab(m_home, tr("Home"));
    tabBar()->setTabButton(homeIndex, QTabBar::RightSide, nullptr);
    setCurrentIndex(homeIndex);

    connect(this, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (index <= 0) {
            return; // Home 页不可关闭
        }
        QWidget *page = widget(index);
        removeTab(index);
        if (page && page != m_home) {
            page->deleteLater();
        }
        emit sessionCountChanged(sessionCount());
    });
}

int SessionArea::addSession(const QString &title, const QString &typeId)
{
    // 会话视图的占位实现：真正的视图按 SESS-001 的契约接入后替换这里。
    // 现在给出足够信息，避免用户以为是空白窗口。
    auto *placeholder = new QWidget(this);
    auto *layout = new QVBoxLayout(placeholder);
    layout->addStretch(1);

    auto *heading = new QLabel(title, placeholder);
    QFont font = heading->font();
    font.setPointSize(font.pointSize() + 4);
    font.setBold(true);
    heading->setFont(font);
    heading->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);

    auto *detail = new QLabel(
        tr("Session type: %1\n\n"
           "This session view is not implemented yet. Session types, their views and "
           "their settings are specified in docs/PRD-actions.md; the matching issue "
           "tracks progress.")
            .arg(typeId),
        placeholder);
    detail->setAlignment(Qt::AlignCenter);
    detail->setWordWrap(true);
    detail->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(detail);
    layout->addStretch(2);

    const int index = addTab(placeholder, title);
    setCurrentIndex(index);
    emit sessionCountChanged(sessionCount());
    LQCOMPARE_INFO("session", QStringLiteral("新建会话标签：%1（类型 %2）").arg(title, typeId));
    return index;
}

bool SessionArea::isHomeCurrent() const
{
    return currentIndex() <= 0;
}

int SessionArea::sessionCount() const
{
    return count() > 0 ? count() - 1 : 0;
}

void SessionArea::closeCurrentSession()
{
    const int index = currentIndex();
    if (index <= 0) {
        return;
    }
    QWidget *page = widget(index);
    removeTab(index);
    if (page && page != m_home) {
        page->deleteLater();
    }
    emit sessionCountChanged(sessionCount());
}

} // namespace LqCompare
