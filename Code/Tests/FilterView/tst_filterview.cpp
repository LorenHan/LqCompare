#include <QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include "filtersettingswidget.h"
#include "session.h"

using namespace LqCompare;
using namespace LqCompare::Filter;

class FilterViewTests : public QObject
{
    Q_OBJECT
private slots:
    void layerIntersectionAndExclusionPriority()
    {
        MaskFilterWidget widget;
        widget.setPreviewNames({"main.cpp", "test.cpp", "main.h", "notes.txt"});
        widget.setDeclaration(FilterLayer::Format, "*.cpp\n*.h");
        widget.setDeclaration(FilterLayer::Session, "*.cpp");
        widget.setDeclaration(FilterLayer::View, "*.cpp\n-test.cpp");
        const auto panel = widget.effectivePanel();
        QCOMPARE(panel.total, 4); QCOMPARE(panel.included, 1); QCOMPARE(panel.layers.size(), 3);
        QVERIFY(panel.semantics.contains(QStringLiteral("交集")));
        QVERIFY(panel.expression.contains("test.cpp"));
        QCOMPARE(widget.filterStack().decide(MaskSubject::forName("test.cpp")).verdict, MaskVerdict::Excluded);
        auto *table = widget.findChild<QTableWidget *>("effectiveFilterLayers"); QVERIFY(table);
        table->item(2, 1)->setCheckState(Qt::Unchecked);
        QCOMPARE(widget.effectivePanel().included, 2);
        QVERIFY(!widget.filterStack().isLayerEnabled(FilterLayer::View));
        widget.setCurrentLayer(FilterLayer::Format);
        QVERIFY(widget.findChild<QPlainTextEdit *>("maskDeclaration")->isReadOnly());
        widget.findChild<QCheckBox *>("maskLayerEnabled")->setChecked(false);
        QVERIFY(!widget.filterStack().isLayerEnabled(FilterLayer::Format));
    }
    void editingShowsErrorsWithoutDiscardingValidLines()
    {
        MaskFilterWidget widget;
        widget.setPreviewNames({"a.cpp", "a.h"});
        auto *editor = widget.findChild<QPlainTextEdit *>("maskDeclaration"); QVERIFY(editor);
        QSignalSpy changed(&widget, &MaskFilterWidget::filterChanged);
        editor->setPlainText("*.cpp\n[bad");
        QCOMPARE(widget.filterStack().declaration(FilterLayer::View), QStringLiteral("*.cpp\n[bad"));
        QVERIFY(!widget.errorText().isEmpty()); QVERIFY(!editor->extraSelections().isEmpty());
        QCOMPARE(widget.effectivePanel().included, 1); QCOMPARE(changed.count(), 1);
        editor->setPlainText("*.h");
        QVERIFY(widget.errorText().isEmpty()); QVERIFY(editor->extraSelections().isEmpty());
        QCOMPARE(widget.effectivePanel().included, 1);
    }
    void effectiveExpressionPreservesEachLayerIntersection()
    {
        MaskFilterWidget widget;
        widget.setDeclaration(FilterLayer::Format, "*.cpp\n*.h");
        widget.setDeclaration(FilterLayer::Session, "main.*\nhelper.*");
        widget.setDeclaration(FilterLayer::View, "*.cpp");
        widget.setPreviewNames({"main.cpp", "main.h", "notes.cpp"});
        QVERIFY(widget.filterStack().accepts(MaskSubject::forName("main.cpp")));
        QVERIFY(!widget.filterStack().accepts(MaskSubject::forName("main.h")));
        QVERIFY(!widget.filterStack().accepts(MaskSubject::forName("notes.cpp")));
        QCOMPARE(widget.effectivePanel().included, 1);
        const QString expression = widget.effectivePanel().expression;
        // All three include groups must remain conjunctive. Flattening them into
        // one OR group contradicts the accepts() results above.
        QCOMPARE(expression.count(QStringLiteral("&&")), 2);
        QVERIFY(expression.contains("*.cpp || *.h"));
        QVERIFY(expression.contains("main.* || helper.*"));
        QCOMPARE(expression.count(QStringLiteral("*.cpp")), 2);
    }
    void binderWritesOnlySelectedStoreAndRejectsInvalidDraft()
    {
        MemorySessionSettings session, view;
        session.setValue(filterDeclarationSettingKey(), "*.cpp");
        FilterLayerBinder binder; binder.setSessionStore(&session); binder.setViewStore(&view);
        binder.setBuiltinDeclaration("-*.tmp"); binder.setBuiltinSource(QStringLiteral("文件格式：源码"));
        MaskFilterWidget widget; widget.setBinder(binder);
        QCOMPARE(widget.filterStack().declaration(FilterLayer::Session), QStringLiteral("*.cpp"));
        widget.setDeclaration(FilterLayer::View, "-test*");
        QVERIFY(!view.contains(filterDeclarationSettingKey()));
        QVERIFY(widget.saveLayer(FilterLayer::View));
        QCOMPARE(view.value(filterDeclarationSettingKey()).toString(), QStringLiteral("-test*"));
        QCOMPARE(session.value(filterDeclarationSettingKey()).toString(), QStringLiteral("*.cpp"));
        QVERIFY(!widget.saveLayer(FilterLayer::Format));
        widget.setDeclaration(FilterLayer::View, "["); QVERIFY(!widget.saveLayer(FilterLayer::View));
        QCOMPARE(view.value(filterDeclarationSettingKey()).toString(), QStringLiteral("-test*"));
        widget.setDeclaration(FilterLayer::View, {}); QVERIFY(widget.saveLayer(FilterLayer::View));
        QVERIFY(!view.contains(filterDeclarationSettingKey()));
        FilterLayerBinder missingView; missingView.setSessionStore(&session); widget.setBinder(missingView);
        widget.setDeclaration(FilterLayer::View, "*.h"); QVERIFY(!widget.saveLayer(FilterLayer::View));
        QCOMPARE(session.value(filterDeclarationSettingKey()).toString(), QStringLiteral("*.cpp"));
    }
    void diagnosticNamesEveryBlockingLayerAndRule()
    {
        MaskFilterWidget widget;
        widget.setDeclaration(FilterLayer::Format, "*.cpp");
        widget.setDeclaration(FilterLayer::Session, "-*.tmp\n-secret.*");
        widget.setDeclaration(FilterLayer::View, "-secret.*");
        const QString explanation = widget.diagnosePath("nested/secret.tmp");
        QVERIFY(explanation.contains(filterLayerLabel(FilterLayer::Format)));
        QVERIFY(explanation.contains(filterLayerLabel(FilterLayer::Session)));
        QVERIFY(explanation.contains(filterLayerLabel(FilterLayer::View)));
        QVERIFY(explanation.contains("*.tmp")); QVERIFY(explanation.contains("secret.*"));
        widget.findChild<QLineEdit *>("filterDiagnosticPath")->setText("nested/secret.tmp");
        QCOMPARE(widget.findChild<QLabel *>("filterDiagnosis")->text(), explanation);
    }
    void helpUsesExecutableServiceReference()
    {
        MaskFilterWidget widget;
        widget.findChild<QPushButton *>("maskHelp")->click();
        auto *dialog = widget.findChild<QDialog *>("maskSyntaxReferenceDialog"); QVERIFY(dialog);
        auto *table = dialog->findChild<QTableWidget *>("maskSyntaxTable"); QVERIFY(table);
        const auto reference = maskSyntaxReference(); QCOMPARE(table->rowCount(), reference.size());
        for (int i = 0; i < reference.size(); ++i) {
            QCOMPARE(table->item(i, 0)->text(), reference.at(i).pattern);
            QCOMPARE(table->item(i, 1)->text(), reference.at(i).meaning);
            for (const auto &sample : reference.at(i).samples) QVERIFY(table->item(i, 2)->text().contains(sample.text));
        }
        dialog->close();
    }
    void nameModesDoNotReinterpretExistingExpressions()
    {
        NameFilterWidget widget;
        QVERIFY(widget.addExpression(NameMatchMode::Exact, "README.md"));
        QVERIFY(widget.addExpression(NameMatchMode::Wildcard, "*.cpp"));
        QVERIFY(widget.addExpression(NameMatchMode::Regex, ".*\\.log"));
        QCOMPARE(widget.nameFilter().expressionCount(), 3);
        QCOMPARE(widget.nameFilter().expressions().at(0).mode, NameMatchMode::Exact);
        QCOMPARE(widget.nameFilter().expressions().at(1).mode, NameMatchMode::Wildcard);
        QCOMPARE(widget.nameFilter().expressions().at(2).mode, NameMatchMode::Regex);
        auto *mode = widget.findChild<QComboBox *>("nameMatchMode");
        const QString before = widget.declaration(); mode->setCurrentIndex(mode->findData(int(NameMatchMode::Regex)));
        QCOMPARE(widget.declaration(), before);
        widget.findChild<QLineEdit *>("nameExpressionInput")->setText("[");
        QVERIFY(!widget.findChild<QPushButton *>("nameAddExpression")->isEnabled());
        QVERIFY(!widget.addExpression(NameMatchMode::Regex, "["));
        QVERIFY(!widget.addExpression(NameMatchMode::Wildcard, "re:foo"));
        QVERIFY(!widget.addExpression(NameMatchMode::Exact, "one\ntwo"));
        widget.setPreviewNames({"README.md", "a.cpp", "a.log", "a.h"});
        QTRY_VERIFY_WITH_TIMEOUT(!widget.isPreviewPending(), 4000);
        QCOMPARE(widget.previewResult().included, 3);
    }
    void nameSemanticsAndLatestAsyncPreview()
    {
        NameFilterWidget widget;
        widget.setPreviewNames({"main.cpp", "test.cpp", "notes.txt"});
        widget.setDeclaration("*.cpp\nmain.*"); widget.setCombineMode(NameCombineMode::AllOf);
        QTRY_VERIFY_WITH_TIMEOUT(!widget.isPreviewPending(), 4000); QCOMPARE(widget.previewResult().included, 1);
        QVERIFY(widget.findChild<QLabel *>("nameFilterSemantics")->text().contains(nameCombineModeExplanation(NameCombineMode::AllOf)));
        widget.setCombineMode(NameCombineMode::NoneOf);
        QTRY_VERIFY_WITH_TIMEOUT(!widget.isPreviewPending(), 4000); QCOMPARE(widget.previewResult().included, 1);
        widget.setCombineMode(NameCombineMode::AnyOf);
        widget.setDeclaration("*.cpp"); widget.setDeclaration("*.txt"); widget.setDeclaration("*.missing");
        QTRY_VERIFY_WITH_TIMEOUT(!widget.isPreviewPending(), 4000); QCOMPARE(widget.previewResult().included, 0);
        widget.setDeclaration("re:[\n*.cpp");
        QVERIFY(!widget.errorText().isEmpty()); QCOMPARE(widget.nameFilter().expressionCount(), 1);
        QVERIFY(!widget.findChild<QPlainTextEdit *>("nameDeclaration")->extraSelections().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!widget.isPreviewPending(), 4000); QCOMPARE(widget.previewResult().included, 2);
    }
    void casePoliciesAreVisibleAndOverrideable()
    {
        NameFilterWidget widget;
        widget.setPreviewNames({"MAIN.CPP", "main.cpp"}); widget.setDeclaration("*.cpp"); widget.setPlatform(MaskPlatform::Windows);
        QTRY_VERIFY_WITH_TIMEOUT(!widget.isPreviewPending(), 4000); QCOMPARE(widget.previewResult().included, 2);
        widget.setCaseSensitivity(Qt::CaseSensitive);
        QTRY_VERIFY_WITH_TIMEOUT(!widget.isPreviewPending(), 4000); QCOMPARE(widget.previewResult().included, 1);
        widget.clearCaseSensitivityOverride();
        QTRY_VERIFY_WITH_TIMEOUT(!widget.isPreviewPending(), 4000); QCOMPARE(widget.previewResult().included, 2);
    }
    void namedPresetsRoundTripAndInvalidImportIsAtomic()
    {
        NameFilterWidget widget;
        widget.setDeclaration("= README.md\n*.cpp"); widget.setCombineMode(NameCombineMode::NoneOf); widget.setCaseSensitivity(Qt::CaseInsensitive);
        QVERIFY(widget.savePreset(QStringLiteral("源码排除"), QStringLiteral("说明")));
        QVERIFY(!widget.savePreset(QStringLiteral("源码排除")));
        const QString exported = widget.exportPresets(); QVERIFY(exported.startsWith(nameFilterPresetFormatHeader()));
        NameFilterWidget imported; QVERIFY(imported.importPresets(exported)); QVERIFY(imported.applyPreset(0));
        QCOMPARE(imported.declaration(), widget.declaration()); QCOMPARE(imported.combineMode(), NameCombineMode::NoneOf);
        QCOMPARE(imported.nameFilter().caseSensitivity(), Qt::CaseInsensitive);
        QVERIFY(!imported.importPresets("unrecognized format")); QCOMPARE(imported.presets().size(), 1);
        imported.setDeclaration("re:["); QVERIFY(!imported.savePreset("invalid"));
    }
    void closingDuringPreviewDoesNotRetainWidget()
    {
        auto *widget = new NameFilterWidget; widget->setDeclaration("re: .*\\.cpp");
        QStringList names; for (int i = 0; i < 8000; ++i) names.append(QString::number(i) + ".cpp");
        widget->setPreviewNames(names); QTest::qWait(150);
        QPointer<NameFilterWidget> pointer(widget); delete widget; QVERIFY(pointer.isNull());
    }
    void replacingInFlightPreviewUsesLatestInputs()
    {
        NameFilterWidget widget;
        widget.setDeclaration("re: .*\\.cpp");
        QStringList names; for (int i = 0; i < 50000; ++i) names.append(QString::number(i) + ".cpp");
        widget.setPreviewNames(names); QTest::qWait(140);
        widget.setPreviewNames({"a.cpp", "b.h", "c.txt"}); widget.setDeclaration("*.txt");
        QTRY_VERIFY_WITH_TIMEOUT(!widget.isPreviewPending(), 4000);
        QCOMPARE(widget.previewResult().total, 3); QCOMPARE(widget.previewResult().included, 1);
        QCOMPARE(widget.nameFilter().toDeclarationText(), QStringLiteral("*.txt"));
    }
    void combinedPanelAndOptionalScreenshot()
    {
        FilterSettingsWidget widget;
        widget.setPreviewSubjects({MaskSubject::forPath("src/main.cpp"), MaskSubject::forPath("src/test.cpp"), MaskSubject::forPath("docs/readme.md")});
        widget.maskWidget()->setDeclaration(FilterLayer::Format, "*.cpp\n*.h");
        widget.maskWidget()->setDeclaration(FilterLayer::Session, "-test.cpp"); widget.maskWidget()->setDeclaration(FilterLayer::View, "*.cpp");
        widget.nameWidget()->setDeclaration("*.cpp"); QCOMPARE(widget.maskWidget()->effectivePanel().included, 1);
        QTRY_VERIFY_WITH_TIMEOUT(!widget.nameWidget()->isPreviewPending(), 4000); QCOMPARE(widget.nameWidget()->previewResult().included, 2);
        const QString screenshot = qEnvironmentVariable("LQCOMPARE_FILTER_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) {
            widget.maskWidget()->setDeclaration(FilterLayer::View, "*.cpp\n[bad");
            widget.resize(900, 750); widget.show(); QTest::qWait(150);
            QVERIFY(widget.grab().save(screenshot));
            widget.nameWidget()->setDeclaration("*.cpp\n= README.md\nre: [");
            widget.findChild<QTabWidget *>()->setCurrentIndex(1);
            QTest::qWait(150); QVERIFY(widget.grab().save(screenshot + ".names.png"));
        }
    }
};
QTEST_MAIN(FilterViewTests)
#include "tst_filterview.moc"
