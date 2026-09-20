#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "fixtures.h"
#include "../../Services/Media/mediametadata.h"

using namespace LqCompare::Media;
using namespace MediaFixtures;

namespace {

bool readFixture(const QByteArray &bytes, Document *document, QString *error = nullptr,
                 const ReadLimits &limits = {}, const QString &suffix = "mp3")
{
    QTemporaryDir directory;
    if (!directory.isValid())
        return false;
    const QString path = directory.filePath("fixture." + suffix);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return false;
    file.close();
    return load(path, document, error, limits);
}

const FieldDifference *findField(const Comparison &comparison, const QString &key, bool technical = false)
{
    for (const FieldDifference &field : comparison.fields)
        if (field.key == key && field.technical == technical)
            return &field;
    return nullptr;
}

}

class MediaTest : public QObject {
    Q_OBJECT
private slots:
    void id3v1Fields();
    void id3Versions_data();
    void id3Versions();
    void unicodeText_data();
    void unicodeText();
    void id3Multivalues();
    void id3Comments();
    void id3CommentLanguagesAndUserText();
    void utf16DescriptionBoundaries_data();
    void utf16DescriptionBoundaries();
    void id3UnknownTextAndPrecedence();
    void id3StructuralOptions_data();
    void id3StructuralOptions();
    void skippedMetadataIsPartial_data();
    void skippedMetadataIsPartial();
    void taglessMpeg();
    void flacFieldsAndMultivalues();
    void flacStreamInfoOnly();
    void malformedId3_data();
    void malformedId3();
    void malformedEncoding_data();
    void malformedEncoding();
    void malformedFlac_data();
    void malformedFlac();
    void limits_data();
    void limits();
    void unsupported_data();
    void unsupported();
    void ioErrors();
    void compareFields();
    void compareTechnicalAndCompleteness();
    void readNeverMutates_data();
    void readNeverMutates();
    void boundedCorruption_data();
    void boundedCorruption();
};

