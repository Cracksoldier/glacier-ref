#pragma once

#include "glacier/io/ImageFormats.h"

#include <QList>
#include <QString>

namespace glacier::io {

struct FormatCheck
{
    ImageFormat format;
    bool ok = false;
    QString detail; // the error when !ok
};

// Encodes (or embeds) a tiny sample of every v1 input format and runs it
// through the real probe and decode path. Packaged builds run this through
// `glacier-ref --check-image-formats` to prove that the shipped Qt image
// plugins (WebP and TIFF in particular, spec §4) are present and work.
// Needs a QCoreApplication (plugin paths) but no display.
QList<FormatCheck> checkDecoders();

} // namespace glacier::io
