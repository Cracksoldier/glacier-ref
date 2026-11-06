#pragma once

#include <QPointF>
#include <QRectF>

namespace glacier {

// 2D affine transform with six coefficients, matching QTransform's convention
// (row vectors, "a then b" is written a * b):
//
//   x' = m11 * x + m21 * y + dx
//   y' = m12 * x + m22 * y + dy
//
// Lives in core because QTransform belongs to Qt Gui. Converts losslessly to
// QTransform(m11, m12, m21, m22, dx, dy) in the canvas layer. The schema
// documentation (M3) describes the serialized order of the coefficients.
struct Affine2D
{
    double m11 = 1.0;
    double m12 = 0.0;
    double m21 = 0.0;
    double m22 = 1.0;
    double dx = 0.0;
    double dy = 0.0;

    static Affine2D translation(double x, double y) { return {1.0, 0.0, 0.0, 1.0, x, y}; }
    static Affine2D scaling(double sx, double sy) { return {sx, 0.0, 0.0, sy, 0.0, 0.0}; }

    QPointF map(QPointF p) const { return {m11 * p.x() + m21 * p.y() + dx, m12 * p.x() + m22 * p.y() + dy}; }

    // Axis-aligned bounding rectangle of the transformed rectangle.
    QRectF mapRect(const QRectF &rect) const;

    double determinant() const { return m11 * m22 - m12 * m21; }
    bool isInvertible() const;
    bool isFinite() const;

    // Returns the inverse; the identity if not invertible (check isInvertible()).
    Affine2D inverted() const;

    QPointF translationPart() const { return {dx, dy}; }

    // Composition with QTransform semantics: (a * b).map(p) == b.map(a.map(p)).
    friend Affine2D operator*(const Affine2D &a, const Affine2D &b);
    friend bool operator==(const Affine2D &, const Affine2D &) = default;
};

} // namespace glacier
