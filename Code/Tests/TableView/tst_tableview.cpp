#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopedPointer>
#include <QSet>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include "tablecomparesession.h"
#include "tablecompareview.h"

using namespace LqCompare;
namespace {
struct CurrentDirectoryGuard {
    const QString original = QDir::currentPath();
    ~CurrentDirectoryGuard() { QDir::setCurrent(original); }
};

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        qFatal("Cannot write table test fixture");
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot read table test fixture");
    return file.readAll();
}
Table::CompareOptions keyNumericRules()
{
    Table::CompareOptions options;
    options.columnMode = Table::ColumnMappingMode::Explicit;
    options.alignment = Table::RowAlignment::Key;
    Table::ColumnRule key;
    key.left = key.right = 0;
    key.role = Table::ColumnRole::Key;
    Table::ColumnRule number;
    number.left = number.right = 1;
    number.numeric = true;
    number.absoluteTolerance = 0.1;
    Table::ColumnRule ignored;
    ignored.left = ignored.right = 2;
    ignored.role = Table::ColumnRole::Ignore;
    options.columns = {key, number, ignored};
    return options;
}
QPushButton *button(QWidget *widget, const QString &text)
{
    for (QPushButton *candidate : widget->findChildren<QPushButton *>())
        if (candidate->text() == text) return candidate;
    return nullptr;
}
}