void MediaTest::id3v1Fields()
{
    Document document;
    QString error = "stale";
    const QByteArray bytes = mpegPayload() + id3v1("Title", "Artist", "Album", "1999", "Comment", 7);
    QVERIFY2(readFixture(bytes, &document, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(document.status, ReadStatus::Ready);
    QCOMPARE(document.fileSize, qint64(bytes.size()));
    QCOMPARE(document.tags.value("title"), QStringList({"Title"}));
    QCOMPARE(document.tags.value("artist"), QStringList({"Artist"}));
    QCOMPARE(document.tags.value("album"), QStringList({"Album"}));
    QCOMPARE(document.tags.value("date"), QStringList({"1999"}));
    QCOMPARE(document.tags.value("track"), QStringList({"7"}));
    QCOMPARE(document.tags.value("comment"), QStringList({"Comment"}));
    QVERIFY(!document.tags.value("genre").isEmpty());
    QVERIFY(readFixture(mpegPayload() + id3v1("Caf\xe9", "", "", "", "full v1 comment", 0, 255), &document));
    QCOMPARE(document.tags.value("title"), QStringList({QString::fromUtf8("Café")}));
    QVERIFY(!document.tags.contains("track"));
}

void MediaTest::id3Versions_data()
{
    QTest::addColumn<int>("version");
    QTest::addColumn<QByteArray>("titleId");
    QTest::addColumn<QByteArray>("artistId");
    QTest::newRow("ID3v2.2") << 2 << QByteArray("TT2") << QByteArray("TP1");
    QTest::newRow("ID3v2.3") << 3 << QByteArray("TIT2") << QByteArray("TPE1");
    QTest::newRow("ID3v2.4") << 4 << QByteArray("TIT2") << QByteArray("TPE1");
}

void MediaTest::id3Versions()
{
    QFETCH(int, version);
    QFETCH(QByteArray, titleId);
    QFETCH(QByteArray, artistId);
    // A frame longer than 127 bytes detects BE/syncsafe size confusion.
    const QString title(160, QLatin1Char('A'));
    const QByteArray frames = id3Frame(version, titleId, textPayload(0, title))
            + id3Frame(version, artistId, textPayload(0, "Singer"));
    Document document;
    QString error;
    QVERIFY2(readFixture(id3Tag(version, frames) + mpegPayload(), &document, &error), qPrintable(error));
    QCOMPARE(document.status, ReadStatus::Ready);
    QCOMPARE(document.tags.value("title"), QStringList({title}));
    QCOMPARE(document.tags.value("artist"), QStringList({"Singer"}));
}

void MediaTest::unicodeText_data()
{
    QTest::addColumn<int>("version");
    QTest::addColumn<int>("encoding");
    QTest::newRow("v2.2 UTF16 BOM") << 2 << 1;
    QTest::newRow("v2.3 UTF16 BOM") << 3 << 1;
    QTest::newRow("v2.4 UTF16 BOM") << 4 << 1;
    QTest::newRow("v2.4 UTF16 BE") << 4 << 2;
    QTest::newRow("v2.4 UTF8") << 4 << 3;
}

void MediaTest::unicodeText()
{
    QFETCH(int, version);
    QFETCH(int, encoding);
    const QString title = QString::fromUtf8("中文曲名 — 😀");
    Document document;
    QString error;
    QVERIFY2(readFixture(id3Tag(version, id3Frame(version, version == 2 ? "TT2" : "TIT2",
                      textPayload(encoding, title))) + mpegPayload(), &document, &error), qPrintable(error));
    QCOMPARE(document.tags.value("title"), QStringList({title}));
}

void MediaTest::id3Multivalues()
{
    Document document;
    const QString artists = QString::fromUtf8("甲") + QChar(0) + QString::fromUtf8("乙");
    for (int encoding : {1, 2, 3}) {
        QString error;
        QVERIFY2(readFixture(id3Tag(4, id3Frame(4, "TPE1", textPayload(encoding, artists)))
                             + mpegPayload(), &document, &error), qPrintable(error));
        QCOMPARE(document.tags.value("artist"), QStringList({QString::fromUtf8("甲"), QString::fromUtf8("乙")}));
    }
    QVERIFY(readFixture(id3Tag(3, id3Frame(3, "TPE1", textPayload(0, "AC/DC"))) + mpegPayload(), &document));
    QCOMPARE(document.tags.value("artist"), QStringList({"AC/DC"}));
}

void MediaTest::id3Comments()
{
    const QByteArray payload = QByteArray(1, char(3)) + "eng" + QByteArray(1, '\0')
            + QString::fromUtf8("中文注释").toUtf8();
    Document document;
    QString error;
    QVERIFY2(readFixture(id3Tag(4, id3Frame(4, "COMM", payload)) + mpegPayload(), &document, &error), qPrintable(error));
    QCOMPARE(document.tags.value("comment"), QStringList({QString::fromUtf8("中文注释")}));
}

void MediaTest::id3CommentLanguagesAndUserText()
{
    const auto comment = [](const QByteArray &language, const QString &description, const QString &value) {
        return QByteArray(1, char(3)) + language + description.toUtf8() + QByteArray(1, '\0') + value.toUtf8();
    };
    Document english, chinese;
    QVERIFY(readFixture(id3Tag(4, id3Frame(4, "COMM", comment("eng", {}, "same"))), &english));
    QVERIFY(readFixture(id3Tag(4, id3Frame(4, "COMM", comment("zho", {}, "same"))), &chinese));
    QCOMPARE(english.tags.value("comment"), QStringList({"same"}));
    QCOMPARE(chinese.tags.value("comment:zho:"), QStringList({"same"}));
    QCOMPARE(compare(english, chinese, true).differenceCount, 2);
    Document descriptions;
    const QByteArray frames = id3Frame(4, "COMM", comment("zho", QString::fromUtf8("描述"), QString::fromUtf8("内容")))
            + id3Frame(4, "TXXX", textPayload(3, QString("USER KEY") + QChar(0) + "first" + QChar(0) + "second"));
    QVERIFY(readFixture(id3Tag(4, frames), &descriptions));
    QCOMPARE(descriptions.tags.value(QString::fromUtf8("comment:zho:描述")), QStringList({QString::fromUtf8("内容")}));
    QCOMPARE(descriptions.tags.value("id3:TXXX:USER KEY"), QStringList({"first", "second"}));
}

void MediaTest::utf16DescriptionBoundaries_data()
{
    QTest::addColumn<QByteArray>("frameId");
    QTest::addColumn<QByteArray>("encoded");
    QTest::addColumn<QString>("description");
    QTest::addColumn<bool>("valid");
    const QByteArray terminator(2, '\0');
    const QString description = QString::fromUtf8("描述");
    const QByteArray descriptionLE = textPayload(1, description).mid(1);
    const QByteArray descriptionBE = QByteArray::fromHex("feff") + textPayload(2, description).mid(1);
    const QByteArray textLE = QByteArray::fromHex("fffe2d4e8765");
    const QByteArray textBE = QByteArray::fromHex("feff4e2d6587");
    for (const QByteArray &id : {QByteArray("COMM"), QByteArray("TXXX")}) {
        QTest::newRow((id + "-empty-description-body-LE-BOM").constData())
                << id << (terminator + textLE) << QString() << true;
        QTest::newRow((id + "-empty-description-body-BE-BOM").constData())
                << id << (terminator + textBE) << QString() << true;
        QTest::newRow((id + "-inherit-description-LE-BOM").constData())
                << id << (descriptionLE + terminator + textLE.mid(2)) << description << true;
        QTest::newRow((id + "-inherit-description-BE-BOM").constData())
                << id << (descriptionBE + terminator + textBE.mid(2)) << description << true;
        QTest::newRow((id + "-reject-opposite-body-BOM").constData())
                << id << (descriptionLE + terminator + textBE) << description << false;
        QTest::newRow((id + "-reject-empty-description-without-any-BOM").constData())
                << id << (terminator + textLE.mid(2)) << QString() << false;
        QTest::newRow((id + "-reject-nonempty-description-without-any-BOM").constData())
                << id << (descriptionLE.mid(2) + terminator + textLE.mid(2)) << description << false;
    }
}

void MediaTest::utf16DescriptionBoundaries()
{
    QFETCH(QByteArray, frameId);
    QFETCH(QByteArray, encoded);
    QFETCH(QString, description);
    QFETCH(bool, valid);
    const QByteArray payload = QByteArray(1, char(1))
            + (frameId == "COMM" ? QByteArray("eng") : QByteArray()) + encoded;
    Document document;
    QString error;
    const bool loaded = readFixture(id3Tag(4, id3Frame(4, frameId, payload)), &document, &error);
    QCOMPARE(loaded, valid);
    if (valid) {
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(document.status, ReadStatus::Ready);
        const QString key = frameId == "TXXX" ? QString("id3:TXXX:") + description
                : description.isEmpty() ? QString("comment") : QString("comment:eng:") + description;
        QCOMPARE(document.tags.value(key), QStringList({QString::fromUtf8("中文")}));
    } else {
        QCOMPARE(document.status, ReadStatus::Malformed);
        QVERIFY(!error.isEmpty());
        QVERIFY(document.tags.isEmpty());
        QVERIFY(document.technical.isEmpty());
    }
}

void MediaTest::id3UnknownTextAndPrecedence()
{
    const QByteArray frames = id3Frame(4, "TIT2", textPayload(3, "new title"))
            + id3Frame(4, "TZZZ", textPayload(3, "preserved"));
    Document document;
    QString error;
    QVERIFY2(readFixture(id3Tag(4, frames) + mpegPayload() + id3v1("old title", "fallback artist"),
                         &document, &error), qPrintable(error));
    QCOMPARE(document.tags.value("title"), QStringList({"new title"}));
    QCOMPARE(document.tags.value("artist"), QStringList({"fallback artist"}));
    QCOMPARE(document.tags.value("id3:TZZZ"), QStringList({"preserved"}));
    QCOMPARE(document.tags.value("id3v1:title"), QStringList({"old title"}));
}

void MediaTest::id3StructuralOptions_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("title");
    const QByteArray v3Frame = id3Frame(3, "TIT2", textPayload(0, "Title"));
    const QByteArray v4Frame = id3Frame(4, "TIT2", textPayload(3, "Title"));
    QTest::newRow("v3-extended-header") << id3Tag(3, be(6, 4) + QByteArray(6, '\0') + v3Frame, 0x40) << QString("Title");
    QTest::newRow("v4-extended-header") << id3Tag(4, syncsafe(6) + QByteArray::fromHex("0100") + v4Frame, 0x40) << QString("Title");
    QByteArray withFooter = id3Tag(4, v4Frame, 0x10);
    QByteArray footer = withFooter.left(10);
    footer.replace(0, 3, "3DI");
    QTest::newRow("v4-footer") << withFooter + footer << QString("Title");
    QTest::newRow("v4-grouped") << id3Tag(4, id3Frame(4, "TIT2", QByteArray(1, char(7)) + textPayload(3, "Title"), 0x40)) << QString("Title");
    QTest::newRow("v4-data-length-indicator") << id3Tag(4, id3Frame(4, "TIT2", syncsafe(6) + textPayload(3, "Title"), 0x01)) << QString("Title");
    const QByteArray unsynchronised = QByteArray::fromHex("0041ff00e042");
    const QString rawTitle = QString::fromLatin1(QByteArray::fromHex("41ffe042"));
    QTest::newRow("v4-frame-unsynchronisation") << id3Tag(4, id3Frame(4, "TIT2", unsynchronised, 0x02)) << rawTitle;
    QByteArray v3UnsyncFrame = id3Frame(3, "TIT2", QByteArray::fromHex("0041ffe042"));
    v3UnsyncFrame.replace(QByteArray::fromHex("ffe0"), QByteArray::fromHex("ff00e0"));
    QTest::newRow("v3-tag-unsynchronisation") << id3Tag(3, v3UnsyncFrame, 0x80) << rawTitle;
    QTest::newRow("zero-padding") << id3Tag(4, v4Frame + QByteArray(12, '\0')) << QString("Title");
}

void MediaTest::id3StructuralOptions()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, title);
    Document document;
    QString error;
    QVERIFY2(readFixture(bytes, &document, &error), qPrintable(error));
    QCOMPARE(document.status, ReadStatus::Ready);
    QCOMPARE(document.tags.value("title"), QStringList({title}));
}

