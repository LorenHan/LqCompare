#include <QtTest>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopedPointer>
#include <QTableView>
#include <QTemporaryDir>
#include "archivecomparesession.h"
#include "archivecompareview.h"

using namespace LqCompare;

class ArchiveViewTests : public QObject
{
    Q_OBJECT
private:
    QString m_fixtures;
    QString fixture(const QString &name) const { return m_fixtures + QLatin1Char('/') + name; }
    static int rowFor(QAbstractItemModel *model, const QString &path)
    {
        for (int row = 0; row < model->rowCount(); ++row)
            if (model->index(row, 0).data().toString() == path) return row;
        return -1;
    }

private slots:
    void initTestCase()
    {
        m_fixtures = QFINDTESTDATA("../Archive/fixtures");
        QVERIFY2(!m_fixtures.isEmpty(), "Archive service fixtures are required");
        m_fixtures = QDir::cleanPath(m_fixtures);
    }

    void emptySession()
    {
        ArchiveCompareSession session;
        QCOMPARE(session.typeId(), QStringLiteral("archive"));
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        auto *table = widget->findChild<QTableView *>("archiveEntries");
        QVERIFY(table);
        QCOMPARE(table->model()->rowCount(), 0);
        QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
        QVERIFY(!session.hasComparison());
        QVERIFY(!session.canSave());
        QVERIFY(!widget->findChild<QPushButton *>("archiveReload")->isEnabled());
        QVERIFY(!widget->findChild<QPushButton *>("archiveNextDifference")->isEnabled());
        QCOMPARE(widget->findChild<QLabel *>("archiveMetadataNotice")->text(), Archive::metadataNotice());
        QVERIFY(!session.reload());
        QVERIFY(!session.statusText().isEmpty());
    }

    void opensPairedMetadataReadOnly()
    {
        ArchiveCompareSession session(fixture("compare-left.zip"), fixture("compare-right.zip"));
        QScopedPointer<QWidget> widget(session.createWidget());
        QSignalSpy changes(&session, &ArchiveCompareSession::comparisonChanged);
        QString error;
        QVERIFY2(session.open(&error), qPrintable(error));
        QCOMPARE(changes.count(), 1);
        QVERIFY(session.hasComparison());
        QCOMPARE(session.leftPath(), fixture("compare-left.zip"));
        QCOMPARE(session.comparison().differenceCount, 6);
        auto *model = widget->findChild<ArchiveEntryModel *>("archiveEntryModel");
        QCOMPARE(model->rowCount(), session.comparison().rows.size());
        QCOMPARE(model->columnCount(), int(ArchiveEntryModel::ColumnCount));
        const int sameRow = rowFor(model, QStringLiteral("same.txt"));
        QVERIFY(sameRow >= 0);
        QCOMPARE(model->index(sameRow, 0).data(ArchiveEntryModel::DifferenceRole).toInt(),
                 static_cast<int>(Archive::Difference::MatchingMetadata));
        QCOMPARE(model->index(sameRow, ArchiveEntryModel::LeftCrc).data(),
                 model->index(sameRow, ArchiveEntryModel::RightCrc).data());
        QVERIFY(!(model->flags(model->index(sameRow, 0)) & Qt::ItemIsEditable));
        QVERIFY(model->index(sameRow, 0).data(Qt::ToolTipRole).toString().contains(Archive::metadataNotice()));
        session.setDirty(true); // Even external dirty state must not enable archive writes.
        QVERIFY(!session.canSave());
        QVERIFY(!session.save(&error));
        session.setDirty(false);
        const QString screenshot = qEnvironmentVariable("LQCOMPARE_ARCHIVE_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) {
            widget->resize(1440, 760);
            widget->show();
            QTest::qWait(100);
            QVERIFY(widget->grab().save(screenshot));
        }
    }

    void pathControlsFilteringAndNavigation()
    {
        ArchiveCompareSession session;
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        auto *left = widget->findChild<QLineEdit *>("archiveLeftPath");
        auto *right = widget->findChild<QLineEdit *>("archiveRightPath");
        left->setText(fixture("compare-left.zip"));
        right->setText(fixture("compare-right.zip"));
        widget->findChild<QPushButton *>("archiveCompare")->click();
        QVERIFY(session.hasComparison());
        auto *table = widget->findChild<QTableView *>("archiveEntries");
        auto *search = widget->findChild<QLineEdit *>("archiveSearch");
        auto *differences = widget->findChild<QCheckBox *>("archiveDifferencesOnly");
        auto *next = widget->findChild<QPushButton *>("archiveNextDifference");
        auto *previous = widget->findChild<QPushButton *>("archivePreviousDifference");
        const int allRows = table->model()->rowCount();
        differences->setChecked(true);
        QCOMPARE(table->model()->rowCount(), 6);
        next->click();
        QCOMPARE(table->currentIndex().row(), 0);
        next->click();
        QCOMPARE(table->currentIndex().row(), 1);
        previous->click();
        QCOMPARE(table->currentIndex().row(), 0);
        previous->click();
        QCOMPARE(table->currentIndex().row(), 0);
        search->setText(QStringLiteral("CRC.TXT"));
        QCOMPARE(table->model()->rowCount(), 1);
        next->click();
        QCOMPARE(table->currentIndex().data().toString(), QStringLiteral("crc.txt"));
        QVERIFY(!widget->findChild<QLabel *>("archiveEvidence")->text().isEmpty());
        search->setText(QStringLiteral("same.txt"));
        QCOMPARE(table->model()->rowCount(), 0);
        QVERIFY(!next->isEnabled());
        differences->setChecked(false);
        QCOMPARE(table->model()->rowCount(), 1);
        QVERIFY(!next->isEnabled());
        search->clear();
        QCOMPARE(table->model()->rowCount(), allRows);
        widget->findChild<QPushButton *>("archiveReload")->click();
        QCOMPARE(table->model()->rowCount(), allRows);
    }

