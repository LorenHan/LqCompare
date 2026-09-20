#include <QtTest>
#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <memory>

#include "mediacomparesession.h"
#include "mediacompareview.h"

using namespace LqCompare;

namespace {
QByteArray number(quint64 value, int count, bool little = false, bool syncsafe = false)
{
    QByteArray bytes(count, '\0');
    for (int i = 0; i < count; ++i) {
        bytes[little ? i : count - 1 - i] = char(value & (syncsafe ? 0x7f : 0xff));
        value >>= syncsafe ? 7 : 8;
    }
    return bytes;
}

QByteArray textFrame(const QByteArray &id, const QString &value, int version = 4)
{
    const QByteArray payload = QByteArray(1, version == 4 ? '\3' : '\0')
        + (version == 4 ? value.toUtf8() : value.toLatin1());
    return id + number(payload.size(), 4, false, version == 4) + QByteArray(2, '\0') + payload;
}

QByteArray mp3(const QByteArray &frames, int version = 4, const QByteArray &audio = {})
{
    return QByteArrayLiteral("ID3") + QByteArray(1, char(version)) + QByteArray(2, '\0')
        + number(frames.size(), 4, false, true) + frames + audio;
}

QByteArray id3v1(const QByteArray &title)
{
    QByteArray bytes(128, '\0');
    bytes.replace(0, 3, "TAG");
    bytes.replace(3, qMin(title.size(), 30), title.left(30));
    bytes[127] = char(255);
    return QByteArray::fromHex("fffb9000") + bytes;
}

QByteArray flac(const QList<QByteArray> &comments, int sampleRate = 44100)
{
    QByteArray streamInfo(34, '\0');
    streamInfo.replace(0, 4, QByteArray::fromHex("00100010"));
    const quint64 packed = (quint64(sampleRate) << 44) | (quint64(1) << 41)
        | (quint64(15) << 36) | 88200;
    streamInfo.replace(10, 8, number(packed, 8));
    const QByteArray vendor = QByteArrayLiteral("MediaViewTests");
    QByteArray vorbis = number(vendor.size(), 4, true) + vendor + number(comments.size(), 4, true);
    for (const auto &comment : comments) vorbis += number(comment.size(), 4, true) + comment;
    return QByteArrayLiteral("fLaC") + QByteArray::fromHex("00000022") + streamInfo
        + QByteArray(1, char(0x84)) + number(vorbis.size(), 3) + vorbis;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

int rowFor(QTableView *table, const QString &key, const QString &type = QString())
{
    for (int row = 0; row < table->model()->rowCount(); ++row)
        if (table->model()->index(row, 1).data().toString() == key
            && (type.isEmpty() || table->model()->index(row, 0).data().toString() == type)) return row;
    return -1;
}
QString cell(QTableView *table, int row, int column)
{
    return table->model()->index(row, column).data().toString();
}
}

class MediaViewTests : public QObject
{
    Q_OBJECT
private slots:
    void emptySessionContract();
    void chineseMissingEmptyAndMultivalue();
    void id3v1Latin1NoticeAndReadOnly();
    void ignoreTechnicalAndSettings();
    void failedReloadReplacesBothSides();
    void unsupportedBothNeverMeansEqual();
    void partialComparisonNeverMeansCompleteEquality();
    void flacFieldsAndTechnicalValues();
    void boundedPreviewRetainsFullText();
    void pathControlsAndFailedReadRetry();
    void destroyedSessionDisablesView();
};

void MediaViewTests::emptySessionContract()
{
    MediaCompareSession session;
    QCOMPARE(session.typeId(), QStringLiteral("media"));
    QVERIFY(session.open());
    QVERIFY(!session.hasReadAttempt());
    QVERIFY(!session.canSave());
    QVERIFY(!session.comparison().complete);
    std::unique_ptr<QWidget> widget(session.createWidget());
    QCOMPARE(session.createWidget(), widget.get());
    QVERIFY(qobject_cast<MediaCompareView *>(widget.get()));
    QCOMPARE(widget->findChild<QTableView *>(QStringLiteral("mediaFields"))->model()->rowCount(), 0);
    QVERIFY(!widget->findChild<QPushButton *>(QStringLiteral("mediaReload"))->isEnabled());
    QVERIFY(widget->findChild<QLabel *>(QStringLiteral("mediaSemantics"))->text()
            .contains(QStringLiteral("Equal tags do not mean equal audio content")));
    const QString support = widget->findChild<QLabel *>(QStringLiteral("mediaSupport"))->text();
    QVERIFY(support.contains(QStringLiteral("MP3")));
    QVERIFY(support.contains(QStringLiteral("FLAC")));
    QVERIFY(support.contains(QStringLiteral("not implemented")));
    session.setDirty(true);
    QString error;
    QVERIFY(!session.save(&error));
    QVERIFY(!error.isEmpty());
    session.close();
    QVERIFY(!widget->isEnabled());
    QVERIFY(!session.setIgnoreTechnical(true, &error));
    QVERIFY(!session.setPaths(QStringLiteral("a"), QStringLiteral("b"), &error));
    QVERIFY(!session.open(&error));
}

void MediaViewTests::chineseMissingEmptyAndMultivalue()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString left = temp.filePath(QStringLiteral("左侧.mp3"));
    const QString right = temp.filePath(QStringLiteral("右侧.mp3"));
    const QString artists = QStringLiteral("歌手甲") + QChar(0) + QStringLiteral("歌手乙");
    QVERIFY(writeFile(left, mp3(textFrame("TIT2", QStringLiteral("春江花月夜"))
        + textFrame("TPE1", QString()) + textFrame("TPE2", artists))));
    QVERIFY(writeFile(right, mp3(textFrame("TIT2", QStringLiteral("春江花月夜·现场"))
        + textFrame("TALB", QStringLiteral("专辑")) + textFrame("TPE2", artists))));
    MediaCompareSession session(left, right);
    QVERIFY(session.setIgnoreTechnical(true));
    QVERIFY(session.open());
    std::unique_ptr<QWidget> widget(session.createWidget());
    auto *table = widget->findChild<QTableView *>(QStringLiteral("mediaFields"));
    QCOMPARE(table->model()->rowCount(), session.comparison().fields.size());
    QVERIFY(session.comparison().complete);
    QCOMPARE(session.comparison().differenceCount, 3);
    const int artist = rowFor(table, QStringLiteral("artist"));
    QVERIFY(artist >= 0);
    QCOMPARE(cell(table, artist, 2), QStringLiteral("(empty)"));
    QCOMPARE(cell(table, artist, 3), QStringLiteral("(missing)"));
    QCOMPARE(cell(table, artist, 4), QStringLiteral("Only left"));
    const int title = rowFor(table, QStringLiteral("title"));
    QVERIFY(cell(table, title, 2).contains(QStringLiteral("春江花月夜")));
    QCOMPARE(cell(table, title, 4), QStringLiteral("Changed"));
    const int album = rowFor(table, QStringLiteral("album"));
    QCOMPARE(cell(table, album, 2), QStringLiteral("(missing)"));
    QCOMPARE(cell(table, album, 4), QStringLiteral("Only right"));
    const int multi = rowFor(table, QStringLiteral("album_artist"));
    QCOMPARE(cell(table, multi, 4), QStringLiteral("Match"));
    QVERIFY(cell(table, multi, 2).contains(QStringLiteral("歌手甲")));
    QVERIFY(cell(table, multi, 2).contains(QStringLiteral("歌手乙")));
    table->setCurrentIndex(table->model()->index(multi, 2));
    const QString detail = widget->findChild<QPlainTextEdit *>(QStringLiteral("mediaLeftDetail"))->toPlainText();
    QVERIFY(detail.contains(QStringLiteral("Value 1:\n歌手甲")));
    QVERIFY(detail.contains(QStringLiteral("Value 2:\n歌手乙")));
    for (int row = 0; row < table->model()->rowCount(); ++row)
        for (int column = 0; column < table->model()->columnCount(); ++column)
            QVERIFY(!(table->model()->flags(table->model()->index(row, column)) & Qt::ItemIsEditable));
    QCOMPARE(table->editTriggers(), QAbstractItemView::EditTriggers(QAbstractItemView::NoEditTriggers));
    const QString screenshot = qEnvironmentVariable("MEDIA_VIEW_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        table->setCurrentIndex(table->model()->index(title, 2));
        widget->resize(1100, 800);
        widget->show();
        QApplication::processEvents();
        QVERIFY(widget->grab().save(screenshot));
    }
}

