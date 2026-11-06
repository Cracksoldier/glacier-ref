#pragma once

#include "glacier/core/Affine2D.h"

#include <QPointF>
#include <QRectF>

namespace glacier::editing {

// Board-space transform helpers for gestures and operations. All transforms
// use the Affine2D/QTransform convention: (a * b).map(p) == b.map(a.map(p)).

Affine2D rotationAbout(QPointF centre, double degrees);
Affine2D scalingAbout(QPointF anchor, double sx, double sy);
Affine2D flipAbout(QPointF centre, Qt::Orientation orientation);

// Scaling in a (possibly rotated) frame: `frame` maps frame-local coordinates
// to the board; `anchor` is frame-local. Returns the board-space delta.
Affine2D scalingInFrame(const Affine2D &frame, QPointF anchor, double sx, double sy);

// Snaps an angle in degrees to the nearest multiple of `step`.
double snapAngle(double degrees, double step = 15.0);

// Rotation of a transform's linear part, in degrees (atan2 of the x axis).
double rotationDegrees(const Affine2D &transform);

// Whether the transform mirrors the image horizontally / vertically once its
// rotation is removed (for Copy Image, spec §3). Shear is ignored.
struct FlipState
{
    bool horizontal = false;
    bool vertical = false;
};
FlipState flipState(const Affine2D &transform);

} // namespace glacier::editing
