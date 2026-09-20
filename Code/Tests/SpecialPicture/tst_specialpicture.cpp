#include <QtTest>
#include <QComboBox>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtEndian>

#include "picturediff.h"
#include "picturecomparesession.h"
#include "picturecompareview.h"

using namespace LqCompare;

namespace {
QImage solid(QSize size, QRgb color)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(color);
    return image;
}
QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
bool writeAll(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QString screenshotPath(const QString &name)
{
    const QString path = QDir::tempPath() + QStringLiteral("/lqcompare-special-picture-") + name + QStringLiteral(".png");
    qInfo().noquote() << "Screenshot:" << path;
    return path;
}
}

class SpecialPictureTests : public QObject {
    Q_OBJECT
private slots:
    void equalAndSinglePixel();
    void toleranceBoundary();
    void transparencyModes();
    void missingPixelsAndUnion();
    void invalidArgumentsAreTransactional();
    void pngRoundTripAndMetadata();
    void formatByContent();
    void unknownAndDamagedFiles();
    void limitsBeforeDecode();
    void animationRejected();
    void apngRejected();
    void readOnlySessionAndAtomicReload();
    void emptySessionAndPathSelection();
    void settingsRecompute();
    void viewModesZoomAndSync();
    void viewOpenError();
};

void SpecialPictureTests::equalAndSinglePixel()
{
    QImage left = solid({4, 3}, qRgba(10, 20, 30, 255)), right = left;
    Picture::Result result;
    QString error;
    QVERIFY2(Picture::compare(left, right, &result, &error), qPrintable(error));
    QCOMPARE(result.totalPixels, qint64(12));
    QCOMPARE(result.differentPixels, qint64(0));
    QVERIFY(result.differenceBounds.isEmpty());
    right.setPixel(2, 1, qRgba(20, 20, 30, 255));
    QVERIFY(Picture::compare(left, right, &result));
    QCOMPARE(result.differentPixels, qint64(1));
    QCOMPARE(result.differenceBounds, QRect(2, 1, 1, 1));
    QCOMPARE(result.differenceImage.pixel(2, 1), qRgb(10, 0, 0));
    QCOMPARE(result.differenceImage.pixel(0, 0), qRgb(0, 0, 0));
}

void SpecialPictureTests::toleranceBoundary()
{
    const QImage left = solid({1, 1}, qRgb(10, 20, 30));
    const QImage right = solid({1, 1}, qRgb(15, 26, 37));
    Picture::Result result;
    Picture::CompareOptions options;
    options.channelTolerance = 6;
    QVERIFY(Picture::compare(left, right, &result, nullptr, options));
    QCOMPARE(result.differentPixels, qint64(1));
    options.channelTolerance = 7;
    QVERIFY(Picture::compare(left, right, &result, nullptr, options));
    QCOMPARE(result.differentPixels, qint64(0));
}

void SpecialPictureTests::transparencyModes()
{
    QImage left = solid({2, 1}, qRgba(10, 20, 30, 0));
    QImage right = left;
    right.setPixel(0, 0, qRgba(10, 20, 30, 100));
    right.setPixel(1, 0, qRgba(100, 20, 30, 0)); // invisible RGB still matters
    Picture::Result result;
    Picture::CompareOptions options;
    QVERIFY(Picture::compare(left, right, &result, nullptr, options));
    QCOMPARE(result.differentPixels, qint64(2));
    QCOMPARE(result.differenceImage.pixel(0, 0), qRgb(100, 0, 100));
    options.alphaMode = Picture::AlphaMode::Ignore;
    QVERIFY(Picture::compare(left, right, &result, nullptr, options));
    QCOMPARE(result.differentPixels, qint64(1));
    QCOMPARE(result.differenceBounds, QRect(1, 0, 1, 1));
    options.alphaMode = Picture::AlphaMode::Only;
    QVERIFY(Picture::compare(left, right, &result, nullptr, options));
    QCOMPARE(result.differentPixels, qint64(1));
    QCOMPARE(result.differenceBounds, QRect(0, 0, 1, 1));
}

void SpecialPictureTests::missingPixelsAndUnion()
{
    const QImage left = solid({3, 1}, qRgba(0, 0, 0, 0));
    const QImage right = solid({1, 3}, qRgba(0, 0, 0, 0));
    Picture::Result result;
    Picture::CompareOptions options;
    options.channelTolerance = 255; // absence is independent of tolerance/alpha
    options.alphaMode = Picture::AlphaMode::Only;
    QVERIFY(Picture::compare(left, right, &result, nullptr, options));
    QCOMPARE(result.canvasSize, QSize(3, 3));
    QCOMPARE(result.totalPixels, qint64(5));
    QCOMPARE(result.differentPixels, qint64(4));
    QCOMPARE(result.leftOnlyPixels, qint64(2));
    QCOMPARE(result.rightOnlyPixels, qint64(2));
    QCOMPARE(result.differencePercent(), 80.0);
    QCOMPARE(result.differenceImage.pixel(2, 2), qRgba(0, 0, 0, 0));
    QCOMPARE(result.differenceImage.pixel(2, 0), qRgb(255, 160, 0));
    QCOMPARE(result.differenceImage.pixel(0, 2), qRgb(0, 200, 255));
}

