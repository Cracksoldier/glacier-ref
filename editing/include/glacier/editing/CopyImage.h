#pragma once

#include "glacier/core/Affine2D.h"

#include <QImage>
#include <QRectF>

namespace glacier::editing {

// Pixels for Copy Image (spec §3): the decoded source cropped to the
// normalized crop rectangle, then turned by the item's orientation on the
// board (rotation and mirroring from `worldTransform`; scale and stretch are
// ignored, so the source resolution is kept). Multiples of 90 degrees are
// lossless; other angles are resampled smoothly into the rotated bounding box
// with transparent corners. The longest side is limited to `maxSide`.
QImage copyImagePixels(const QImage &decoded, const QRectF &normalizedCrop, const Affine2D &worldTransform,
                       int maxSide);

} // namespace glacier::editing