void MediaViewTests::id3v1Latin1NoticeAndReadOnly()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString left = temp.filePath(QStringLiteral("latin1.mp3"));
    const QString right = temp.filePath(QStringLiteral("other.mp3"));
    const QByteArray original = id3v1(QByteArray("caf\xe9"));
    QVERIFY(writeFile(left, original));
    QVERIFY(writeFile(right, id3v1("cafe")));
    MediaCompareSession session(left, right);
    QVERIFY(session.open());
    std::unique_ptr<QWidget> widget(session.createWidget());
    auto *table = widget->findChild<QTableView *>(QStringLiteral("mediaFields"));
    QVERIFY(cell(table, rowFor(table, QStringLiteral("title")), 2).contains(QStringLiteral("café")));
    QVERIFY(widget->findChild<QLabel *>(QStringLiteral("mediaLeftInfo"))->text().contains(QStringLiteral("Latin-1")));
    session.setDirty(true);
    QVERIFY(!session.canSave());
    QVERIFY(!session.save());
    QCOMPARE(readFile(left), original);
    QVERIFY(widget->findChild<QPlainTextEdit *>(QStringLiteral("mediaLeftDetail"))->isReadOnly());
}

void MediaViewTests::ignoreTechnicalAndSettings()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString left = temp.filePath(QStringLiteral("v3.mp3"));
    const QString right = temp.filePath(QStringLiteral("v4.mp3"));
    QVERIFY(writeFile(left, mp3(textFrame("TIT2", QStringLiteral("Same"), 3), 3)));
    QVERIFY(writeFile(right, mp3(textFrame("TIT2", QStringLiteral("Same"), 4), 4)));
    MediaCompareSession session(left, right);
    QVERIFY(session.open());
    std::unique_ptr<QWidget> widget(session.createWidget());
    auto *table = widget->findChild<QTableView *>(QStringLiteral("mediaFields"));
    auto *ignore = widget->findChild<QCheckBox *>(QStringLiteral("mediaIgnoreTechnical"));
    QVERIFY(session.comparison().differenceCount > 0);
    QVERIFY(rowFor(table, QStringLiteral("id3_version"), QStringLiteral("Technical")) >= 0);
    const int size = rowFor(table, QStringLiteral("file_size"));
    QVERIFY(size >= 0);
    QVERIFY(cell(table, size, 2).contains(QString::number(session.leftDocument().fileSize)));
    QVERIFY(!cell(table, size, 2).contains(QStringLiteral("missing")));
    ignore->setChecked(true);
    QVERIFY(session.ignoreTechnical());
    QCOMPARE(session.comparison().differenceCount, 0);
    QCOMPARE(table->model()->rowCount(), 1);
    QVERIFY(session.statusText().contains(QStringLiteral("metadata fields match")));
    QVERIFY(session.statusText().contains(QStringLiteral("Audio content has not been compared")));
    QVERIFY(!session.isDirty());
    QVERIFY(session.sessionSettings()->value(QStringLiteral("media.ignoreTechnical")).toBool());
    QVERIFY(session.sessionSettings()->setValue(QStringLiteral("media.ignoreTechnical"), false));
    QVERIFY(!ignore->isChecked());
    QVERIFY(session.comparison().differenceCount > 0);
    QVERIFY(session.setIgnoreTechnical(true));
    session.sessionSettings()->clear();
    QVERIFY(!session.ignoreTechnical());
    QVERIFY(!ignore->isChecked());
}