void MediaTest::skippedMetadataIsPartial_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("suffix");
    const QByteArray title = id3Frame(4, "TIT2", textPayload(3, "Title"));
    QTest::newRow("id3-artwork") << id3Tag(4, title + id3Frame(4, "APIC", "unparsed image")) << QString("mp3");
    QTest::newRow("id3-compressed") << id3Tag(4, title + id3Frame(4, "TPE1", syncsafe(100) + "compressed", 0x09)) << QString("mp3");
    QTest::newRow("id3-encrypted") << id3Tag(4, title + id3Frame(4, "TPE1", "encrypted", 0x04)) << QString("mp3");
    QTest::newRow("id3-experimental") << id3Tag(4, title, 0x20) << QString("mp3");
    QTest::newRow("id3-update") << id3Tag(4, syncsafe(7) + QByteArray::fromHex("014000") + title, 0x40) << QString("mp3");
    QTest::newRow("id3-CRC") << id3Tag(4, syncsafe(12) + QByteArray::fromHex("0120050000000000") + title, 0x40) << QString("mp3");
    QTest::newRow("flac-picture") << ("fLaC" + flacBlock(0, flacStreamInfo(), false)
                                     + flacBlock(4, vorbisComments({"TITLE=Title"}), false)
                                     + flacBlock(6, "unparsed picture", true)) << QString("flac");
}

