#include "homepage.h"
#include "sessiontype.h"

#include <QApplication>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTest>
#include <QTranslator>

using namespace LqCompare;

namespace {

const QString buttonPrefix = QStringLiteral("newSession-");

struct DisplayedEntry {
    QPushButton *button;
    QString groupLabel;
};

// Read layout order, not QObject child-creation order. Recognize catalog group
// titles so adding an optional hint does not change a row's group association.
void collectEntries(QLayout *layout, const QStringList &groupLabels, QString &precedingLabel,
                    QVector<DisplayedEntry> &entries)
{
    if (!layout)
        return;
    for (int index = 0; index < layout->count(); ++index) {
        QLayoutItem *item = layout->itemAt(index);
        if (item->layout()) {
            collectEntries(item->layout(), groupLabels, precedingLabel, entries);
            continue;
        }
        QWidget *widget = item->widget();
        if (!widget)
            continue;
        if (auto *scroll = qobject_cast<QScrollArea *>(widget)) {
            if (scroll->widget())
                collectEntries(scroll->widget()->layout(), groupLabels, precedingLabel, entries);
        } else if (auto *label = qobject_cast<QLabel *>(widget)) {
            if (groupLabels.contains(label->text()))
                precedingLabel = label->text();
        } else if (auto *button = qobject_cast<QPushButton *>(widget)) {
            if (button->objectName().startsWith(buttonPrefix))
                entries.append({button, precedingLabel});
        } else {
            collectEntries(widget->layout(), groupLabels, precedingLabel, entries);
        }
    }
}

void verifyHomeMatchesRegistry(HomePage &home, const SessionTypeRegistry &registry)
{
    QVector<DisplayedEntry> displayed;
    QStringList groupLabels;
    for (SessionGroup group : registry.groups(false))
        groupLabels.append(sessionGroupLabel(group));
    QString precedingLabel;
    collectEntries(home.layout(), groupLabels, precedingLabel, displayed);
    QCOMPARE(displayed.size(), registry.count());

    QList<QPushButton *> allEntryButtons;
    for (QPushButton *button : home.findChildren<QPushButton *>()) {
        if (button->objectName().startsWith(buttonPrefix))
            allEntryButtons.append(button);
    }
    // Also catch duplicate/extra buttons that were created outside the layout.
    QCOMPARE(allEntryButtons.size(), registry.count());

    int position = 0;
    for (SessionGroup group : registry.groups(false)) {
        const QString groupLabel = sessionGroupLabel(group);
        int matchingHeaders = 0;
        for (QLabel *label : home.findChildren<QLabel *>()) {
            if (label->text() == groupLabel)
                ++matchingHeaders;
        }
        QCOMPARE(matchingHeaders, 1);

        // Include platform-restricted entries: Home keeps the complete catalog,
        // and availability is controlled separately by setTypeAvailable().
        for (const SessionTypeEntry *entry : registry.byGroup(group, false)) {
            const QString objectName = buttonPrefix + entry->type.id;
            const auto matches = home.findChildren<QPushButton *>(objectName);
            QCOMPARE(matches.size(), 1);
            const DisplayedEntry &actual = displayed.at(position++);
            QCOMPARE(actual.button, matches.first());
            QCOMPARE(actual.button->objectName(), objectName);
            QCOMPARE(actual.button->text(), entry->type.displayName);
            QCOMPARE(actual.groupLabel, groupLabel);
        }
    }
    QCOMPARE(position, displayed.size());
}

void verifyClicksUseStableIds(HomePage &home, const SessionTypeRegistry &registry)
{
    QSignalSpy requested(&home, &HomePage::sessionTypeRequested);
    QVERIFY(requested.isValid());
    for (const SessionTypeEntry *entry : registry.entries()) {
        const QString id = entry->type.id;
        auto *button = home.findChild<QPushButton *>(buttonPrefix + id);
        QVERIFY2(button, qPrintable(id));
        // Repeated availability changes must not reconnect and duplicate signals.
        for (int round = 0; round < 2; ++round) {
            home.setTypeAvailable(id, true);
            QVERIFY(button->isEnabled());
            button->click();
            QCOMPARE(requested.count(), 1);
            const QList<QVariant> arguments = requested.takeFirst();
            QCOMPARE(arguments.size(), 1);
            QCOMPARE(arguments.first().toString(), id);

            home.setTypeAvailable(id, false, QStringLiteral("Unavailable for this test"));
            QVERIFY(!button->isEnabled());
            button->click();
            QCOMPARE(requested.count(), 0);
        }
    }
}

class MetadataTranslator final : public QTranslator
{
public:
    bool isEmpty() const override { return false; }