void MediaViewTests::failedReloadReplacesBothSides()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString left = temp.filePath(QStringLiteral("left.mp3"));
    const QString right = temp.filePath(QStringLiteral("right.mp3"));
    QVERIFY(writeFile(left, mp3(textFrame("TIT2", QStringLiteral("old left"))
        + textFrame("TALB", QStringLiteral("stale album")))));
    QVERIFY(writeFile(right, mp3(textFrame("TIT2", QStringLiteral("old right")))));
    MediaCompareSession session(left, right);
    QVERIFY(session.open());
    std::unique_ptr<QWidget> widget(session.createWidget());
    auto *table = widget->findChild<QTableView *>(QStringLiteral("mediaFields"));
    QVERIFY(writeFile(left, QByteArrayLiteral("ID3\4")));
    QVERIFY(writeFile(right, mp3(textFrame("TIT2", QStringLiteral("fresh right")))));
    QString error;
    QVERIFY(!session.reload(&error));
    QVERIFY(error.contains(QStringLiteral("Left:")));
    QCOMPARE(session.leftDocument().status, Media::ReadStatus::Malformed);
    QVERIFY(session.leftDocument().tags.isEmpty());
    QCOMPARE(session.rightDocument().tags.value(QStringLiteral("title")), QStringList{QStringLiteral("fresh right")});
    QCOMPARE(session.leftDocument().fileSize, qint64(4));
    QVERIFY(!session.comparison().complete);
    QCOMPARE(rowFor(table, QStringLiteral("album")), -1);
    const int title = rowFor(table, QStringLiteral("title"));
    QCOMPARE(cell(table, title, 2), QStringLiteral("(unavailable)"));
    QVERIFY(cell(table, title, 3).contains(QStringLiteral("fresh right")));
    QCOMPARE(cell(table, title, 4), QStringLiteral("Not comparable"));
    const QString leftInfo = widget->findChild<QLabel *>(QStringLiteral("mediaLeftInfo"))->text();
    QVERIFY(leftInfo.contains(QStringLiteral("4 bytes")));
    QVERIFY(leftInfo.contains(QStringLiteral("Malformed")));
    QVERIFY(session.statusText().contains(QStringLiteral("unavailable")));
    QVERIFY(!session.progress().isActive());
}

