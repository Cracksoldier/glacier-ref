#pragma once

// Helpers for building boards in editing tests (no image bytes needed).

#include "glacier/core/BoardDocument.h"

#include <QByteArray>
#include <QString>

namespace glacier::test {

inline QByteArray fakeDigest(int n)
{
    return QByteArray::number(n).rightJustified(64, '0');
}

inline NewImage fakeImage(int n, QPointF pos, QSize size = {100, 100})
{
    NewImage image;
    image.sha256 = fakeDigest(n);
    image.mediaType = QStringLiteral("image/png");
    image.orientedSize = size;
    image.byteLength = 10;
    image.position = pos;
    return image;
}

// World position of an image-local point (e.g. a feature of the picture).
inline QPointF worldPoint(const BoardDocument &doc, const Id &id, QPointF local)
{
    return doc.worldTransform(id).map(local);
}

inline bool fuzzyPoint(QPointF a, QPointF b, double eps = 1e-6)
{
    return std::abs(a.x() - b.x()) <= eps && std::abs(a.y() - b.y()) <= eps;
}

inline bool fuzzyRect(const QRectF &a, const QRectF &b, double eps = 1e-6)
{
    return fuzzyPoint(a.topLeft(), b.topLeft(), eps) && fuzzyPoint(a.bottomRight(), b.bottomRight(), eps);
}

} // namespace glacier::test
