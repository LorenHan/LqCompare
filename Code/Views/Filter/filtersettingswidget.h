#ifndef LQCOMPARE_FILTERSETTINGSWIDGET_H
#define LQCOMPARE_FILTERSETTINGSWIDGET_H

#include "filterstack.h"
#include "namefilter.h"
#include <QWidget>
#include <QFutureWatcher>
#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;

namespace LqCompare {

/// Standalone mask editor. Edits change the draft immediately; saveLayer() is the
/// only storage write. Stores borrowed by setBinder() must outlive this widget.
class MaskFilterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MaskFilterWidget(QWidget *parent = nullptr);
    void setFilterStack(const Filter::FilterStack &stack);
    const Filter::FilterStack &filterStack() const { return m_stack; }
    void setBinder(const Filter::FilterLayerBinder &binder);
    void reloadFromStores();
    bool saveLayer(Filter::FilterLayer layer);
    void setCurrentLayer(Filter::FilterLayer layer);
    Filter::FilterLayer currentLayer() const;
    void setDeclaration(Filter::FilterLayer layer, const QString &declaration);
    void setLayerEnabled(Filter::FilterLayer layer, bool enabled);
    void setPreviewSubjects(const QVector<Filter::MaskSubject> &subjects);
    void setPreviewNames(const QStringList &names);
    Filter::EffectiveFilterPanel effectivePanel() const { return m_panel; }
    QString errorText() const;
    QString diagnosePath(const QString &relativePath) const;

public slots:
    void showSyntaxReference();

signals:
    void filterChanged();
    void validationChanged(bool valid);
    void layerSaved();
    void saveFailed(const QString &message);

private:
    void refreshEditor();
    void refreshResult();
    Filter::FilterStack m_stack;
    Filter::FilterLayerBinder m_binder;
    QVector<Filter::MaskSubject> m_subjects;
    Filter::EffectiveFilterPanel m_panel;
    QComboBox *m_layer;
    QComboBox *m_case;
    QCheckBox *m_enabled;
    QPlainTextEdit *m_editor;
    QLabel *m_errors;
    QLabel *m_source;
    QLabel *m_summary;
    QLabel *m_expression;
    QLabel *m_semantics;
    QLabel *m_diagnosis;
    QLabel *m_saveStatus;
    QLineEdit *m_diagnosticPath;
    QTableWidget *m_layers;
    QPushButton *m_save;
};

struct NameFilterWidgetPreview
{
    int total = 0;
    int included = 0;
    int issueCount = 0;
    QStringList issues;
    bool cancelled = false;
};

/// Advanced name filter editor. Regex previews run asynchronously with the
/// service's timeout/circuit breaker and an independent runner per preview.
class NameFilterWidget : public QWidget
{
    Q_OBJECT
public:
    explicit NameFilterWidget(QWidget *parent = nullptr);
    ~NameFilterWidget() override;
    void setDeclaration(const QString &declaration);
    QString declaration() const;
    const Filter::NameFilter &nameFilter() const { return m_filter; }
    void setCombineMode(Filter::NameCombineMode mode);
    Filter::NameCombineMode combineMode() const;
    void setPlatform(Filter::MaskPlatform platform);
    void setCaseSensitivity(Qt::CaseSensitivity sensitivity);
    void clearCaseSensitivityOverride();
    void setPreviewNames(const QStringList &names);
    bool isPreviewPending() const { return m_previewPending; }
    NameFilterWidgetPreview previewResult() const { return m_preview; }
    QString errorText() const;
    bool addExpression(Filter::NameMatchMode mode, const QString &text, QString *error = nullptr);
    bool savePreset(const QString &name, const QString &note = QString(), QString *error = nullptr);
    QVector<Filter::NamedNameFilter> presets() const { return m_presets; }
    void setPresets(const QVector<Filter::NamedNameFilter> &presets);
    bool applyPreset(int index);
    QString exportPresets() const;
    bool importPresets(const QString &text, QString *error = nullptr);

signals:
    void filterChanged();
    void validationChanged(bool valid);
    void previewChanged();
    void presetsChanged();

private:
    void reparse();
    void queuePreview();
    void startPreview();
    void validateAddition();
    void refreshPresets();
    Filter::NameFilter m_filter;
    Filter::MaskPlatform m_platform = Filter::currentMaskPlatform();
    QStringList m_names;
    QVector<Filter::NamedNameFilter> m_presets;
    NameFilterWidgetPreview m_preview;
    quint64 m_generation = 0;
    quint64 m_runningGeneration = 0;
    bool m_previewPending = false;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QFutureWatcher<NameFilterWidgetPreview> *m_watcher;
    QTimer *m_timer;
    QComboBox *m_combine;
    QComboBox *m_mode;
    QComboBox *m_case;
    QComboBox *m_presetList;
    QPlainTextEdit *m_editor;
    QLineEdit *m_addition;
    QLineEdit *m_presetName;
    QLabel *m_errors;
    QLabel *m_semantics;
    QLabel *m_additionError;
    QLabel *m_previewLabel;
    QLabel *m_previewErrors;
    QLabel *m_presetStatus;
    QPushButton *m_add;
};

/// Embeddable settings page, with no App dependency. The two filter families are
/// separate service contracts; callers compose their decisions explicitly.
class FilterSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FilterSettingsWidget(QWidget *parent = nullptr);
    MaskFilterWidget *maskWidget() const { return m_masks; }
    NameFilterWidget *nameWidget() const { return m_names; }
    void setPreviewSubjects(const QVector<Filter::MaskSubject> &subjects);
signals:
    void filterChanged();
private:
    MaskFilterWidget *m_masks;
    NameFilterWidget *m_names;
};

} // namespace LqCompare
#endif