void MediaTest::skippedMetadataIsPartial()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, suffix);
    Document document;
    QString error;
    QVERIFY2(readFixture(bytes, &document, &error, {}, suffix), qPrintable(error));
    QCOMPARE(document.status, ReadStatus::Partial);
    QCOMPARE(document.tags.value("title"), QStringList({"Title"}));
    QVERIFY(!document.warnings.isEmpty());
    QVERIFY(!compare(document, document).complete);
}

void MediaTest::taglessMpeg()
{
    Document document;
    QString error;
    QVERIFY2(readFixture(mpegPayload(), &document, &error), qPrintable(error));
    QCOMPARE(document.status, ReadStatus::Partial);
    QVERIFY(document.tags.isEmpty());
    QVERIFY(!document.complete());
    QVERIFY(!document.message.isEmpty() || !document.warnings.isEmpty());
    QVERIFY(document.usable());
}

void MediaTest::flacFieldsAndMultivalues()
{
    const QByteArray bytes = flac({QString::fromUtf8("TITLE=中文标题"), "artist=First", "ARTIST=Second",
                                  "ALBUM=Record", "ALBUMARTIST=Various", "TRACKNUMBER=3/12",
                                  "DATE=2026", "GENRE=Jazz", "COMMENT=Note", "COMPOSER=Composer",
                                  "COPYRIGHT=Owner", "ENCODER=Fixture", "CUSTOM=value=with=equals"});
    Document document;
    QString error;
    QVERIFY2(readFixture(bytes, &document, &error, {}, "flac"), qPrintable(error));
    QCOMPARE(document.status, ReadStatus::Ready);
    QCOMPARE(document.tags.value("title"), QStringList({QString::fromUtf8("中文标题")}));
    QCOMPARE(document.tags.value("artist"), QStringList({"First", "Second"}));
    QCOMPARE(document.tags.value("album_artist"), QStringList({"Various"}));
    QCOMPARE(document.tags.value("track"), QStringList({"3/12"}));
    QCOMPARE(document.tags.value("date"), QStringList({"2026"}));
    QCOMPARE(document.tags.value("vorbis:CUSTOM"), QStringList({"value=with=equals"}));
    QCOMPARE(document.technical.value("sample_rate"), QStringList({"44100"}));
    QCOMPARE(document.technical.value("channels"), QStringList({"2"}));
    QCOMPARE(document.technical.value("bits_per_sample"), QStringList({"16"}));
    QCOMPARE(document.technical.value("total_samples"), QStringList({"88200"}));
}

