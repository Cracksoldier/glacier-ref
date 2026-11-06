#include "glacier/io/ImageDecoder.h"

#include "ExifOrientation.h"
#include "glacier/io/SvgValidator.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QImageReader>
#include <QPainter>
#include <QSemaphore>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace glacier::io {

namespace {

constexpr int DefaultSvgSide = 512;
constexpr int MaxSvgIntrinsicSide = 16384;
constexpr qint64 LargeDecodePixels = qint64(16) * 1000 * 1000;

QString tr(const char *text)
{
    return QCoreApplication::translate("glacier::io", text);
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

bool qtAppliesOrientation(ImageFormat format)
{
    // Measured with Qt 6.12 (see ExifOrientation.h).
    return format == ImageFormat::Jpeg || format == ImageFormat::Tiff;
}

QImageIOHandler::Transformations ownOrientation(const QByteArray &bytes, ImageFormat format)
{
    QByteArrayView exif;
    if (format == ImageFormat::Png)
        exif = detail::findPngExif(bytes);
    else if (format == ImageFormat::Webp)
        exif = detail::findWebpExif(bytes);
    return exif.isEmpty() ? QImageIOHandler::TransformationNone
                          : detail::transformationFromExif(detail::exifOrientationFromTiff(exif));
}

QSize fitWithin(QSize size, int maxSide)
{
    const int longest = std::max(size.width(), size.height());
    if (maxSide <= 0 || longest <= maxSide)
        return size;
    const double scale = double(maxSide) / longest;
    return QSize(std::max(1, int(std::lround(size.width() * scale))),
                 std::max(1, int(std::lround(size.height() * scale))));
}

QImage toDisplayImage(QImage image)
{
    if (image.isNull())
        return image;
    const QImage::Format target = image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied
                                                          : QImage::Format_RGB32;
    if (image.format() != target && image.format() != QImage::Format_ARGB32)
        image.convertTo(target);
    const QColorSpace space = image.colorSpace();
    if (space.isValid() && space != QColorSpace(QColorSpace::SRgb))
        image.convertToColorSpace(QColorSpace(QColorSpace::SRgb));
    if (image.format() != target)
        image.convertTo(target);
    return image;
}

// At most one large raw decode at a time bounds peak memory (spec §4, §9).
QSemaphore &largeDecodePermit()
{
    static QSemaphore permit(1);
    return permit;
}

ProbeOutcome probeSvg(const QByteArray &bytes, const Limits &limits)
{
    ProbeOutcome outcome;
    const SvgValidation validation = validateSvg(bytes, limits);
    if (!validation.ok) {
        outcome.error = tr("Unsupported SVG: %1").arg(validation.error);
        return outcome;
    }
    QSvgRenderer renderer;
    renderer.setOptions(QtSvg::DisableAnimations);
    if (!renderer.load(bytes) || !renderer.isValid()) {
        outcome.error = tr("The SVG file could not be read.");
        return outcome;
    }
    QSize size = renderer.defaultSize();
    if (!size.isValid() || size.isEmpty())
        size = QSize(DefaultSvgSide, DefaultSvgSide);
    size = fitWithin(size, MaxSvgIntrinsicSide);

    ImageInfo info;
    info.format = ImageFormat::Svg;
    info.orientedSize = size;
    info.animated = validation.animated;
    if (info.animated)
        outcome.notices.append(tr("Animation is not supported; the SVG is shown as a still image."));
    outcome.info = info;
    return outcome;
}

QImage renderSvg(const QByteArray &bytes, const ImageInfo &info, int maxSide, const Limits &limits,
                 QString *error)
{
    const SvgValidation validation = validateSvg(bytes, limits); // defense in depth
    if (!validation.ok) {
        setError(error, tr("Unsupported SVG: %1").arg(validation.error));
        return {};
    }
    QSvgRenderer renderer;
    renderer.setOptions(QtSvg::DisableAnimations);
    if (!renderer.load(bytes)) {
        setError(error, tr("The SVG file could not be read."));
        return {};
    }
    const QSize size = fitWithin(info.orientedSize, std::min(maxSide > 0 ? maxSide : MaxDecodeSide, MaxDecodeSide));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(size)));
    painter.end();
    return image;
}

} // namespace

void configureImageAllocationLimit(const Limits &limits)
{
    const qint64 megabytes = limits.maxDecodedPixels * 4 / (1024 * 1024) + 64;
    QImageReader::setAllocationLimit(int(std::min<qint64>(megabytes, 4096)));
}

