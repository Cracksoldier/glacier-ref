#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

class QIODevice;

namespace glacier::io {

// The v1 input formats (spec §4). Anything else is rejected, even when an
// installed Qt image plugin (e.g. KDE kimageformats) could decode it.
enum class ImageFormat { Png, Jpeg, Webp, Bmp, Gif, Tiff, Svg };

// Detects the format from content (never from the file name) and maps it onto
// the allowlist. Returns std::nullopt for anything not on it, including svgz.
std::optional<ImageFormat> detectFormat(const QByteArray &bytes);

QByteArray qtFormatName(ImageFormat format);
QString mediaType(ImageFormat format);
std::optional<ImageFormat> formatFromMediaType(const QString &mediaType);
QString displayName(ImageFormat format);

// File dialog filter, e.g. "Images (*.png *.jpg ...)".
QString fileDialogFilter();

} // namespace glacier::io
