#include "glacier/io/DecoderSelfCheck.h"

#include "glacier/io/ImageDecoder.h"

#include <QBuffer>
#include <QImage>
#include <QImageWriter>

using namespace Qt::StringLiterals;

namespace glacier::io {

namespace {

// 1x1 GIF89a (Qt cannot write GIF), global colour table {red, black}.
const unsigned char TinyGif[] = {
    'G', 'I', 'F', '8', '9', 'a', 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x21, 0xf9, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00, 0x3b,
};

const char *TinySvg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"6\">"
                      "<rect width=\"8\" height=\"6\" fill=\"#3080c0\"/></svg>";

QByteArray sample(ImageFormat format, QString *error)
{
    switch (format) {
    case ImageFormat::Gif:
        return QByteArray(reinterpret_cast<const char *>(TinyGif), sizeof(TinyGif));
    case ImageFormat::Svg:
        return QByteArray(TinySvg);
    default:
        break;
    }
    QImage image(8, 6, QImage::Format_RGB32);
    image.fill(qRgb(48, 128, 192));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, qtFormatName(format));
    if (!writer.write(image)) {
        *error = u"cannot encode a sample: %1"_s.arg(writer.errorString());
        return {};
    }
    return bytes;
}

} // namespace

QList<FormatCheck> checkDecoders()
{
    QList<FormatCheck> results;
    for (const ImageFormat format : {ImageFormat::Png, ImageFormat::Jpeg, ImageFormat::Webp, ImageFormat::Bmp,
                                     ImageFormat::Gif, ImageFormat::Tiff, ImageFormat::Svg}) {
        FormatCheck check{format, false, {}};
        const QByteArray bytes = sample(format, &check.detail);
        if (!bytes.isEmpty()) {
            const ProbeOutcome probe = probeImage(bytes);
            if (!probe.info) {
                check.detail = probe.error;
            } else if (probe.info->format != format) {
                check.detail = u"detected as %1"_s.arg(displayName(probe.info->format));
            } else {
                QString error;
                const QImage decoded = decodeImage(bytes, *probe.info, 0, {}, &error);
                check.ok = !decoded.isNull() && decoded.width() > 0;
                if (!check.ok)
                    check.detail = error.isEmpty() ? u"decoded to an empty image"_s : error;
            }
        }
        results.append(check);
    }
    return results;
}

} // namespace glacier::io
