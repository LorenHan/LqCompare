#include "homepage.h"
#include "sessiontype.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace LqCompare {

HomePage::HomePage(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    auto *content = new QWidget(scroll);
    scroll->setWidget(content);

    m_body = new QVBoxLayout(content);
    m_body->setContentsMargins(24, 20, 24, 20);
    m_body->setSpacing(16);

    auto *heading = new QLabel(tr("LqCompare"), content);
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 8);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    m_body->addWidget(heading);

    auto *subtitle = new QLabel(
        tr("File and folder comparison. Pick a session type below, "
           "or drag files and folders onto this window."),
        content);
    subtitle->setWordWrap(true);
    m_body->addWidget(subtitle);

    buildSections();

    // --- 最近会话（SESS-009 的界面部分） -----------------------------------
    auto *recentTitle = new QLabel(tr("Recent Sessions"), content);
    QFont sectionFont = recentTitle->font();
    sectionFont.setBold(true);
    recentTitle->setFont(sectionFont);
    m_body->addWidget(recentTitle);

    m_recentLayout = new QVBoxLayout();
    m_recentLayout->setSpacing(4);
    m_body->addLayout(m_recentLayout);

    m_recentEmptyHint = new QLabel(tr("Nothing opened yet."), content);
    m_recentEmptyHint->setForegroundRole(QPalette::PlaceholderText);
    m_recentLayout->addWidget(m_recentEmptyHint);

    m_body->addStretch(1);
}

void HomePage::buildSections()
{
    for (const Section &section : sections()) {
        auto *title = new QLabel(section.title, this);
        QFont font = title->font();
        font.setBold(true);
        title->setFont(font);
        m_body->addWidget(title);

        auto *hint = new QLabel(section.hint, this);
        hint->setWordWrap(true);
        hint->setForegroundRole(QPalette::PlaceholderText);
        m_body->addWidget(hint);

        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        for (const auto &entry : section.entries) {
            const QString typeId = entry.first;
            auto *button = new QPushButton(entry.second, this);
            button->setObjectName(QStringLiteral("newSession-") + typeId);
            const bool available = typeId == QLatin1String("text") || typeId == QLatin1String("folder");
            button->setEnabled(available);
            button->setToolTip(available ? tr("New %1").arg(entry.second)
                                        : tr("This comparison type is not yet available."));
            connect(button, &QPushButton::clicked, this,
                    [this, typeId]() { emit sessionTypeRequested(typeId); });
            row->addWidget(button);
        }
        row->addStretch(1);
        m_body->addLayout(row);
    }
}

QList<HomePage::Section> HomePage::sections() const
{
    QList<Section> result;
    for (const auto group : {SessionGroup::Text, SessionGroup::Folders,
                            SessionGroup::Data, SessionGroup::Advanced}) {
        Section section;
        section.title = sessionGroupLabel(group);
        for (const auto &type : builtInSessionTypes()) {
            if (type.group == group) section.entries.append({type.id, type.displayName});
        }
        result.append(section);
    }
    return result;
}

void HomePage::setRecentSessions(const QList<QPair<QString, QString>> &entries)
{
    while (QLayoutItem *item = m_recentLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_recentEmptyHint = nullptr;
    for (int i = 0; i < entries.size(); ++i) {
        auto *button = new QPushButton(entries[i].first + QStringLiteral("\n") + entries[i].second, this);
        button->setObjectName(QStringLiteral("recentSession-%1").arg(i));
        button->setToolTip(entries[i].second);
        connect(button, &QPushButton::clicked, this, [this, i] { emit recentSessionRequested(i); });
        m_recentLayout->addWidget(button);
    }
    if (entries.isEmpty()) m_recentLayout->addWidget(new QLabel(tr("Nothing opened yet."), this));
}

void HomePage::setTypeAvailable(const QString &typeId, bool available, const QString &reason)
{
    if (auto *button = findChild<QPushButton *>(QStringLiteral("newSession-") + typeId)) {
        button->setEnabled(available);
        button->setToolTip(available ? tr("Start a new comparison") : reason);
    }
}

} // namespace LqCompare
