#include "homepage.h"

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace LqCompare {

namespace {

QFrame *makeCard(const QString &title, const QString &detail, QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setFrameShape(QFrame::StyledPanel);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(2);

    auto *titleLabel = new QLabel(title, card);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setWordWrap(true);

    auto *detailLabel = new QLabel(detail, card);
    detailLabel->setWordWrap(true);
    detailLabel->setForegroundRole(QPalette::PlaceholderText);

    layout->addWidget(titleLabel);
    layout->addWidget(detailLabel);
    return card;
}

} // namespace

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
            button->setToolTip(tr("New session: %1 (type id: %2)").arg(entry.second, typeId));
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
    // 分组与 Beyond Compare 的 Home 视图一致：文本类 / 文件夹类 / 数据类 / 其它。
    return {
        {tr("Text"), tr("Line-oriented comparison with configurable ignore rules."),
         {{QStringLiteral("text"), tr("Text Compare")},
          {QStringLiteral("text-merge"), tr("Text Merge")},
          {QStringLiteral("text-edit"), tr("Text Edit")},
          {QStringLiteral("text-patch"), tr("Text Patch")}}},
        {tr("Folders"), tr("Directory trees, synchronization and three-way merge."),
         {{QStringLiteral("folder"), tr("Folder Compare")},
          {QStringLiteral("folder-sync"), tr("Folder Sync")},
          {QStringLiteral("folder-merge"), tr("Folder Merge")}}},
        {tr("Data"), tr("Cell-oriented, byte-oriented and media comparison."),
         {{QStringLiteral("table"), tr("Table Compare")},
          {QStringLiteral("hex"), tr("Hex Compare")},
          {QStringLiteral("picture"), tr("Picture Compare")},
          {QStringLiteral("media"), tr("Media Compare")}}},
        {tr("System"), tr("Platform-specific session types."),
         {{QStringLiteral("registry"), tr("Registry Compare")},
          {QStringLiteral("version"), tr("Version Compare")},
          {QStringLiteral("archive"), tr("Archive Compare")}}},
    };
}

void HomePage::rememberSession(const QString &title, const QString &detail)
{
    if (!m_recentLayout) {
        return;
    }
    if (m_recentCount == 0 && m_recentEmptyHint) {
        m_recentEmptyHint->hide();
    }
    auto *row = makeCard(title, detail, this);
    m_recentLayout->addWidget(row);
    ++m_recentCount;
}

} // namespace LqCompare
