#include "glacier/canvas/ImageItem.h"

#include "glacier/canvas/ImageCache.h"

#include <QPaintDevice>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>

namespace glacier::canvas {

namespace {
const QColor PlaceholderColor(0x50, 0x53, 0x57);
}

ImageItem::ImageItem(const Id &itemId, const QByteArray &sha256, QSizeF displaySize, ImageCache *cache)
    : m_itemId(itemId)
    , m_sha256(sha256)
    , m_displaySize(displaySize)
    , m_cache(cache)
{
    setFlag(ItemIsSelectable);
}

QRectF ImageItem::boundingRect() const
{
    return QRectF(QPointF(0, 0), m_displaySize);
}

int ImageItem::neededSide(qreal levelOfDetail, qreal devicePixelRatio) const
{
    const qreal longest = std::max(m_displaySize.width(), m_displaySize.height());
    return std::max(1, int(std::ceil(longest * levelOfDetail * devicePixelRatio)));
}

void ImageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    const QRectF rect = boundingRect();
    const qreal lod = option->levelOfDetailFromTransform(painter->worldTransform());
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;

    const QPixmap pixmap = m_cache ? m_cache->best(m_sha256, neededSide(lod, dpr)) : QPixmap();
    if (pixmap.isNull()) {
        painter->fillRect(rect, PlaceholderColor);
        return;
    }
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawPixmap(rect, pixmap, QRectF(pixmap.rect()));
}

} // namespace glacier::canvas
