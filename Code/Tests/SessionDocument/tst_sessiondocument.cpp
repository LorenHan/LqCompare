#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#include "sessiondocument.h"
using namespace LqCompare;
class SessionDocumentTests : public QObject {
    Q_OBJECT
private slots:
    void roundTripPreservesUnknownAndRelativePaths() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject json{{"version", 1}, {"type", "text"}, {"future", QJsonArray{1,2}},
                         {"sources", QJsonObject{{"left", "a.txt"}, {"right", "子目录/b.txt"}, {"other", true}}},
                         {"settings", QJsonObject{{"future.option", QJsonObject{{"x",1}}}}}};
        SessionDocument doc; QString error;
        QVERIFY2(SessionDocument::fromJson(json, dir.path(), &doc, &error), qPrintable(error));
        QCOMPARE(doc.leftPath, dir.filePath(QStringLiteral("a.txt")));
        doc.notes = QStringLiteral("备注");
        QVERIFY(doc.save(dir.filePath(QStringLiteral("saved.lqc")), &error));
        SessionDocument reread;
        QVERIFY(SessionDocument::load(dir.filePath(QStringLiteral("saved.lqc")), &reread, &error));
        QCOMPARE(reread.notes, doc.notes);
        QCOMPARE(reread.rightPath, doc.rightPath);
        QCOMPARE(reread.original.value("future"), json.value("future"));
        QCOMPARE(reread.original.value("sources").toObject().value("other"), QJsonValue(true));
        QCOMPARE(reread.settings, doc.settings);
    }
    void badFieldDoesNotChangeOutput() {
        SessionDocument doc; doc.notes = QStringLiteral("keep"); QString error;
        QVERIFY(!SessionDocument::fromJson(QJsonObject{{"sources", QJsonObject{{"left", 12}}}}, "/", &doc, &error));
        QVERIFY(error.contains(QStringLiteral("sources.left")));
        QCOMPARE(doc.notes, QStringLiteral("keep"));
        QVERIFY(!SessionDocument::fromJson(QJsonObject{{"version", 2}}, "/", &doc, &error));
        QVERIFY(!SessionDocument::fromJson(QJsonObject{{"version", 0.5}}, "/", &doc, &error));
    }
    void oldDocumentUsesDefaults() {
        SessionDocument doc;
        QVERIFY(SessionDocument::fromJson({}, "/tmp", &doc));
        QCOMPARE(doc.typeId, QStringLiteral("text"));
        QVERIFY(doc.leftPath.isEmpty());
        QVERIFY(doc.settings.isEmpty());
    }
    void corruptJsonReportsLine() {
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("bad.lqc")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\n\"type\":broken\n}"); f.close();
        SessionDocument doc; QString error;
        QVERIFY(!SessionDocument::load(f.fileName(), &doc, &error));
        QVERIFY(error.contains(QStringLiteral("line 2")));
    }
    void invalidSavePreservesDestination() {
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("old.lqc")));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); file.close();
        SessionDocument doc; doc.typeId = QStringLiteral("bad type");
        QVERIFY(!doc.save(file.fileName()));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("original"));
    }
    void saveCannotOverwriteAnySource_data() {
        QTest::addColumn<QString>("role");
        for (const auto *role : {"left", "right", "base", "output"}) QTest::newRow(role) << QString::fromLatin1(role);
    }
    void saveCannotOverwriteAnySource() {
        QFETCH(QString, role);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile source(dir.filePath(QStringLiteral("source.lqc")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("valuable source bytes"), qint64(21)); source.close();
        SessionDocument doc; QString error;
        QVERIFY(SessionDocument::fromJson(QJsonObject{{"sources", QJsonObject{{role, "source.lqc"}}}},
                                         dir.path(), &doc, &error));
        // Lexically different paths must still protect the same file.
        QVERIFY(!doc.save(dir.filePath(QStringLiteral("./source.lqc")), &error));
        QVERIFY(error.contains(QStringLiteral("cannot overwrite")));
        QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(), QByteArray("valuable source bytes"));
    }
    void saveProtectsSymbolicLinkAliases() {
#ifndef Q_OS_UNIX
        QSKIP("Creating portable symbolic links requires platform privileges outside this test.");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QStringLiteral("source.lqc"));
        QFile source(path);
        QVERIFY(source.open(QIODevice::WriteOnly)); source.write("keep"); source.close();
        const auto alias = dir.filePath(QStringLiteral("alias.lqc"));
        QCOMPARE(::symlink(QFile::encodeName(path).constData(), QFile::encodeName(alias).constData()), 0);
        SessionDocument doc; doc.basePath = alias;
        QVERIFY(!doc.save(path)); // A source link protects its referent.
        doc.basePath = path;
        QVERIFY(!doc.save(alias)); // An output link cannot bypass that protection.
        QVERIFY(QFileInfo(alias).isSymLink());
        QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(), QByteArray("keep"));
