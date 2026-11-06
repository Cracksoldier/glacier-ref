// Verifies that the Qt build and its runtime plugins support every v1 input
// format. Catches CI or packaged Qt installs missing Qt Image Formats (WebP,
// TIFF) or Qt SVG.

#include "GlacierTestMain.h"

#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QObject>

using namespace Qt::StringLiterals;

namespace {

QImage makePattern()
{
    QImage image(8, 6, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgba(x * 30, y * 40, 200, 255));
    return image;
}

// 1x1 GIF89a, global colour table {red, black}, pixel index 0 (red).
const unsigned char TinyGif[] = {
    'G', 'I', 'F', '8', '9', 'a', 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x21, 0xf9, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00, 0x3b,
};

} // namespace

class TestImagePlugins : public QObject
{
    Q_OBJECT

private slots:
    void requiredFormatsAvailable_data()
    {
        QTest::addColumn<QByteArray>("format");
        for (const char *format : {"png", "jpeg", "webp", "bmp", "gif", "tiff", "svg"})
            QTest::newRow(format) << QByteArray(format);
    }

    void requiredFormatsAvailable()
    {
        QFETCH(QByteArray, format);
        QVERIFY2(QImageReader::supportedImageFormats().contains(format),
                 "missing Qt image plugin: " + format);
    }

    void encodeDecodeRoundTrip_data()
    {
        QTest::addColumn<QByteArray>("format");
        QTest::addColumn<bool>("lossless");
        QTest::newRow("png") << QByteArray("png") << true;
        QTest::newRow("bmp") << QByteArray("bmp") << true;
        QTest::newRow("tiff") << QByteArray("tiff") << true;
        QTest::newRow("jpeg") << QByteArray("jpeg") << false;
        QTest::newRow("webp") << QByteArray("webp") << false;
    }

    void encodeDecodeRoundTrip()
    {
        QFETCH(QByteArray, format);
        QFETCH(bool, lossless);

        const QImage source = makePattern();
        QByteArray bytes;
        {
            QBuffer buffer(&bytes);
            QVERIFY(buffer.open(QIODevice::WriteOnly));
            QImageWriter writer(&buffer, format);
            QVERIFY2(writer.write(source), qPrintable(writer.errorString()));
        }

        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        QImageReader reader(&buffer); // content sniffing, no format hint
        QCOMPARE(reader.format(), format);
        const QImage decoded = reader.read();
        QVERIFY2(!decoded.isNull(), qPrintable(reader.errorString()));
        QCOMPARE(decoded.size(), source.size());
        if (lossless) {
            QCOMPARE(decoded.convertToFormat(QImage::Format_ARGB32),
                     source.convertToFormat(QImage::Format_ARGB32));
        }
    }

    void decodesGif()
    {
        const QByteArray bytes(reinterpret_cast<const char *>(TinyGif), sizeof TinyGif);
        QImage image;
        QVERIFY(image.loadFromData(bytes, "gif"));
        QCOMPARE(image.size(), QSize(1, 1));
        QCOMPARE(QColor(image.pixel(0, 0)), QColor(255, 0, 0));
    }

    void decodesSvg()
    {
        const QByteArray svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4">)"
                               R"(<rect width="4" height="4" fill="#00ff00"/></svg>)";
        QImage image;
        QVERIFY(image.loadFromData(svg, "svg"));
        QCOMPARE(image.size(), QSize(4, 4));
        QCOMPARE(QColor(image.pixel(2, 2)), QColor(0, 255, 0));
    }
};

GLACIER_TEST_GUI_MAIN(TestImagePlugins)
#include "tst_imageplugins.moc"
