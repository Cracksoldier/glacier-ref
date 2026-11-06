#include "glacier/canvas/BoardScene.h"

#include "glacier/canvas/ImageCache.h"
#include "glacier/canvas/ImageItem.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/core/Limits.h"

#include <QTransform>

namespace glacier::canvas {

namespace {

QTransform toQTransform(const Affine2D &t)
{
    return QTransform(t.m11, t.m12, t.m21, t.m22, t.dx, t.dy);
}

} // namespace

BoardScene::BoardScene(BoardDocument *document, ImageCache *cache, QObject *parent)
    : QGraphicsScene(parent)
    , m_document(document)
    , m_cache(cache)
{
    const double limit = Limits::CoordinateLimit;
    setSceneRect(-limit, -limit, 2 * limit, 2 * limit);
    connect(document, &BoardDocument::itemsAdded, this, &BoardScene::addItems);
    connect(document, &BoardDocument::itemsRemoved, this, &BoardScene::removeItems);
    if (cache)
        connect(cache, &ImageCache::levelReady, this, &BoardScene::onLevelReady);
    addItems(document->itemIdsByZ());
}

void BoardScene::addItems(const QList<Id> &itemIds)
{
    for (const Id &id : itemIds) {
        const ImageItemData *data = m_document->item(id);
        const Asset *asset = data ? m_document->asset(data->assetId) : nullptr;
        if (!asset || m_items.contains(id))
            continue;
        if (m_cache)
            m_cache->registerAsset(asset->sha256, asset->mediaType, asset->orientedSize);
        auto *item = new ImageItem(id, asset->sha256, data->displaySize, m_cache);
        item->setTransform(toQTransform(data->transform));
        item->setZValue(data->z);
        addItem(item); // scene takes ownership
        m_items.insert(id, item);
        m_itemsByDigest.insert(asset->sha256, item);
    }
}

void BoardScene::removeItems(const QList<Id> &itemIds)
{
    for (const Id &id : itemIds) {
        ImageItem *item = m_items.take(id);
        if (!item)
            continue;
        m_itemsByDigest.remove(item->sha256(), item);
        delete item; // removes itself from the scene
    }
}

void BoardScene::onLevelReady(const QByteArray &sha256)
{
    const auto items = m_itemsByDigest.values(sha256);
    for (ImageItem *item : items)
        item->update();
}

QList<Id> BoardScene::selectedItemIds() const
{
    QList<Id> ids;
    const auto items = selectedItems();
    for (QGraphicsItem *item : items) {
        if (item->type() == ImageItem::Type)
            ids.append(static_cast<ImageItem *>(item)->itemId());
    }
    return ids;
}

QRectF BoardScene::selectionBounds() const
{
    QRectF bounds;
    const auto items = selectedItems();
    for (QGraphicsItem *item : items)
        bounds = bounds.isNull() ? item->sceneBoundingRect() : bounds.united(item->sceneBoundingRect());
    return bounds;
}

QRectF BoardScene::contentBounds() const
{
    return m_document ? m_document->boundingRect() : QRectF();
}

} // namespace glacier::canvas
