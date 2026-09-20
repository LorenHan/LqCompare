#ifndef LQCOMPARE_MERGEOUTPUT_H
#define LQCOMPARE_MERGEOUTPUT_H

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace LqCompare::Merge {

// An output-only writer. Selecting a path snapshots its current contents; saves
// refuse changed destinations, source aliases and symbolic-link destinations.
// Atomic replacement protects against partial writes. As with QSaveFile, the
// final verification and commit are not an OS-level compare-and-swap operation.
class OutputFile
{
public:
    bool setPath(const QString &path, const QStringList &inputPaths,
                 QString *error = nullptr);
    QString path() const;
    bool save(const QByteArray &bytes, QString *error = nullptr);
    bool checkUnchanged(QString *error = nullptr) const;
    bool hasSaved() const;

private:
    QString m_path;
    QString m_canonicalParent;
    QStringList m_inputPaths;
    QStringList m_inputCanonicalPaths;
    QList<QByteArray> m_inputIdentities;
    QByteArray m_digest;
    QByteArray m_identity;
    bool m_exists = false;
    bool m_hasSaved = false;
};

} // namespace LqCompare::Merge

#endif // LQCOMPARE_MERGEOUTPUT_H
