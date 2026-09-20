#include <QtTest>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QTreeWidget>

#include "registrycomparesession.h"
#include "registrycompareview.h"

using namespace LqCompare;
namespace Reg = LqCompare::Registry;

namespace {
void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size())
        qFatal("Cannot write registry fixture: %s", qPrintable(file.errorString()));
}
QByteArray contents(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot read registry fixture");
    return file.readAll();
}
QByteArray exportText(const QString &body)
{
    return QTextCodec::codecForName("UTF-16LE")->fromUnicode(
        QStringLiteral("Windows Registry Editor Version 5.00\r\n\r\n") + body);
}
const QString keyPath = QStringLiteral("HKEY_CURRENT_USER\\Software\\LqCompare测试");
const QString leftBody = QStringLiteral(
    "[HKEY_CURRENT_USER\\Software\\LqCompare测试]\r\n"
    "@=\"默认\"\r\n"
    "\"Equal\"=\"same\"\r\n"
    "\"Data\"=dword:0000002a\r\n"
    "\"Type\"=\"42\"\r\n"
    "\"OnlyLeft\"=hex:00,01,ff\r\n"
    "[HKEY_CURRENT_USER\\Software\\LqCompare测试\\Child]\r\n"
    "\"Nested\"=\"old\"\r\n");
const QString rightBody = QStringLiteral(
    "[HKEY_CURRENT_USER\\Software\\LqCompare测试]\r\n"
    "@=\"默认\"\r\n"
    "\"Equal\"=\"same\"\r\n"
    "\"Data\"=dword:0000002b\r\n"
    "\"Type\"=dword:0000002a\r\n"
    "\"OnlyRight\"=hex:de,ad\r\n"
    "[HKEY_CURRENT_USER\\Software\\LqCompare测试\\Child]\r\n"
    "\"Nested\"=\"new\"\r\n");

QTreeWidgetItem *findItem(QTreeWidget *tree, Reg::EntryKind kind, const QString &path,
                          const QString &name = QString())
{
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        QTreeWidgetItem *item = *it;
        if (item->data(0, RegistryCompareView::KindRole).toInt() == int(kind)
            && Reg::identity(item->data(0, RegistryCompareView::KeyPathRole).toString()) == Reg::identity(path)
            && (kind == Reg::EntryKind::Key || item->data(0, RegistryCompareView::ValueNameRole).toString() == name))
            return item;
    }
    return nullptr;
}
QByteArray valueData(const Reg::Snapshot &snapshot, const QString &name)
{
    return snapshot.keys.value(Reg::identity(keyPath)).values.value(Reg::identity(name)).data;
}
class CountingProvider : public Reg::Provider {
public:
    mutable int calls = 0;
    Reg::ReadResult read(const QString &, const Reg::ReadOptions &) const override
    {
        ++calls;
        Reg::ReadResult result;
        result.error = QStringLiteral("The empty session must not invoke this provider.");
        return result;
    }
};
}

class RegistryViewTests : public QObject {
    Q_OBJECT
private slots:
    void emptySessionDoesNotEnumerateRegistry()
    {
        auto provider = std::make_shared<CountingProvider>();
        RegistryCompareSession session;
        QVERIFY(session.setLocalProvider(provider));
        QCOMPARE(session.typeId(), QStringLiteral("registry"));
        QVERIFY(session.open());
        QCOMPARE(provider->calls, 0);
        QVERIFY(session.reload());
        QCOMPARE(provider->calls, 0);
        QVERIFY(!session.isLoaded());
        QVERIFY(session.comparison().entries.isEmpty());
        QVERIFY(!session.canSave());
        QVERIFY(!session.setLocalProvider(provider));
        QString error;
        QVERIFY(!session.setPaths({}, QStringLiteral("HKCU\\Software"), &error));
        QVERIFY(error.contains(QStringLiteral("both")));
        QCOMPARE(provider->calls, 0);
    }

