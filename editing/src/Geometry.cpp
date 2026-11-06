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

FlipState flipState(const Affine2D &t)
{
    FlipState state;
    if (t.determinant() >= 0.0)
        return state; // no mirroring
    // A mirrored transform is either "flip horizontally, then rotate by phiH"
    // or "flip vertically, then rotate by phiV", with phiH = phiV + 180.
    // Copy Image drops the rotation, so pick the reading with the smaller one:
    // a plain horizontal flip reads as horizontal, not as vertical + 180.
    auto normalize = [](double deg) {
        while (deg > 180.0)
            deg -= 360.0;
        while (deg <= -180.0)
            deg += 360.0;
        return deg;
    };
    const double phiV = normalize(std::atan2(t.m12, t.m11) / DegToRad);
    const double phiH = normalize(phiV + 180.0);
    if (std::abs(phiH) <= std::abs(phiV))
        state.horizontal = true;
    else
        state.vertical = true;
    return state;
}

} // namespace glacier::editing
