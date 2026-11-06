#include "ExifOrientation.h"

#include <QImage>
#include <QTransform>
#include <QtEndian>

namespace glacier::io::detail {

namespace {

quint32 read32(QByteArrayView d, qsizetype pos, bool bigEndian)
{
    const auto *p = reinterpret_cast<const uchar *>(d.data() + pos);
    return bigEndian ? qFromBigEndian<quint32>(p) : qFromLittleEndian<quint32>(p);
}

quint16 read16(QByteArrayView d, qsizetype pos, bool bigEndian)
{
    const auto *p = reinterpret_cast<const uchar *>(d.data() + pos);
    return bigEndian ? qFromBigEndian<quint16>(p) : qFromLittleEndian<quint16>(p);
}

} // namespace

int exifOrientationFromTiff(QByteArrayView exif)
{
    if (exif.startsWith(QByteArrayView("Exif\0\0", 6)))
        exif = exif.sliced(6);
    if (exif.size() < 8)
        return 0;
    bool bigEndian;
    if (exif.startsWith(QByteArrayView("MM\0*", 4)))
        bigEndian = true;
    else if (exif.startsWith(QByteArrayView("II*\0", 4)))
        bigEndian = false;
    else
        return 0;

    const quint32 ifd = read32(exif, 4, bigEndian);
    if (ifd < 8 || qsizetype(ifd) + 2 > exif.size())
        return 0;
    const quint16 count = read16(exif, ifd, bigEndian);
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype entry = qsizetype(ifd) + 2 + qsizetype(i) * 12;
        if (entry + 12 > exif.size())
            return 0;
        if (read16(exif, entry, bigEndian) != 0x0112)
            continue;
        const quint16 type = read16(exif, entry + 2, bigEndian);
        const quint32 n = read32(exif, entry + 4, bigEndian);
        if (type != 3 || n != 1) // SHORT, single value
            return 0;
        const quint16 value = read16(exif, entry + 8, bigEndian);
        return value >= 1 && value <= 8 ? value : 0;
    }
    return 0;
}

QByteArrayView findPngExif(QByteArrayView png)
{
    static constexpr char signature[] = "\x89PNG\r\n\x1a\n";
    if (!png.startsWith(QByteArrayView(signature, 8)))
        return {};
    qsizetype pos = 8;
    while (pos + 12 <= png.size()) {
        const quint32 length = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(png.data() + pos));
        const QByteArrayView type = png.sliced(pos + 4, 4);
        if (qsizetype(length) > png.size() - pos - 12)
            return {};
        if (type == "eXIf")
            return png.sliced(pos + 8, length);
        if (type == "IDAT" || type == "IEND")
            return {}; // eXIf must precede image data to be honored
        pos += 12 + qsizetype(length);
    }
    return {};
}

QByteArrayView findWebpExif(QByteArrayView webp)
{
    if (webp.size() < 12 || !webp.startsWith("RIFF") || webp.sliced(8, 4) != "WEBP")
        return {};
    qsizetype pos = 12;
    while (pos + 8 <= webp.size()) {
        const quint32 length = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(webp.data() + pos + 4));
        if (qsizetype(length) > webp.size() - pos - 8)
            return {};
        if (webp.sliced(pos, 4) == "EXIF")
            return webp.sliced(pos + 8, length);
        pos += 8 + qsizetype(length) + (length & 1);
    }
    return {};
}

QImageIOHandler::Transformations transformationFromExif(int orientation)
{
    switch (orientation) {
    case 2: return QImageIOHandler::TransformationMirror;
    case 3: return QImageIOHandler::TransformationRotate180;
    case 4: return QImageIOHandler::TransformationFlip;
    case 5: return QImageIOHandler::TransformationFlipAndRotate90;
    case 6: return QImageIOHandler::TransformationRotate90;
    case 7: return QImageIOHandler::TransformationMirrorAndRotate90;
    case 8: return QImageIOHandler::TransformationRotate270;
    default: return QImageIOHandler::TransformationNone;
    }
}

QImage applyTransformation(const QImage &image, QImageIOHandler::Transformations transformation)
{
    QImage result = image;
    Qt::Orientations flips;
    if (transformation & QImageIOHandler::TransformationMirror)
        flips |= Qt::Horizontal;
    if (transformation & QImageIOHandler::TransformationFlip)
        flips |= Qt::Vertical;
    if (flips)
        result = result.flipped(flips);
    if (transformation & QImageIOHandler::TransformationRotate90)
        result = result.transformed(QTransform().rotate(90));
    return result;
}

} // namespace glacier::io::detail