void MediaTest::flacStreamInfoOnly()
{
    Document document;
    QString error;
    QVERIFY2(readFixture("fLaC" + flacBlock(0, flacStreamInfo(48000, 1, 24, 0), true),
                         &document, &error, {}, "flac"), qPrintable(error));
    QCOMPARE(document.status, ReadStatus::Ready);
    QVERIFY(document.tags.isEmpty());
    QCOMPARE(document.technical.value("sample_rate"), QStringList({"48000"}));
    QCOMPARE(document.technical.value("channels"), QStringList({"1"}));
    QCOMPARE(document.technical.value("bits_per_sample"), QStringList({"24"}));
    QCOMPARE(document.technical.value("total_samples"), QStringList({"0"}));
}

void MediaTest::malformedId3_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("truncated-header") << QByteArray("ID3\x04\0", 5);
    QByteArray invalidSize = id3Tag(4, {});
    invalidSize[6] = char(0x80);
    QTest::newRow("invalid-tag-syncsafe") << invalidSize;
    QTest::newRow("truncated-tag") << id3Tag(4, QByteArray(32, '\0')).left(15);
    QTest::newRow("v2.2-frame-overrun") << id3Tag(2, QByteArray("TT2") + be(42, 3) + "\0a");
    QTest::newRow("v2.3-frame-overrun") << id3Tag(3, QByteArray("TIT2") + be(42, 4) + QByteArray(2, '\0') + "\0a");
    QTest::newRow("v2.4-frame-overrun") << id3Tag(4, QByteArray("TIT2") + syncsafe(42) + QByteArray(2, '\0') + "\0a");
    QByteArray frame = id3Frame(4, "TIT2", textPayload(3, "x"));
    frame[4] = char(0x80);
    QTest::newRow("invalid-frame-syncsafe") << id3Tag(4, frame);
    QTest::newRow("short-frame-header") << id3Tag(4, "TIT2");
    QTest::newRow("invalid-frame-identifier") << id3Tag(4, id3Frame(4, "tit2", textPayload(3, "bad")));
    QTest::newRow("truncated-comment-language") << id3Tag(4, id3Frame(4, "COMM", QByteArray::fromHex("03656e")));
    QTest::newRow("invalid-header-flags") << id3Tag(4, {}, 0x01);
    QTest::newRow("nonzero-after-padding") << id3Tag(4, QByteArray::fromHex("000001"));
    QTest::newRow("v3-invalid-text-encoding") << id3Tag(3, id3Frame(3, "TIT2", textPayload(3, "bad")));
    QTest::newRow("invalid-frame-flags") << id3Tag(4, id3Frame(4, "TIT2", textPayload(3, "bad"), 0x10));
    QTest::newRow("compressed-without-length-indicator") << id3Tag(4, id3Frame(4, "TIT2", "bad", 0x08));
    QTest::newRow("wrong-data-length-indicator") << id3Tag(4, id3Frame(4, "TIT2", syncsafe(100) + textPayload(3, "bad"), 0x01));
    QTest::newRow("invalid-extended-header") << id3Tag(4, syncsafe(6) + QByteArray::fromHex("0200"), 0x40);
    QTest::newRow("missing-footer") << id3Tag(4, id3Frame(4, "TIT2", textPayload(3, "bad")), 0x10);
    QByteArray paddedFooter = id3Tag(4, id3Frame(4, "TIT2", textPayload(3, "bad")) + QByteArray(1, '\0'), 0x10);
    QByteArray footer = paddedFooter.left(10);
    footer.replace(0, 3, "3DI");
    QTest::newRow("footer-with-padding") << paddedFooter + footer;
}

void MediaTest::malformedId3()
{
    QFETCH(QByteArray, bytes);
    Document document;
    QString error;
    QVERIFY(!readFixture(bytes, &document, &error));
    QCOMPARE(document.status, ReadStatus::Malformed);
    QVERIFY(!error.isEmpty());
    QVERIFY(!document.complete());
    QVERIFY(document.tags.isEmpty());
    QVERIFY(document.technical.isEmpty());
}

void MediaTest::malformedEncoding_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::newRow("UTF8-truncated") << QByteArray::fromHex("03e4b8");
    QTest::newRow("UTF8-overlong") << QByteArray::fromHex("03c0af");
    QTest::newRow("UTF8-surrogate") << QByteArray::fromHex("03eda080");
    QTest::newRow("UTF8-beyond-range") << QByteArray::fromHex("03f4908080");
    QTest::newRow("UTF16-no-BOM") << QByteArray::fromHex("016100");
    QTest::newRow("UTF16-odd-length") << QByteArray::fromHex("01fffe6100ff");
    QTest::newRow("UTF16-high-surrogate") << QByteArray::fromHex("01fffe00d8");
    QTest::newRow("UTF16-low-surrogate") << QByteArray::fromHex("01fffe00dc");
    QTest::newRow("UTF16BE-high-surrogate") << QByteArray::fromHex("02d800");
    QTest::newRow("UTF16-internal-opposite-BOM") << QByteArray::fromHex("01fffe4100feff4200");
    QTest::newRow("unknown-encoding") << QByteArray::fromHex("0461");
}

