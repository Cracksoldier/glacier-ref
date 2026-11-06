#include "glacier/core/BatchLayout.h"

#include "glacier/core/Limits.h"

#include <algorithm>
#include <cmath>

namespace glacier {

namespace {

// Rows from (0,0); returns positions and the block size.
QList<QPointF> rows(const QList<QSizeF> &sizes, double spacing, QSizeF *block)
{
    QList<QPointF> positions;
    positions.reserve(sizes.size());
    if (sizes.isEmpty())
        return positions;

    double area = 0.0;
    double widest = 0.0;
    for (const QSizeF &s : sizes) {
        area += (s.width() + spacing) * (s.height() + spacing);
        widest = std::max(widest, s.width());
    }
    const double targetWidth = std::max(std::sqrt(area), widest);

    double x = 0.0, y = 0.0, rowHeight = 0.0, blockWidth = 0.0;
    for (const QSizeF &s : sizes) {
        if (x > 0.0 && x + s.width() > targetWidth) {
            x = 0.0;
            y += rowHeight + spacing;
            rowHeight = 0.0;
        }
        positions.append(QPointF(x, y));
        blockWidth = std::max(blockWidth, x + s.width());
        rowHeight = std::max(rowHeight, s.height());
        x += s.width() + spacing;
    }
    *block = QSizeF(blockWidth, y + rowHeight);
    return positions;
}

} // namespace

QList<QPointF> layoutRows(const QList<QSizeF> &sizes, QPointF topLeft, double spacing)
{
    QSizeF block;
    QList<QPointF> positions = rows(sizes, spacing, &block);
    for (QPointF &p : positions)
        p += topLeft;
    return positions;
}

QList<QPointF> layoutBatch(const QList<QSizeF> &sizes, QPointF anchor, double spacing)
{
    QSizeF block;
    QList<QPointF> positions = rows(sizes, spacing, &block);
    if (positions.isEmpty())
        return positions;
    const double blockWidth = block.width();
    const double blockHeight = block.height();

    // Centre on the anchor, then keep the block inside the coordinate range.
    const double limit = Limits::CoordinateLimit;
    double left = anchor.x() - blockWidth / 2.0;
    double top = anchor.y() - blockHeight / 2.0;
    left = std::clamp(left, -limit, std::max(-limit, limit - blockWidth));
    top = std::clamp(top, -limit, std::max(-limit, limit - blockHeight));
    for (QPointF &p : positions)
        p += QPointF(left, top);
    return positions;
}

} // namespace glacier
