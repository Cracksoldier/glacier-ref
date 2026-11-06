#include "glacier/canvas/BoardScene.h"

#include "glacier/canvas/ImageCache.h"
#include "glacier/core/Limits.h"

namespace glacier::canvas {

BoardScene::BoardScene(BoardDocument *document, ImageCache *cache, QObject *parent)
    : QGraphicsScene(parent)
    , m_document(document)
    , m_cache(cache)
{
    const double limit = Limits::CoordinateLimit;
    setSceneRect(-limit, -limit, 2 * limit, 2 * limit);
    connect(document, &BoardDocument::changed, this, &BoardScene::applyDelta);
    if (cache)
        connect(cache, &ImageCache::levelReady, this, &BoardScene::onLevelReady);

    DocumentDelta initial;
    for (const GroupData &g : document->groups())
        initial.inserted.append(g.id);
    for (const ImageItemData &i : document->images())
        initial.inserted.append(i.id);
    for (const NoteData &n : document->notes())
        initial.inserted.append(n.id);
    initial.viewChanged = true;
    applyDelta(initial);
}

ImageItem *BoardScene::imageItem(const Id &id) const
{
    BoardItem *item = m_items.value(id);
    return item && item->type() == ImageItem::Type ? static_cast<ImageItem *>(item) : nullptr;
}

NoteItem *BoardScene::noteItem(const Id &id) const
{
    BoardItem *item = m_items.value(id);
    return item && item->type() == NoteItem::Type ? static_cast<NoteItem *>(item) : nullptr;
}

GroupItem *BoardScene::groupItem(const Id &id) const
{
    BoardItem *item = m_items.value(id);
    return item && item->type() == GroupItem::Type ? static_cast<GroupItem *>(item) : nullptr;
}

void BoardScene::applyDelta(const DocumentDelta &delta)
{
    if (!m_document)
        return;
    // 1. New groups first, so children can be parented to them.
    for (const Id &id : delta.inserted)
        if (const GroupData *g = m_document->group(id); g && !m_items.contains(id)) {
            auto *item = new GroupItem(*g);
            addItem(item);
            m_items.insert(id, item);
        }
    // 2. Removals (children that survive a removed group are detached first).
    for (const Id &id : delta.removed)
        removeEntity(id);
    // 3. Images and notes: create or refresh, with the correct parent.
    for (const Id &id : delta.inserted)
        refreshEntity(id);
    for (const Id &id : delta.updated)
        refreshEntity(id);
    // 4. Groups: transform, lock, child order.
    for (const Id &id : delta.inserted)
        refreshGroup(id);
    for (const Id &id : delta.updated)
        refreshGroup(id);

    if (!m_activeGroup.isNull() && !m_document->group(m_activeGroup))
        leaveGroup();
    applySelectability();
    if (delta.viewChanged)
        applyViewSettings();
}

void BoardScene::refreshEntity(const Id &id)
{
    const ImageItemData *image = m_document->image(id);
    const NoteData *note = m_document->note(id);
    if (!image && !note)
        return;
    BoardItem *item = m_items.value(id);
    if (image) {
        if (!item) {
            const Asset *asset = m_document->asset(image->assetId);
            if (!asset)
                return;
            if (m_cache)
                m_cache->registerAsset(asset->sha256, asset->mediaType, asset->orientedSize);
            auto *imageItem = new ImageItem(*image, asset->sha256, m_cache);
            imageItem->setGrayscale(m_grayscale);
            m_imagesByDigest.insert(asset->sha256, imageItem);
            item = imageItem;
            addItem(item);
            m_items.insert(id, item);
        } else {
            static_cast<ImageItem *>(item)->setData(*image);
        }
    } else {
        if (!item) {
            item = new NoteItem(*note);
            addItem(item);
            m_items.insert(id, item);
        } else {
            static_cast<NoteItem *>(item)->setData(*note);
        }
    }
    const Id groupId = image ? image->groupId : note->groupId;
    QGraphicsItem *parent = groupId.isNull() ? nullptr : m_items.value(groupId);
    if (item->parentItem() != parent) {
        const bool wasSelected = item->isSelected();
        item->setParentItem(parent); // keeps the local transform from the model
        item->setSelected(wasSelected && item->flags().testFlag(QGraphicsItem::ItemIsSelectable));
    }
    if (groupId.isNull())
        item->setZValue(image ? image->z : note->z);
}

void BoardScene::refreshGroup(const Id &id)
{
    const GroupData *g = m_document->group(id);
    GroupItem *item = groupItem(id);
    if (!g || !item)
        return;
    item->setData(*g);
    item->setZValue(g->z);
    for (qsizetype i = 0; i < g->children.size(); ++i)
        if (BoardItem *child = m_items.value(g->children[i]))
            child->setZValue(double(i));
}

void BoardScene::removeEntity(const Id &id)
{
    BoardItem *item = m_items.take(id);
    if (!item)
        return;
    if (item->type() == GroupItem::Type) {
        const auto children = item->childItems();
        for (QGraphicsItem *child : children)
            child->setParentItem(nullptr); // survivors are re-parented in step 3
    }
    if (item->type() == ImageItem::Type) {
        auto *image = static_cast<ImageItem *>(item);
        m_imagesByDigest.remove(image->sha256(), image);
    }
    delete item;
}

void BoardScene::applySelectability()
{
    for (BoardItem *item : std::as_const(m_items)) {
        const Id groupId = m_document->groupOf(item->entityId());
        const bool selectable = m_activeGroup.isNull()
                                  ? (groupId.isNull() || item->type() == GroupItem::Type)
                                  : groupId == m_activeGroup;
        const bool groupInActiveMode = item->type() == GroupItem::Type && !m_activeGroup.isNull();
        const bool enable = selectable && !groupInActiveMode;
        if (!enable && item->isSelected())
            item->setSelected(false);
        item->setFlag(QGraphicsItem::ItemIsSelectable, enable);
    }
}

void BoardScene::applyViewSettings()
{
    const bool grayscale = m_document->view().grayscale;
    if (grayscale != m_grayscale) {
        m_grayscale = grayscale;
        for (ImageItem *item : std::as_const(m_imagesByDigest))
            item->setGrayscale(grayscale);
    }
    emit viewSettingsChanged();
}

void BoardScene::enterGroup(const Id &groupId)
{
    if (!m_document || !m_document->group(groupId) || m_activeGroup == groupId)
        return;
    clearSelection();
    m_activeGroup = groupId;
    applySelectability();
    emit activeGroupChanged(groupId);
}

void BoardScene::leaveGroup()
{
    if (m_activeGroup.isNull())
        return;
    const Id previous = m_activeGroup;
    clearSelection();
    m_activeGroup = Id();
    applySelectability();
    if (GroupItem *g = groupItem(previous))
        g->setSelected(true);
    emit activeGroupChanged(Id());
}

void BoardScene::onLevelReady(const QByteArray &sha256)
{
    const auto items = m_imagesByDigest.values(sha256);
    for (ImageItem *item : items)
        item->update();
}

QList<Id> BoardScene::selectedIds() const
{
    QList<Id> ids;
    const auto items = selectedItems();
    for (QGraphicsItem *item : items)
        if (const auto *board = dynamic_cast<const BoardItem *>(item))
            ids.append(board->entityId());
    // Stable order: back to front.
    std::sort(ids.begin(), ids.end(), [this](const Id &a, const Id &b) {
        const BoardItem *ia = m_items.value(a), *ib = m_items.value(b);
        return ia->zValue() != ib->zValue() ? ia->zValue() < ib->zValue() : a.toString() < b.toString();
    });
    return ids;
}

void BoardScene::selectIds(const QList<Id> &ids)
{
    clearSelection();
    for (const Id &id : ids)
        if (BoardItem *item = m_items.value(id); item && item->flags().testFlag(QGraphicsItem::ItemIsSelectable))
            item->setSelected(true);
}

QRectF BoardScene::selectionBounds() const
{
    QRectF bounds;
    for (const Id &id : selectedIds()) {
        const QRectF b = m_document->worldBounds(id);
        bounds = bounds.isNull() ? b : bounds.united(b);
    }
    return bounds;
}

QRectF BoardScene::contentBounds() const
{
    return m_document ? m_document->boundingRect() : QRectF();
}

} // namespace glacier::canvas
