#ifndef LQCOMPARE_REGISTRYCOMPARESESSION_H
#define LQCOMPARE_REGISTRYCOMPARESESSION_H

#include "comparesession.h"
#include "registrycompare.h"

namespace LqCompare {

class RegistryCompareSession : public CompareSession {
    Q_OBJECT
public:
    explicit RegistryCompareSession(QObject *parent = nullptr);
    RegistryCompareSession(const QString &left, const QString &right, QObject *parent = nullptr);

    QString leftPath() const { return m_leftPath; }
    QString rightPath() const { return m_rightPath; }
    bool setPaths(const QString &left, const QString &right, QString *error = nullptr);
    bool isLoaded() const { return m_loaded; }
    const Registry::Snapshot &leftSnapshot() const { return m_left; }
    const Registry::Snapshot &rightSnapshot() const { return m_right; }
    const Registry::Comparison &comparison() const { return m_comparison; }
    Registry::ReadOptions readOptions() const { return m_options; }
    bool setReadOptions(const Registry::ReadOptions &options, QString *error = nullptr);

    // Injection is restricted to an unopened session. Providers have no mutation API.
    bool setLocalProvider(std::shared_ptr<const Registry::Provider> provider);

signals:
    void pathsChanged();
    void comparisonChanged();
    void readOptionsChanged();

protected:
    QWidget *createView(QWidget *parent) override;
    bool doOpen(QString *error) override;
    bool doReload(QString *error) override;
    void doClose() override;
    bool canSaveNow() const override { return false; }

private:
    Registry::ReadResult readSource(const QString &path, const Registry::ReadOptions &options) const;
    bool loadPair(const QString &left, const QString &right,
                  const Registry::ReadOptions &options, QString *error);
    void updateTitle();
    void updateStatus();
    QString m_leftPath, m_rightPath;
    Registry::Snapshot m_left, m_right;
    Registry::Comparison m_comparison;
    Registry::ReadOptions m_options;
    std::shared_ptr<const Registry::Provider> m_provider;
    bool m_loaded = false;
};

} // namespace LqCompare
#endif
