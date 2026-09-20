#ifndef LQCOMPARE_BLAMEVIEW_H
#define LQCOMPARE_BLAMEVIEW_H

#include <QSharedPointer>
#include <QWidget>
#include <memory>
#include "vcsbackend.h"

namespace LqCompare {

// Read-only history of a file at one resolved commit. Queries run off the UI
// thread, and closing the view cancels outstanding work without waiting.
class BlameView final : public QWidget {
    Q_OBJECT
public:
    enum ColorMode { Author, Age, CommitBlock };
    Q_ENUM(ColorMode)

    explicit BlameView(const QString &filePath, QWidget *parent = nullptr);
    BlameView(const QString &filePath, QSharedPointer<Vcs::Backend> backend, QWidget *parent = nullptr);
    ~BlameView() override;
    QString path() const;
    QString revision() const;
    bool isBusy() const;
    ColorMode colorMode() const;

public slots:
    void setPath(const QString &filePath);
    void setRevision(const QString &revision);
    void setColorMode(ColorMode mode);
    void refresh();
    void cancel();
    void showSelectedRevision();
    void compareSelectedWithParent();

signals:
    // The app can route this to its Git log. All fields refer to the line's
    // original commit/path, which may differ from the currently named file.
    void revisionRequested(const QString &repositoryRoot, const QString &commit,
                           const QString &relativePath, int originalLine);
    // Keep Comparison/lifetime alive on the receiving read-only compare session.
    void compareRequested(const LqCompare::Vcs::Comparison &comparison);
    void errorOccurred(const QString &message);
    void statusChanged(const QString &message);

private:
    class Private;
    std::unique_ptr<Private> d;
};

}
#endif
