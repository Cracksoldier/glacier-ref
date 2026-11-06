#pragma once

#include "glacier/canvas/BoardItems.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/core/Id.h"

#include <QGraphicsScene>
#include <QHash>
#include <QMultiHash>
#include <QPointer>

namespace glacier::canvas {

class ImageCache;

// Presents a BoardDocument (spec §6): mirrors every DocumentDelta into graphics
// items, updating existing items in place so selection survives edits and
// undo. Group children are child items of their GroupItem.
//
// Selection units are top-level entities; in group-edit mode (enterGroup) the
// children of the active group are selectable instead.
class BoardScene : public QGraphicsScene
{
    Q_OBJECT

public:
    BoardScene(BoardDocument *document, ImageCache *cache, QObject *parent = nullptr);

    BoardDocument *document() const { return m_document; }
    BoardItem *itemFor(const Id &id) const { return m_items.value(id); }
    ImageItem *imageItem(const Id &id) const;
    NoteItem *noteItem(const Id &id) const;
    GroupItem *groupItem(const Id &id) const;

    QList<Id> selectedIds() const;
    void selectIds(const QList<Id> &ids);
    QRectF selectionBounds() const;
    QRectF contentBounds() const;

    // Group-edit mode.
    void enterGroup(const Id &groupId);
    void leaveGroup();
    Id activeGroup() const { return m_activeGroup; }

    bool grayscale() const { return m_grayscale; }

    // Backward-compatible name used by M1 code and tests.
    QList<Id> selectedItemIds() const { return selectedIds(); }

signals:
    void viewSettingsChanged();
    void activeGroupChanged(const glacier::Id &groupId);

private:
    void applyDelta(const DocumentDelta &delta);
    void refreshEntity(const Id &id);
    void refreshGroup(const Id &id);
    void removeEntity(const Id &id);
    void applySelectability();
    void applyViewSettings();
    void onLevelReady(const QByteArray &sha256);

    QPointer<BoardDocument> m_document;
    QPointer<ImageCache> m_cache;
    QHash<Id, BoardItem *> m_items; // owned by the scene (or by a parent item)
    QMultiHash<QByteArray, ImageItem *> m_imagesByDigest;
    Id m_activeGroup;
    bool m_grayscale = false;
};

} // namespace glacier::canvas
