#include "mergeoutput.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace LqCompare::Merge {
namespace {

struct FileState {
    bool exists = false;
    QByteArray identity;
    QByteArray changeToken;
    QByteArray digest;
};

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

QString absolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool samePath(const QString &left, const QString &right)
{
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

#ifdef Q_OS_WIN
bool inspectHandle(HANDLE handle, FileState *state, QString *error)
{
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(handle, &info)
        || GetFileType(handle) != FILE_TYPE_DISK)
        return fail(error, QStringLiteral("Cannot inspect merge output file identity."));
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        return fail(error, QStringLiteral("Merge output must be a regular file, not a directory or symbolic link."));
    state->exists = true;
    state->identity = QByteArray::number(info.dwVolumeSerialNumber) + ':'
        + QByteArray::number(info.nFileIndexHigh) + ':' + QByteArray::number(info.nFileIndexLow);
    state->changeToken = QByteArray::number(info.nFileSizeHigh) + ':'
        + QByteArray::number(info.nFileSizeLow) + ':'
        + QByteArray::number(info.ftLastWriteTime.dwHighDateTime) + ':'
        + QByteArray::number(info.ftLastWriteTime.dwLowDateTime);
    return true;
}

bool inspectPath(const QString &path, FileState *state, QString *error)
{
    *state = {};
    const QString native = QDir::toNativeSeparators(path);
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
        return fail(error, QStringLiteral("Cannot inspect merge output: %1 (Windows error %2).")
                    .arg(path).arg(code));
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        return fail(error, QStringLiteral("Merge output must be a regular file, not a directory or symbolic link: %1").arg(path));
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),
                                     FILE_READ_ATTRIBUTES,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return fail(error, QStringLiteral("Cannot inspect merge output: %1 (Windows error %2).")
                    .arg(path).arg(GetLastError()));
    const bool ok = inspectHandle(handle, state, error);
    CloseHandle(handle);
    return ok;
}

bool inspectDevice(const QFileDevice &file, FileState *state, QString *error)
{
    const intptr_t handle = _get_osfhandle(file.handle());
    if (handle == -1) return fail(error, QStringLiteral("Cannot inspect open merge output."));
    return inspectHandle(reinterpret_cast<HANDLE>(handle), state, error);
}
#else
bool inspectStat(const struct stat &info, FileState *state, QString *error)
{
    if (!S_ISREG(info.st_mode))
        return fail(error, QStringLiteral("Merge output must be a regular file, not a directory or symbolic link."));
    state->exists = true;
    state->identity = QByteArray::number(qulonglong(info.st_dev)) + ':'
        + QByteArray::number(qulonglong(info.st_ino));
    state->changeToken = QByteArray::number(qlonglong(info.st_size)) + ':';
#ifdef Q_OS_MACOS
    state->changeToken += QByteArray::number(qlonglong(info.st_mtimespec.tv_sec)) + ':'
        + QByteArray::number(qlonglong(info.st_mtimespec.tv_nsec)) + ':'
        + QByteArray::number(qlonglong(info.st_ctimespec.tv_sec)) + ':'
        + QByteArray::number(qlonglong(info.st_ctimespec.tv_nsec));
#else
    state->changeToken += QByteArray::number(qlonglong(info.st_mtim.tv_sec)) + ':'
        + QByteArray::number(qlonglong(info.st_mtim.tv_nsec)) + ':'
        + QByteArray::number(qlonglong(info.st_ctim.tv_sec)) + ':'
        + QByteArray::number(qlonglong(info.st_ctim.tv_nsec));
#endif
    return true;
}

bool inspectPath(const QString &path, FileState *state, QString *error)
{
    *state = {};
    struct stat info;
    if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
        const int code = errno;
        if (code == ENOENT) return true;
        return fail(error, QStringLiteral("Cannot inspect merge output: %1 (system error %2).")
                    .arg(path).arg(code));
    }
    return inspectStat(info, state, error);
}

bool inspectDevice(const QFileDevice &file, FileState *state, QString *error)
{
    struct stat info;
    if (::fstat(file.handle(), &info) != 0)
        return fail(error, QStringLiteral("Cannot inspect open merge output."));
    return inspectStat(info, state, error);
}
#endif

bool snapshot(const QString &path, FileState *state, QString *error)
{
    if (!inspectPath(path, state, error)) return false;
    if (!state->exists) return true;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot read merge output %1: %2").arg(path, file.errorString()));
    FileState opened;
    if (!inspectDevice(file, &opened, error)) return false;
    if (opened.identity != state->identity || opened.changeToken != state->changeToken)
        return fail(error, QStringLiteral("Merge output changed while it was being inspected. Choose the output again."));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || file.error() != QFileDevice::NoError)
        return fail(error, QStringLiteral("Cannot read merge output %1: %2").arg(path, file.errorString()));
    FileState finalPath, finalHandle;
    if (!inspectPath(path, &finalPath, error) || !inspectDevice(file, &finalHandle, error)) return false;
    if (!finalPath.exists || finalPath.identity != opened.identity
        || finalHandle.changeToken != opened.changeToken
        || finalPath.changeToken != opened.changeToken)
        return fail(error, QStringLiteral("Merge output changed while it was being inspected. Choose the output again."));
    state->digest = hash.result();
    return true;
}

