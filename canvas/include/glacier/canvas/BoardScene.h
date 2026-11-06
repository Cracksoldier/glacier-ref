#pragma once

#include "glacier/core/BoardDocument.h"
#include "glacier/core/Id.h"

#include <QGraphicsScene>
#include <QHash>
#include <QMultiHash>
#include <QPointer>

namespace glacier::canvas {

class ImageCache;
class ImageItem;

// Presents a BoardDocument (spec §6): creates and removes graphics items from
// document signals. The scene rect is the supported coordinate range
// (+/- 1e7), which also bounds QGraphicsView's scroll range.
class BoardScene : public QGraphicsScene
{
    Q_OBJECT

public:
    BoardScene(BoardDocument *document, ImageCache *cache, QObject *parent = nullptr);

    BoardDocument *document() const { return m_document; }
    ImageItem *itemFor(const Id &itemId) const { return m_items.value(itemId); }
    QList<Id> selectedItemIds() const;
    QRectF selectionBounds() const;
    QRectF contentBounds() const;

private:
    void addItems(const QList<Id> &itemIds);
    void removeItems(const QList<Id> &itemIds);
    void onLevelReady(const QByteArray &sha256);

    QPointer<BoardDocument> m_document;
    QPointer<ImageCache> m_cache;
    QHash<Id, ImageItem *> m_items;          // owned by the scene
    QMultiHash<QByteArray, ImageItem *> m_itemsByDigest;
};

} // namespace glacier::canvas
