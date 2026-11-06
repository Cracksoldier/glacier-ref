#include "glacier/core/Affine2D.h"

#include <algorithm>
#include <cmath>

namespace glacier {

QRectF Affine2D::mapRect(const QRectF &rect) const
{
    const QPointF corners[] = {map(rect.topLeft()), map(rect.topRight()),
                               map(rect.bottomLeft()), map(rect.bottomRight())};
    double left = corners[0].x(), right = left, top = corners[0].y(), bottom = top;
    for (const QPointF &c : corners) {
        left = std::min(left, c.x());
        right = std::max(right, c.x());
        top = std::min(top, c.y());
        bottom = std::max(bottom, c.y());
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

bool Affine2D::isFinite() const
{
    return std::isfinite(m11) && std::isfinite(m12) && std::isfinite(m21) && std::isfinite(m22)
        && std::isfinite(dx) && std::isfinite(dy);
}

bool Affine2D::isInvertible() const
{
    const double det = determinant();
    return isFinite() && std::isfinite(det) && std::abs(det) > 1e-12;
}

Affine2D Affine2D::inverted() const
{
    if (!isInvertible())
        return {};
    const double inv = 1.0 / determinant();
    Affine2D r;
    r.m11 = m22 * inv;
    r.m12 = -m12 * inv;
    r.m21 = -m21 * inv;
    r.m22 = m11 * inv;
    r.dx = (m21 * dy - m22 * dx) * inv;
    r.dy = (m12 * dx - m11 * dy) * inv;
    return r;
}

Affine2D operator*(const Affine2D &a, const Affine2D &b)
{
    // Row-vector convention: p' = p * A * B.
    Affine2D r;
    r.m11 = a.m11 * b.m11 + a.m12 * b.m21;
    r.m12 = a.m11 * b.m12 + a.m12 * b.m22;
    r.m21 = a.m21 * b.m11 + a.m22 * b.m21;
    r.m22 = a.m21 * b.m12 + a.m22 * b.m22;
    r.dx = a.dx * b.m11 + a.dy * b.m21 + b.dx;
    r.dy = a.dx * b.m12 + a.dy * b.m22 + b.dy;
    return r;
}

} // namespace glacier
