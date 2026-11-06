#pragma once

#include <QList>
#include <QPointF>
#include <QSizeF>

namespace glacier {

// Deterministic placement for a batch of new items (imports, spec §4): items
// keep input order and fill rows whose target width is about sqrt(total area
// including spacing). The block is centred on `anchor` and shifted, if
// necessary, to stay inside the supported coordinate range. Returns top-left
// positions in input order.
QList<QPointF> layoutBatch(const QList<QSizeF> &sizes, QPointF anchor, double spacing = 20.0);

// The same row layout starting at `topLeft` (auto-arrange, spec §3).
QList<QPointF> layoutRows(const QList<QSizeF> &sizes, QPointF topLeft, double spacing = 20.0);

} // namespace glacier
