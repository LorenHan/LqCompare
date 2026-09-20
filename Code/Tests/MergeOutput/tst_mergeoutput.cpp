#include <QtTest>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include "mergeoutput.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

using namespace LqCompare::Merge;

namespace {
void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        qFatal("Could not write test fixture");
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) qFatal("Could not read test fixture");
    return file.readAll();
}

bool hardLink(const QString &source, const QString &link)
{
#ifdef Q_OS_WIN
    return CreateHardLinkW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(link).utf16()),
                           reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(source).utf16()),
                           nullptr) != 0;
#else
    return ::link(QFile::encodeName(source).constData(), QFile::encodeName(link).constData()) == 0;
#endif
}

bool symbolicLink(const QString &source, const QString &link, bool directory = false)
{
#ifdef Q_OS_WIN
    DWORD flags = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
    const QString nativeLink = QDir::toNativeSeparators(link);
    const QString nativeSource = QDir::toNativeSeparators(source);
    return CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()),
                              reinterpret_cast<LPCWSTR>(nativeSource.utf16()), flags | 0x2)
        || CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(nativeLink.utf16()),
                               reinterpret_cast<LPCWSTR>(nativeSource.utf16()), flags);
#else
    Q_UNUSED(directory)
    return ::symlink(QFile::encodeName(source).constData(), QFile::encodeName(link).constData()) == 0;
#endif
}
}

class MergeOutputTests : public QObject
{
    Q_OBJECT
private slots:
    void noDestination()
    {
        OutputFile output;
        QString error;
        QVERIFY(output.path().isEmpty());
        QVERIFY(!output.save("merged", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(output.setPath({}, {}, &error));
        QVERIFY(error.isEmpty());
        QVERIFY(!output.checkUnchanged(&error));
        QVERIFY(!output.hasSaved());
    }

    void newDestinationAndRepeatedSave()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("merged.txt");
        OutputFile output;
        QString error;
        QVERIFY2(output.setPath(path, {}, &error), qPrintable(error));
        QCOMPARE(output.path(), path);
        QVERIFY(!output.hasSaved());
        QVERIFY2(output.save("first\n", &error), qPrintable(error));
        QCOMPARE(readFile(path), QByteArray("first\n"));
        QVERIFY(output.hasSaved());
        QVERIFY2(output.checkUnchanged(&error), qPrintable(error));
        QVERIFY2(output.save("second\n", &error), qPrintable(error));
        QCOMPARE(readFile(path), QByteArray("second\n"));
        QVERIFY2(output.save({}, &error), qPrintable(error));
        QCOMPARE(readFile(path), QByteArray());
    }

    void existingOutputDoesNotModifyInputs()
    {
        QTemporaryDir dir;
        const QString base = dir.filePath("base");
        const QString left = dir.filePath("left");
        const QString right = dir.filePath("right");
        const QString path = dir.filePath("merged");
        writeFile(base, "base\n"); writeFile(left, "left\n"); writeFile(right, "right\n");
        writeFile(path, "previous output\n");
        OutputFile output;
        QString error;
        QVERIFY2(output.setPath(path, {base, left, right}, &error), qPrintable(error));
        QVERIFY2(output.save("combined\n", &error), qPrintable(error));
        QCOMPARE(readFile(path), QByteArray("combined\n"));
        QCOMPARE(readFile(base), QByteArray("base\n"));
        QCOMPARE(readFile(left), QByteArray("left\n"));
        QCOMPARE(readFile(right), QByteArray("right\n"));
    }

    void sameSizeAndTimeExternalChange()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("merged");
        writeFile(path, "AAAA");
        const QDateTime originalTime = QFileInfo(path).lastModified();
        OutputFile output;
        QString error;
        QVERIFY(output.setPath(path, {}, &error));
        writeFile(path, "BBBB");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(originalTime, QFileDevice::FileModificationTime));
        file.close();
        QCOMPARE(QFileInfo(path).lastModified(), originalTime);
        QVERIFY(!output.checkUnchanged(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!output.save("CCCC", &error));
        QCOMPARE(readFile(path), QByteArray("BBBB"));
        QVERIFY(!output.hasSaved());
        // A failed save must not adopt the externally changed snapshot.
        writeFile(path, "AAAA");
        QVERIFY2(output.save("CCCC", &error), qPrintable(error));
        QCOMPARE(readFile(path), QByteArray("CCCC"));
    }