    void failedReplacementKeepsResult_data()
    {
        QTest::addColumn<QString>("badFile");
        for (const QString &name : {QStringLiteral("missing.zip"), QStringLiteral("unknown.zip"),
                                   QStringLiteral("truncated.zip"), QStringLiteral("encrypted.zip"),
                                   QStringLiteral("unsafe-traversal.zip"), QStringLiteral("zip64-count.zip")})
            QTest::newRow(qPrintable(name)) << name;
    }

    void failedReplacementKeepsResult()
    {
        QFETCH(QString, badFile);
        ArchiveCompareSession session(fixture("compare-left.zip"), fixture("compare-right.zip"));
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        const int count = session.comparison().rows.size();
        QSignalSpy changed(&session, &ArchiveCompareSession::comparisonChanged);
        QSignalSpy errors(&session, &CompareSession::errorReported);
        QString error;
        QVERIFY(!session.setPaths(fixture("empty.zip"), fixture(badFile), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(errors.count(), 1);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(session.state(), CompareSession::State::Open);
        QCOMPARE(session.leftPath(), fixture("compare-left.zip"));
        QCOMPARE(session.rightPath(), fixture("compare-right.zip"));
        QCOMPARE(session.comparison().rows.size(), count);
        QCOMPARE(widget->findChild<QTableView *>("archiveEntries")->model()->rowCount(), count);
        QVERIFY(widget->findChild<QLabel *>("archiveStatus")->text().contains(error));
        QVERIFY(session.statusText().contains(QStringLiteral("保留")));
    }

    void failedReloadAndClose()
    {
        QTemporaryDir directory;
        const QString left = directory.filePath("left.zip"), right = directory.filePath("right.zip");
        QVERIFY(QFile::copy(fixture("compare-left.zip"), left));
        QVERIFY(QFile::copy(fixture("compare-right.zip"), right));
        ArchiveCompareSession session(left, right);
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        const int count = session.comparison().rows.size();
        QVERIFY(QFile::remove(right));
        QString error;
        QVERIFY(!session.reload(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(session.comparison().rows.size(), count);
        QCOMPARE(session.state(), CompareSession::State::Open);
        session.close();
        QVERIFY(!widget->isEnabled());
        QVERIFY(!session.hasComparison());
        QVERIFY(session.comparison().rows.isEmpty());
        QCOMPARE(widget->findChild<QTableView *>("archiveEntries")->model()->rowCount(), 0);
        QVERIFY(!session.setPaths(left, fixture("empty.zip"), &error));
        QVERIFY(!session.open(&error));
        QVERIFY(!session.createWidget());
        session.close();
    }

    void failedInitialOpenCanRecoverAndLateViewWorks()
    {
        ArchiveCompareSession session(fixture("missing.zip"), fixture("empty.zip"));
        QString error;
        QVERIFY(!session.open(&error));
        QCOMPARE(session.state(), CompareSession::State::Failed);
        QVERIFY(session.setPaths(fixture("compare-left.zip"), fixture("compare-right.zip"), &error));
        QVERIFY(session.open(&error));
        QScopedPointer<QWidget> widget(session.createWidget());
        QCOMPARE(widget->findChild<QTableView *>("archiveEntries")->model()->rowCount(),
                 session.comparison().rows.size());
        QCOMPARE(widget->findChild<QLabel *>("archiveStatus")->text(), session.statusText());
    }

    void actualCrcCollisionNeverClaimsByteEquality()
    {
        ArchiveCompareSession session(fixture("collision-left.zip"), fixture("collision-right.zip"));
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        QCOMPARE(session.comparison().rows.size(), 1);
        QCOMPARE(session.comparison().differenceCount, 0);
        const auto row = widget->findChild<QTableView *>("archiveEntries")->model()->index(0, ArchiveEntryModel::Status);
        QCOMPARE(row.data().toString(), Archive::differenceLabel(Archive::Difference::MatchingMetadata));
        QVERIFY(widget->findChild<QLabel *>("archiveMetadataNotice")->text().contains(Archive::metadataNotice()));
        QVERIFY(session.statusText().contains(QStringLiteral("未经")));
    }

    void largeModelCanFilterAndSortNumericSizes()
    {
        Archive::Comparison comparison;
        for (int i = 0; i < 50000; ++i) {
            Archive::Entry entry;
            entry.path = QStringLiteral("item-%1.bin").arg(i, 5, 10, QLatin1Char('0'));
            entry.uncompressedSize = 50000 - i;
            comparison.left.entries.append(entry);
            Archive::Row row;
            row.path = entry.path;
            row.leftIndex = i;
            row.difference = Archive::Difference::LeftOnly;
            comparison.rows.append(row);
        }
        comparison.differenceCount = 50000;
        ArchiveCompareView view;
        QElapsedTimer timer;
        timer.start();
        view.setComparison(comparison);
        auto *table = view.findChild<QTableView *>("archiveEntries");
        QCOMPARE(table->model()->rowCount(), 50000);
        table->sortByColumn(ArchiveEntryModel::LeftSize, Qt::AscendingOrder);
        QCOMPARE(table->model()->index(0, ArchiveEntryModel::LeftSize).data().toString(), QStringLiteral("1"));
        view.findChild<QLineEdit *>("archiveSearch")->setText(QStringLiteral("item-49999"));
        QCOMPARE(table->model()->rowCount(), 1);
        QVERIFY2(timer.elapsed() < 10000, "50k-row model, sort and search should complete within ten seconds");
    }
};

QTEST_MAIN(ArchiveViewTests)
#include "tst_archiveview.moc"