void MediaViewTests::unsupportedBothNeverMeansEqual()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString left = temp.filePath(QStringLiteral("left.wav"));
    const QString right = temp.filePath(QStringLiteral("right.wav"));
    QVERIFY(writeFile(left, QByteArrayLiteral("RIFF placeholder")));
    QVERIFY(writeFile(right, QByteArrayLiteral("RIFF placeholder")));
    MediaCompareSession session(left, right);
    std::unique_ptr<QWidget> widget(session.createWidget());
    QString error;
    QVERIFY(!session.open(&error));
    QVERIFY(error.contains(QStringLiteral("Left:")));
    QVERIFY(error.contains(QStringLiteral("Right:")));
    QCOMPARE(session.leftDocument().status, Media::ReadStatus::Unsupported);
    QCOMPARE(session.rightDocument().status, Media::ReadStatus::Unsupported);
    QVERIFY(!session.comparison().complete);
    QVERIFY(session.statusText().contains(QStringLiteral("Comparison unavailable")));
    QVERIFY(!session.statusText().contains(QStringLiteral("metadata fields match")));
    QVERIFY(session.setIgnoreTechnical(true));
    QVERIFY(session.statusText().contains(QStringLiteral("Comparison unavailable")));
    QCOMPARE(widget->findChild<QTableView *>(QStringLiteral("mediaFields"))->model()->rowCount(), 0);
    const QString missing = temp.filePath(QStringLiteral("missing.mp3"));
    QVERIFY(session.setPaths(missing, right));
    QVERIFY(!session.hasReadAttempt());
    QVERIFY(session.leftDocument().tags.isEmpty());
    QVERIFY(!session.open());
    QCOMPARE(session.leftDocument().status, Media::ReadStatus::IoError);
    QCOMPARE(session.rightDocument().status, Media::ReadStatus::Unsupported);
}

void MediaViewTests::partialComparisonNeverMeansCompleteEquality()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("untagged.mp3"));
    QVERIFY(writeFile(path, QByteArray::fromHex("fffb9000")));
    MediaCompareSession session(path, path);
    QVERIFY(session.open());
    QCOMPARE(session.leftDocument().status, Media::ReadStatus::Partial);
    QCOMPARE(session.comparison().differenceCount, 0);
    QVERIFY(!session.comparison().complete);
    QVERIFY(session.statusText().contains(QStringLiteral("Incomplete metadata comparison")));
    QVERIFY(session.statusText().contains(QStringLiteral("Complete tag equality is unknown")));
    std::unique_ptr<QWidget> widget(session.createWidget());
    QVERIFY(widget->findChild<QLabel *>(QStringLiteral("mediaLeftInfo"))->text().contains(QStringLiteral("Partially supported")));
    QVERIFY(session.setIgnoreTechnical(true));
    QVERIFY(!session.statusText().contains(QStringLiteral("metadata fields match")));
}