class TableViewTests : public QObject {
    Q_OBJECT
private slots:
    void emptyReadOnlySession()
    {
        TableCompareSession session;
        QCOMPARE(session.typeId(), QString("table"));
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        QCOMPARE(session.createWidget(), widget.data());
        auto *left = widget->findChild<QTableView *>("leftTable");
        auto *right = widget->findChild<QTableView *>("rightTable");
        QVERIFY(left);
        QVERIFY(right);
        QCOMPARE(left->model()->rowCount(), 0);
        QCOMPARE(right->model()->rowCount(), 0);
        QCOMPARE(left->editTriggers(), QAbstractItemView::EditTriggers(QAbstractItemView::NoEditTriggers));
        QVERIFY(!session.canSave());
        QString error;
        QVERIFY(!session.save(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!session.isDirty());
        QCOMPARE(session.currentDifference(), -1);
        session.firstDifference();
        session.nextDifference();
        session.lastDifference();
        QCOMPARE(session.currentDifference(), -1);
    }

    void mappedGridsHighlightsAndFilters()
    {
        QTemporaryDir directory;
        const QString leftPath = directory.filePath("left.csv");
        const QString rightPath = directory.filePath("right.csv");
        const QByteArray leftBytes = "id,name,amount\n1001,Northwind,10\n1002,Contoso,20\n1003,Adventure Works,30\n";
        const QByteArray rightBytes = "amount,id,name\n10,1001,Northwind\n25,1002,Contoso\n30,1003,Adventure Works\n";
        writeFile(leftPath, leftBytes);
        writeFile(rightPath, rightBytes);
        TableCompareSession session(leftPath, rightPath);
        QString error;
        QVERIFY2(session.open(&error), qPrintable(error));
        QScopedPointer<QWidget> widget(session.createWidget());
        auto *left = widget->findChild<QTableView *>("leftTable");
        auto *right = widget->findChild<QTableView *>("rightTable");
        QVERIFY(left && right);
        auto *a = left->model();
        auto *b = right->model();
        QCOMPARE(a->rowCount(), 3);
        QCOMPARE(b->rowCount(), 3);
        QCOMPARE(a->columnCount(), 3);
        QCOMPARE(b->data(b->index(0, 0)).toString(), QString("1001"));
        QCOMPARE(b->data(b->index(0, 1)).toString(), QString("Northwind"));
        QCOMPARE(b->data(b->index(1, 2)).toString(), QString("25"));
        QVERIFY(b->headerData(0, Qt::Horizontal).toString().contains("2: id"));
        QVERIFY(b->headerData(2, Qt::Horizontal).toString().contains("1: amount"));
        QVERIFY(a->data(a->index(1, 2), Qt::BackgroundRole).isValid());
        QVERIFY(b->data(b->index(1, 2), Qt::BackgroundRole).isValid());
        QVERIFY(a->data(a->index(1, 0), Qt::BackgroundRole)
                != a->data(a->index(1, 2), Qt::BackgroundRole));
        QCOMPARE(a->data(a->index(1, 2), Qt::BackgroundRole),
                 b->data(b->index(1, 2), Qt::BackgroundRole));
        QCOMPARE(session.comparison().statistics.equalRows, 2);
        QCOMPARE(session.comparison().statistics.differentRows, 1);
        QCOMPARE(session.comparison().statistics.differentCells, 1);
        QVERIFY(!(a->flags(a->index(0, 0)) & Qt::ItemIsEditable));
        QVERIFY(!a->setData(a->index(0, 0), QString("edited")));
        auto *rows = widget->findChild<QCheckBox *>("differenceOnly");
        auto *columns = widget->findChild<QCheckBox *>("differenceColumnsOnly");
        QVERIFY(rows && columns);
        rows->setChecked(true);
        QCOMPARE(a->rowCount(), 1);
        QCOMPARE(b->rowCount(), 1);
        QCOMPARE(a->data(a->index(0, 0)).toString(), QString("1002"));
        columns->setChecked(true);
        QVERIFY(left->isColumnHidden(0));
        QVERIFY(right->isColumnHidden(1));
        QVERIFY(!left->isColumnHidden(2));
        QVERIFY(!right->isColumnHidden(2));
        rows->setChecked(false);
        columns->setChecked(false);
        QCOMPARE(a->rowCount(), 3);
        QVERIFY(!left->isColumnHidden(0));
        QVERIFY(!session.save(&error));
        QCOMPARE(readFile(leftPath), leftBytes);
        QCOMPARE(readFile(rightPath), rightBytes);
        const QString screenshot = qEnvironmentVariable("LQ_TABLE_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            widget->resize(1500, 800);
            widget->show();
            QTest::qWait(100);
            QVERIFY(widget->grab().save(screenshot));
        }
    }

    void raggedMissingVersusEmptyCell()
    {
        QTemporaryDir directory;
        const auto leftPath = directory.filePath("left.csv"), rightPath = directory.filePath("right.csv");
        writeFile(leftPath, "id,note\n1\n2,\n");
        writeFile(rightPath, "id,note\n1,\n2,\n");
        TableCompareSession session(leftPath, rightPath);
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        auto *a = widget->findChild<QTableView *>("leftTable")->model();
        auto *b = widget->findChild<QTableView *>("rightTable")->model();
        QCOMPARE(session.leftDocument().rows.at(0).size(), 1);
        QCOMPARE(session.rightDocument().rows.at(0).size(), 2);
        QCOMPARE(a->data(a->index(0, 1)).toString(), QString::fromUtf8("∅"));
        QCOMPARE(b->data(b->index(0, 1)).toString(), QString());
        QVERIFY(a->data(a->index(0, 1), Qt::ToolTipRole).toString().contains("Missing cell"));
        QCOMPARE(session.comparison().statistics.differentRows, 1);
        QCOMPARE(session.comparison().statistics.equalRows, 1);
        QCOMPARE(session.comparison().statistics.differentCells, 1);
    }

    void explicitKeyNumericAndIgnoredRules()
    {
        QTemporaryDir directory;
        const auto leftPath = directory.filePath("left.csv"), rightPath = directory.filePath("right.csv");
        writeFile(leftPath, "id,amount,note\n1,10.0,left\n2,20,old\n");
        writeFile(rightPath, "id,amount,note\n2,20.09,new\n1,10,right\n");
        TableCompareSession session(leftPath, rightPath);
        QVERIFY(session.open());
        auto options = keyNumericRules();
        QString error;
        QVERIFY2(session.setComparisonOptions(options, &error), qPrintable(error));
        QCOMPARE(session.comparison().statistics.equalRows, 2);
        QCOMPARE(session.comparison().statistics.ignoredCells, 2);
        QVERIFY(session.comparison().differences.isEmpty());
        QCOMPARE(session.comparison().rows.at(0).left, 0);
        QCOMPARE(session.comparison().rows.at(0).right, 1);
        QScopedPointer<QWidget> widget(session.createWidget());
        auto *model = widget->findChild<QTableView *>("rightTable")->model();
        QCOMPARE(model->data(model->index(0, 0)).toString(), QString("1"));
        QVERIFY(model->headerData(1, Qt::Horizontal).toString().contains("Numeric"));
        QVERIFY(model->headerData(2, Qt::Horizontal).toString().contains("Ignored"));
        QVERIFY(widget->findChild<QLabel *>("tableWarnings")->text().contains("Numeric rules"));
        options.columns[1].compareFormatting = true;
        QVERIFY(session.setComparisonOptions(options));
        QCOMPARE(session.comparison().statistics.differentRows, 2);
        QCOMPARE(session.comparison().statistics.differentCells, 2);
        QCOMPARE(session.sessionSettings()->value("table.columns").toList().size(), 3);
    }

    void duplicateKeyOccurrencesRemainVisible()
    {
        QTemporaryDir directory;
        const auto leftPath = directory.filePath("left.csv"), rightPath = directory.filePath("right.csv");
        writeFile(leftPath, "id,value\nx,a\nx,b\n");
        writeFile(rightPath, "id,value\nx,b\nx,c\nx,d\n");
        TableCompareSession session(leftPath, rightPath);
        QVERIFY(session.open());
        auto options = keyNumericRules();
        options.columns.removeLast();
        options.columns[1].numeric = false;
        QVERIFY(session.setComparisonOptions(options));
        QSet<int> leftRows, rightRows;
        for (const auto &row : session.comparison().rows) {
            if (row.left >= 0) leftRows.insert(row.left);
            if (row.right >= 0) rightRows.insert(row.right);
            QVERIFY(row.duplicateKey);
        }
        QCOMPARE(leftRows, QSet<int>({0, 1}));
        QCOMPARE(rightRows, QSet<int>({0, 1, 2}));
        QCOMPARE(session.comparison().rows.size(), 3);
        QVERIFY(!session.comparison().warnings.isEmpty());
        QScopedPointer<QWidget> widget(session.createWidget());
        auto *model = widget->findChild<QTableView *>("leftTable")->model();
        QCOMPARE(model->rowCount(), 3);
        QVERIFY(model->data(model->index(0, 0), Qt::ToolTipRole).toString().contains("Duplicate key"));
    }

    void reloadAndPairFailureAreAtomic()
    {
        QTemporaryDir directory;
        const auto leftPath = directory.filePath("left.csv"), rightPath = directory.filePath("right.csv");
        const auto newLeft = directory.filePath("new-left.csv"), badRight = directory.filePath("bad-right.csv");
        writeFile(leftPath, "id,value\n1,left\n");
        writeFile(rightPath, "id,value\n1,left\n");
        TableCompareSession session(leftPath, rightPath);
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        QVERIFY(session.comparison().differences.isEmpty());
        writeFile(rightPath, "id,value\n1,right\n");
        QVERIFY(session.reload());
        QCOMPARE(session.comparison().statistics.differentRows, 1);
        QCOMPARE(session.rightDocument().rows.at(0).at(1), QString("right"));
        const auto oldLeft = session.leftDocument().rows, oldRight = session.rightDocument().rows;
        writeFile(newLeft, "id,value\n9,new\n");
        writeFile(badRight, "id,value\n1,\"unterminated");
        QString error;
        QVERIFY(!session.setPaths(newLeft, badRight, &error));
        QVERIFY(error.contains("Right file"));
        QCOMPARE(session.leftPath(), leftPath);
        QCOMPARE(session.rightPath(), rightPath);
        QCOMPARE(session.leftDocument().rows, oldLeft);
        QCOMPARE(session.rightDocument().rows, oldRight);
        QVERIFY(!session.setPaths(newLeft, directory.filePath("missing.csv"), &error));
        QCOMPARE(session.leftPath(), leftPath);
        QCOMPARE(session.rightPath(), rightPath);
        QCOMPARE(session.leftDocument().rows, oldLeft);
        QCOMPARE(session.rightDocument().rows, oldRight);
        writeFile(leftPath, "id,value\n7,external\n");
        writeFile(rightPath, "id,value\n1,\"bad");
        QVERIFY(!session.reload(&error));
        QCOMPARE(session.leftDocument().rows, oldLeft);
        QCOMPARE(session.rightDocument().rows, oldRight);
        QCOMPARE(session.leftPath(), leftPath);
        QCOMPARE(session.rightPath(), rightPath);
        QCOMPARE(widget->findChild<QLineEdit *>("leftTablePath")->text(), leftPath);
        QCOMPARE(widget->findChild<QLineEdit *>("rightTablePath")->text(), rightPath);
    }

    void relativePathsRemainReloadableAfterDirectoryChange()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        CurrentDirectoryGuard restore;
        writeFile(directory.filePath("left.csv"), "id,value\n1,left\n");
        writeFile(directory.filePath("right.csv"), "id,value\n1,right\n");
        QVERIFY(QDir::setCurrent(directory.path()));
        const QString absoluteLeft = QDir::current().absoluteFilePath("left.csv");
        const QString absoluteRight = QDir::current().absoluteFilePath("right.csv");
        TableCompareSession session("left.csv", "right.csv");
        QString error;
        QVERIFY2(session.open(&error), qPrintable(error));
        QVERIFY(QDir::isAbsolutePath(session.leftPath()));
        QVERIFY(QDir::isAbsolutePath(session.rightPath()));
        QCOMPARE(session.leftPath(), absoluteLeft);
        QCOMPARE(session.rightPath(), absoluteRight);
        QCOMPARE(session.leftDocument().path, absoluteLeft);
        QCOMPARE(session.rightDocument().path, absoluteRight);
        QVERIFY(QDir::setCurrent(restore.original));
        writeFile(absoluteRight, "id,value\n1,left\n");
        QVERIFY2(session.reload(&error), qPrintable(error));
        QCOMPARE(session.leftPath(), absoluteLeft);
        QCOMPARE(session.rightPath(), absoluteRight);
        QCOMPARE(session.rightDocument().rows.at(0).at(1), QString("left"));
        QCOMPARE(session.comparison().statistics.equalRows, 1);
        QVERIFY(session.comparison().differences.isEmpty());
    }

