#include "glacier/io/ImageFormats.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QImageReader>

using namespace Qt::StringLiterals;

namespace glacier::io {

namespace {

// Qt SVG's handler only recognizes some prologues; accept documents whose
// first element is <svg> after an optional BOM, XML declaration, comments, or
// whitespace. DOCTYPEs are rejected later by the SVG validator.
bool looksLikeSvg(const QByteArray &bytes)
{
    const QByteArray head = bytes.left(4096);
    qsizetype i = head.startsWith("\xEF\xBB\xBF") ? 3 : 0;
    while (i < head.size()) {
        while (i < head.size() && QChar::isSpace(uchar(head[i])))
            ++i;
        if (head.mid(i, 5) == "<?xml" || head.mid(i, 4) == "<!--" || head.mid(i, 9) == "<!DOCTYPE") {
            const QByteArray terminator = head.mid(i, 4) == "<!--" ? QByteArray("-->") : QByteArray(">");
            const qsizetype end = head.indexOf(terminator, i);
            if (end < 0)
                return false;
            i = end + terminator.size();
            continue;
        }
        const QByteArray tag = head.mid(i, 5);
        return tag == "<svg " || tag == "<svg>" || tag == "<svg\n" || tag == "<svg\t" || tag == "<svg\r";
    }
    return false;
}

} // namespace

std::optional<ImageFormat> detectFormat(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return std::nullopt;
    if (bytes.startsWith("\x1f\x8b"))
        return std::nullopt; // gzip, e.g. svgz: not supported in v1

    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray name = QImageReader::imageFormat(&buffer).toLower();

    if (name == "png")
        return ImageFormat::Png;
    if (name == "jpeg" || name == "jpg")
        return ImageFormat::Jpeg;
    if (name == "webp")
        return ImageFormat::Webp;
    if (name == "bmp")
        return ImageFormat::Bmp;
    if (name == "gif")
        return ImageFormat::Gif;
    if (name == "tiff" || name == "tif")
        return ImageFormat::Tiff;
    if (name == "svg")
        return ImageFormat::Svg;
    if (name.isEmpty() && looksLikeSvg(bytes))
        return ImageFormat::Svg;
    return std::nullopt;
}

QByteArray qtFormatName(ImageFormat format)
{
    switch (format) {
    case ImageFormat::Png: return "png";
    case ImageFormat::Jpeg: return "jpeg";
    case ImageFormat::Webp: return "webp";
    case ImageFormat::Bmp: return "bmp";
    case ImageFormat::Gif: return "gif";
    case ImageFormat::Tiff: return "tiff";
    case ImageFormat::Svg: return "svg";
    }
    return {};
}

QString mediaType(ImageFormat format)
{
    switch (format) {
    case ImageFormat::Png: return u"image/png"_s;
    case ImageFormat::Jpeg: return u"image/jpeg"_s;
    case ImageFormat::Webp: return u"image/webp"_s;
    case ImageFormat::Bmp: return u"image/bmp"_s;
    case ImageFormat::Gif: return u"image/gif"_s;
    case ImageFormat::Tiff: return u"image/tiff"_s;
    case ImageFormat::Svg: return u"image/svg+xml"_s;
    }
    return {};
}

std::optional<ImageFormat> formatFromMediaType(const QString &type)
{
    for (ImageFormat f : {ImageFormat::Png, ImageFormat::Jpeg, ImageFormat::Webp, ImageFormat::Bmp,
                          ImageFormat::Gif, ImageFormat::Tiff, ImageFormat::Svg}) {
        if (mediaType(f) == type)
            return f;
    }
    return std::nullopt;
}

QString displayName(ImageFormat format)
{
    switch (format) {
    case ImageFormat::Png: return u"PNG"_s;
    case ImageFormat::Jpeg: return u"JPEG"_s;
    case ImageFormat::Webp: return u"WebP"_s;
    case ImageFormat::Bmp: return u"BMP"_s;
    case ImageFormat::Gif: return u"GIF"_s;
    case ImageFormat::Tiff: return u"TIFF"_s;
    case ImageFormat::Svg: return u"SVG"_s;
    }
    return {};
}

QString fileDialogFilter()
{
    return QCoreApplication::translate("glacier::io", "Images")
         + u" (*.png *.jpg *.jpeg *.jpe *.webp *.bmp *.gif *.tif *.tiff *.svg)"_s;
}

} // namespace glacier::io