void SpecialPictureTests::invalidArgumentsAreTransactional()
{
    Picture::Result result;
    result.differentPixels = 42;
    QString error;
    QImage image = solid({4, 4}, qRgb(1, 2, 3));
    QVERIFY(!Picture::compare({}, image, &result, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(result.differentPixels, qint64(42));
    Picture::CompareOptions options;
    options.channelTolerance = 256;
    QVERIFY(!Picture::compare(image, image, &result, &error, options));
    options.channelTolerance = 0;
    options.alphaMode = static_cast<Picture::AlphaMode>(8);
    QVERIFY(!Picture::compare(image, image, &result, &error, options));
    Picture::Limits limits;
    limits.maximumPixels = 4;
    QVERIFY(!Picture::compare(image, image, &result, &error, {}, limits));
    QCOMPARE(result.differentPixels, qint64(42));
    QCOMPARE(image.pixel(0, 0), qRgb(1, 2, 3));
}

void SpecialPictureTests::pngRoundTripAndMetadata()
{
    QTemporaryDir dir;
    QImage image = solid({12, 10}, qRgba(20, 30, 40, 0));
    image.setDotsPerMeterX(3780); image.setDotsPerMeterY(3780);
    const QString path = dir.filePath(QStringLiteral("image.png"));
    QVERIFY(image.save(path));
    Picture::Document document;
    QString error;
    QVERIFY2(Picture::load(path, &document, &error), qPrintable(error));
    QCOMPARE(document.originalSize, QSize(12, 10));
    QCOMPARE(document.format, QByteArray("PNG"));
    QVERIFY(document.hasAlpha);
    QVERIFY(std::abs(document.horizontalDpi - 96) < 0.1);
    QCOMPARE(document.image.pixel(0, 0), qRgba(20, 30, 40, 0));
    QCOMPARE(document.image.devicePixelRatio(), 1.0);
    QCOMPARE(document.image.format(), QImage::Format_ARGB32);
}

void SpecialPictureTests::formatByContent()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("misleading.dat"));
    QVERIFY(solid({2, 3}, qRgb(5, 6, 7)).save(path, "PNG"));
    Picture::Document document;
    QString error;
    QVERIFY2(Picture::load(path, &document, &error), qPrintable(error));
    QCOMPARE(document.format, QByteArray("PNG"));
    QCOMPARE(document.originalSize, QSize(2, 3));
}

void SpecialPictureTests::unknownAndDamagedFiles()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("broken.png"));
    Picture::Document document;
    document.path = QStringLiteral("sentinel");
    QString error;
    QVERIFY(writeAll(path, QByteArray("not an image")));
    QVERIFY(!Picture::load(path, &document, &error));
    QVERIFY(error.contains(path));
    QCOMPARE(document.path, QStringLiteral("sentinel"));
    QVERIFY(writeAll(path, QByteArray::fromHex("89504e470d0a1a0a0000000d4948445200000001")));
    QVERIFY(!Picture::load(path, &document, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!Picture::load(dir.path(), &document, &error));
    QVERIFY(!Picture::load(dir.filePath(QStringLiteral("absent.png")), &document, &error));
}

void SpecialPictureTests::limitsBeforeDecode()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("huge.bmp"));
    QByteArray bmp;
    QDataStream data(&bmp, QIODevice::WriteOnly);
    data.setByteOrder(QDataStream::LittleEndian);
    data.writeRawData("BM", 2);
    data << quint32(54) << quint16(0) << quint16(0) << quint32(54)
         << quint32(40) << qint32(8000) << qint32(8000) << quint16(1) << quint16(24)
         << quint32(0) << quint32(0) << qint32(0) << qint32(0) << quint32(0) << quint32(0);
    QVERIFY(writeAll(path, bmp));
    Picture::Document document;
    QString error;
    QVERIFY(!Picture::load(path, &document, &error));
    QVERIFY2(error.contains(QStringLiteral("exceed")), qPrintable(error));
    const QString small = dir.filePath(QStringLiteral("small.png"));
    QVERIFY(solid({3, 3}, qRgb(0, 0, 0)).save(small));
    Picture::Limits limits; limits.maximumFileBytes = 1;
    QVERIFY(!Picture::load(small, &document, &error, limits));
    QVERIFY(error.contains(QStringLiteral("input limit")));
}

