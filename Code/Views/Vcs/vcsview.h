#ifndef LQCOMPARE_VCSVIEW_H
#define LQCOMPARE_VCSVIEW_H

#include <QWidget>
#include <QSharedPointer>
#include <memory>
#include "vcsbackend.h"

namespace LqCompare {

// All repository access runs through the injectable backend on a worker thread.
// The receiver must retain Comparison (including lifetime) while displaying it.
class VcsView final : public QWidget {
    Q_OBJECT
public:
    enum Mode { Head, Index, Revisions, History };
    Q_ENUM(Mode)

    explicit VcsView(const QString &path, QWidget *parent = nullptr);
    VcsView(const QString &path, QSharedPointer<Vcs::Backend> backend, QWidget *parent = nullptr);
    ~VcsView() override;

    Mode mode() const;
    QString path() const;
    bool isBusy() const;

public slots:
    void setMode(Mode mode);
    void setPath(const QString &path);
    void refresh();
    void cancel();

signals:
    void compareRequested(const LqCompare::Vcs::Comparison &comparison);
    void errorOccurred(const QString &message);
    void statusChanged(const QString &message);

private:
    class Private;
    std::unique_ptr<Private> d;
};

}
#endif
