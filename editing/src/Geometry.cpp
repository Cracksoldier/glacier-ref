#include "glacier/editing/Geometry.h"

#include <cmath>
#include <numbers>

namespace glacier::editing {

namespace {
constexpr double DegToRad = std::numbers::pi / 180.0;
}

Affine2D rotationAbout(QPointF c, double degrees)
{
    const double r = degrees * DegToRad;
    const double cs = std::cos(r), sn = std::sin(r);
    // Clockwise on screen (y down), matching QTransform::rotate.
    const Affine2D rotation{cs, sn, -sn, cs, 0.0, 0.0};
    return Affine2D::translation(-c.x(), -c.y()) * rotation * Affine2D::translation(c.x(), c.y());
}

Affine2D scalingAbout(QPointF a, double sx, double sy)
{
    return Affine2D::translation(-a.x(), -a.y()) * Affine2D::scaling(sx, sy) * Affine2D::translation(a.x(), a.y());
}

Affine2D flipAbout(QPointF c, Qt::Orientation orientation)
{
    return orientation == Qt::Horizontal ? scalingAbout(c, -1.0, 1.0) : scalingAbout(c, 1.0, -1.0);
}

Affine2D scalingInFrame(const Affine2D &frame, QPointF anchor, double sx, double sy)
{
    return frame.inverted() * scalingAbout(anchor, sx, sy) * frame;
}

double snapAngle(double degrees, double step)
{
    return std::round(degrees / step) * step;
}

double rotationDegrees(const Affine2D &t)
{
    return std::atan2(t.m12, t.m11) / DegToRad;
}

Affine2D orientationOf(const Affine2D &t)
{
    // Column-vector form M = [[m11, m21], [m12, m22]]. The nearest rotation is
    // [[c, -s], [s, c]] with angle atan2(m12 - m21, m11 + m22); the nearest
    // reflection is [[c, s], [s, -c]] with angle atan2(m21 + m12, m11 - m22).
    const bool mirrored = t.determinant() < 0.0;
    const double angle = mirrored ? std::atan2(t.m21 + t.m12, t.m11 - t.m22) : std::atan2(t.m12 - t.m21, t.m11 + t.m22);
    double c = std::cos(angle);
    double s = std::sin(angle);
    const double quarters = angle / (std::numbers::pi / 2.0);
    if (std::abs(quarters - std::round(quarters)) < 1e-9) {
        const int q = ((static_cast<int>(std::round(quarters)) % 4) + 4) % 4;
        constexpr double Cos[] = {1.0, 0.0, -1.0, 0.0};
        constexpr double Sin[] = {0.0, 1.0, 0.0, -1.0};
        c = Cos[q];
        s = Sin[q];
    }
    if (mirrored)
        return Affine2D{c, s, s, -c, 0.0, 0.0};
    return Affine2D{c, s, -s, c, 0.0, 0.0};
}

} // namespace glacier::editing