void MediaTest::malformedEncoding()
{
    QFETCH(QByteArray, payload);
    Document document;
    QString error;
    QVERIFY(!readFixture(id3Tag(4, id3Frame(4, "TIT2", payload)) + mpegPayload(), &document, &error));
    QCOMPARE(document.status, ReadStatus::Malformed);
    QVERIFY(!error.isEmpty());
}

void MediaTest::malformedFlac_data()
{
    QTest::addColumn<QByteArray>("bytes");
    const QByteArray prefix = "fLaC" + flacBlock(0, flacStreamInfo(), false);
    QTest::newRow("truncated-block-header") << QByteArray::fromHex("664c61438000");
    QTest::newRow("truncated-streaminfo") << ("fLaC" + flacBlock(0, flacStreamInfo(), true)).chopped(1);
    QTest::newRow("short-streaminfo") << ("fLaC" + flacBlock(0, QByteArray(33, '\0'), true));
    QTest::newRow("streaminfo-not-first") << ("fLaC" + flacBlock(4, vorbisComments({}), true));
    QTest::newRow("missing-last-block") << prefix;
    QTest::newRow("vendor-length-overrun") << prefix + flacBlock(4, le32(999) + "x", true);
    QTest::newRow("comment-count-overrun") << prefix + flacBlock(4, le32(0) + le32(1), true);
    QTest::newRow("comment-length-overrun") << prefix + flacBlock(4, le32(0) + le32(1) + le32(999) + "x", true);
    QTest::newRow("invalid-comment-UTF8") << prefix + flacBlock(4, le32(0) + le32(1) + le32(8) + QByteArray::fromHex("5449544c453dc0af"), true);
    QTest::newRow("invalid-vendor-UTF8") << prefix + flacBlock(4, le32(2) + QByteArray::fromHex("c0af") + le32(0), true);
    QTest::newRow("missing-field-equals") << prefix + flacBlock(4, vorbisComments({"TITLE"}), true);
    QTest::newRow("empty-field-name") << prefix + flacBlock(4, vorbisComments({"=value"}), true);
    QTest::newRow("nonascii-field-name") << prefix + flacBlock(4, vorbisComments({QString::fromUtf8("标题=value")}), true);
    QTest::newRow("duplicate-streaminfo") << prefix + flacBlock(0, flacStreamInfo(), true);
    QTest::newRow("duplicate-comment-block") << prefix + flacBlock(4, vorbisComments({}), false) + flacBlock(4, vorbisComments({}), true);
    QTest::newRow("comment-trailing-garbage") << prefix + flacBlock(4, vorbisComments({}) + "garbage", true);
    QTest::newRow("reserved-block-type") << prefix + flacBlock(127, {}, true);
    QTest::newRow("invalid-seektable-length") << prefix + flacBlock(3, "x", true);
    QTest::newRow("zero-sample-rate") << ("fLaC" + flacBlock(0, flacStreamInfo(0), true));
}

void MediaTest::malformedFlac()
{
    QFETCH(QByteArray, bytes);
    Document document;
    QString error;
    QVERIFY(!readFixture(bytes, &document, &error, {}, "flac"));
    QCOMPARE(document.status, ReadStatus::Malformed);
    QVERIFY(!error.isEmpty());
}

