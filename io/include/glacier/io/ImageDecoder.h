#pragma once

#include "glacier/core/Limits.h"
#include "glacier/io/ImageFormats.h"

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QStringList>

#include <optional>

namespace glacier::io {

struct ImageInfo
{
    ImageFormat format = ImageFormat::Png;
    QSize orientedSize; // after EXIF orientation; SVG: intrinsic size
    bool animated = false;
    bool multiPage = false;
};

struct ProbeOutcome
{
    std::optional<ImageInfo> info;
    QString error;       // set when !info
    QStringList notices; // e.g. "imported the first frame"
};

// Largest side ever decoded for display; bigger sources are downscaled.
inline constexpr int MaxDecodeSide = 8192;

// Sets Qt's global image allocation limit to fit Limits::maxDecodedPixels.
// Call once at startup (and in tests) before decoding.
void configureImageAllocationLimit(const Limits &limits);

// Checks format allowlist, size and dimension limits, orientation, animation
// and pages without decoding pixel data. SVG input is validated (SvgValidator).
// Thread-safe; intended for worker threads.
ProbeOutcome probeImage(const QByteArray &bytes, const Limits &limits = {});

// Decodes to an sRGB, correctly oriented image whose longest side is at most
// maxSide (0 = full size, still capped at MaxDecodeSide). Returns a null image
// and sets error on failure. Thread-safe; intended for worker threads.
QImage decodeImage(const QByteArray &bytes, const ImageInfo &info, int maxSide,
                   const Limits &limits = {}, QString *error = nullptr);

} // namespace glacier::io