void MediaViewTests::flacFieldsAndTechnicalValues()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString left = temp.filePath(QStringLiteral("左侧.flac"));
    const QString right = temp.filePath(QStringLiteral("右侧.flac"));
    const QByteArray title = QStringLiteral("TITLE=你好，世界").toUtf8();
    QVERIFY(writeFile(left, flac({title, "ARTIST=", "ARTIST=Two"}, 44100)));
    QVERIFY(writeFile(right, flac({title}, 48000)));
    MediaCompareSession session(left, right);
    QVERIFY(session.open());
    std::unique_ptr<QWidget> widget(session.createWidget());
    auto *table = widget->findChild<QTableView *>(QStringLiteral("mediaFields"));
    const int sampleRate = rowFor(table, QStringLiteral("sample_rate"));
    QVERIFY(sampleRate >= 0);
    QVERIFY(cell(table, sampleRate, 2).contains(QStringLiteral("44100")));
    QVERIFY(cell(table, sampleRate, 3).contains(QStringLiteral("48000")));
    QCOMPARE(cell(table, sampleRate, 4), QStringLiteral("Changed"));
    QCOMPARE(cell(table, rowFor(table, QStringLiteral("title")), 4), QStringLiteral("Match"));
    QVERIFY(session.setIgnoreTechnical(true));
    QCOMPARE(session.comparison().differenceCount, 1);
    QCOMPARE(rowFor(table, QStringLiteral("sample_rate")), -1);
    const int artist = rowFor(table, QStringLiteral("artist"));
    QVERIFY(cell(table, artist, 2).contains(QStringLiteral("(empty)")));
    QCOMPARE(cell(table, artist, 3), QStringLiteral("(missing)"));
}

void MediaViewTests::boundedPreviewRetainsFullText()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("long.mp3"));
    const QString title = QStringLiteral("<b>literal</b>\n\t") + QChar(0x202e) + QString(10000, QLatin1Char('x'));
    QVERIFY(writeFile(path, mp3(textFrame("TIT2", title))));
    MediaCompareSession session(path, path);
    QVERIFY(session.open());
    std::unique_ptr<QWidget> widget(session.createWidget());
    auto *table = widget->findChild<QTableView *>(QStringLiteral("mediaFields"));
    const int row = rowFor(table, QStringLiteral("title"));
    QVERIFY(cell(table, row, 2).size() < 1000);
    QVERIFY(cell(table, row, 2).contains(QStringLiteral("\\n\\t\\u202e")));
    table->setCurrentIndex(table->model()->index(row, 2));
    auto *detail = widget->findChild<QPlainTextEdit *>(QStringLiteral("mediaLeftDetail"));
    QCOMPARE(detail->toPlainText(), QStringLiteral("Value 1:\n") + title);
    QCOMPARE(widget->findChild<QLabel *>(QStringLiteral("mediaLeftInfo"))->textFormat(), Qt::PlainText);
}

void MediaViewTests::pathControlsAndFailedReadRetry()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString left = temp.filePath(QStringLiteral("left.mp3"));
    const QString right = temp.filePath(QStringLiteral("right.mp3"));
    QVERIFY(writeFile(left, QByteArrayLiteral("ID3")));
    QVERIFY(writeFile(right, mp3(textFrame("TIT2", QStringLiteral("Right")))));
    MediaCompareSession session;
    std::unique_ptr<QWidget> widget(session.createWidget());
    widget->findChild<QLineEdit *>(QStringLiteral("mediaLeftPath"))->setText(left);
    widget->findChild<QLineEdit *>(QStringLiteral("mediaRightPath"))->setText(right);
    widget->findChild<QPushButton *>(QStringLiteral("mediaCompare"))->click();
    QCOMPARE(session.state(), CompareSession::State::Failed);
    QVERIFY(!session.leftDocument().usable());
    QVERIFY(session.rightDocument().usable());
    auto *reload = widget->findChild<QPushButton *>(QStringLiteral("mediaReload"));
    QVERIFY(reload->isEnabled());
    QVERIFY(writeFile(left, mp3(textFrame("TIT2", QStringLiteral("Recovered")))));
    reload->click();
    QCOMPARE(session.state(), CompareSession::State::Open);
    QCOMPARE(session.leftDocument().tags.value(QStringLiteral("title")), QStringList{QStringLiteral("Recovered")});
    QVERIFY(writeFile(right, mp3(textFrame("TIT2", QStringLiteral("Reloaded")))));
    reload->click();
    QCOMPARE(session.rightDocument().tags.value(QStringLiteral("title")), QStringList{QStringLiteral("Reloaded")});
    QCOMPARE(session.leftPath(), left);
    QCOMPARE(session.rightPath(), right);
}

void MediaViewTests::destroyedSessionDisablesView()
{
    auto session = std::make_unique<MediaCompareSession>();
    std::unique_ptr<QWidget> widget(session->createWidget());
    session.reset();
    QVERIFY(!widget->isEnabled());
    widget->findChild<QPushButton *>(QStringLiteral("mediaCompare"))->click();
}

QTEST_MAIN(MediaViewTests)
#include "tst_mediaview.moc"
