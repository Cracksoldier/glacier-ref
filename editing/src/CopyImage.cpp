#include "glacier/editing/CopyImage.h"

#include "glacier/editing/Geometry.h"

#include <QTransform>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace glacier::editing {

namespace {

bool isRectilinear(const Affine2D &u)
{
    return (u.m12 == 0.0 && u.m21 == 0.0) || (u.m11 == 0.0 && u.m22 == 0.0);
}

// Lossless rotation by a multiple of 90 degrees, clockwise on screen.
QImage rotateQuarters(const QImage &image, int quarters)
{
    switch (((quarters % 4) + 4) % 4) {
    case 1:
        return image.transformed(QTransform(0, 1, -1, 0, 0, 0));
    case 2:
        return image.flipped(Qt::Horizontal | Qt::Vertical);
    case 3:
        return image.transformed(QTransform(0, -1, 1, 0, 0, 0));
    default:
        return image;
    }
}

} // namespace

QImage copyImagePixels(const QImage &decoded, const QRectF &crop, const Affine2D &worldTransform, int maxSide)
{
    if (decoded.isNull() || maxSide <= 0)
        return {};
    const QRect pixels = QRectF(crop.x() * decoded.width(), crop.y() * decoded.height(),
                                crop.width() * decoded.width(), crop.height() * decoded.height())
                             .toAlignedRect()
                             .intersected(decoded.rect());
    if (pixels.isEmpty())
        return {};
    QImage cropped = decoded.copy(pixels);

    const Affine2D u = orientationOf(worldTransform);
    if (isRectilinear(u)) {
        // A reflection is a vertical flip followed by a rotation (see orientationOf).
        const bool mirrored = u.determinant() < 0.0;
        if (mirrored)
            cropped = cropped.flipped(Qt::Vertical);
        const double angle = std::atan2(u.m12, u.m11) / (std::numbers::pi / 2.0);
        QImage result = rotateQuarters(cropped, static_cast<int>(std::lround(angle)));
        if (std::max(result.width(), result.height()) > maxSide)
            result = result.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return result;
    }

    QTransform t(u.m11, u.m12, u.m21, u.m22, 0.0, 0.0);
    const QRectF bounds = t.mapRect(QRectF(cropped.rect()));
    const double longest = std::max(bounds.width(), bounds.height());
    if (longest > maxSide) {
        const double f = maxSide / longest;
        t = QTransform::fromScale(f, f) * t;
    }
    if (cropped.format() != QImage::Format_ARGB32_Premultiplied)
        cropped.convertTo(QImage::Format_ARGB32_Premultiplied);
    return cropped.transformed(t, Qt::SmoothTransformation);
}

} // namespace glacier::editing