    void utf16FilesLoadAndNeverWrite()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString left = dir.filePath(QStringLiteral("左.reg")), right = dir.filePath(QStringLiteral("右.reg"));
        const QByteArray originalLeft = exportText(leftBody), originalRight = exportText(rightBody);
        QVERIFY(originalLeft.startsWith(QByteArray::fromHex("fffe")));
        writeFile(left, originalLeft);
        writeFile(right, originalRight);
        RegistryCompareSession session(left, right);
        QString error;
        QVERIFY2(session.open(&error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(session.isLoaded());
        QCOMPARE(session.comparison().differenceCount, 5);
        QVERIFY(session.comparison().complete());
        QCOMPARE(valueData(session.leftSnapshot(), QStringLiteral("Data")), QByteArray::fromHex("2a000000"));
        QCOMPARE(valueData(session.rightSnapshot(), QStringLiteral("Data")), QByteArray::fromHex("2b000000"));
        QVERIFY(!session.canSave());
        session.setDirty(true);
        QVERIFY(!session.canSave());
        QVERIFY(!session.save(&error));
        QCOMPARE(contents(left), originalLeft);
        QCOMPARE(contents(right), originalRight);
        session.setDirty(false);
        QVERIFY(session.statusText().contains(QStringLiteral("Read-only")));
        session.close();
        QVERIFY(!session.isLoaded());
        QVERIFY(session.comparison().entries.isEmpty());
        QVERIFY(!session.setPaths(left, right, &error));
        QVERIFY(!session.open(&error));
    }

    void failedReplacementAndReloadAreAtomic()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString left = dir.filePath(QStringLiteral("left.reg")), right = dir.filePath(QStringLiteral("right.reg"));
        writeFile(left, exportText(leftBody));
        writeFile(right, exportText(rightBody));
        RegistryCompareSession session(left, right);
        QVERIFY(session.open());
        const QString oldTitle = session.title(), oldStatus = session.statusText();
        const QByteArray oldValue = valueData(session.leftSnapshot(), QStringLiteral("Data"));
        QSignalSpy comparisonChanged(&session, &RegistryCompareSession::comparisonChanged);
        QSignalSpy pathsChanged(&session, &RegistryCompareSession::pathsChanged);
        QSignalSpy errors(&session, &CompareSession::errorReported);
        writeFile(left, exportText(rightBody));
        QString error;
        QVERIFY(!session.setPaths(left, dir.filePath(QStringLiteral("missing.reg")), &error));
        QVERIFY(error.contains(QStringLiteral("Right")));
        QCOMPARE(session.leftPath(), left);
        QCOMPARE(session.rightPath(), right);
        QCOMPARE(session.title(), oldTitle);
        QCOMPARE(session.statusText(), oldStatus);
        QCOMPARE(valueData(session.leftSnapshot(), QStringLiteral("Data")), oldValue);
        QCOMPARE(comparisonChanged.count(), 0);
        QCOMPARE(pathsChanged.count(), 0);
        QCOMPARE(errors.count(), 1);
        writeFile(right, "REGEDIT4\n[broken\n");
        QVERIFY(!session.reload(&error));
        QVERIFY(error.contains(QStringLiteral("line 2")));
        QCOMPARE(session.state(), CompareSession::State::Open);
        QCOMPARE(valueData(session.leftSnapshot(), QStringLiteral("Data")), oldValue);
        QCOMPARE(session.comparison().differenceCount, 5);
        QCOMPARE(comparisonChanged.count(), 0);
        writeFile(right, exportText(rightBody));
        QVERIFY2(session.reload(&error), qPrintable(error));
        QVERIFY(session.comparison().equal());
        QCOMPARE(comparisonChanged.count(), 1);
        QVERIFY(error.isEmpty());
    }

