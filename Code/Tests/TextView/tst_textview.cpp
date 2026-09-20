#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QScopedPointer>
#include <QScrollBar>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QComboBox>
#include <QShortcut>
#include "textcomparesession.h"
#include "textcompareview.h"

using namespace LqCompare;
namespace {
void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) qFatal("fixture write failed");
}
QByteArray readFile(const QString &path)
{
    QFile file(path); file.open(QIODevice::ReadOnly); return file.readAll();
}
}
class TextViewTests : public QObject {
    Q_OBJECT
private slots:
    void emptySessionAndRealFiles()
    {
        TextCompareSession session;
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        QVERIFY(widget->findChild<TextPane *>("leftTextPane"));
        QVERIFY(widget->findChild<TextPane *>("rightTextPane"));
        QTemporaryDir directory;
        const QString left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, "a\nb\nc\nd\n"); writeFile(right, "a\nnew\nb\nchanged\nd\n");
        QVERIFY(session.setPaths(left, right));
        QCOMPARE(session.leftPath(), left);
        QCOMPARE(session.comparison().differences.size(), 2);
        auto *a = widget->findChild<TextPane *>("leftTextPane");
        auto *b = widget->findChild<TextPane *>("rightTextPane");
        QCOMPARE(a->blockCount(), b->blockCount());
        QVERIFY(a->isReadOnly()); QVERIFY(b->isReadOnly());
        session.firstDifference(); QCOMPARE(session.currentDifference(), 0);
        session.nextDifference(); QCOMPARE(session.currentDifference(), 1);
        session.nextDifference(); QCOMPARE(session.currentDifference(), 1);
        QVERIFY(session.statusText().contains("last"));
        session.previousDifference(); QCOMPARE(session.currentDifference(), 0);
        const QString screenshot = qEnvironmentVariable("LQCOMPARE_TEXT_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) {
            widget->resize(1280, 720);
            widget->show();
            QTest::qWait(100);
            QVERIFY(widget->grab().save(screenshot));
        }
    }
    void copyThenSaveIsExplicit()
    {
        QTemporaryDir directory;
        const QString left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, "a\nleft\nc\n"); writeFile(right, "a\nright\nc\n");
        TextCompareSession session(left, right);
        QVERIFY(session.open());
        QVERIFY(session.copyDifference(true));
        QVERIFY(session.isDirty());
        QCOMPARE(readFile(right), QByteArray("a\nright\nc\n"));
        QVERIFY(session.comparison().differences.isEmpty());
        QVERIFY(!session.reload());
        QVERIFY(!session.setPaths({}, {}));
        QVERIFY(session.save());
        QVERIFY(!session.isDirty());
        QCOMPARE(readFile(right), readFile(left));
    }
    void dirtyExternalConflictAndFailedReload()
    {
        QTemporaryDir directory;
        const QString left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, "left\n"); writeFile(right, "right\n");
        TextCompareSession session(left, right);
        QVERIFY(session.open());
        QVERIFY(session.setText(true, "edited left\n"));
        QVERIFY(session.setText(false, "edited right\n"));
        writeFile(right, "external\n");
        QString error;
        QVERIFY(!session.save(&error));
        QCOMPARE(readFile(left), QByteArray("left\n"));
        QCOMPARE(readFile(right), QByteArray("external\n"));
        QVERIFY(session.isDirty());
        QVERIFY(!session.setEncoding(true, "ISO-8859-1", &error));
        session.setDirty(false); // Explicit discard authorized by shell.
        QVERIFY(session.reload());
        QCOMPARE(session.leftDocument().normalizedText(), QStringLiteral("left\n"));
        QVERIFY(!session.setPaths(directory.filePath("missing"), right, &error));
        QCOMPARE(session.leftPath(), left);
        QCOMPARE(session.leftDocument().normalizedText(), QStringLiteral("left\n"));
    }
    void ruleUiChangesActualResult()
    {
        TextCompareSession session;
        QVERIFY(session.open());
        QVERIFY(session.setText(true, "Hello\n"));
        QVERIFY(session.setText(false, "hello\n"));
        QScopedPointer<QWidget> widget(session.createWidget());
        QCOMPARE(session.comparison().differences.size(), 1);
        widget->findChild<QCheckBox *>("ignoreCase")->setChecked(true);
        QCOMPARE(session.comparison().differences.size(), 0);
        QCOMPARE(session.comparison().ignoredBlocks, 1);
        QCOMPARE(session.leftDocument().normalizedText(), QStringLiteral("Hello\n"));
        QCOMPARE(session.sessionSettings()->value("text.ignoreCase").toBool(), true);
    }
    void saveAsProtectsOtherSide()
    {
        QTemporaryDir directory;
        const QString left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, "left"); writeFile(right, "right");
        TextCompareSession session(left, right);
        QVERIFY(session.open());
        QString error;
        QVERIFY(!session.saveSideAs(true, right, true, &error));
        QCOMPARE(readFile(right), QByteArray("right"));
        const auto output = directory.filePath("output.txt");
        QVERIFY(session.saveSideAs(true, output, false, &error));
        QCOMPARE(session.leftPath(), output);
    }
    void editorAppliesWithoutWritingPadding()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath("right.txt");
        writeFile(path, "one\r\ntwo\r\n");
        TextCompareSession session({}, path);
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        widget->resize(1280, 700);
        widget->show();
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) qFatal("Editor did not open");
            auto *editor = dialog->findChild<QPlainTextEdit *>("textBufferEditor");
            if (!editor) qFatal("Editor missing");
            editor->selectAll();
            editor->insertPlainText(QStringLiteral("one\ntwo changed\n"));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
        });
        widget->findChild<QPushButton *>("editRight")->click();
        QVERIFY(session.isDirty());
        QCOMPARE(session.rightDocument().bytes(), QByteArray("one\r\ntwo changed\r\n"));
        QCOMPARE(readFile(path), QByteArray("one\r\ntwo\r\n"));
        QVERIFY(session.saveSide(false));
        QCOMPARE(readFile(path), QByteArray("one\r\ntwo changed\r\n"));
    }
    void scrollingUsesAlignedRows()
    {
        TextCompareSession session;
        QVERIFY(session.open());
        QString left, right;
        for (int i = 0; i < 180; ++i) {
            const QString line = QStringLiteral("line %1\n").arg(i);
            left += line;
            if (i % 8 == 0) right += QStringLiteral("inserted\n");
            right += line;
        }
        QVERIFY(session.setText(true, left)); QVERIFY(session.setText(false, right));
        QScopedPointer<QWidget> widget(session.createWidget());
        widget->resize(1280, 700); widget->show(); QTest::qWait(30);
        auto *a = widget->findChild<TextPane *>("leftTextPane");
        auto *b = widget->findChild<TextPane *>("rightTextPane");
        a->verticalScrollBar()->setValue(80);
        QCOMPARE(a->verticalScrollBar()->value(), b->verticalScrollBar()->value());
        QCOMPARE(a->blockCount(), b->blockCount());
        QCOMPARE(a->blockCount(), session.comparison().rows.size());
    }
    void undoRedoSurvivesRuleAndSaveChanges()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("right.txt");
        writeFile(path, "original\r\n");
        TextCompareSession session({}, path);
        QVERIFY(session.open());
        QVERIFY(!session.canUndo());
        QVERIFY(session.setText(false, "first\n"));
        QVERIFY(session.setText(false, "second\n"));
        QVERIFY(session.canUndo()); QVERIFY(!session.canRedo());
        session.undo();
        QCOMPARE(session.rightDocument().bytes(), QByteArray("first\r\n"));
        QVERIFY(session.canRedo());
        auto options = session.comparisonOptions(); options.ignoreCase = true;
        session.setComparisonOptions(options);
        QVERIFY(session.canRedo());
        session.redo();
        QCOMPARE(session.rightDocument().bytes(), QByteArray("second\r\n"));
        QVERIFY(session.save()); QVERIFY(!session.isDirty());
        session.undo(); QVERIFY(session.isDirty());
        QCOMPARE(session.rightDocument().bytes(), QByteArray("first\r\n"));
        QCOMPARE(readFile(path), QByteArray("second\r\n"));
        session.redo(); QVERIFY(!session.isDirty());
        session.setLineEnding(false, Text::Eol::LF);
        QCOMPARE(session.rightDocument().bytes(), QByteArray("second\n"));
        session.undo(); QCOMPARE(session.rightDocument().bytes(), QByteArray("second\r\n"));
    }
    void searchAndLineJumpUseOriginalLineNumbers()
    {
        TextCompareSession session;
        QVERIFY(session.open());
        QVERIFY(session.setText(true, "a\nb\nsearch target\nd\n"));
        QVERIFY(session.setText(false, "a\ninserted\nb\nchanged\nd\n"));
        QScopedPointer<QWidget> widget(session.createWidget());
        widget->resize(1280, 700); widget->show();
        auto *left = widget->findChild<TextPane *>("leftTextPane");
        left->setFocus();
        QVERIFY(session.findText(QStringLiteral("target")));
        QCOMPARE(left->textCursor().selectedText(), QStringLiteral("target"));
        QVERIFY(!session.findText(QStringLiteral("missing")));
        QVERIFY(session.goToLine(3, true));
        QCOMPARE(left->textCursor().blockNumber(), 3); // Row 1 is an insertion gap.
        QVERIFY(!session.goToLine(20, true));
        QVERIFY(!session.goToLine(0, true));
    }
    void restoredRuleSettingsRecompute()
    {
        TextCompareSession session;
        session.sessionSettings()->setValue(QStringLiteral("text.ignoreCase"), true);
        session.sessionSettings()->setValue(QStringLiteral("text.whitespace"), 2);
        QVERIFY(session.open());
        QVERIFY(session.setText(true, "HELLO world\n"));
        QVERIFY(session.setText(false, "helloworld\n"));
        QVERIFY(session.comparison().differences.isEmpty());
        QCOMPARE(session.comparison().ignoredBlocks, 1);
        session.sessionSettings()->setValue(QStringLiteral("text.ignoreCase"), false);
        QCOMPARE(session.comparison().differences.size(), 1);
    }
    void cannotCopyDamagedDecoding()
    {
        QTemporaryDir directory;
        const auto path = directory.filePath("invalid.txt");
        writeFile(path, QByteArray::fromHex("6162ff63"));
        TextCompareSession session(path, {});
        QVERIFY(session.open());
        QVERIFY(!session.leftDocument().canEdit());
        QString error;
        QVERIFY(!session.copyDifference(true, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!session.isDirty());
        QVERIFY(session.setEncoding(true, "ISO-8859-1", &error));
        QVERIFY(session.leftDocument().canEdit());
        QVERIFY(session.copyDifference(true, &error));
        QVERIFY(session.isDirty());
    }
    void readOnlyIsEnforcedByEveryMutationApi()
    {
        QTemporaryDir directory;
        const auto left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, "left\r\n"); writeFile(right, "right\r\n");
        TextCompareSession session(left, right);
        session.setReadOnly(true, true);
        QVERIFY(session.open());
        QVERIFY(session.isSideReadOnly(true)); QVERIFY(session.isSideReadOnly(false));
        QString error;
        for (bool side : {true, false}) {
            QVERIFY(!session.setText(side, "changed\n", &error));
            QVERIFY(error.contains("read-only"));
            QVERIFY(!session.saveSide(side, &error));
            QVERIFY(!session.saveSideAs(side, directory.filePath(side ? "copyLeft" : "copyRight"), true, &error));
            QVERIFY(!session.setEncoding(side, "ISO-8859-1", &error));
            session.setLineEnding(side, Text::Eol::LF);
        }
        QVERIFY(!session.copyDifference(true, &error));
        QVERIFY(!session.copyDifference(false, &error));
        QVERIFY(!session.isDirty()); QVERIFY(!session.canSave());
        QVERIFY(!session.save(&error));
        QCOMPARE(session.leftDocument().bytes(), QByteArray("left\r\n"));
        QCOMPARE(session.rightDocument().bytes(), QByteArray("right\r\n"));
        QCOMPARE(readFile(left), QByteArray("left\r\n"));
        QCOMPARE(readFile(right), QByteArray("right\r\n"));
        QVERIFY(!QFile::exists(directory.filePath("copyLeft")));
        QVERIFY(!QFile::exists(directory.filePath("copyRight")));
        session.sessionSettings()->setValue(QStringLiteral("text.leftEncoding"), QByteArray("ISO-8859-1"));
        QVERIFY(session.reload());
        QCOMPARE(session.leftDocument().codecName(), QByteArray("UTF-8"));
        QScopedPointer<QWidget> widget(session.createWidget());
        auto *view = qobject_cast<TextCompareView *>(widget.data());
        QVERIFY(view);
        session.editSide(true);
        view->edit(false);
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(session.findText(QStringLiteral("left")));
        QVERIFY(session.goToLine(1, false));
        session.firstDifference(); QCOMPARE(session.currentDifference(), 0);
        session.lastDifference(); QCOMPARE(session.currentDifference(), 0);
    }
    void readonlyUiCannotBypassServiceGuards()
    {
        TextCompareSession session;
        QVERIFY(session.open());
        QVERIFY(session.setText(true, "left\r\n"));
        QVERIFY(session.setText(false, "right\r\n"));
        QScopedPointer<QWidget> widget(session.createWidget());
        session.setReadOnly(true, true);
        const auto beforeLeft = session.leftDocument().bytes();
        const auto beforeRight = session.rightDocument().bytes();
        for (const QString &name : {QStringLiteral("editLeft"), QStringLiteral("editRight"),
                                   QStringLiteral("saveLeft"), QStringLiteral("saveRight"),
                                   QStringLiteral("saveAsLeft"), QStringLiteral("saveAsRight"),
                                   QStringLiteral("copyToLeft"), QStringLiteral("copyToRight")}) {
            auto *button = widget->findChild<QPushButton *>(name);
            QVERIFY(button); QVERIFY(!button->isEnabled());
        }
        for (const QString &name : {QStringLiteral("encodingLeft"), QStringLiteral("encodingRight"),
                                   QStringLiteral("lineEndingLeft"), QStringLiteral("lineEndingRight")})
            QVERIFY(!widget->findChild<QComboBox *>(name)->isEnabled());
        // Deliberately re-enable UI controls: service guards remain authoritative.
        auto *edit = widget->findChild<QPushButton *>("editLeft");
        edit->setEnabled(true); edit->click();
        auto *saveAs = widget->findChild<QPushButton *>("saveAsRight");
        saveAs->setEnabled(true); saveAs->click();
        QVERIFY(!QApplication::activeModalWidget());
        auto *copy = widget->findChild<QPushButton *>("copyToRight");
        copy->setEnabled(true);
        QTimer::singleShot(0, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->accept();
        });
        copy->click();
        auto *ending = widget->findChild<QComboBox *>("lineEndingLeft");
        ending->setEnabled(true);
        QVERIFY(QMetaObject::invokeMethod(ending, "activated", Qt::DirectConnection, Q_ARG(int, 3)));
        QCOMPARE(session.leftDocument().bytes(), beforeLeft);
        QCOMPARE(session.rightDocument().bytes(), beforeRight);
    }
    void writableSideSavesBesideReadonlySource()
    {
        QTemporaryDir directory;
        const auto left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, "source\n"); writeFile(right, "destination\n");
        TextCompareSession session(left, right);
        session.setReadOnly(true, false);
        QVERIFY(session.open());
        QVERIFY(session.copyDifference(true));
        QVERIFY(session.canSave());
        QVERIFY(session.canUndo());
        session.undo();
        QCOMPARE(session.rightDocument().bytes(), QByteArray("destination\n"));
        QVERIFY(session.canRedo()); session.redo();
        QVERIFY(session.save());
        QCOMPARE(readFile(right), QByteArray("source\n"));
        QCOMPARE(readFile(left), QByteArray("source\n"));
        QVERIFY(session.setText(false, "a new target\n"));
        QString error;
        QVERIFY(!session.copyDifference(false, &error));
        QVERIFY(error.contains("read-only"));
    }
    void undoAndRedoCannotModifyLockedSides()
    {
        TextCompareSession session;
        QVERIFY(session.open());
        QVERIFY(session.setText(true, "left first\n"));
        QVERIFY(session.setText(false, "right first\n"));
        session.setReadOnly(true, false);
        QVERIFY(session.canUndo()); // Top operation only changes the right side.
        session.undo();
        QVERIFY(!session.canUndo());
        session.undo();
        QCOMPARE(session.leftDocument().normalizedText(), QStringLiteral("left first\n"));
        QVERIFY(session.canRedo()); session.redo();
        QCOMPARE(session.rightDocument().normalizedText(), QStringLiteral("right first\n"));
        session.setReadOnly(true, true);
        QVERIFY(!session.canUndo()); QVERIFY(!session.canSave());
        session.undo(); QCOMPARE(session.rightDocument().normalizedText(), QStringLiteral("right first\n"));
        session.setReadOnly(false, false);
        session.undo(); QVERIFY(session.canRedo());
        session.setReadOnly(false, true);
        QVERIFY(!session.canRedo());
        session.redo(); QVERIFY(session.rightDocument().lines().isEmpty());
        session.setReadOnly(false, false); QVERIFY(session.canRedo());
        session.redo(); QCOMPARE(session.rightDocument().normalizedText(), QStringLiteral("right first\n"));
    }
    void localShortcutsCanBeDelegatedBeforeAndAfterViewCreation()
    {
        TextCompareSession session;
        QVERIFY(session.open());
        QVERIFY(session.usesLocalShortcuts());
        session.setUseLocalShortcuts(false);
        QScopedPointer<QWidget> widget(session.createWidget());
        const auto shortcuts = widget->findChildren<QShortcut *>();
        QCOMPARE(shortcuts.size(), 5);
        for (QShortcut *shortcut : shortcuts) QVERIFY(!shortcut->isEnabled());
        session.setUseLocalShortcuts(true);
        for (QShortcut *shortcut : shortcuts) QVERIFY(shortcut->isEnabled());
        session.setUseLocalShortcuts(false);
        for (QShortcut *shortcut : shortcuts) QVERIFY(!shortcut->isEnabled());
    }
};
QTEST_MAIN(TextViewTests)
#include "tst_textview.moc"