void SpecialPictureTests::animationRejected()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("animated.gif"));
    // Two 1×1 GIF89a frames; generated inline so no binary fixture is required.
    const QByteArray header = QByteArray::fromHex("47494638396101000100800000000000ffffff");
    const QByteArray frame = QByteArray::fromHex("21f90400000000002c0000000001000100000202440100");
    QVERIFY(writeAll(path, header + frame + frame + QByteArray(1, char(0x3b))));
    Picture::Document document;
    QString error;
    QVERIFY(!Picture::load(path, &document, &error));
    QVERIFY2(error.contains(QStringLiteral("frame")), qPrintable(error));
    const QString mpo = dir.filePath(QStringLiteral("multi.mpo"));
    QVERIFY(solid({2, 2}, qRgb(10, 20, 30)).save(mpo, "JPEG"));
    QByteArray jpeg = readAll(mpo);
    jpeg.insert(2, QByteArray::fromHex("ffe200064d504600")); // MPF APP2 signature
    QVERIFY(writeAll(mpo, jpeg));
    QVERIFY(!Picture::load(mpo, &document, &error));
    QVERIFY2(error.contains(QStringLiteral("JPEG/MPO")), qPrintable(error));
}

void SpecialPictureTests::apngRejected()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("animated.png"));
    QVERIFY(solid({1, 1}, qRgb(0, 0, 0)).save(path));
    QByteArray png = readAll(path);
    // A valid acTL with 2 frames and 0 loops. No decoder must see first-frame-only
    // success even when Qt's PNG plugin does not advertise APNG support.
    png.insert(33, QByteArray::fromHex("000000086163544c0000000200000000f38d9370"));
    QVERIFY(writeAll(path, png));
    Picture::Document document;
    QString error;
    QVERIFY(!Picture::load(path, &document, &error));
    QVERIFY2(error.contains(QStringLiteral("Animated PNG")), qPrintable(error));
}

void SpecialPictureTests::readOnlySessionAndAtomicReload()
{
    QTemporaryDir dir;
    const QString left = dir.filePath(QStringLiteral("left.png")), right = dir.filePath(QStringLiteral("right.png"));
    QVERIFY(solid({3, 2}, qRgb(10, 20, 30)).save(left));
    QVERIFY(solid({3, 2}, qRgb(10, 20, 30)).save(right));
    const QByteArray beforeLeft = readAll(left), beforeRight = readAll(right);
    PictureCompareSession session(left, right);
    QString error;
    QVERIFY2(session.open(&error), qPrintable(error));
    QCOMPARE(session.typeId(), QStringLiteral("picture"));
    QVERIFY(!session.canSave());
    session.setDirty(true);
    QVERIFY(!session.canSave());
    QVERIFY(!session.save(&error));
    session.setDirty(false);
    QVERIFY(!session.setPaths(left, dir.filePath(QStringLiteral("missing.png")), &error));
    QCOMPARE(session.rightPath(), right);
    QCOMPARE(session.comparison().differentPixels, qint64(0));
    QCOMPARE(readAll(left), beforeLeft); QCOMPARE(readAll(right), beforeRight);
    QVERIFY(writeAll(right, QByteArray("broken")));
    QVERIFY(!session.reload(&error));
    QCOMPARE(session.state(), CompareSession::State::Open);
    QCOMPARE(session.rightDocument().image.pixel(0, 0), qRgb(10, 20, 30));
    QVERIFY(solid({3, 2}, qRgb(40, 20, 30)).save(right));
    QVERIFY(session.reload(&error));
    QCOMPARE(session.comparison().differentPixels, qint64(6));
    session.close();
    QVERIFY(session.leftDocument().image.isNull());
    QVERIFY(!session.setPaths(left, right, &error));
}

void SpecialPictureTests::emptySessionAndPathSelection()
{
    PictureCompareSession session;
    QVERIFY(session.open());
    QCOMPARE(session.state(), CompareSession::State::Open);
    std::unique_ptr<QWidget> view(session.createWidget());
    QVERIFY(view);
    QVERIFY(session.statusText().contains(QStringLiteral("Choose")));
    QVERIFY(!session.setPaths(QStringLiteral("missing"), QString(), nullptr));
    QVERIFY(session.leftPath().isEmpty());
}