void MediaTest::limits_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("suffix");
    QTest::addColumn<int>("kind");
    QTest::newRow("id3-total-bytes") << id3Tag(4, id3Frame(4, "TIT2", textPayload(3, QString(100, 'a')))) << QString("mp3") << 0;
    QTest::newRow("id3-field-bytes") << id3Tag(4, id3Frame(4, "TIT2", textPayload(3, QString(40, 'a')))) << QString("mp3") << 1;
    QTest::newRow("id3-frame-count") << id3Tag(4, id3Frame(4, "TIT2", textPayload(3, "a")) + id3Frame(4, "TPE1", textPayload(3, "b"))) << QString("mp3") << 2;
    QTest::newRow("id3-multivalue-count") << id3Tag(4, id3Frame(4, "TPE1", textPayload(3, QString("A") + QChar(0) + "B" + QChar(0) + "C"))) << QString("mp3") << 4;
    QTest::newRow("id3v1-total-bytes") << (mpegPayload() + id3v1()) << QString("mp3") << 0;
    QTest::newRow("id3v1-field-bytes") << (mpegPayload() + id3v1(QByteArray(20, 'A'))) << QString("mp3") << 5;
    QTest::newRow("id3v1-field-count") << (mpegPayload() + id3v1()) << QString("mp3") << 2;
    QTest::newRow("combined-id3-field-count") << (id3Tag(4, id3Frame(4, "TIT2", textPayload(3, "Title"))) + mpegPayload() + id3v1()) << QString("mp3") << 4;
    QTest::newRow("flac-total-bytes") << flac({"TITLE=" + QString(100, 'a')}) << QString("flac") << 0;
    QTest::newRow("flac-field-bytes") << flac({"TITLE=" + QString(40, 'a')}) << QString("flac") << 1;
    QTest::newRow("flac-comment-count") << flac({"TITLE=a", "ARTIST=b"}) << QString("flac") << 2;
    QTest::newRow("flac-block-count") << flac({"TITLE=a"}) << QString("flac") << 3;
    QTest::newRow("flac-huge-comment-count") << ("fLaC" + flacBlock(0, flacStreamInfo(), false) + flacBlock(4, le32(0) + le32(0xffffffff), true)) << QString("flac") << 2;
}

void MediaTest::limits()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, suffix);
    QFETCH(int, kind);
    ReadLimits limits;
    if (kind == 0) limits.maxTagBytes = 64;
    if (kind == 1) limits.maxFieldBytes = 32;
    if (kind == 2) limits.maxFields = 1;
    if (kind == 3) limits.maxMetadataBlocks = 1;
    if (kind == 4) limits.maxFields = 2;
    if (kind == 5) limits.maxFieldBytes = 16;
    Document document;
    QString error;
    QVERIFY(!readFixture(bytes, &document, &error, limits, suffix));
    QCOMPARE(document.status, ReadStatus::LimitExceeded);
    QVERIFY(!error.isEmpty());
}

void MediaTest::unsupported_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("suffix");
    QTest::newRow("OGG") << QByteArray("OggSxxxxxxxx") << QString("ogg");
    QTest::newRow("WAV") << QByteArray("RIFFxxxxWAVE") << QString("wav");
    QTest::newRow("M4A") << QByteArray::fromHex("00000018667479704d344120") << QString("m4a");
    QTest::newRow("plain-text-with-mp3-extension") << QByteArray("this is not audio") << QString("mp3");
    QTest::newRow("empty") << QByteArray() << QString("mp3");
    QTest::newRow("ID3-unsupported-version") << id3Tag(5, {}) << QString("mp3");
}

void MediaTest::unsupported()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, suffix);
    Document document;
    QString error;
    QVERIFY(!readFixture(bytes, &document, &error, {}, suffix));
    QCOMPARE(document.status, ReadStatus::Unsupported);
    QVERIFY(!document.usable());
    QVERIFY(!error.isEmpty());
}

void MediaTest::ioErrors()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Document document;
    document.tags.insert("title", {"stale"});
    QString error;
    QVERIFY(!load(directory.filePath("missing.mp3"), &document, &error));
    QCOMPARE(document.status, ReadStatus::IoError);
    QVERIFY(document.tags.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(!load(directory.path(), &document, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!load(directory.filePath("missing.mp3"), nullptr, &error));
    QVERIFY(!error.isEmpty());
}

void MediaTest::compareFields()
{
    Document left, right;
    left.status = right.status = ReadStatus::Ready;
    left.fileSize = right.fileSize = 100;
    left.tags = {{"title", {"same"}}, {"artist", {"A", "B"}}, {"album", {"left"}}};
    right.tags = {{"title", {"same"}}, {"artist", {"B", "A"}}, {"date", {"2026"}}};
    const Comparison comparison = compare(left, right, true);
    QVERIFY(comparison.complete);
    QCOMPARE(comparison.fields.size(), 4);
    QCOMPARE(comparison.differenceCount, 3);
    const FieldDifference *title = findField(comparison, "title");
    const FieldDifference *artist = findField(comparison, "artist");
    const FieldDifference *album = findField(comparison, "album");
    const FieldDifference *date = findField(comparison, "date");
    QVERIFY(title && artist && album && date);
    QCOMPARE(title->difference, Difference::Equal);
    QCOMPARE(artist->difference, Difference::Changed);
    QCOMPARE(artist->left, QStringList({"A", "B"}));
    QCOMPARE(artist->right, QStringList({"B", "A"}));
    QCOMPARE(album->difference, Difference::LeftOnly);
    QCOMPARE(date->difference, Difference::RightOnly);
}

void MediaTest::compareTechnicalAndCompleteness()
{
    Document left, right;
    left.status = right.status = ReadStatus::Ready;
    left.fileSize = 100;
    right.fileSize = 101;
    left.tags = right.tags = {{"title", {"same"}}};
    left.technical = {{"sample_rate", {"44100"}}};
    right.technical = {{"sample_rate", {"48000"}}};
    const Comparison withTechnical = compare(left, right);
    QVERIFY(withTechnical.complete);
    QCOMPARE(withTechnical.differenceCount, 2);
    const FieldDifference *rate = findField(withTechnical, "sample_rate", true);
    QVERIFY(rate);
    QCOMPARE(rate->difference, Difference::Changed);
    const Comparison tagsOnly = compare(left, right, true);
    QCOMPARE(tagsOnly.differenceCount, 0);
    QCOMPARE(tagsOnly.fields.size(), 1);
    for (ReadStatus status : {ReadStatus::Partial, ReadStatus::Unsupported, ReadStatus::Malformed,
                              ReadStatus::IoError, ReadStatus::LimitExceeded}) {
        left.status = status;
        QVERIFY(!compare(left, right, true).complete);
    }
}

void MediaTest::readNeverMutates_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("valid-ID3") << id3Tag(4, id3Frame(4, "TIT2", textPayload(3, QString::fromUtf8("只读")))) + mpegPayload();
    QTest::newRow("valid-FLAC") << flac({"TITLE=Readonly"});
    QTest::newRow("malformed") << id3Tag(4, id3Frame(4, "TIT2", QByteArray::fromHex("03ff")));
    QTest::newRow("unsupported") << QByteArray("OggSxxxxxxxx");
}

