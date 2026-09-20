#ifndef LQCOMPARE_OPTIONSRUNTIME_H
#define LQCOMPARE_OPTIONSRUNTIME_H

#include <QFont>
#include <QObject>
#include <QPalette>
#include <QPointer>

namespace LqCompare {
namespace Settings { class OptionsRepository; }
namespace Options {

// Create one instance after QApplication and keep it alive for the application.
// Content views connect contentFontChanged and use contentFont on construction.
class OptionsRuntime : public QObject {
    Q_OBJECT
public:
    explicit OptionsRuntime(Settings::OptionsRepository *repository, QObject *parent = nullptr);
    QFont contentFont() const { return m_contentFont; }
    QString lastError() const { return m_lastError; }

public slots:
    bool applyCurrent();

signals:
    void contentFontChanged(const QFont &font);
    void runtimeError(const QString &message);

private:
    bool applyKeys(const QStringList &keys);
    QPointer<Settings::OptionsRepository> m_repository;
    QFont m_systemFont;
    QPalette m_systemPalette;
    QFont m_contentFont;
    QString m_lastError;
};

} // namespace Options
} // namespace LqCompare
#endif