    QString translate(const char *context, const char *sourceText,
                      const char *disambiguation = nullptr, int n = -1) const override
    {
        Q_UNUSED(disambiguation)
        Q_UNUSED(n)
        if (qstrcmp(context, "SessionType") == 0)
            return QStringLiteral("Registry translation: ") + QString::fromUtf8(sourceText);
        return {};
    }
};

class InstalledTranslator final
{
public:
    explicit InstalledTranslator(QTranslator &translator)
        : m_translator(translator), installed(QCoreApplication::installTranslator(&translator))
    {}

    ~InstalledTranslator() { QCoreApplication::removeTranslator(&m_translator); }

private:
    QTranslator &m_translator;

public:
    const bool installed;
};

} // namespace

class TstHomeRegistry : public QObject
{
    Q_OBJECT

private slots:
    void entriesMatchRegistryIncludingGroupsAndOrder();
    void availabilityGatesEachStableIdExactlyOnce();
    void newlyConstructedHomeUsesTranslatedRegistryMetadata();
};

void TstHomeRegistry::entriesMatchRegistryIncludingGroupsAndOrder()
{
    SessionTypeRegistry registry;
    QString error;
    QCOMPARE(registry.addBuiltInTypes(&error), builtInSessionTypes().size());
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(!registry.isEmpty());
    HomePage home;
    verifyHomeMatchesRegistry(home, registry);
}

void TstHomeRegistry::availabilityGatesEachStableIdExactlyOnce()
{
    SessionTypeRegistry registry;
    QCOMPARE(registry.addBuiltInTypes(), builtInSessionTypes().size());
    HomePage home;
    verifyClicksUseStableIds(home, registry);
}

void TstHomeRegistry::newlyConstructedHomeUsesTranslatedRegistryMetadata()
{
    // Construct before translation as well, so a cached first-use copy of the
    // catalog cannot make this test pass accidentally.
    SessionTypeRegistry original;
    QCOMPARE(original.addBuiltInTypes(), builtInSessionTypes().size());
    HomePage originalHome;
    verifyHomeMatchesRegistry(originalHome, original);

    {
        MetadataTranslator translator;
        InstalledTranslator installed(translator);
        QVERIFY(installed.installed);
        SessionTypeRegistry translated;
        QCOMPARE(translated.addBuiltInTypes(), original.count());
        for (const SessionTypeEntry *entry : translated.entries()) {
            const SessionTypeEntry *before = original.find(entry->type.id);
            QVERIFY(before);
            QVERIFY(entry->type.displayName.startsWith(QStringLiteral("Registry translation: ")));
            QVERIFY(entry->type.displayName != before->type.displayName);
        }
        for (SessionGroup group : translated.groups(false))
            QVERIFY(sessionGroupLabel(group).startsWith(QStringLiteral("Registry translation: ")));

        HomePage translatedHome;
        verifyHomeMatchesRegistry(translatedHome, translated);
        verifyClicksUseStableIds(translatedHome, translated);
    }

    // Removing the translator must also affect the next constructed Home.
    HomePage restoredHome;
    verifyHomeMatchesRegistry(restoredHome, original);
}

QTEST_MAIN(TstHomeRegistry)
#include "tst_homeregistry.moc"