    void removedOutput()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("merged");
        writeFile(path, "old");
        OutputFile output;
        QVERIFY(output.setPath(path, {}));
        QVERIFY(QFile::remove(path));
        QString error;
        QVERIFY(!output.save("new", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(path));
    }

    void newlyCreatedOutput()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("merged");
        OutputFile output;
        QVERIFY(output.setPath(path, {}));
        writeFile(path, "external");
        QVERIFY(!output.save("new"));
        QCOMPARE(readFile(path), QByteArray("external"));
        // Explicitly choosing this existing output resets the baseline.
        QVERIFY(output.setPath(path, {}));
        QVERIFY(output.save("new"));
        QCOMPARE(readFile(path), QByteArray("new"));
    }

    void replacedWithIdenticalContents()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("merged");
        writeFile(path, "same");
        OutputFile output;
        QVERIFY(output.setPath(path, {}));
        QVERIFY(QFile::rename(path, dir.filePath("previous")));
        writeFile(path, "same");
        QVERIFY(!output.checkUnchanged());
        QVERIFY(!output.save("new"));
        QCOMPARE(readFile(path), QByteArray("same"));
    }

    void rejectInputPathAndKeepPreviousSelection()
    {
        QTemporaryDir dir;
        const QString input = dir.filePath("input");
        const QString path = dir.filePath("merged");
        writeFile(input, "input");
        OutputFile output;
        QVERIFY(output.setPath(path, {input}));
        QString error;
        QVERIFY(!output.setPath(input, {input}, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(output.path(), path);
        QVERIFY(output.save("merged"));
        QCOMPARE(readFile(input), QByteArray("input"));
        QVERIFY(!output.setPath(dir.filePath("./input"), {input}));
    }

    void rejectSymbolicOutput()
    {
        QTemporaryDir dir;
        const QString real = dir.filePath("real");
        const QString link = dir.filePath("link");
        writeFile(real, "real");
        if (!symbolicLink(real, link)) QSKIP("Symbolic links unavailable on this platform or account.");
        OutputFile output;
        QVERIFY(!output.setPath(link, {}));
        QVERIFY(QFile::remove(link));
        QVERIFY(symbolicLink(dir.filePath("missing"), link));
        QVERIFY(!output.setPath(link, {}));
        QCOMPARE(readFile(real), QByteArray("real"));
    }

    void rejectOutputReplacedBySymbolicLink()
    {
        QTemporaryDir dir;
        const QString real = dir.filePath("real");
        const QString path = dir.filePath("merged");
        writeFile(real, "real");
        writeFile(path, "original");
        OutputFile output;
        QVERIFY(output.setPath(path, {}));
        QVERIFY(QFile::remove(path));
        if (!symbolicLink(real, path)) QSKIP("Symbolic links unavailable on this platform or account.");
        QVERIFY(!output.save("new"));
        QCOMPARE(readFile(real), QByteArray("real"));
        QVERIFY(QFileInfo(path).isSymLink());
    }

    void rejectHardLinkedSource()
    {
        QTemporaryDir dir;
        const QString input = dir.filePath("input");
        const QString path = dir.filePath("merged");
        writeFile(input, "input");
        if (!hardLink(input, path)) QSKIP("Hard links unavailable on this filesystem.");
        OutputFile output;
        QVERIFY(!output.setPath(path, {input}));
        QCOMPARE(readFile(input), QByteArray("input"));
    }

    void sourceAliasIntroducedAfterSelection()
    {
        QTemporaryDir dir;
        const QString input = dir.filePath("input");
        const QString path = dir.filePath("merged");
        writeFile(input, "input");
        writeFile(path, "output");
        OutputFile output;
        QVERIFY(output.setPath(path, {input}));
        QVERIFY(QFile::remove(input));
        if (!hardLink(path, input)) QSKIP("Hard links unavailable on this filesystem.");
        QString error;
        QVERIFY(!output.save("new", &error));
        QVERIFY(error.contains("input"));
        QCOMPARE(readFile(input), QByteArray("output"));
        QCOMPARE(readFile(path), QByteArray("output"));
    }

    void sourceSymbolicAlias()
    {
        QTemporaryDir dir;
        const QString input = dir.filePath("input");
        const QString real = dir.filePath("real");
        writeFile(real, "input");
        if (!symbolicLink(real, input)) QSKIP("Symbolic links unavailable on this platform or account.");
        OutputFile output;
        QVERIFY(!output.setPath(real, {input}));
        QCOMPARE(readFile(real), QByteArray("input"));
    }

    void parentSymbolicLinkRetarget()
    {
        QTemporaryDir dir;
        const QString first = dir.filePath("first");
        const QString second = dir.filePath("second");
        const QString link = dir.filePath("current");
        QVERIFY(QDir().mkdir(first)); QVERIFY(QDir().mkdir(second));
        writeFile(QDir(first).filePath("merged"), "first");
        writeFile(QDir(second).filePath("merged"), "second");
        if (!symbolicLink(first, link, true)) QSKIP("Symbolic links unavailable on this platform or account.");
        OutputFile output;
        QString error;
        QVERIFY2(output.setPath(QDir(link).filePath("merged"), {}, &error), qPrintable(error));
        QVERIFY2(output.save("first saved", &error), qPrintable(error));
        QVERIFY(QFile::remove(link));
        QVERIFY(symbolicLink(second, link, true));
        QVERIFY(!output.save("new", &error));
        QVERIFY(error.contains("directory"));
        QCOMPARE(readFile(QDir(first).filePath("merged")), QByteArray("first saved"));
        QCOMPARE(readFile(QDir(second).filePath("merged")), QByteArray("second"));
    }

    void rejectNonRegularAndMissingParent()
    {
        QTemporaryDir dir;
        OutputFile output;
        QVERIFY(!output.setPath(dir.path(), {}));
        QVERIFY(!output.setPath(dir.filePath("missing/merged"), {}));
#ifndef Q_OS_WIN
        const QString fifo = dir.filePath("pipe");
        QVERIFY(::mkfifo(QFile::encodeName(fifo).constData(), 0600) == 0);
        QVERIFY(!output.setPath(fifo, {}));
#endif
    }

    void atomicReplacementPreservesOtherHardLinks()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("merged");
        const QString other = dir.filePath("previous");
        writeFile(path, "previous");
        if (!hardLink(path, other)) QSKIP("Hard links unavailable on this filesystem.");
        OutputFile output;
        QVERIFY(output.setPath(path, {}));
        QVERIFY(output.save("new"));
        QCOMPARE(readFile(path), QByteArray("new"));
        QCOMPARE(readFile(other), QByteArray("previous"));
    }

    void refusesDirectWriteFallback()
    {
#ifdef Q_OS_WIN
        QSKIP("Directory permission fixture uses POSIX permission bits.");
#else
        QTemporaryDir dir;
        const QString path = dir.filePath("merged");
        writeFile(path, "previous");
        OutputFile output;
        QVERIFY(output.setPath(path, {}));
        const QFileDevice::Permissions permissions = QFile::permissions(dir.path());
        QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        QFile probe(dir.filePath("permission-probe"));
        const bool canWriteDirectory = probe.open(QIODevice::WriteOnly);
        probe.close();
        if (canWriteDirectory) {
            QFile::setPermissions(dir.path(), permissions);
            QSKIP("This account bypasses directory write permissions.");
        }
        QString error;
        const bool saved = output.save("replacement", &error);
        // Restore directory permissions before any assertion may return.
        QVERIFY(QFile::setPermissions(dir.path(), permissions));
        QVERIFY(!saved);
        QVERIFY(!error.isEmpty());
        QCOMPARE(readFile(path), QByteArray("previous"));
        QVERIFY(!output.hasSaved());
        QVERIFY2(output.save("replacement", &error), qPrintable(error));
        QCOMPARE(readFile(path), QByteArray("replacement"));
#endif
    }
};

QTEST_APPLESS_MAIN(MergeOutputTests)
#include "tst_mergeoutput.moc"
