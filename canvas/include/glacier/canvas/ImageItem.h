#pragma once

#include "glacier/core/Id.h"

#include <QByteArray>
#include <QGraphicsItem>
#include <QPointer>
#include <QSizeF>

namespace glacier::canvas {

class ImageCache;

// Graphics item for one board image. Presentation only: geometry comes from
// the BoardDocument (spec §6). Draws the best cached resolution level for the
// current zoom and device pixel ratio. Selection outlines are drawn by
// BoardView so they stay legible at every zoom level.
class ImageItem : public QGraphicsItem
{
public:
    enum { Type = UserType + 1 };

    ImageItem(const Id &itemId, const QByteArray &sha256, QSizeF displaySize, ImageCache *cache);

    int type() const override { return Type; }
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    const Id &itemId() const { return m_itemId; }
    const QByteArray &sha256() const { return m_sha256; }

    // Device pixels needed for the longest side at the given level of detail.
    int neededSide(qreal levelOfDetail, qreal devicePixelRatio) const;

private:
    Id m_itemId;
    QByteArray m_sha256;
    QSizeF m_displaySize;
    QPointer<ImageCache> m_cache;
};

} // namespace glacier::canvas
