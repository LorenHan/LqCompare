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
#include <QTextEdit>
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
// 行号 → 该行**实际生效**的底色。
//
// 读控件里的 `extraSelections`，而不是另抄一份调色板：抄一份就有了第二份事实来源，
// 改一边不会让另一边红（本仓已经把「同一件事两份实现」列为坑，见 handoff §6）。
// 顺带这也回答了「视图到底画了什么」这个问题——只有真正进了 `extraSelections`
// 的行才会被填色，跳过某个 Change 的实现会在这里露出「这一行没有标记」。
QHash<int, QColor> rowColours(const TextPane *pane)
{
    QHash<int, QColor> colours;
    for (const QTextEdit::ExtraSelection &selection : pane->extraSelections())
        colours.insert(selection.cursor.blockNumber(), selection.format.background().color());
    return colours;
}
// 「弱」的可比量：底色带多少彩度（最大通道 − 最小通道）。
//
// 不用「到白色的距离」或亮度：忽略色 (237,240,246) 的亮度比某些差异色还高，
// 拿亮度比会得出「忽略比差异更显眼」这种与直觉相反的结论。彩色差异色与
// 忽略色的真正区别在**有没有颜色**上，所以彩度才是这里该比的量。
int chromaOf(const QColor &colour)
{
    return qMax(qMax(colour.red(), colour.green()), colour.blue())
         - qMin(qMin(colour.red(), colour.green()), colour.blue());
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
        // 差异**块**是 3 块（新增 `new`、删除 `c`、新增 `changed`），
        // 而「一处改动」是 2 处（`new` 那处插入，以及 `c`→`changed` 那处改写）。
        // 两个数都要断言：TXT-005 之后块的粒度可以比「一处改动」更细，
        // 界面上的导航/状态栏/复制全都按后者算，只盯块数会把这条契约漏掉。
        QCOMPARE(session.comparison().differences.size(), 3);
        QCOMPARE(session.differenceCount(), 2);
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

    // TXT-008 标准 1 后半句：被忽略的差异**仍要在视图里留下弱化标记**。
    //
    // 为什么这一条非要有视图级用例：标准的前半句（不再算差异）服务层已经守住了，
    // 而「仍有弱化提示」只在**画出来的东西**上成立。一个把 `Change::Ignored`
    // 直接 `continue` 掉的 `highlight()` 能让服务层的所有用例全绿，
    // 界面上却表现为「明明有大小写差异，却干干净净什么都看不出来」——
    // 用户会以为文件完全一样，这正是本条要防的那种错。
    void ignoredRowsStayMarkedAndWeakerThanRealDifferences()
    {
        // ① 同一对文件里既有「仅大小写不同」的行，也有一处真差异：
        //    这样忽略色与真实差异色可以在**同一次渲染**里直接比。
        TextCompareSession session;
        QVERIFY(session.open());
        QVERIFY(session.setText(true, "Alpha\nkeep\nBravo\n"));
        QVERIFY(session.setText(false, "alpha\nkeep\nBravo2\n"));
        QScopedPointer<QWidget> widget(session.createWidget());
        widget->resize(1280, 700);
        widget->show();
        // 关着时两行都算差异；开了之后只剩第 2 行那处真差异。
        QCOMPARE(session.comparison().differences.size(), 2);
        widget->findChild<QCheckBox *>("ignoreCase")->setChecked(true);
        QCOMPARE(session.comparison().differences.size(), 1);
        QCOMPARE(session.comparison().ignoredBlocks, 1);
        auto *left = widget->findChild<TextPane *>("leftTextPane");
        auto *right = widget->findChild<TextPane *>("rightTextPane");
        const auto leftColours = rowColours(left);
        const auto rightColours = rowColours(right);
        // 第 0 行是「仅大小写不同」→ 差异身份没了，但两侧都仍必须被标记。
        QVERIFY2(leftColours.contains(0), "左侧的被忽略行没有留下任何标记");
        QVERIFY2(rightColours.contains(0), "右侧的被忽略行没有留下任何标记");
        // 第 1 行真的相同，不该有标记——否则「有标记」这件事就不传递任何信息了。
        QVERIFY(!leftColours.contains(1));
        QVERIFY(!rightColours.contains(1));
        // 第 2 行是真差异，必须换一种底色。
        QVERIFY(leftColours.contains(2));
        QVERIFY(rightColours.contains(2));
        const QColor ignored = leftColours.value(0);
        const QColor replaced = leftColours.value(2);
        QVERIFY2(ignored != replaced, "被忽略的行与真实差异行用了同一种底色");
        // 「弱化」在这里的判据是彩度更低：忽略色的作用是让人**能看见但不去处理**，
        // 真实差异色的作用正相反。这条语料里真差异只会是 Replace（两侧行数相同、
        // 逐行对应），所以这个比较是确定的。
        QVERIFY2(chromaOf(ignored) < chromaOf(replaced),
                 qPrintable(QStringLiteral("忽略色 %1 的彩度不低于差异色 %2")
                                .arg(ignored.name(), replaced.name())));
        // 而且标记必须看得见：底色不能等于编辑区自己的底色（等于白底 = 没标记）。
        QVERIFY2(ignored != left->palette().base().color(),
                 "被忽略行的底色与编辑区底色相同，等于没有提示");

        // ② 上面只比过 Replace。把 Insert 与 Delete 的底色也采出来，
        //    断言忽略色与**每一种**真实差异色都不同——否则「弱化」可能只是
        //    恰好等于某个差异色，在别的语料里就看不出来了。
        QSet<QRgb> realDifferenceColours;
        const QVector<QPair<QString, QString>> fixtures{
            {QStringLiteral("A\ntwo\nC\n"), QStringLiteral("A\nXXX\nC\nNEW\n")},  // Replace + Insert
            {QStringLiteral("A\ntwo\nC\nZed\n"), QStringLiteral("A\nXXX\nC\n")},  // Replace + Delete
        };
        for (const auto &paths : fixtures) {
            TextCompareSession other;
            QVERIFY(other.open());
            QVERIFY(other.setText(true, paths.first));
            QVERIFY(other.setText(false, paths.second));
            QScopedPointer<QWidget> holder(other.createWidget());
            holder->resize(1280, 700);
            holder->show();
            for (const char *name : {"leftTextPane", "rightTextPane"}) {
                auto *pane = holder->findChild<TextPane *>(QString::fromLatin1(name));
                const auto colours = rowColours(pane);
                for (auto it = colours.constBegin(); it != colours.constEnd(); ++it)
                    realDifferenceColours.insert(it.value().rgb());
            }
        }
        // Insert 与 Delete 必须真的被采到，否则上面那句「与每一种都不同」是空话。
        QVERIFY2(realDifferenceColours.size() >= 3,
                 qPrintable(QStringLiteral("夹具没有造齐三种真实差异色（只采到 %1 种）")
                                .arg(realDifferenceColours.size())));
        QVERIFY(!realDifferenceColours.contains(ignored.rgb()));
    }
    // TXT-009 第 3、4 条在**视图这一侧**能验的那一半。
    //
    // 第 3 条原本的隐患不是「枚举不互斥」——枚举天生互斥——而是界面与枚举之间
    // 靠**序号**对应：下拉里写死三行文案，读回时 `static_cast<Whitespace>(currentIndex())`。
    // 那种写法下调换两条文案、或在枚举中间插一个取值，「忽略全部空白」会静默
    // 变成另一个模式，而构建、运行、既有用例**全都不红**。下面这组断言把
    // 界面上的第 i 行钉死在模式表的第 i 项上。
    //
    // 第 4 条只验「可视标记」那一半：视图里没有 View 页设置界面
    // （那属于 OPT-007 文本编辑与视图选项，尚未落地），所以「标记可被开关关闭」
    // 无从实现，issue #64 里已按「部分完成」记下这一半的依赖。
    void whitespaceComboFollowsTheModeTableAndMarksIgnoredRows()
    {
        TextCompareSession session;
        QVERIFY(session.open());
        // 语料按「两级开关的分水岭」挑：
        //   第 0 行 两侧只差**空白的数量**（一个空格 vs 两个）→ 第 1 级就判等；
        //   第 2 行 两侧差**空白的有无**（`gamma delta` vs `gammadelta`）→ 只有第 2 级判等。
        // 两行合起来，三个模式各自给出不同的结论，序号错位无处可藏。
        // 中间那行**必须**逐字符相同：`differences` 数的是**变更块**而不是行，
        // 相邻的两处替换会被并成一个块，那样三个模式的块数就分不出来了（本轮踩过）。
        QVERIFY(session.setText(true, "alpha beta\nkeep\ngamma delta\n"));
        QVERIFY(session.setText(false, "alpha  beta\nkeep\ngammadelta\n"));
        QScopedPointer<QWidget> widget(session.createWidget());
        widget->resize(1280, 700);
        widget->show();
        auto *combo = widget->findChild<QComboBox *>("whitespace");
        QVERIFY2(combo, "空白模式下拉必须能被找到，否则这一条无从验起");

        const QVector<Text::Whitespace> modes = Text::availableWhitespaces();
        QCOMPARE(combo->count(), modes.size());
        // 先钉「界面第 i 行**显示的字**」↔「模式表第 i 项」。
        //
        // 这一条与下面那条（选了第 i 行、引擎用的就是表第 i 项）是一对，缺一不可：
        // 只钉值那一半的话，把下拉的铺法改成倒序（文案与序号脱钩）**不会让任何用例变红**
        // ——用户选「忽略全部空白」却得到「比较空白」的行为，而测试全绿。本轮实测漏检过。
        // 期望文案从视图自己的标签函数取，这样不复制任何界面字符串（同一件事两份文案
        // 是本仓记过的坑），同时顺序仍然由模式表决定，所以不是同义反复。
        for (int index = 0; index < modes.size(); ++index)
            QCOMPARE(combo->itemText(index),
                     TextCompareView::whitespaceLabel(modes.at(index)));
        // 三个文案还必须彼此不同，否则「用户选的是哪一行」在界面上根本分辨不出来
        // （三条一样的文案照样能通过上面那条逐项比对）。
        QCOMPARE(QSet<QString>({combo->itemText(0), combo->itemText(1), combo->itemText(2)}).size(), 3);
        // 逐行核对「界面第 i 行」↔「表第 i 项」↔「会话选项」↔「落盘的设置值」。
        // 最后那段是**故意**钉住「落盘存的整数就是枚举序号」这件事：
        // 表一旦重排，旧的会话设置会被解释成另一个模式，这是必须配迁移的破坏性改动，
        // 让它在这里红，比让用户在半年后发现「我的会话选项自己变了」要好。
        for (int index = 0; index < modes.size(); ++index) {
            combo->setCurrentIndex(index);
            QCOMPARE(session.comparisonOptions().whitespace, modes.at(index));
            QCOMPARE(session.sessionSettings()->value(QStringLiteral("text.whitespace")).toInt(),
                     static_cast<int>(modes.at(index)));
        }

        combo->setCurrentIndex(modes.indexOf(Text::Whitespace::Exact));
        QCOMPARE(session.comparison().differences.size(), 2);
        QCOMPARE(session.comparison().ignoredBlocks, 0);
        // 第 1 级：只有「数量不同」的那行被判等，且是**忽略**而不是相等。
        combo->setCurrentIndex(modes.indexOf(Text::Whitespace::IgnoreChanges));
        QCOMPARE(session.comparison().differences.size(), 1);
        QCOMPARE(session.comparison().ignoredBlocks, 1);
        // 第 2 级：空白的「有无」也不再算差异，两行都判等。
        combo->setCurrentIndex(modes.indexOf(Text::Whitespace::IgnoreAll));
        QVERIFY(session.comparison().differences.isEmpty());
        QCOMPARE(session.comparison().ignoredBlocks, 2);

        // 第 4 条那一半：被忽略的**空白**差异在两侧都必须留下看得见的标记。
        auto *left = widget->findChild<TextPane *>("leftTextPane");
        auto *right = widget->findChild<TextPane *>("rightTextPane");
        const auto leftColours = rowColours(left);
        const auto rightColours = rowColours(right);
        for (int row : {0, 2}) {
            QVERIFY2(leftColours.contains(row),
                     qPrintable(QStringLiteral("左侧第 %1 行被忽略却没有留下标记").arg(row)));
            QVERIFY2(rightColours.contains(row),
                     qPrintable(QStringLiteral("右侧第 %1 行被忽略却没有留下标记").arg(row)));
        }
        // 第 1 行真的相同，不该有标记——否则「有标记」这件事不传递任何信息。
        QVERIFY(!leftColours.contains(1));
        QVERIFY(!rightColours.contains(1));
        // 而且必须看得见：底色等于编辑区底色就等于没标记。
        QVERIFY2(leftColours.value(0) != left->palette().base().color(),
                 "被忽略空白差异的底色与编辑区底色相同，等于没有提示");
    }
};
QTEST_MAIN(TextViewTests)
#include "tst_textview.moc"