void MediaTest::readNeverMutates()
{
    QFETCH(QByteArray, bytes);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("readonly.media");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner));
    const QDateTime modified = QFileInfo(path).lastModified();
    Document left, right;
    load(path, &left);
    load(path, &right);
    compare(left, right);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
    QCOMPARE(QFileInfo(path).lastModified(), modified);
    QCOMPARE(file.permissions() & QFileDevice::WriteOwner, QFileDevice::Permissions());
}

void MediaTest::boundedCorruption_data()
{
    QTest::addColumn<QByteArray>("bytes");
    const QVector<QByteArray> originals = {
        id3Tag(4, id3Frame(4, "TIT2", textPayload(3, QString::fromUtf8("中文标题")))
               + id3Frame(4, "TPE1", textPayload(1, QString::fromUtf8("歌手")))) + mpegPayload(),
        flac({QString::fromUtf8("TITLE=中文标题"), "ARTIST=First", "ARTIST=Second"})
    };
    quint32 state = 0x4d454431;
    const auto next = [&state]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    // Fixed seed and bounded 256 samples make every failure reproducible.
    // Arbitrary mutations can still be valid; only actual failures are required
    // to discard all fields. Every sample must preserve the file's bytes.
    for (int i = 0; i < 256; ++i) {
        QByteArray bytes = originals[(i / 2) % originals.size()];
        const bool truncation = i % 2 == 0;
        if (truncation) {
            bytes.truncate(int(next() % quint32(bytes.size())));
        } else {
            const int mutations = 1 + int(next() % 4);
            for (int n = 0; n < mutations; ++n) {
                const int offset = int(next() % quint32(bytes.size()));
                bytes[offset] = char(quint8(bytes[offset]) ^ quint8(1 + next() % 255));
            }
        }
        const QByteArray name = QByteArray(truncation ? "truncate-" : "mutate-") + QByteArray::number(i);
        QTest::newRow(name.constData()) << bytes;
    }
}

void MediaTest::boundedCorruption()
{
    QFETCH(QByteArray, bytes);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath("corrupted.media");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner));
    const QDateTime modified = QFileInfo(path).lastModified();
    Document document;
    document.tags.insert("old", {"stale"});
    document.technical.insert("old", {"stale"});
    QString error;
    if (load(path, &document, &error)) {
        QVERIFY(document.usable());
        QVERIFY(error.isEmpty());
    } else {
        QVERIFY(!document.usable());
        QVERIFY(!error.isEmpty());
        QVERIFY(document.tags.isEmpty());
        QVERIFY(document.technical.isEmpty());
    }
    QVERIFY(!document.tags.contains("old"));
    QVERIFY(!document.technical.contains("old"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), bytes);
    QCOMPARE(QFileInfo(path).lastModified(), modified);
    QCOMPARE(file.permissions() & QFileDevice::WriteOwner, QFileDevice::Permissions());
}

QTEST_GUILESS_MAIN(MediaTest)
#include "tst_media.moc"
