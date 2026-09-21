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
#include <QSplitter>
#include <QElapsedTimer>
#include <QAbstractTextDocumentLayout>
#include <QTextBlock>
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
// 单个文档行在窗格里占的像素高度。
//
// `QPlainTextEdit::blockBoundingGeometry()` 是 protected 的——`TextPane` 自己在
// `paintGutter()` 里能用，从外面不行——所以走文档布局层取同一个量（行高由字体决定，
// 两处问的是同一件事）。TXT-001 标准 2 的「行高严格对齐」要靠它比出来。
int rowHeightOf(const QPlainTextEdit *pane, int blockNumber)
{
    const QTextBlock block = pane->document()->findBlockByNumber(blockNumber);
    return qRound(pane->document()->documentLayout()->blockBoundingRect(block).height());
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

    // =========================================================================
    // TXT-001 文本比对会话与双窗格视图骨架（issue #56）
    //
    // 这一条与 TXT-002 / TXT-008 属同一类：**实现早就在**——`TextCompareSession`
    // 与 `TextCompareView` 从会话框架落地那天起就是这套形状，issue 上却一直是「待实现」。
    // 因此本轮不新增模块，只把四条完成标准逐条变成**会真的红的断言**。
    //
    // 骨架最容易烂掉的方式不是「功能消失」，而是「两侧各自显示自己的文件」——
    // 那种状态看起来完全正常，既有用例也一条都不会红，却让所有对齐工作白做。
    // 所以下面每一条都尽量落到「两个窗格是同一个模型的两半」这件事上。
    // =========================================================================

    // 标准 1：可打开两个本地文本文件并显示并排双栏视图。
    //
    // 「并排」是这一条真正的要求，也最容易被满足成「都在界面上」——两块窗格上下
    // 堆叠同样能让「两个 TextPane 都找得到」的检查通过。因此这里断言的是**几何关系**
    // （水平排列、不重叠、纵向齐平），而不是存在性。
    void twoLocalFilesOpenSideBySide()
    {
        QTemporaryDir directory;
        const QString left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        writeFile(left, "alpha\nbravo\ncharlie\n");
        writeFile(right, "alpha\nBRAVO\ncharlie\ndelta\n");
        // 走「构造期就带上两个路径」这条路：规格的入口是「打开两个文本文件」，
        // 用户按的是这个。已有的 `emptySessionAndRealFiles` 覆盖的是另一条
        // （先开空会话、再在界面里选文件），两条都要有人在。
        TextCompareSession session(left, right);
        QVERIFY(session.open());
        QScopedPointer<QWidget> widget(session.createWidget());
        widget->resize(1280, 700);
        widget->show();
        QTest::qWait(30);
        auto *a = widget->findChild<TextPane *>("leftTextPane");
        auto *b = widget->findChild<TextPane *>("rightTextPane");
        QVERIFY2(a && b, "双栏视图必须真的有两块编辑区");
        auto *splitter = widget->findChild<QSplitter *>("textSplitter");
        QVERIFY2(splitter, "双栏必须是可拖动的分栏，否则「并排」只写在文档里");
        QVERIFY2(splitter->orientation() == Qt::Horizontal, "两块窗格没有按水平方向排列");
        const QPoint aOrigin = a->mapTo(widget.data(), QPoint(0, 0));
        const QPoint bOrigin = b->mapTo(widget.data(), QPoint(0, 0));
        QVERIFY2(aOrigin.x() < bOrigin.x(), "左侧窗格不在右侧窗格左边（可能被上下堆叠了）");
        QCOMPARE(aOrigin.y(), bOrigin.y()); // 纵向齐平：不齐平的话翻到同一行也会错开
        QVERIFY2(a->width() > 100 && b->width() > 100, "两块窗格都必须有可见宽度");
        // 两侧读到的必须**是各自那个文件**，不是把同一个文件显示了两遍。
        QCOMPARE(session.leftDocument().normalizedText(), QStringLiteral("alpha\nbravo\ncharlie\n"));
        QCOMPARE(session.rightDocument().normalizedText(), QStringLiteral("alpha\nBRAVO\ncharlie\ndelta\n"));
        QVERIFY(a->toPlainText().contains(QStringLiteral("bravo")));
        QVERIFY(b->toPlainText().contains(QStringLiteral("BRAVO")));
    }

    // 标准 2：两个窗格共享同一个比对结果模型，行号与行高严格对齐。
    //
    // 语料里**必须**有填充行（下面右第 1 行是插入）：没有填充行时，
    // 「两侧各显示自己的文件」与「两侧共用模型」给出完全一样的结果，这一条就白验了。
    // 填充行让两者分家——共用模型时左窗格那一行只能是空行。
    void panesShareOneModelAndKeepRowGeometryAligned()
    {
        TextCompareSession session;
        QVERIFY(session.open());
        QVERIFY(session.setText(true, "one\ntwo\nfour\nfive\n"));
        QVERIFY(session.setText(false, "one\ninserted\ntwo\nFOUR\nfive\n"));
        QScopedPointer<QWidget> widget(session.createWidget());
        widget->resize(1280, 700);
        widget->show();
        QTest::qWait(30);
        auto *a = widget->findChild<TextPane *>("leftTextPane");
        auto *b = widget->findChild<TextPane *>("rightTextPane");
        const auto &result = session.comparison();
        QVERIFY(!result.rows.isEmpty());
        // 「共享同一个结果模型」的可观测形式：两侧的**视觉行数**都等于模型的行数。
        // 于是第 i 行在两个窗格里是同一个 `Row` 的两半，「行号对齐」才有意义。
        QCOMPARE(a->blockCount(), result.rows.size());
        QCOMPARE(b->blockCount(), result.rows.size());
        const QStringList leftRows = a->toPlainText().split(QLatin1Char('\n'));
        const QStringList rightRows = b->toPlainText().split(QLatin1Char('\n'));
        QCOMPARE(leftRows.size(), result.rows.size());
        QCOMPARE(rightRows.size(), result.rows.size());
        // 号码槽与正文是**两条链**，必须各钉一遍。只钉正文的话，「号码根本没传下去」
        // 或「号码传成了另一侧那份」都不会红，而用户看到的行号就会与真正显示的内容
        // 对不上——那正是「行号与行高严格对齐」这一条要防的东西。
        QCOMPARE(a->lineNumbers().size(), result.rows.size());
        QCOMPARE(b->lineNumbers().size(), result.rows.size());
        int paddingRows = 0;
        int lastLeftLine = -1, lastRightLine = -1;
        for (int row = 0; row < result.rows.size(); ++row) {
            const Text::Row &entry = result.rows.at(row);
            // 逐行核对「模型 → 两侧显示的文字」。行号那一半也在这里：
            // 窗格左侧的号码槽画的就是 `leftLine + 1` / `rightLine + 1`，
            // 所以「第几行显示的是第几行原文」被钉住，号码就不可能错位。
            QCOMPARE(leftRows.at(row), entry.leftLine >= 0
                         ? session.leftDocument().lines().at(entry.leftLine).text : QString());
            QCOMPARE(rightRows.at(row), entry.rightLine >= 0
                         ? session.rightDocument().lines().at(entry.rightLine).text : QString());
            QCOMPARE(a->lineNumbers().at(row), entry.leftLine);
            QCOMPARE(b->lineNumbers().at(row), entry.rightLine);
            // 顺序不变量：**有内容的**行下标只增不减（填充行的 -1 不参与）。
            // 两侧行数相同但整体错开一格时，上面两条逐行比对会红；这一条防的是
            // 更隐蔽的「同一个原文行在一列里出现两次」——那会让行号槽指着两行。
            if (entry.leftLine >= 0) {
                QVERIFY2(entry.leftLine > lastLeftLine, "左侧行下标没有严格递增");
                lastLeftLine = entry.leftLine;
            } else {
                ++paddingRows;
            }
            if (entry.rightLine >= 0) {
                QVERIFY2(entry.rightLine > lastRightLine, "右侧行下标没有严格递增");
                lastRightLine = entry.rightLine;
            }
        }
        QVERIFY2(paddingRows > 0, "夹具没有造出填充行，这一条验不到「共用模型」");
        // 行高的那一半：同一个 `Row` 在两侧必须占同一个高度。字体不同、或某一侧
        // 开了自动换行，行高就会分家，表现是「滚到下面越来越错位」。
        QCOMPARE(a->font(), b->font());
        QCOMPARE(a->fontMetrics().height(), b->fontMetrics().height());
        QCOMPARE(a->lineWrapMode(), b->lineWrapMode());
        QVERIFY2(a->lineWrapMode() == QPlainTextEdit::NoWrap,
                 "窗格一旦开了自动换行，一个模型行会占多行，「行高严格对齐」就不再成立");
        QCOMPARE(rowHeightOf(a, 0), rowHeightOf(b, 0));
        // 末尾那行的行高也要比：只比第一行的话，「某一侧最后一行被撑高」
        // （例如末尾多了一个换行）不会被发现，而它同样会让滚动同步错位。
        QCOMPARE(rowHeightOf(a, a->blockCount() - 1), rowHeightOf(b, b->blockCount() - 1));
    }

    // 标准 3：单侧为空（新文件）时另一侧全部行标记为新增/删除而不是报错。
    //
    // 「新文件」有两条来源：磁盘上真实存在的 0 字节文件，以及根本没有文件的那一侧。
    // 下面走前者（走真实文件路径才对得上规格里的入口），后者由既有的
    // `editorAppliesWithoutWritingPadding` 覆盖（那里左路径是空串）。
    void emptySideMarksEveryOtherRowAsInsertOrDeleteWithoutError()
    {
        QTemporaryDir directory;
        const QString blank = directory.filePath("new.txt"), filled = directory.filePath("filled.txt");
        writeFile(blank, QByteArray());
        writeFile(filled, "alpha\nbravo\ncharlie\n");
        {
            TextCompareSession session(blank, filled);
            QString error;
            QVERIFY2(session.open(&error), qPrintable(error));
            QVERIFY2(error.isEmpty(), qPrintable(error));
            // 空侧是「新文件」而不是「读不出来的文件」：0 行、可编辑。
            QVERIFY(session.leftDocument().lines().isEmpty());
            QVERIFY(session.leftDocument().canEdit());
            const auto &result = session.comparison();
            QCOMPARE(result.rows.size(), 3);
            for (int row = 0; row < result.rows.size(); ++row) {
                QCOMPARE(result.rows.at(row).leftLine, -1);
                QCOMPARE(result.rows.at(row).rightLine, row);
                QCOMPARE(result.rows.at(row).change, Text::Change::Insert);
            }
            // 整段是**一个**新增块，不是「替换成空」：`leftCount` 为 0 是这一条的分水岭，
            // 若实现走成 Replace，界面上会显示成「左边原来有内容被删掉了」。
            QCOMPARE(result.blocks.size(), 1);
            QCOMPARE(result.blocks.first().change, Text::Change::Insert);
            QCOMPARE(result.blocks.first().leftCount, 0);
            QCOMPARE(result.blocks.first().rightCount, 3);
            // 视图里两侧行数一致，且**两侧每一行**都被标记（左侧是填充行，
            // 它记的是「这一行在左边不存在」），一行都不许漏。
            QScopedPointer<QWidget> widget(session.createWidget());
            widget->resize(1280, 700);
            widget->show();
            QTest::qWait(30);
            auto *left = widget->findChild<TextPane *>("leftTextPane");
            auto *right = widget->findChild<TextPane *>("rightTextPane");
            QCOMPARE(left->blockCount(), 3);
            QCOMPARE(right->blockCount(), 3);
            QCOMPARE(left->toPlainText(), QStringLiteral("\n\n"));
            QCOMPARE(rowColours(left).size(), 3);
            QCOMPARE(rowColours(right).size(), 3);
        }
        {
            // 反向：右侧为空时全部是删除。两个方向都要验——只验一侧的话，
            // 把插入与删除写反（界面上表现为「新增的文件显示成被删除」）不会有人发现。
            TextCompareSession session(filled, blank);
            QString error;
            QVERIFY2(session.open(&error), qPrintable(error));
            const auto &result = session.comparison();
            QCOMPARE(result.rows.size(), 3);
            for (int row = 0; row < result.rows.size(); ++row) {
                QCOMPARE(result.rows.at(row).leftLine, row);
                QCOMPARE(result.rows.at(row).rightLine, -1);
                QCOMPARE(result.rows.at(row).change, Text::Change::Delete);
            }
            QCOMPARE(result.blocks.size(), 1);
            QCOMPARE(result.blocks.first().change, Text::Change::Delete);
            QCOMPARE(result.blocks.first().leftCount, 3);
            QCOMPARE(result.blocks.first().rightCount, 0);
        }
    }

    // 标准 4：两侧完全不相关时不发生算法退化（无超长耗时、无栈溢出）。
    //
    // 这一条在引擎层已经有人守着了（`Tests/Text` 的 `tenThousandUnrelatedLines`
    // 与 `boundedAdversarialInputIsReportedAsLimited`），这里补的是**会话层**：
    // 服务层的 `alignmentLimited` 必须一路走到用户看得见的状态栏文字上，
    // 否则「文件太大只做了粗略对齐」这件事只存在于一个没人读的字段里。
    void unrelatedFilesStayBoundedWithoutDegrading()
    {
        QTemporaryDir directory;
        const QString left = directory.filePath("left.txt"), right = directory.filePath("right.txt");
        // ① 两侧毫无公共行：必须走精确的快速路径，既不受限也不出错。
        {
            QString a, b;
            for (int i = 0; i < 4000; ++i) {
                a += QStringLiteral("left %1\n").arg(i);
                b += QStringLiteral("right %1\n").arg(i);
            }
            writeFile(left, a.toUtf8());
            writeFile(right, b.toUtf8());
        }
        {
            TextCompareSession session(left, right);
            QElapsedTimer timer;
            timer.start();
            // 「不相关」不是错误：打开必须成功，而不是拿「文件差异过大」把用户挡回去。
            QVERIFY2(session.open(), "两侧完全不相关时打开失败了");
            QVERIFY2(timer.elapsed() < 5000, "4000 行完全不相关的文件超过了 5s");
            const auto &result = session.comparison();
            QCOMPARE(result.blocks.size(), 1);
            QCOMPARE(result.blocks.first().change, Text::Change::Replace);
            QCOMPARE(result.blocks.first().leftCount, 4000);
            QCOMPARE(result.blocks.first().rightCount, 4000);
            QVERIFY2(!result.alignmentLimited, "无公共行是不消耗预算的精确路径，不该报告受限");
            QCOMPARE(result.rows.size(), 4000); // 一行都不许丢
            // 视图也要能起来，并且两侧行数一致——退化到栈溢出时这里根本走不到。
            QScopedPointer<QWidget> widget(session.createWidget());
            widget->resize(1280, 700);
            widget->show();
            QTest::qWait(30);
            auto *a = widget->findChild<TextPane *>("leftTextPane");
            auto *b = widget->findChild<TextPane *>("rightTextPane");
            QCOMPARE(a->blockCount(), 4000);
            QCOMPARE(b->blockCount(), 4000);
        }
        // ② 每隔一行共有一行：相邻相同段只有 1 行，是最容易把递归深度或工作量
        //    放大的形状。同样必须跑完、且结果结构完整。
        {
            QString a, b;
            for (int i = 0; i < 4000; ++i) {
                if (i % 2) { a += QStringLiteral("left %1\n").arg(i); b += QStringLiteral("right %1\n").arg(i); }
                else       { a += QStringLiteral("shared %1\n").arg(i); b += QStringLiteral("shared %1\n").arg(i); }
            }
            writeFile(left, a.toUtf8());
            writeFile(right, b.toUtf8());
        }
        {
            TextCompareSession session(left, right);
            QElapsedTimer timer;
            timer.start();
            QVERIFY(session.open());
            QVERIFY2(timer.elapsed() < 5000, "交错共享行的 4000 行输入超过了 5s");
            const auto &result = session.comparison();
            // 共享行必须被认出来。期望值 2000 **是可以推出来的**，不是照抄观测值：
            // 两侧的公共行只有那 2000 行 `shared i`（`left i` 与 `right i` 互不相同），
            // 因此最长公共子序列的长度就是 2000，任何正确的对齐都必须把这 2000 行
            // 报成 `Equal`。若哪天预算被调小、这一段退化成显式替换，这个数会变小——
            // 那时先确认是不是预算/算法真被改好了（那是好事），再改这里的期望值，
            // 而不要把这条删掉：它守的是「锚点要被找到」。
            int equalRows = 0;
            for (const Text::Row &row : result.rows) {
                if (row.change != Text::Change::Equal) continue;
                ++equalRows;
                // 顺带守一件事：`Equal` 必须是**真的**相同。把没比过的两行说成
                // 「一样」是最坏的一种错——它会让用户在界面上直接漏掉一处差异。
                QVERIFY(session.leftDocument().lines().at(row.leftLine).text
                        == session.rightDocument().lines().at(row.rightLine).text);
            }
            QCOMPARE(equalRows, 2000);
            int leftCursor = 0, rightCursor = 0;
            for (const Text::Block &block : result.blocks) {
                QCOMPARE(block.leftStart, leftCursor);
                QCOMPARE(block.rightStart, rightCursor);
                leftCursor += block.leftCount;
                rightCursor += block.rightCount;
            }
            QCOMPARE(leftCursor, 4000);
            QCOMPARE(rightCursor, 4000);
        }
        // ③ 预算真的用尽时：必须是**被报告**的受限，而不是一次静默的粗略对齐。
        //    构造与 `Tests/Text` 的同名用例一致（两侧各 2000 行互不相同、
        //    正中间共享一行）——没有那一行公共行时 `run()` 会走不耗预算的快速路径。
        {
            QString a, b;
            for (int i = 0; i < 2000; ++i) {
                a += QStringLiteral("L%1\n").arg(i);
                b += QStringLiteral("R%1\n").arg(i);
            }
            a += QStringLiteral("shared\n");
            b += QStringLiteral("shared\n");
            for (int i = 2000; i < 4000; ++i) {
                a += QStringLiteral("L%1\n").arg(i);
                b += QStringLiteral("R%1\n").arg(i);
            }
            writeFile(left, a.toUtf8());
            writeFile(right, b.toUtf8());
        }
        {
            TextCompareSession session(left, right);
            QVERIFY(session.open());
            const auto &result = session.comparison();
            QVERIFY2(result.alignmentLimited, "预算用尽的输入必须报告 alignmentLimited");
            // 会话层要把它说到用户看得见的地方：状态栏文字里必须有那句提示。
            // 只断言字段而不断言文字的话，把 `updateStatus()` 里那一行删掉不会有人发现，
            // 而用户看到的就是「两份大文件莫名显示成全都不一样」。
            QVERIFY2(session.statusText().contains(QStringLiteral("work limit")),
                     qPrintable(session.statusText()));
            // 受限不等于「放弃」：两侧的每一行仍然必须被某个块覆盖到，
            // 一行都不许在粗略对齐里凭空消失。夹具是 2000 + 1 + 2000 = 4001 行。
            int leftCursor = 0, rightCursor = 0;
            for (const Text::Block &block : result.blocks) {
                QCOMPARE(block.leftStart, leftCursor);
                QCOMPARE(block.rightStart, rightCursor);
                leftCursor += block.leftCount;
                rightCursor += block.rightCount;
            }
            QCOMPARE(leftCursor, 4001);
            QCOMPARE(rightCursor, 4001);
            QVERIFY(!result.rows.isEmpty());
        }
    }
};
QTEST_MAIN(TextViewTests)
#include "tst_textview.moc"