    void optionsReloadAtomicallyAndAnsiCodecIsExplicit()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("ansi.reg"));
        writeFile(path, QByteArray("REGEDIT4\r\n[HKEY_CURRENT_USER\\Software\\ANSI]\r\n\"Currency\"=\"")
            + char(0x80) + QByteArray("\"\r\n"));
        RegistryCompareSession session(path, path);
        QVERIFY(session.open());
        const auto value = session.leftSnapshot().keys.first().values.first();
        QCOMPARE(value.data, Reg::encodeString(QString::fromUtf8("€")));
        QCOMPARE(session.readOptions().ansiCodec, QByteArray("Windows-1252"));
        auto options = session.readOptions();
        options.ansiCodec = "Windows-1251";
        QVERIFY(session.setReadOptions(options));
        QCOMPARE(session.readOptions().ansiCodec, QByteArray("Windows-1251"));
        QVERIFY(session.leftSnapshot().keys.first().values.first().data != value.data);
        const auto previous = session.leftSnapshot().keys.first().values.first().data;
        options.maxBytes = 1;
        QString error;
        QVERIFY(!session.setReadOptions(options, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(session.readOptions().maxBytes, qint64(32 * 1024 * 1024));
        QCOMPARE(session.leftSnapshot().keys.first().values.first().data, previous);
    }

    void hkcuOnlyAndMemoryProviderUncertainty()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto parsed = Reg::parseReg(exportText(leftBody));
        QVERIFY(parsed.ok);
        Reg::Snapshot unavailable = parsed.snapshot;
        auto &key = unavailable.keys[Reg::identity(keyPath)];
        key.values.clear();
        key.error = QStringLiteral("Access denied (5)");
        key.nativeError = 5;
        writeFile(dir.filePath(QStringLiteral("right.reg")), exportText(leftBody));
        RegistryCompareSession session(QStringLiteral("HKCU\\Software"), dir.filePath(QStringLiteral("right.reg")));
        QVERIFY(session.setLocalProvider(std::make_shared<Reg::MemoryProvider>(unavailable)));
        QString error;
        QVERIFY2(session.open(&error), qPrintable(error));
        QVERIFY(!session.comparison().complete());
        QVERIFY(!session.comparison().equal());
        QVERIFY(session.comparison().unreadableKeys > 0);
        QVERIFY(session.statusText().contains(QStringLiteral("unreadable")));
        QVERIFY(session.statusText().contains(QStringLiteral("Incomplete")));
        QVERIFY(!session.statusText().contains(QStringLiteral("• Equal")));
        QWidget owner;
        auto *view = session.createWidget(&owner);
        auto *tree = view->findChild<QTreeWidget *>(QStringLiteral("registryTree"));
        QVERIFY(tree);
        QTreeWidgetItem *row = findItem(tree, Reg::EntryKind::Key, keyPath);
        QVERIFY(row);
        QCOMPARE(row->data(0, RegistryCompareView::StatusRole).toInt(), int(Reg::Status::Unreadable));
        QVERIFY(row->text(3).contains(QStringLiteral("Access denied")));

        auto counter = std::make_shared<CountingProvider>();
        RegistryCompareSession blocked(QStringLiteral("HKLM\\Software"), dir.filePath(QStringLiteral("right.reg")));
        QVERIFY(blocked.setLocalProvider(counter));
        QVERIFY(!blocked.open(&error));
        QVERIFY(error.contains(QStringLiteral("HKCU")));
        QCOMPARE(counter->calls, 0);
    }

    void localEntryReflectsPlatformAvailability()
    {
        RegistryCompareSession session;
        QWidget owner;
        auto *view = session.createWidget(&owner);
        auto *button = view->findChild<QPushButton *>(QStringLiteral("registryLeftLocal"));
        auto *scope = view->findChild<QLabel *>(QStringLiteral("registryScope"));
        QVERIFY(button && scope);
        QCOMPARE(button->isEnabled(), Reg::localProviderAvailable());
        QVERIFY(!button->toolTip().isEmpty());
        QVERIFY(scope->text().contains(Reg::localProviderDescription()));
#ifndef Q_OS_WIN
        QVERIFY(!button->isEnabled());
        RegistryCompareSession native(QStringLiteral("HKCU\\Software"), QStringLiteral("HKCU\\Software"));
        QString error;
        QVERIFY(!native.open(&error));
        QVERIFY(error.contains(QStringLiteral("Windows"), Qt::CaseInsensitive));
#endif
    }

    void treeHierarchyTypesFilterAndSort()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("left.reg")), exportText(leftBody));
        writeFile(dir.filePath(QStringLiteral("right.reg")), exportText(rightBody));
        RegistryCompareSession session(dir.filePath(QStringLiteral("left.reg")), dir.filePath(QStringLiteral("right.reg")));
        QVERIFY(session.open());
        QWidget owner;
        auto *view = session.createWidget(&owner);
        auto *tree = view->findChild<QTreeWidget *>(QStringLiteral("registryTree"));
        auto *filter = view->findChild<QComboBox *>(QStringLiteral("registryStatusFilter"));
        QVERIFY(tree && filter);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("HKEY_CURRENT_USER"));
        auto *key = findItem(tree, Reg::EntryKind::Key, keyPath);
        auto *nested = findItem(tree, Reg::EntryKind::Key, keyPath + QStringLiteral("\\Child"));
        auto *type = findItem(tree, Reg::EntryKind::Value, keyPath, QStringLiteral("Type"));
        auto *data = findItem(tree, Reg::EntryKind::Value, keyPath, QStringLiteral("Data"));
        auto *equal = findItem(tree, Reg::EntryKind::Value, keyPath, QStringLiteral("Equal"));
        auto *defaultValue = findItem(tree, Reg::EntryKind::Value, keyPath);
        QVERIFY(key && nested && type && data && equal && defaultValue);
        QCOMPARE(nested->parent(), key);
        QCOMPARE(type->parent(), key);
        QCOMPARE(defaultValue->text(0), QStringLiteral("(Default)"));
        QVERIFY(defaultValue->text(3).contains(QStringLiteral("默认")));
        QCOMPARE(type->text(2), QStringLiteral("REG_SZ"));
        QCOMPARE(type->text(4), QStringLiteral("REG_DWORD"));
        QVERIFY(data->text(3).contains(QStringLiteral("42")));
        QVERIFY(data->text(3).contains(QStringLiteral("0x")));
        QVERIFY(tree->columnWidth(1) >= tree->fontMetrics().horizontalAdvance(type->text(1)) + 20);
        QVERIFY(tree->columnWidth(4) >= tree->fontMetrics().horizontalAdvance(type->text(4)) + 20);
        QCOMPARE(type->data(0, RegistryCompareView::StatusRole).toInt(), int(Reg::Status::TypeChanged));
        QVERIFY(key->toolTip(1).contains(QStringLiteral("compared separately")));
        QVERIFY(!(type->flags() & Qt::ItemIsEditable));
        QCOMPARE(tree->editTriggers(), QAbstractItemView::EditTriggers(QAbstractItemView::NoEditTriggers));

        filter->setCurrentIndex(filter->findData(int(Reg::Status::TypeChanged)));
        QVERIFY(!type->isHidden());
        QVERIFY(equal->isHidden());
        QVERIFY(data->isHidden());
        QVERIFY(nested->isHidden());
        QVERIFY(!key->isHidden());
        QVERIFY(!key->parent()->isHidden());
        filter->setCurrentIndex(filter->findData(int(Reg::Status::OnlyLeft)));
        QVERIFY(!findItem(tree, Reg::EntryKind::Value, keyPath, QStringLiteral("OnlyLeft"))->isHidden());
        QVERIFY(type->isHidden());
        filter->setCurrentIndex(filter->findData(-1));
        QVERIFY(!equal->isHidden());
        QVERIFY(!nested->isHidden());

        for (const Qt::SortOrder order : {Qt::AscendingOrder, Qt::DescendingOrder}) {
            tree->sortItems(1, order);
            int previous = order == Qt::AscendingOrder ? -100 : 100;
            for (int i = 0; i < key->childCount(); ++i) {
                const int current = key->child(i)->data(0, RegistryCompareView::StatusRole).toInt();
                QVERIFY(order == Qt::AscendingOrder ? current >= previous : current <= previous);
                previous = current;
            }
        }
        tree->sortItems(0, Qt::AscendingOrder);
        view->resize(1500, 700);
        owner.resize(view->size());
        owner.show();
        view->show();
        tree->expandAll();
        QCoreApplication::processEvents();
        QVERIFY(!view->grab().isNull());
        const QString screenshot = qEnvironmentVariable("LQCOMPARE_REGISTRY_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(view->grab().save(screenshot));
    }

    void deletionInstructionsRemainExplicit()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("delete.reg"));
        writeFile(path, exportText(QStringLiteral("[-HKEY_CURRENT_USER\\Software\\DeleteMe]\r\n"
            "[HKEY_CURRENT_USER\\Software\\Values]\r\n\"Gone\"=-\r\n")));
        RegistryCompareSession session(path, path);
        QVERIFY(session.open());
        QWidget owner;
        auto *tree = session.createWidget(&owner)->findChild<QTreeWidget *>(QStringLiteral("registryTree"));
        QVERIFY(tree);
        auto *key = findItem(tree, Reg::EntryKind::Key, QStringLiteral("HKEY_CURRENT_USER\\Software\\DeleteMe"));
        auto *value = findItem(tree, Reg::EntryKind::Value, QStringLiteral("HKEY_CURRENT_USER\\Software\\Values"), QStringLiteral("Gone"));
        QVERIFY(key && value);
        QVERIFY(key->text(3).contains(QStringLiteral("not executed")));
        QVERIFY(key->text(5).contains(QStringLiteral("not executed")));
        QVERIFY(value->text(3).contains(QStringLiteral("not executed")));
        QCOMPARE(value->text(2), QStringLiteral("Delete instruction"));
        QCOMPARE(value->text(4), QStringLiteral("Delete instruction"));
        QVERIFY(!session.canSave());
    }

    void sourceControlsSurfaceFailureAndRecover()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString left = dir.filePath(QStringLiteral("left.reg")), right = dir.filePath(QStringLiteral("right.reg"));
        writeFile(left, exportText(leftBody));
        writeFile(right, exportText(rightBody));
        RegistryCompareSession session;
        QVERIFY(session.open());
        QWidget owner;
        auto *view = session.createWidget(&owner);
        auto *leftEdit = view->findChild<QLineEdit *>(QStringLiteral("registryLeftPath"));
        auto *rightEdit = view->findChild<QLineEdit *>(QStringLiteral("registryRightPath"));
        auto *compare = view->findChild<QPushButton *>(QStringLiteral("registryCompare"));
        auto *error = view->findChild<QLabel *>(QStringLiteral("registryError"));
        QVERIFY(leftEdit && rightEdit && compare && error);
        leftEdit->setText(left);
        rightEdit->setText(right);
        compare->click();
        QVERIFY(session.isLoaded());
        QCOMPARE(session.comparison().differenceCount, 5);
        rightEdit->setText(dir.filePath(QStringLiteral("absent.reg")));
        compare->click();
        QVERIFY(!error->isHidden());
        QVERIFY(error->text().contains(QStringLiteral("previous comparison")));
        QCOMPARE(session.rightPath(), right);
        QCOMPARE(session.comparison().differenceCount, 5);
        rightEdit->setText(left);
        QVERIFY(QMetaObject::invokeMethod(rightEdit, "returnPressed", Qt::DirectConnection));
        QVERIFY(session.comparison().equal());
        QVERIFY(error->isHidden());
        session.close();
        QVERIFY(!view->isEnabled());
    }

    void viewSurvivesSessionDestructionAndRecreation()
    {
        QWidget owner;
        auto *session = new RegistryCompareSession;
        QVERIFY(session->open());
        QWidget *view = session->createWidget(&owner);
        QCOMPARE(session->createWidget(&owner), view);
        delete view;
        QVERIFY(!session->widget());
        view = session->createWidget(&owner);
        QVERIFY(view);
        delete session;
        QVERIFY(!view->isEnabled());
        auto *path = view->findChild<QLineEdit *>(QStringLiteral("registryLeftPath"));
        QVERIFY(path);
        QVERIFY(QMetaObject::invokeMethod(path, "returnPressed", Qt::DirectConnection));
        QVERIFY(!view->grab().isNull());
    }
};

QTEST_MAIN(RegistryViewTests)
#include "tst_registryview.moc"
