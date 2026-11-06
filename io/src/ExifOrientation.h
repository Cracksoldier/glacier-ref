#pragma once

// Internal: EXIF orientation for formats where Qt does not apply it.
// Measured with Qt 6.12: JPEG and TIFF orientation is applied by Qt
// (QImageReader autoTransform); PNG eXIf and WebP EXIF chunks are not.

#include <QByteArray>
#include <QByteArrayView>
#include <QImageIOHandler>

namespace glacier::io::detail {

// Parses a TIFF-structured EXIF block ("II*\0"/"MM\0*" header, optionally
// prefixed with "Exif\0\0") and returns its IFD0 Orientation tag (1..8), or 0.
int exifOrientationFromTiff(QByteArrayView exif);

// Finds the EXIF block of a PNG (eXIf chunk) or WebP (EXIF chunk); empty if none.
QByteArrayView findPngExif(QByteArrayView png);
QByteArrayView findWebpExif(QByteArrayView webp);

// Maps EXIF orientation 1..8 to Qt's transformation flags.
QImageIOHandler::Transformations transformationFromExif(int orientation);

// Applies Qt-style transformation flags (flips first, then 90 degrees clockwise).
QImage applyTransformation(const QImage &image, QImageIOHandler::Transformations transformation);

} // namespace glacier::io::detail