ProbeOutcome probeImage(const QByteArray &bytes, const Limits &limits)
{
    ProbeOutcome outcome;
    if (bytes.isEmpty()) {
        outcome.error = tr("The file is empty.");
        return outcome;
    }
    if (bytes.size() > limits.maxAssetBytes) {
        outcome.error = tr("The file is larger than the %1 MiB limit.").arg(limits.maxAssetBytes / (1024 * 1024));
        return outcome;
    }
    const std::optional<ImageFormat> format = detectFormat(bytes);
    if (!format) {
        outcome.error = tr("Unsupported file format. Supported: PNG, JPEG, WebP, BMP, GIF, TIFF, SVG.");
        return outcome;
    }
    if (*format == ImageFormat::Svg)
        return probeSvg(bytes, limits);

    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, qtFormatName(*format));
    reader.setAutoTransform(qtAppliesOrientation(*format));

    const QSize raw = reader.size();
    if (!raw.isValid() || raw.isEmpty()) {
        outcome.error = tr("The image header could not be read.");
        return outcome;
    }
    if (qint64(raw.width()) * raw.height() > limits.maxDecodedPixels) {
        outcome.error = tr("The image is %1 x %2 pixels, above the %3 megapixel limit.")
                            .arg(raw.width()).arg(raw.height()).arg(limits.maxDecodedPixels / 1000000);
        return outcome;
    }

    const QImageIOHandler::Transformations transformation =
        qtAppliesOrientation(*format) ? reader.transformation() : ownOrientation(bytes, *format);

    ImageInfo info;
    info.format = *format;
    info.orientedSize = (transformation & QImageIOHandler::TransformationRotate90) ? raw.transposed() : raw;
    // Count frames/pages with a separate reader: Qt 6.12's handlers are
    // order-sensitive (TIFF reports one page after transformation(); WebP
    // fails size() after imageCount()).
    QBuffer countBuffer;
    countBuffer.setData(bytes);
    countBuffer.open(QIODevice::ReadOnly);
    const int count = QImageReader(&countBuffer, qtFormatName(*format)).imageCount();
    if ((*format == ImageFormat::Gif || *format == ImageFormat::Webp) && count > 1) {
        info.animated = true;
        outcome.notices.append(tr("Animation is not supported; the first frame was imported."));
    }
    if (*format == ImageFormat::Tiff && count > 1) {
        info.multiPage = true;
        outcome.notices.append(tr("The TIFF has %1 pages; the first page was imported.").arg(count));
    }
    outcome.info = info;
    return outcome;
}

QImage decodeImage(const QByteArray &bytes, const ImageInfo &info, int maxSide, const Limits &limits,
                   QString *error)
{
    if (info.format == ImageFormat::Svg)
        return renderSvg(bytes, info, maxSide, limits, error);

    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, qtFormatName(info.format));
    const bool qtOrients = qtAppliesOrientation(info.format);
    reader.setAutoTransform(qtOrients);

    const QSize raw = reader.size();
    if (!raw.isValid() || raw.isEmpty() || qint64(raw.width()) * raw.height() > limits.maxDecodedPixels) {
        setError(error, tr("The image header could not be read."));
        return {};
    }
    const QImageIOHandler::Transformations transformation =
        qtOrients ? reader.transformation() : ownOrientation(bytes, info.format);
    const bool swap = transformation & QImageIOHandler::TransformationRotate90;

    const int side = std::min(maxSide > 0 ? maxSide : MaxDecodeSide, MaxDecodeSide);
    const QSize oriented = swap ? raw.transposed() : raw;
    const QSize targetOriented = fitWithin(oriented, side);
    if (targetOriented != oriented)
        reader.setScaledSize(swap ? targetOriented.transposed() : targetOriented);

    const bool large = qint64(raw.width()) * raw.height() > LargeDecodePixels;
    if (large)
        largeDecodePermit().acquire();
    QImage image = reader.read();
    if (large)
        largeDecodePermit().release();

    if (image.isNull()) {
        setError(error, tr("The image could not be decoded (%1).").arg(reader.errorString()));
        return {};
    }
    if (!qtOrients)
        image = detail::applyTransformation(image, transformation);
    return toDisplayImage(std::move(image));
}

} // namespace glacier::io
