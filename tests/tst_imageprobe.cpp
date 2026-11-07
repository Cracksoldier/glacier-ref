#include "GlacierTestMain.h"
#include "TestImages.h"

#include "glacier/io/DecoderSelfCheck.h"
#include "glacier/io/ImageDecoder.h"

#include <QGuiApplication>
#include <QObject>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::io;
using namespace glacier::test;

namespace {

bool isRedish(QRgb c)
{
    return qRed(c) > 180 && qGreen(c) < 90 && qBlue(c) < 90;
}

bool isBlueish(QRgb c)
{
    return qBlue(c) > 180 && qRed(c) < 90 && qGreen(c) < 90;
}

// Ordinary (not raw) string literal: moc mis-tokenizes raw strings at file scope.
const char *svgSquare = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"32\">"
                        "<rect width=\"64\" height=\"32\" fill=\"#00ff00\"/></svg>";

} // namespace

class TestImageProbe : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { configureImageAllocationLimit(Limits{}); }

    // The packaged-build self-check (glacier-ref --check-image-formats)
    // covers every allowlisted format and passes with the test's Qt.
    void decoderSelfCheckPasses()
    {
        const QList<FormatCheck> checks = checkDecoders();
        QCOMPARE(checks.size(), 7);
        for (const FormatCheck &check : checks)
            QVERIFY2(check.ok, qPrintable(displayName(check.format) + u": "_s + check.detail));
    }

    void acceptsAllowlistedFormats_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("format");
        QTest::addColumn<QSize>("size");
        const QImage image = orientationPattern();
        QTest::newRow("png") << encode(image, "png") << int(ImageFormat::Png) << QSize(40, 20);
        QTest::newRow("jpeg") << encode(image, "jpeg") << int(ImageFormat::Jpeg) << QSize(40, 20);
        QTest::newRow("webp") << encode(image, "webp") << int(ImageFormat::Webp) << QSize(40, 20);
        QTest::newRow("bmp") << encode(image, "bmp") << int(ImageFormat::Bmp) << QSize(40, 20);
        QTest::newRow("tiff") << encode(image, "tiff") << int(ImageFormat::Tiff) << QSize(40, 20);
        QTest::newRow("gif") << gifFrames(1) << int(ImageFormat::Gif) << QSize(1, 1);
        QTest::newRow("svg") << QByteArray(svgSquare) << int(ImageFormat::Svg) << QSize(64, 32);
    }

    void acceptsAllowlistedFormats()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(int, format);
        QFETCH(QSize, size);
        const ProbeOutcome probe = probeImage(bytes);
        QVERIFY2(probe.info, qPrintable(probe.error));
        QCOMPARE(int(probe.info->format), format);
        QCOMPARE(probe.info->orientedSize, size);
        QVERIFY(probe.notices.isEmpty());

        QString error;
        const QImage decoded = decodeImage(bytes, *probe.info, 0, {}, &error);
        QVERIFY2(!decoded.isNull(), qPrintable(error));
        QCOMPARE(decoded.size(), size);
        QCOMPARE(decoded.colorSpace().isValid() ? decoded.colorSpace() : QColorSpace(QColorSpace::SRgb),
                 QColorSpace(QColorSpace::SRgb));
    }

    void rejectsUnsupportedOrBrokenInput_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("garbage") << QByteArray(2048, 'z');
        QTest::newRow("psd") << psdHeader(); // rejected even where kimageformats can read it
        QTest::newRow("svgz") << QByteArray("\x1f\x8b\x08\x00\x00\x00\x00\x00", 8) + QByteArray(64, 'x');
        QTest::newRow("png header 50000x50000") << pngHeaderOnly(50000, 50000);
        QTest::newRow("png header 0x10") << pngHeaderOnly(0, 10);
        QTest::newRow("svg with script")
            << QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"><script>alert(1)</script></svg>");
    }

    void rejectsUnsupportedOrBrokenInput()
    {
        QFETCH(QByteArray, bytes);
        const ProbeOutcome probe = probeImage(bytes);
        QVERIFY(!probe.info);
        QVERIFY(!probe.error.isEmpty());
    }

    void enforcesByteLimit()
    {
        Limits limits;
        limits.maxAssetBytes = 1000;
        const ProbeOutcome probe = probeImage(encode(QImage(64, 64, QImage::Format_RGB32), "bmp"), limits);
        QVERIFY(!probe.info);
        QVERIFY(probe.error.contains(u"limit"_s));
    }

    void truncatedImageFailsToDecode()
    {
        QImage image(200, 200, QImage::Format_RGB32);
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                image.setPixel(x, y, qRgb(x, y, (x * y) & 0xff));
        const QByteArray png = encode(image, "png");
        const QByteArray truncated = png.left(png.size() / 2);
        const ProbeOutcome probe = probeImage(truncated);
        QVERIFY(probe.info); // the header alone is fine
        QString error;
        QVERIFY(decodeImage(truncated, *probe.info, 0, {}, &error).isNull());
        QVERIFY(!error.isEmpty());
    }

    void reportsAnimationAndPages()
    {
        const ProbeOutcome gif = probeImage(gifFrames(2));
        QVERIFY2(gif.info, qPrintable(gif.error));
        QVERIFY(gif.info->animated);
        QCOMPARE(gif.notices.size(), 1);
        QVERIFY(!decodeImage(gifFrames(2), *gif.info, 0).isNull());

        const ProbeOutcome tiff = probeImage(tiffPages(2));
        QVERIFY2(tiff.info, qPrintable(tiff.error));
        QVERIFY(tiff.info->multiPage);
        QCOMPARE(tiff.notices.size(), 1);
        QCOMPARE(tiff.info->orientedSize, QSize(1, 1));

        const ProbeOutcome single = probeImage(tiffPages(1));
        QVERIFY(single.info);
        QVERIFY(!single.info->multiPage);
        QVERIFY(single.notices.isEmpty());
    }

    void appliesExifOrientation_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        const QImage image = orientationPattern();
        QTest::newRow("jpeg (Qt)") << jpegWithExif(encode(image, "jpeg", 95), 6);
        QTest::newRow("tiff (Qt)") << [&] {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            QImageWriter writer(&buffer, "tiff");
            writer.setTransformation(QImageIOHandler::TransformationRotate90);
            writer.write(image);
            return bytes;
        }();
        QTest::newRow("png eXIf (own)") << pngWithExif(encode(image, "png"), 6);
        QTest::newRow("webp EXIF (own)") << webpWithExif(encode(image, "webp", 100), image.size(), 6);
    }

    void appliesExifOrientation()
    {
        QFETCH(QByteArray, bytes);
        const ProbeOutcome probe = probeImage(bytes);
        QVERIFY2(probe.info, qPrintable(probe.error));
        QCOMPARE(probe.info->orientedSize, QSize(20, 40)); // rotated 90 degrees

        const QImage full = decodeImage(bytes, *probe.info, 0);
        QCOMPARE(full.size(), QSize(20, 40));
        // The red top-left block of the source ends up at the top-right.
        QVERIFY(isRedish(full.pixel(17, 3)));
        QVERIFY(isBlueish(full.pixel(2, 3)));
        QVERIFY(isBlueish(full.pixel(17, 30)));

        const QImage scaled = decodeImage(bytes, *probe.info, 20);
        QCOMPARE(scaled.size(), QSize(10, 20));
    }

    void decodesAtRequestedSize()
    {
        QImage image(400, 200, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        for (const char *format : {"png", "jpeg", "webp", "bmp", "tiff"}) {
            const QByteArray bytes = encode(image, format);
            const ProbeOutcome probe = probeImage(bytes);
            QVERIFY2(probe.info, format);
            QCOMPARE(decodeImage(bytes, *probe.info, 100).size(), QSize(100, 50));
            QCOMPARE(decodeImage(bytes, *probe.info, 1000).size(), QSize(400, 200)); // never upscales
        }
        const QByteArray svg(svgSquare);
        const ProbeOutcome probe = probeImage(svg);
        QVERIFY(probe.info);
        const QImage rendered = decodeImage(svg, *probe.info, 32);
        QCOMPARE(rendered.size(), QSize(32, 16));
        QCOMPARE(QColor(rendered.pixel(16, 8)), QColor(0, 255, 0));
    }

    void preservesAlpha()
    {
        QImage image(8, 8, QImage::Format_ARGB32);
        image.fill(qRgba(255, 128, 0, 100));
        // Lossy WebP (VP8X + ALPH): Qt 6.12 cannot read back its own lossless
        // encoding of a uniform 8x8 image, a degenerate case unrelated to alpha.
        for (const char *format : {"png", "webp", "tiff"}) {
            const QByteArray bytes = encode(image, format, qstrcmp(format, "webp") == 0 ? 90 : -1);
            const ProbeOutcome probe = probeImage(bytes);
            QVERIFY2(probe.info, qPrintable(QString::fromLatin1(format) + u": "_s + probe.error));
            const QImage decoded = decodeImage(bytes, *probe.info, 0);
            QVERIFY2(decoded.hasAlphaChannel(), format);
            QVERIFY2(std::abs(qAlpha(decoded.pixel(4, 4)) - 100) <= 2, format);
        }
    }

    void convertsColorProfilesToSrgb()
    {
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(qRgb(200, 100, 50));
        image.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
        const QByteArray bytes = encode(image, "png");
        const ProbeOutcome probe = probeImage(bytes);
        QVERIFY(probe.info);
        const QImage decoded = decodeImage(bytes, *probe.info, 0);
        QCOMPARE(decoded.colorSpace(), QColorSpace(QColorSpace::SRgb));
        const QRgb px = decoded.pixel(4, 4);
        // Display P3 (200,100,50) is a more saturated colour than sRGB (200,100,50).
        QVERIFY2(qRed(px) > 205 && qBlue(px) < 45, qPrintable(QColor(px).name()));
    }
};

GLACIER_TEST_GUI_MAIN(TestImageProbe)
#include "tst_imageprobe.moc"