bool protectInputs(const QString &outputPath, const QString &canonicalOutput,
                   const FileState &output, const QStringList &inputPaths,
                   const QStringList &originalCanonicalPaths,
                   const QList<QByteArray> &originalIdentities, QString *error)
{
    for (int i = 0; i < inputPaths.size(); ++i) {
        const QString &input = inputPaths.at(i);
        const QFileInfo info(input);
        const QString canonical = info.canonicalFilePath();
        if (samePath(outputPath, input)
            || (!canonical.isEmpty() && samePath(canonicalOutput, canonical))
            || (!originalCanonicalPaths.value(i).isEmpty()
                && samePath(canonicalOutput, originalCanonicalPaths.at(i))))
            return fail(error, QStringLiteral("Merge output cannot overwrite an input file: %1").arg(input));
        if (!output.exists) continue;
        if (!originalIdentities.value(i).isEmpty() && output.identity == originalIdentities.at(i))
            return fail(error, QStringLiteral("Merge output is a hard link to an input file: %1").arg(input));
        if (!canonical.isEmpty()) {
            FileState source;
            if (!inspectPath(canonical, &source, error)) return false;
            if (source.exists && source.identity == output.identity)
                return fail(error, QStringLiteral("Merge output is a hard link to an input file: %1").arg(input));
        }
    }
    return true;
}

} // namespace

bool OutputFile::setPath(const QString &path, const QStringList &inputPaths, QString *error)
{
    if (error) error->clear();
    OutputFile selected;
    for (const QString &input : inputPaths) {
        if (input.isEmpty()) continue;
        selected.m_inputPaths.append(absolutePath(input));
        const QString canonical = QFileInfo(input).canonicalFilePath();
        selected.m_inputCanonicalPaths.append(canonical);
        FileState source;
        if (!canonical.isEmpty() && !inspectPath(canonical, &source, error)) return false;
        selected.m_inputIdentities.append(source.identity);
    }
    if (path.isEmpty()) {
        *this = selected;
        return true;
    }
    selected.m_path = absolutePath(path);
    const QFileInfo info(selected.m_path);
    selected.m_canonicalParent = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (selected.m_canonicalParent.isEmpty() || !QFileInfo(selected.m_canonicalParent).isDir())
        return fail(error, QStringLiteral("Merge output directory does not exist: %1").arg(info.absolutePath()));
    FileState state;
    if (!snapshot(selected.m_path, &state, error)) return false;
    const QString canonicalOutput = QDir(selected.m_canonicalParent).filePath(info.fileName());
    if (!protectInputs(selected.m_path, canonicalOutput, state, selected.m_inputPaths,
                       selected.m_inputCanonicalPaths, selected.m_inputIdentities, error)) return false;
    selected.m_exists = state.exists;
    selected.m_digest = state.digest;
    selected.m_identity = state.identity;
    *this = selected;
    return true;
}

QString OutputFile::path() const { return m_path; }
bool OutputFile::hasSaved() const { return m_hasSaved; }

bool OutputFile::checkUnchanged(QString *error) const
{
    if (error) error->clear();
    if (m_path.isEmpty()) return fail(error, QStringLiteral("Choose a merge output file before saving."));
    const QFileInfo info(m_path);
    const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (parent.isEmpty() || !samePath(parent, m_canonicalParent))
        return fail(error, QStringLiteral("Merge output directory changed externally. Choose the output again."));
    FileState state;
    if (!snapshot(m_path, &state, error)) return false;
    if (!protectInputs(m_path, QDir(parent).filePath(info.fileName()), state,
                       m_inputPaths, m_inputCanonicalPaths, m_inputIdentities, error)) return false;
    if (state.exists != m_exists || state.identity != m_identity || state.digest != m_digest)
        return fail(error, QStringLiteral("Merge output was changed, replaced, created or deleted externally. Choose the output again before saving."));
    return true;
}

bool OutputFile::save(const QByteArray &bytes, QString *error)
{
    if (!checkUnchanged(error)) return false;
    // Use the previously resolved directory so a retargeted parent symlink is
    // never followed by QSaveFile. The public path is still rechecked below.
    QSaveFile file(QDir(m_canonicalParent).filePath(QFileInfo(m_path).fileName()));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, QStringLiteral("Cannot open merge output %1: %2").arg(m_path, file.errorString()));
    if (file.write(bytes) != bytes.size() || !file.flush()) {
        const QString detail = file.errorString();
        file.cancelWriting();
        return fail(error, QStringLiteral("Cannot write merge output %1: %2").arg(m_path, detail));
    }
    FileState pending;
    if (!inspectDevice(file, &pending, error) || !checkUnchanged(error)) {
        file.cancelWriting();
        return false;
    }
    // QSaveFile's rename is atomic, but checking and renaming are separate OS
    // operations. A concurrent writer in this final interval cannot be locked
    // out by this portable API; never advertise compare-and-swap guarantees.
    if (!file.commit())
        return fail(error, QStringLiteral("Cannot commit merge output %1: %2").arg(m_path, file.errorString()));
    m_exists = true;
    m_identity = pending.identity;
    m_digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    m_hasSaved = true;
    return true;
}

} // namespace LqCompare::Merge