#endif
    }
    void saveProtectsMissingOutputThroughDirectoryAlias() {
#ifndef Q_OS_UNIX
        QSKIP("Creating portable symbolic links requires platform privileges outside this test.");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("real")));
        const auto real = dir.filePath(QStringLiteral("real"));
        const auto alias = dir.filePath(QStringLiteral("alias"));
        QCOMPARE(::symlink(QFile::encodeName(real).constData(), QFile::encodeName(alias).constData()), 0);
        SessionDocument doc;
        doc.outputPath = alias + QStringLiteral("/future.lqc");
        QVERIFY(!doc.save(real + QStringLiteral("/future.lqc")));
        QVERIFY(!QFileInfo::exists(doc.outputPath));
        // Resolving an existing aliased parent must also work above missing directories.
        doc.outputPath = alias + QStringLiteral("/not-created/future.lqc");
        QString error;
        QVERIFY(!doc.save(real + QStringLiteral("/not-created/future.lqc"), &error));
        QVERIFY(error.contains(QStringLiteral("cannot overwrite")));
#endif
    }
    void saveProtectsHardLinkAlias() {
#ifndef Q_OS_UNIX
        QSKIP("This file identity regression exercises Unix hard links.");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath(QStringLiteral("source.lqc"));
        const auto alias = dir.filePath(QStringLiteral("alias.lqc"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep"); file.close();
        QCOMPARE(::link(QFile::encodeName(path).constData(), QFile::encodeName(alias).constData()), 0);
        SessionDocument doc; doc.leftPath = path;
        QVERIFY(!doc.save(alias));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("keep")); file.close();
        file.setFileName(alias);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("keep"));
#endif
    }
    void oversizedSavePreservesDestination() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(QStringLiteral("saved.lqc")));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("original"); file.close();
        SessionDocument doc;
        // Unknown fields count as part of the serialized file, too.
        doc.original.insert(QStringLiteral("future"), QString(8 * 1024 * 1024, QLatin1Char('x')));
        QString error;
        QVERIFY(!doc.save(file.fileName(), &error));
        QVERIFY(error.contains(QStringLiteral("8 MiB")));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("original"));
        const auto fresh = dir.filePath(QStringLiteral("new.lqc"));
        QVERIFY(!doc.save(fresh, &error)); QVERIFY(!QFileInfo::exists(fresh));
    }
    void boundedSaveCanBeReopened() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SessionDocument doc;
        doc.notes = QString(8 * 1024 * 1024 - 4096, QLatin1Char('x'));
        QString error;
        const auto path = dir.filePath(QStringLiteral("large.lqc"));
        QVERIFY2(doc.save(path, &error), qPrintable(error));
        SessionDocument restored;
        QVERIFY2(SessionDocument::load(path, &restored, &error), qPrintable(error));
        QCOMPARE(restored.notes, doc.notes);
    }
    void invalidOutputPathDoesNotCreateFile() {
        SessionDocument doc; QString error;
        QVERIFY(!doc.save({}, &error));
        QVERIFY(error.contains(QStringLiteral("path")));
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("new.lqc"));
        QVERIFY(!doc.save(path + QChar::Null + QStringLiteral("suffix"), &error));
        QVERIFY(!QFileInfo::exists(path));
    }
};
QTEST_APPLESS_MAIN(SessionDocumentTests)
#include "tst_sessiondocument.moc"
