#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>

#include "optionsdialog.h"
#include "optionsruntime.h"
#include "logging.h"

using namespace LqCompare;

class ProbeDialog : public Options::OptionsDialog {
public:
    explicit ProbeDialog(Settings::OptionsRepository *repository) : OptionsDialog(repository) {}
    PendingChoice pendingChoice = PendingChoice::Cancel;
    bool allowReset = false;
    bool allowImport = true;
    int pendingCount = 0;
    int resetCount = 0;
    int importCount = 0;
    QString resetScope;
    QStringList chosenKeys;
    Settings::ImportPreview seenPreview;
    std::function<void()> afterPreview;
protected:
    PendingChoice confirmPendingChanges(const QString &) override {
        ++pendingCount;
        return pendingChoice;
    }
    bool confirmReset(const QString &category) override {
        ++resetCount;
        resetScope = category;
        return allowReset;
    }
    bool chooseImportEntries(const Settings::ImportPreview &preview, QStringList *keys) override {
        ++importCount;
        seenPreview = preview;
        *keys = chosenKeys;
        if (afterPreview) afterPreview();
        return allowImport;
    }
};

class OptionsDialogTests : public QObject {
    Q_OBJECT
private slots:
    void init() {
        m_font = QApplication::font();
        m_palette = QApplication::palette();
        m_logLevel = Log::level();
        m_logPath = Log::logFile();
    }
    void cleanup() {
        QApplication::setFont(m_font);
        QApplication::setPalette(m_palette);
        Log::setLevel(m_logLevel);
        Log::setLogFile(m_logPath);
    }
    void formEditsAndPersistence() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        QVERIFY(repository.load().ok);
        ProbeDialog dialog(&repository);
        QCOMPARE(dialog.categories().size(), 5);
        for (const auto &definition : Settings::OptionsRepository::definitions()) {
            QVERIFY2(dialog.editorFor(definition.key), qPrintable(definition.key));
            QVERIFY(dialog.editorFor(definition.key)->toolTip().contains(QStringLiteral("默认值")));
        }
        auto *theme = qobject_cast<QComboBox *>(dialog.editorFor(QStringLiteral("display.theme")));
        QVERIFY(theme);
        theme->setCurrentIndex(theme->findData(QStringLiteral("dark")));
        QVERIFY(dialog.isDirty());
        QCOMPARE(repository.value(QStringLiteral("display.theme")).toString(), QStringLiteral("system"));
        QVERIFY(dialog.applyChanges());
        QVERIFY(!dialog.isDirty());
        Settings::OptionsRepository reread({temp.path(), false});
        QVERIFY(reread.load().ok);
        QCOMPARE(reread.value(QStringLiteral("display.theme")).toString(), QStringLiteral("dark"));
        auto *size = qobject_cast<QSpinBox *>(dialog.editorFor(QStringLiteral("display.uiFontSize")));
        QVERIFY(size);
        QCOMPARE(dialog.draftValue(QStringLiteral("display.uiFontSize")).toInt(), 0);
        size->stepUp();
        QCOMPARE(dialog.draftValue(QStringLiteral("display.uiFontSize")).toInt(), 6);
        size->stepDown();
        QCOMPARE(dialog.draftValue(QStringLiteral("display.uiFontSize")).toInt(), 0);
    }
    void pendingCategoryAndClose() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        QVERIFY(dialog.setDraftValue(QStringLiteral("general.singleInstance"), false));
        QVERIFY(!dialog.selectCategory(QStringLiteral("display")));
        QCOMPARE(dialog.currentCategory(), QStringLiteral("general"));
        QCOMPARE(dialog.pendingCount, 1);
        dialog.pendingChoice = ProbeDialog::PendingChoice::Discard;
        QVERIFY(dialog.selectCategory(QStringLiteral("display")));
        QVERIFY(!dialog.isDirty());
        QVERIFY(repository.value(QStringLiteral("general.singleInstance")).toBool());
        dialog.setDraftValue(QStringLiteral("display.theme"), QStringLiteral("dark"));
        dialog.pendingChoice = ProbeDialog::PendingChoice::Apply;
        QVERIFY(dialog.selectCategory(QStringLiteral("logging")));
        QCOMPARE(repository.value(QStringLiteral("display.theme")).toString(), QStringLiteral("dark"));
        dialog.setDraftValue(QStringLiteral("logging.level"), QStringLiteral("debug"));
        dialog.pendingChoice = ProbeDialog::PendingChoice::Cancel;
        dialog.show();
        dialog.reject();
        QVERIFY(dialog.isVisible());
        dialog.pendingChoice = ProbeDialog::PendingChoice::Discard;
        dialog.reject();
        QVERIFY(!dialog.isVisible());
        QCOMPARE(repository.value(QStringLiteral("logging.level")).toString(), QStringLiteral("warning"));
    }
    void searchLocatesAndHighlights() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        const QStringList found = dialog.search(QStringLiteral("display.theme"));
        QCOMPARE(found, QStringList{QStringLiteral("display.theme")});
        QCOMPARE(dialog.currentCategory(), QStringLiteral("display"));
        QWidget *row = dialog.findChild<QWidget *>(QStringLiteral("optionsRow_display.theme"));
        QVERIFY(row);
        QVERIFY(row->property("optionsSearchMatch").toBool());
        QVERIFY(dialog.search(QStringLiteral("not-a-real-setting")).isEmpty());
        QVERIFY(!row->property("optionsSearchMatch").toBool());
    }
    void validationKeepsDirtyAndVisible() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        dialog.setDraftValue(QStringLiteral("logging.filePath"), QStringLiteral("relative.log"));
        QSignalSpy errors(&dialog, &Options::OptionsDialog::operationFailed);
        dialog.show();
        dialog.accept();
        QVERIFY(dialog.isVisible());
        QVERIFY(dialog.isDirty());
        QVERIFY(!dialog.lastError().isEmpty());
        QCOMPARE(errors.count(), 1);
        QCOMPARE(dialog.currentCategory(), QStringLiteral("logging"));
        QVERIFY(repository.value(QStringLiteral("logging.filePath")).toString().isEmpty());
    }
    void externalChangeDoesNotClobberUneditedKeys() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog first(&repository), second(&repository);
        first.setDraftValue(QStringLiteral("display.theme"), QStringLiteral("dark"));
        second.setDraftValue(QStringLiteral("logging.level"), QStringLiteral("debug"));
        QVERIFY(second.applyChanges());
        QCOMPARE(first.draftValue(QStringLiteral("logging.level")).toString(), QStringLiteral("debug"));
        QVERIFY(first.applyChanges());
        QCOMPARE(repository.value(QStringLiteral("logging.level")).toString(), QStringLiteral("debug"));
        QCOMPARE(repository.value(QStringLiteral("display.theme")).toString(), QStringLiteral("dark"));
        QVERIFY(!second.isDirty());
        QCOMPARE(second.draftValue(QStringLiteral("display.theme")).toString(), QStringLiteral("dark"));
    }
    void resetRequiresConfirmationAndBackup() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        QVERIFY(repository.apply({{QStringLiteral("display.theme"), QStringLiteral("dark")},
                                  {QStringLiteral("logging.level"), QStringLiteral("debug")}}).ok);
        QFile original(repository.location().filePath());
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray before = original.readAll();
        original.close();
        ProbeDialog dialog(&repository);
        QVERIFY(!dialog.resetCategory(QStringLiteral("display")));
        QCOMPARE(repository.value(QStringLiteral("display.theme")).toString(), QStringLiteral("dark"));
        dialog.allowReset = true;
        QVERIFY(dialog.resetCategory(QStringLiteral("display")));
        QCOMPARE(dialog.resetScope, QStringLiteral("display"));
        QCOMPARE(repository.value(QStringLiteral("display.theme")).toString(), QStringLiteral("system"));
        QCOMPARE(repository.value(QStringLiteral("logging.level")).toString(), QStringLiteral("debug"));
        const auto backups = QDir(temp.path()).entryList({QStringLiteral("*.bak*")}, QDir::Files);
        QVERIFY(!backups.isEmpty());
        bool exactBackup = false;
        for (const QString &backup : backups) {
            QFile file(QDir(temp.path()).filePath(backup));
            if (file.open(QIODevice::ReadOnly) && file.readAll() == before) exactBackup = true;
        }
        QVERIFY(exactBackup);
        QVERIFY(dialog.resetAll());
        QVERIFY(dialog.resetScope.isEmpty());
        QCOMPARE(repository.values(), Settings::OptionsRepository::defaults());
    }
    void selectiveImportExportAndConflictPreview() {
        QTemporaryDir temp;
        Settings::OptionsRepository source({temp.path() + QStringLiteral("/source"), false});
        QVERIFY(source.apply({{QStringLiteral("display.theme"), QStringLiteral("dark")},
                              {QStringLiteral("logging.level"), QStringLiteral("debug")}}).ok);
        const QString exported = temp.path() + QStringLiteral("/transfer.json");
        QVERIFY(source.exportFile(exported).ok);
        Settings::OptionsRepository target({temp.path() + QStringLiteral("/target"), false});
        ProbeDialog dialog(&target);
        dialog.chosenKeys = QStringList{QStringLiteral("display.theme")};
        QVERIFY(dialog.importSettings(exported));
        QCOMPARE(dialog.importCount, 1);
        bool themeConflict = false;
        for (const auto &entry : dialog.seenPreview.entries)
            if (entry.key == QStringLiteral("display.theme")) themeConflict = entry.conflict;
        QVERIFY(themeConflict);
        QCOMPARE(target.value(QStringLiteral("display.theme")).toString(), QStringLiteral("dark"));
        QCOMPARE(target.value(QStringLiteral("logging.level")).toString(), QStringLiteral("warning"));
        const QString themePath = temp.path() + QStringLiteral("/theme.json");
        QVERIFY(dialog.exportSettings(themePath, true));
        QFile theme(themePath);
        QVERIFY(theme.open(QIODevice::ReadOnly));
        const auto settings = QJsonDocument::fromJson(theme.readAll()).object().value(QStringLiteral("settings")).toObject();
        QVERIFY(!settings.isEmpty());
        for (const QString &key : settings.keys()) QVERIFY(key.startsWith(QStringLiteral("display.")));
    }
    void changedImportFileMustBePreviewedAgain() {
        QTemporaryDir temp;
        Settings::OptionsRepository source({temp.path() + QStringLiteral("/source"), false});
        QVERIFY(source.apply({{QStringLiteral("display.theme"), QStringLiteral("dark")}}).ok);
        const QString path = temp.path() + QStringLiteral("/transfer.json");
        QVERIFY(source.exportFile(path).ok);
        Settings::OptionsRepository target({temp.path() + QStringLiteral("/target"), false});
        ProbeDialog dialog(&target);
        dialog.chosenKeys = QStringList{QStringLiteral("display.theme")};
        dialog.afterPreview = [&] {
            QVERIFY(source.apply({{QStringLiteral("display.theme"), QStringLiteral("light")}}).ok);
            QVERIFY(source.exportFile(path).ok);
        };
        QVERIFY(!dialog.importSettings(path));
        QVERIFY(!dialog.lastError().isEmpty());
        QCOMPARE(target.value(QStringLiteral("display.theme")).toString(), QStringLiteral("system"));
    }
    void exportResolvesPendingDraft() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        dialog.setDraftValue(QStringLiteral("display.theme"), QStringLiteral("dark"));
        const QString path = temp.path() + QStringLiteral("/export.json");
        QVERIFY(!dialog.exportSettings(path));
        QVERIFY(!QFile::exists(path));
        dialog.pendingChoice = ProbeDialog::PendingChoice::Apply;
        QVERIFY(dialog.exportSettings(path));
        QCOMPARE(repository.value(QStringLiteral("display.theme")).toString(), QStringLiteral("dark"));
    }
    void runtimeActuallyChangesApplicationAndLog() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        const QFont originalFont = QApplication::font();
        const QPalette originalPalette = QApplication::palette();
        Options::OptionsRuntime runtime(&repository);
        QCOMPARE(Log::logFile(), temp.path() + QStringLiteral("/logs/lqcompare.log"));
        QSignalSpy fontChanged(&runtime, &Options::OptionsRuntime::contentFontChanged);
        QVERIFY(repository.apply({{QStringLiteral("display.theme"), QStringLiteral("dark")},
                                  {QStringLiteral("display.uiFontSize"), 16},
                                  {QStringLiteral("display.contentFontSize"), 19},
                                  {QStringLiteral("logging.level"), QStringLiteral("debug")}}).ok);
        QCOMPARE(QApplication::font().pointSize(), 16);
        QVERIFY(QApplication::palette().color(QPalette::Window).lightness() < 80);
        QCOMPARE(runtime.contentFont().pointSize(), 19);
        QCOMPARE(fontChanged.count(), 1);
        QCOMPARE(Log::level(), Log::Level::Debug);
        const QString path = temp.path() + QStringLiteral("/custom/debug.log");
        QVERIFY(repository.apply({{QStringLiteral("logging.filePath"), path}}).ok);
        QCOMPARE(Log::logFile(), path);
        Log::write(Log::Level::Debug, QStringLiteral("OptionsTest"), QStringLiteral("actual-log-output"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("actual-log-output"));
        file.close();
        QVERIFY(repository.apply({{QStringLiteral("logging.fileEnabled"), false}}).ok);
        QVERIFY(Log::logFile().isEmpty());
        QVERIFY(repository.apply({{QStringLiteral("display.theme"), QStringLiteral("system")},
                                  {QStringLiteral("display.uiFontSize"), 0}}).ok);
        QCOMPARE(QApplication::font(), originalFont);
        QCOMPARE(QApplication::palette(), originalPalette);
        QVERIFY(runtime.lastError().isEmpty());
    }
    void displayChangesPreserveCommandLineLoggingOverride() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        Options::OptionsRuntime runtime(&repository);
        Log::setLevel(Log::Level::Error);
        QVERIFY(Log::setLogFile(QString()));
        QVERIFY(repository.apply({{QStringLiteral("display.theme"), QStringLiteral("dark")}}).ok);
        QCOMPARE(Log::level(), Log::Level::Error);
        QVERIFY(Log::logFile().isEmpty());
        QVERIFY(repository.apply({{QStringLiteral("logging.level"), QStringLiteral("debug")}}).ok);
        QCOMPARE(Log::level(), Log::Level::Debug);
        QVERIFY(!Log::logFile().isEmpty());
    }
    void unchangedAcceptDoesNotWriteSettings() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        dialog.accept();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!QFile::exists(repository.location().filePath()));
    }
    void runtimeFileFailurePreservesPreviousTarget() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path() + QStringLiteral("/settings"), false});
        const QString desiredPath = temp.path() + QStringLiteral("/new-log.txt");
        QVERIFY(repository.apply({{QStringLiteral("logging.filePath"), desiredPath}}).ok);
        // Simulate a filesystem change between validation and runtime application.
        QVERIFY(QDir().mkpath(desiredPath));
        const QString previousPath = temp.path() + QStringLiteral("/previous.log");
        QVERIFY(Log::setLogFile(previousPath));
        Options::OptionsRuntime runtime(&repository);
        QVERIFY(!runtime.lastError().isEmpty());
        QCOMPARE(Log::logFile(), previousPath);
        QSignalSpy errors(&runtime, &Options::OptionsRuntime::runtimeError);
        QVERIFY(!runtime.applyCurrent());
        QCOMPARE(errors.count(), 1);
        QCOMPARE(Log::logFile(), previousPath);
        QVERIFY(QDir().rmdir(desiredPath));
        QVERIFY(runtime.applyCurrent());
        QCOMPARE(Log::logFile(), desiredPath);
        QVERIFY(runtime.lastError().isEmpty());
    }
    void fileOperationPageWarnsAboutIrreversibleChoices() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        QCOMPARE(dialog.categories().size(), 5);
        QVERIFY(dialog.categories().contains(QStringLiteral("fileops")));
        QVERIFY(dialog.selectCategory(QStringLiteral("fileops")));

        auto *hint = dialog.findChild<QLabel *>(QStringLiteral("optionsFileOpsSafety"));
        QVERIFY(hint);
        // 默认必须说明「进回收站、可还原」——只有永久删除那一侧才该出现不可恢复的字样。
        QVERIFY2(hint->text().contains(QStringLiteral("回收站")), qPrintable(hint->text()));
        QVERIFY2(!hint->text().contains(QStringLiteral("不可恢复")), qPrintable(hint->text()));
        QVERIFY2(hint->text().contains(QStringLiteral("逐个询问")), qPrintable(hint->text()));
        QVERIFY2(!hint->text().contains(QStringLiteral("直接覆盖")), qPrintable(hint->text()));

        // 提示必须跟着草稿走：改了选项而提示不变，用户只会以为提示与选项无关。
        QVERIFY(dialog.setDraftValue(QStringLiteral("fileops.deleteMode"), QStringLiteral("permanent")));
        QVERIFY2(hint->text().contains(QStringLiteral("不可恢复")), qPrintable(hint->text()));
        QVERIFY(dialog.setDraftValue(QStringLiteral("fileops.overwritePolicy"), QStringLiteral("overwrite")));
        QVERIFY2(hint->text().contains(QStringLiteral("直接覆盖")), qPrintable(hint->text()));
        QVERIFY2(hint->text().contains(QStringLiteral("较新")), qPrintable(hint->text()));

        // 下拉项的中文标签取自服务层，不在这张全局标签表里再写一份。
        auto *mode = qobject_cast<QComboBox *>(dialog.editorFor(QStringLiteral("fileops.deleteMode")));
        QVERIFY(mode);
        // 上面刚把草稿改成了永久删除，所以当前选中项就是它；两个选项的中文
        // 标签都由服务层给出（不在这张全局标签表里再写一份）。
        QCOMPARE(mode->currentData().toString(), QStringLiteral("permanent"));
        const int trashIndex = mode->findData(QStringLiteral("trash"));
        const int permanentIndex = mode->findData(QStringLiteral("permanent"));
        QVERIFY(trashIndex >= 0 && permanentIndex >= 0);
        QCOMPARE(mode->itemText(trashIndex), QStringLiteral("移入回收站"));
        QCOMPARE(mode->itemText(permanentIndex), QStringLiteral("永久删除"));

        // 数字项的单位来自定义表。曾经界面写死「 pt」，于是兆字节阈值会显示成
        // 「100 pt」——数字对、单位错，而没有任何断言会失败。
        auto *megabytes = qobject_cast<QSpinBox *>(dialog.editorFor(QStringLiteral("fileops.largeFileConfirmMegabytes")));
        QVERIFY(megabytes);
        QCOMPARE(megabytes->suffix(), QStringLiteral(" MB"));
        QCOMPARE(megabytes->value(), 100);
        auto *count = qobject_cast<QSpinBox *>(dialog.editorFor(QStringLiteral("fileops.batchDeleteConfirmCount")));
        QVERIFY(count);
        QCOMPARE(count->suffix(), QStringLiteral(" 个"));
        QCOMPARE(count->value(), 20);
        auto *fontSize = qobject_cast<QSpinBox *>(dialog.editorFor(QStringLiteral("display.contentFontSize")));
        QVERIFY(fontSize);
        QCOMPARE(fontSize->suffix(), QStringLiteral(" pt"));
    }
    void loggingPageExposesClearAndDiagnosticsButtons() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        // 按钮按 objectName 找：文案会改，对象名是这条断言的稳定依据。
        QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("optionsClearLog")));
        QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("optionsExportDiagnostics")));
        QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("optionsOpenLogDirectory")));
        // 轮转与性能计时开关都是**数据驱动**生成的控件：定义表里有这几项，
        // 页面上就必须有对应的编辑器——否则「设置项登记了但改不动」。
        for (const QString &key : {QStringLiteral("logging.rotationMode"),
                                   QStringLiteral("logging.rotationMaximumMegabytes"),
                                   QStringLiteral("logging.rotationKeepFiles"),
                                   QStringLiteral("logging.performanceTiming")})
            QVERIFY2(dialog.editorFor(key) != nullptr, qPrintable(key));
        auto *mode = qobject_cast<QComboBox *>(dialog.editorFor(QStringLiteral("logging.rotationMode")));
        QVERIFY(mode);
        // 下拉项的**标识**（不是显示名）必须与按键名逐一对应，且默认选中「不轮转」。
        QCOMPARE(mode->count(), Log::rotationModeChoices().size());
        QCOMPARE(mode->currentData().toString(), QStringLiteral("none"));
    }
    void clearLogFailsWhenFileLoggingIsOff() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        Log::setLogFile(QString());
        QVERIFY(!dialog.clearLog());
        QVERIFY(!dialog.lastError().isEmpty());
    }
    void clearLogTruncatesTheConfiguredFile() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        const QString logPath = temp.filePath(QStringLiteral("lqcompare.log"));
        {
            QFile seed(logPath);
            QVERIFY(seed.open(QIODevice::WriteOnly));
            QVERIFY(seed.write(QByteArray(2048, 'x')) == 2048);
        }
        QVERIFY(Log::setLogFile(logPath));
        QVERIFY(dialog.clearLog());
        // 文件必须还在（可能正被追加写入），只是内容为空。删掉它会让写入落到
        // 已删除的 inode 上——`ls` 看不到增长，而调用方以为日志重新开始了。
        QVERIFY(QFile::exists(logPath));
        QCOMPARE(QFileInfo(logPath).size(), 0LL);
    }
    void exportDiagnosticsHonoursTheRedactionChoice() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        const QString logPath = temp.filePath(QStringLiteral("lqcompare.log"));
        {
            QFile seed(logPath);
            QVERIFY(seed.open(QIODevice::WriteOnly));
            seed.write("碰到 " + QDir::homePath().toUtf8() + "/work/a.txt\n");
        }
        QVERIFY(Log::setLogFile(logPath));

        QString redactedBundle;
        QVERIFY2(dialog.exportDiagnostics(temp.path(), true, &redactedBundle), qPrintable(dialog.lastError()));
        const QJsonObject redacted = QJsonDocument::fromJson(
                [&redactedBundle] {
                    QFile file(redactedBundle + QStringLiteral("/manifest.json"));
                    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
                }()).object();
        QCOMPARE(redacted.value(QStringLiteral("redacted")).toBool(), true);
        QVERIFY(redacted.value(QStringLiteral("logIncluded")).toBool());
        QVERIFY(QFile::exists(redactedBundle + QStringLiteral("/environment.txt")));

        // 不脱敏那一份的口径必须真的不同：包里的日志应保留原始家目录路径。
        QString rawBundle;
        QVERIFY2(dialog.exportDiagnostics(temp.path(), false, &rawBundle), qPrintable(dialog.lastError()));
        QVERIFY(rawBundle != redactedBundle);
        QFile archived(rawBundle + QStringLiteral("/logs/lqcompare.log"));
        QVERIFY(archived.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(archived.readAll()).contains(QDir::homePath()));
    }
    void exportDiagnosticsReportsAnUnusableDirectory() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        QString bundle;
        QVERIFY(!dialog.exportDiagnostics(QString(), true, &bundle));
        QVERIFY(!dialog.lastError().isEmpty());
        QVERIFY(bundle.isEmpty());
    }
    void applyingRotationSettingsReachesTheLogModule() {
        // 「设置项存进去了」不等于「策略生效了」：中间那一步在 OptionsRuntime 里，
        // 而它没有任何界面现象——轮转策略没被接上时，设置页照常保存、照常回显，
        // 只是文件永远不轮转。这条用例专门盯住那次转发。
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        Options::OptionsRuntime runtime(&repository);
        const auto result = repository.apply({{Log::rotationModeKey(), QStringLiteral("size")},
                                              {Log::rotationMaximumMegabytesKey(), 9},
                                              {Log::rotationKeepFilesKey(), 4},
                                              {Log::performanceTimingKey(), true}});
        QVERIFY2(result.ok, qPrintable(result.error));

        const Log::RotationPolicy policy = Log::rotationPolicy();
        QCOMPARE(QString::fromLatin1(Log::rotationModeIdentifier(policy.mode)),
                 QStringLiteral("size"));
        QCOMPARE(policy.maximumMegabytes(), 9LL);
        QCOMPARE(policy.keepFiles, 4);
        QVERIFY(Log::performanceTimingEnabled());

        // 本套件其余的用例不该继承这个全局状态（文件输出位置由 cleanup 还原）。
        Log::setRotationPolicy(Log::RotationPolicy());
        Log::setPerformanceTimingEnabled(false);
    }
    void saveReviewScreenshots() {
        QTemporaryDir temp;
        Settings::OptionsRepository repository({temp.path(), false});
        ProbeDialog dialog(&repository);
        dialog.show();
        for (const QString &category : {QStringLiteral("general"), QStringLiteral("display"), QStringLiteral("storage")}) {
            QVERIFY(dialog.selectCategory(category));
            QApplication::processEvents();
            QVERIFY(dialog.grab().save(QDir::current().filePath(QStringLiteral("options-") + category + QStringLiteral(".png"))));
        }
    }
private:
    QFont m_font;
    QPalette m_palette;
    Log::Level m_logLevel;
    QString m_logPath;
};

QTEST_MAIN(OptionsDialogTests)
#include "tst_optionsdialog.moc"