    void unsupportedWorkbookIsExplicit()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("source.xlsx");
        writeFile(path, "id,value\n1,2\n");
        TableCompareSession session(path, {});
        QString error;
        QVERIFY(!session.open(&error));
        QVERIFY(error.contains("Excel"));
        QVERIFY(error.contains("not supported"));
        QCOMPARE(readFile(path), QByteArray("id,value\n1,2\n"));
    }

    void settingsRoundTripAndLiveApplication()
    {
        QTemporaryDir directory;
        const auto leftPath = directory.filePath("left.txt"), rightPath = directory.filePath("right.tsv");
        writeFile(leftPath, "id||amount||note\n1||10.0||left\n2||20||old\n");
        writeFile(rightPath, "id\tamount\tnote\n2\t20.09\tnew\n1\t10\tright\n");
        TableCompareSession original(leftPath, rightPath);
        Table::ParseOptions left, right;
        left.delimiter = "||";
        left.encoding = "UTF-8";
        right.delimiter = "\t";
        right.encoding = "UTF-8";
        QVERIFY(original.setParseOptions(left, right));
        QVERIFY(original.open());
        auto options = keyNumericRules();
        options.minimumSimilarity = 0.75;
        options.columns[1].relativeTolerance = 0.01;
        QVERIFY(original.setComparisonOptions(options));
        TableCompareSession restored(leftPath, rightPath);
        for (const QString &key : original.sessionSettings()->keys())
            QVERIFY(restored.sessionSettings()->setValue(key, original.sessionSettings()->value(key)));
        QVERIFY(restored.open());
        QCOMPARE(restored.parseOptions(true).delimiter, QString("||"));
        QCOMPARE(restored.parseOptions(false).delimiter, QString("\t"));
        QCOMPARE(restored.parseOptions(true).encoding, QByteArray("UTF-8"));
        QCOMPARE(restored.parseOptions(false).firstRowHeader, true);
        QCOMPARE(restored.comparisonOptions().alignment, Table::RowAlignment::Key);
        QCOMPARE(restored.comparisonOptions().columnMode, Table::ColumnMappingMode::Explicit);
        QCOMPARE(restored.comparisonOptions().minimumSimilarity, 0.75);
        QCOMPARE(restored.comparisonOptions().columns.at(1).absoluteTolerance, 0.1);
        QCOMPARE(restored.comparisonOptions().columns.at(1).relativeTolerance, 0.01);
        QCOMPARE(restored.comparisonOptions().columns.at(2).role, Table::ColumnRole::Ignore);
        QCOMPARE(restored.comparison().statistics.equalRows, 2);
        QVariantList columns = restored.sessionSettings()->value("table.columns").toList();
        QVariantMap numeric = columns[1].toMap();
        numeric["compareFormatting"] = true;
        columns[1] = numeric;
        QVERIFY(restored.sessionSettings()->setValue("table.columns", columns));
        QCOMPARE(restored.comparison().statistics.differentRows, 2);
        QVERIFY(!restored.isDirty());
    }

    void failedParseOptionsKeepDocumentsAndSettings()
    {
        QTemporaryDir directory;
        const auto leftPath = directory.filePath("left.csv"), rightPath = directory.filePath("right.csv");
        writeFile(leftPath, "id,value\n1,2\n");
        writeFile(rightPath, "id,value\n1,3\n");
        TableCompareSession session(leftPath, rightPath);
        QVERIFY(session.open());
        const auto oldLeft = session.leftDocument().rows, oldRight = session.rightDocument().rows;
        Table::ParseOptions left, right;
        left.delimiter = ";";
        right.delimiter = "\"";
        QString error;
        QVERIFY(!session.setParseOptions(left, right, &error));
        QCOMPARE(session.parseOptions(true).delimiter, QString());
        QCOMPARE(session.parseOptions(false).delimiter, QString());
        QCOMPARE(session.leftDocument().rows, oldLeft);
        QCOMPARE(session.rightDocument().rows, oldRight);
        QCOMPARE(session.leftPath(), leftPath);
        QCOMPARE(session.rightPath(), rightPath);
        QSignalSpy errors(&session, &CompareSession::errorReported);
        session.sessionSettings()->setValue("table.right.delimiter", QString("\""));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(session.sessionSettings()->value("table.right.delimiter").toString(), QString());
        QCOMPARE(session.parseOptions(false).delimiter, QString());
        QCOMPARE(session.leftDocument().rows, oldLeft);
        QCOMPARE(session.rightDocument().rows, oldRight);
    }

    void formatControlsApplyIndependentDelimiters()
    {
        QTemporaryDir directory;
        const auto leftPath = directory.filePath("left.txt"), rightPath = directory.filePath("right.txt");
        writeFile(leftPath, "id||value\n1||same\n");
        writeFile(rightPath, "id;;value\n1;;same\n");
        TableCompareSession session(leftPath, rightPath);
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        auto *left = widget->findChild<QComboBox *>("leftTableDelimiter");
        auto *right = widget->findChild<QComboBox *>("rightTableDelimiter");
        QVERIFY(left && right);
        left->setEditText("||");
        right->setEditText(";;");
        auto *apply = button(widget.data(), "Apply formats");
        QVERIFY(apply);
        apply->click();
        QCOMPARE(session.parseOptions(true).delimiter, QString("||"));
        QCOMPARE(session.parseOptions(false).delimiter, QString(";;"));
        QCOMPARE(session.leftDocument().columnCount(), 2);
        QCOMPARE(session.rightDocument().columnCount(), 2);
        QCOMPARE(session.comparison().statistics.equalRows, 1);
        QVERIFY(session.comparison().ok());
        QVERIFY(widget->findChild<QLabel *>("tableWarnings")->text().contains("different parsing formats"));
    }

    void differenceNavigationSelectsAlignedRows()
    {
        QTemporaryDir directory;
        const auto leftPath = directory.filePath("left.csv"), rightPath = directory.filePath("right.csv");
        writeFile(leftPath, "id,value\n1,left\n2,same\n3,left\n4,same\n");
        writeFile(rightPath, "id,value\n1,right\n2,same\n3,right\n4,same\n");
        TableCompareSession session(leftPath, rightPath);
        QVERIFY(session.open());
        auto options = session.comparisonOptions();
        options.alignment = Table::RowAlignment::Position;
        QVERIFY(session.setComparisonOptions(options));
        QScopedPointer<QWidget> widget(session.createWidget());
        auto *left = widget->findChild<QTableView *>("leftTable");
        auto *right = widget->findChild<QTableView *>("rightTable");
        QCOMPARE(session.comparison().differences.size(), 2);
        session.firstDifference();
        QCOMPARE(session.currentDifference(), 0);
        QCOMPARE(left->selectionModel()->selectedRows().first().row(), 0);
        session.nextDifference();
        QCOMPARE(session.currentDifference(), 1);
        QCOMPARE(left->selectionModel()->selectedRows().first().row(), 2);
        QCOMPARE(right->selectionModel()->selectedRows().first().row(), 2);
        session.nextDifference();
        QCOMPARE(session.currentDifference(), 1);
        session.previousDifference();
        QCOMPARE(session.currentDifference(), 0);
        session.previousDifference();
        QCOMPARE(session.currentDifference(), 0);
        widget->findChild<QCheckBox *>("differenceOnly")->setChecked(true);
        session.lastDifference();
        QCOMPARE(left->selectionModel()->selectedRows().first().row(), 1);
        QCOMPARE(right->selectionModel()->selectedRows().first().row(), 1);
    }

    void columnDialogAppliesRulesAndPreservesTinyTolerance()
    {
        QTemporaryDir directory;
        const auto leftPath = directory.filePath("left.csv"), rightPath = directory.filePath("right.csv");
        writeFile(leftPath, "id,amount,note\nK1,1,left\n");
        writeFile(rightPath, "amount,note,id\n1.0000000000005,right,K1\n");
        TableCompareSession session(leftPath, rightPath);
        QVERIFY(session.open());
        auto position = session.comparisonOptions();
        position.columnMode = Table::ColumnMappingMode::Position;
        QVERIFY(session.setComparisonOptions(position));
        QScopedPointer<QWidget> widget(session.createWidget());
        auto *openRules = widget->findChild<QPushButton *>("tableColumnRules");
        QVERIFY(openRules);
        bool callbackRan = false;
        bool duplicateRejected = false;
        bool duplicateMessage = false;
        bool selectionIndependent = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Column rules dialog did not open");
            auto *grid = dialog->findChild<QTableWidget *>();
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!grid || !buttons || grid->rowCount() != 3)
                qFatal("Column rules controls are missing");
            auto *keyRight = qobject_cast<QComboBox *>(grid->cellWidget(0, 1));
            auto *valueRight = qobject_cast<QComboBox *>(grid->cellWidget(1, 1));
            auto *noteRight = qobject_cast<QComboBox *>(grid->cellWidget(2, 1));
            auto *absolute = qobject_cast<QLineEdit *>(grid->cellWidget(1, 4));
            auto *relative = qobject_cast<QLineEdit *>(grid->cellWidget(1, 5));
            if (!keyRight || !valueRight || !noteRight || !absolute || !relative)
                qFatal("Column rules editor types changed");
            keyRight->setCurrentIndex(keyRight->findData(0));
            valueRight->setCurrentIndex(valueRight->findData(0));
            buttons->button(QDialogButtonBox::Ok)->click();
            duplicateRejected = dialog->isVisible();
            for (QLabel *label : dialog->findChildren<QLabel *>())
                duplicateMessage = duplicateMessage || label->text().contains("used more than once");
            keyRight->setCurrentIndex(keyRight->findData(2));
            valueRight->setCurrentIndex(valueRight->findData(0));
            noteRight->setCurrentIndex(noteRight->findData(1));
            selectionIndependent = keyRight->currentData().toInt() == 2
                && valueRight->currentData().toInt() == 0 && noteRight->currentData().toInt() == 1;
            qobject_cast<QComboBox *>(grid->cellWidget(0, 2))->setCurrentIndex(int(Table::ColumnRole::Key));
            qobject_cast<QComboBox *>(grid->cellWidget(1, 3))->setCurrentIndex(1);
            qobject_cast<QComboBox *>(grid->cellWidget(2, 2))->setCurrentIndex(int(Table::ColumnRole::Ignore));
            absolute->setText("1e-12");
            relative->setText("0");
            for (QCheckBox *check : dialog->findChildren<QCheckBox *>())
                if (check->text().startsWith("Align rows")) check->setChecked(true);
            callbackRan = true;
            buttons->button(QDialogButtonBox::Ok)->click();
            // A validation regression must fail a test rather than strand its modal loop.
            if (dialog->isVisible()) dialog->reject();
        });
        openRules->click();
        QVERIFY(callbackRan);
        QVERIFY(duplicateRejected);
        QVERIFY(duplicateMessage);
        QVERIFY(selectionIndependent);
        QCOMPARE(session.comparisonOptions().columnMode, Table::ColumnMappingMode::Explicit);
        QCOMPARE(session.comparisonOptions().alignment, Table::RowAlignment::Key);
        QCOMPARE(session.comparisonOptions().columns.at(0).right, 2);
        QCOMPARE(session.comparisonOptions().columns.at(1).right, 0);
        QCOMPARE(session.comparisonOptions().columns.at(2).right, 1);
        QCOMPARE(session.comparisonOptions().columns.at(1).absoluteTolerance, 1e-12);
        QCOMPARE(session.comparison().statistics.equalRows, 1);
        QCOMPARE(session.comparison().statistics.ignoredCells, 1);
        QCOMPARE(session.sessionSettings()->value("table.columns").toList().at(1).toMap()
                 .value("absoluteTolerance").toDouble(), 1e-12);
        double reopenedTolerance = 0;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Column rules dialog did not reopen");
            auto *grid = dialog->findChild<QTableWidget *>();
            reopenedTolerance = qobject_cast<QLineEdit *>(grid->cellWidget(1, 4))->text().toDouble();
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            if (dialog->isVisible()) dialog->reject();
        });
        openRules->click();
        QCOMPARE(reopenedTolerance, 1e-12);
        QCOMPARE(session.comparisonOptions().columns.at(1).absoluteTolerance, 1e-12);
        QCOMPARE(session.comparison().statistics.equalRows, 1);
    }
};

QTEST_MAIN(TableViewTests)
#include "tst_tableview.moc"