void SpecialPictureTests::settingsRecompute()
{
    QTemporaryDir dir;
    const QString left = dir.filePath(QStringLiteral("left.png")), right = dir.filePath(QStringLiteral("right.png"));
    QVERIFY(solid({2, 2}, qRgba(10, 20, 30, 0)).save(left));
    QVERIFY(solid({2, 2}, qRgba(10, 20, 30, 4)).save(right));
    PictureCompareSession session(left, right);
    QVERIFY(session.open());
    QCOMPARE(session.comparison().differentPixels, qint64(4));
    QVERIFY(session.sessionSettings()->setValue(QStringLiteral("picture.channelTolerance"), 4));
    QCOMPARE(session.comparison().differentPixels, qint64(0));
    Picture::CompareOptions options;
    options.alphaMode = Picture::AlphaMode::Ignore;
    QVERIFY(session.setComparisonOptions(options));
    QCOMPARE(session.sessionSettings()->value(QStringLiteral("picture.alphaMode")).toInt(), 1);
    QCOMPARE(session.comparison().differentPixels, qint64(0));
    QVERIFY(!session.isDirty());
}

void SpecialPictureTests::viewModesZoomAndSync()
{
    QTemporaryDir dir;
    const QString left = dir.filePath(QStringLiteral("left.png")), right = dir.filePath(QStringLiteral("right.png"));
    QImage a = solid({600, 400}, qRgba(30, 110, 180, 255));
    QImage b = a;
    for (int y = 60; y < 150; ++y) for (int x = 100; x < 200; ++x) b.setPixel(x, y, qRgba(230, 80, 60, 140));
    QVERIFY(a.save(left)); QVERIFY(b.save(right));
    PictureCompareSession session(left, right);
    QVERIFY(session.open());
    std::unique_ptr<PictureCompareView> view(qobject_cast<PictureCompareView *>(session.createWidget()));
    QVERIFY(view);
    view->resize(1080, 700); view->show();
    QTest::qWait(20);
    view->setZoomFactor(2.0);
    QCOMPARE(view->zoomFactor(), 2.0);
    auto *leftScroll = view->findChild<QScrollArea *>(QStringLiteral("pictureLeftScroll"));
    auto *rightScroll = view->findChild<QScrollArea *>(QStringLiteral("pictureRightScroll"));
    QVERIFY(leftScroll && rightScroll);
    leftScroll->horizontalScrollBar()->setValue(50);
    leftScroll->verticalScrollBar()->setValue(60);
    QCOMPARE(rightScroll->horizontalScrollBar()->value(), 50);
    QCOMPARE(rightScroll->verticalScrollBar()->value(), 60);
    auto *mode = view->findChild<QComboBox *>(QStringLiteral("pictureMode"));
    QVERIFY(mode); mode->setCurrentIndex(1); QCOMPARE(view->zoomFactor(), 2.0);
    QVERIFY(view->grab().save(screenshotPath(QStringLiteral("overlay"))));
    mode->setCurrentIndex(2); QCOMPARE(view->zoomFactor(), 2.0);
    QVERIFY(view->grab().save(screenshotPath(QStringLiteral("difference"))));
    mode->setCurrentIndex(0); QCOMPARE(view->zoomFactor(), 2.0);
    view->fitToWindow(); QVERIFY(view->zoomFactor() < 1.0);
    auto *alpha = view->findChild<QComboBox *>(QStringLiteral("pictureAlpha"));
    QVERIFY(alpha); alpha->setCurrentIndex(2);
    QCOMPARE(session.comparisonOptions().alphaMode, Picture::AlphaMode::Only);
    auto *tolerance = view->findChild<QSpinBox *>(QStringLiteral("pictureTolerance"));
    QVERIFY(tolerance); tolerance->setValue(255);
    QCOMPARE(session.comparison().differentPixels, qint64(0));
    tolerance->setValue(0); alpha->setCurrentIndex(0);
    const QPixmap screenshot = view->grab();
    QVERIFY(!screenshot.isNull());
    QVERIFY(screenshot.save(screenshotPath(QStringLiteral("ui"))));
}

void SpecialPictureTests::viewOpenError()
{
    PictureCompareSession session;
    QVERIFY(session.open());
    std::unique_ptr<QWidget> view(session.createWidget());
    auto *left = view->findChild<QLineEdit *>(QStringLiteral("pictureLeftPath"));
    auto *button = view->findChild<QPushButton *>(QStringLiteral("pictureOpen"));
    auto *status = view->findChild<QLabel *>(QStringLiteral("pictureStatus"));
    QVERIFY(left && button && status);
    left->setText(QStringLiteral("/missing/image.png"));
    button->click();
    QVERIFY(status->text().startsWith(QStringLiteral("Error:")));
    QVERIFY(session.leftDocument().image.isNull());
}

QTEST_MAIN(SpecialPictureTests)
#include "tst_specialpicture.moc"
