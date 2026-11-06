#pragma once

#include "glacier/core/BoardDocument.h"

#include <QList>
#include <QPointF>
#include <QString>

#include <optional>

namespace glacier::editing {

// Result of building an edit. `change` is empty (nullopt) when there is
// nothing to do or the edit is not allowed; `error` then explains why (it may
// be empty for a plain no-op). `skippedLocked` counts locked entities that
// were left untouched. `created` lists new top-level entity IDs.
struct OpResult
{
    std::optional<DocumentChange> change;
    QString error;
    int skippedLocked = 0;
    QList<Id> created;

    explicit operator bool() const { return change.has_value(); }
};

enum class ZOrder { Raise, Lower, BringToFront, SendToBack };
enum class Align { Left, HCenter, Right, Top, VCenter, Bottom };

// Pure edit builders (no side effects): each inspects the document and returns
// the DocumentChange that performs the edit, enforcing the rules of spec §3:
// locked entities (and children of locked groups) are never modified, groups
// are flat, and every change keeps the document valid.
namespace ops {

// Locked itself or a member of a locked group.
bool isLocked(const BoardDocument &doc, const Id &id);

// Applies a board-space delta G to the selected entities (top-level entities
// or children of one group): newWorld = world * G.
OpResult transform(const BoardDocument &doc, const QList<Id> &ids, const Affine2D &boardDelta);
OpResult flip(const BoardDocument &doc, const QList<Id> &ids, Qt::Orientation orientation);
OpResult rotate(const BoardDocument &doc, const QList<Id> &ids, double degrees);

OpResult setCrop(const BoardDocument &doc, const Id &imageId, const QRectF &crop);
OpResult resetCrop(const BoardDocument &doc, const QList<Id> &ids);

OpResult group(const BoardDocument &doc, const QList<Id> &ids, const QString &name);
OpResult ungroup(const BoardDocument &doc, const QList<Id> &groupIds);
OpResult renameGroup(const BoardDocument &doc, const Id &groupId, const QString &name);

// Lock state applies to top-level entities only (children follow their group).
OpResult setLocked(const BoardDocument &doc, const QList<Id> &ids, bool locked);
OpResult zOrder(const BoardDocument &doc, const QList<Id> &ids, ZOrder op);

OpResult align(const BoardDocument &doc, const QList<Id> &ids, Align mode);
// Rows in reading order from the top-left of the units' previous bounds.
// With no ids, arranges all unlocked top-level entities.
OpResult arrange(const BoardDocument &doc, const QList<Id> &ids, double spacing);

// Removes entities; groups take their children; a group whose last child is
// removed is removed too.
OpResult remove(const BoardDocument &doc, const QList<Id> &ids);

// Notes. Heights are computed with NoteLayout.
OpResult createNote(const BoardDocument &doc, QPointF centre, const QString &text = {},
                    double fontSize = 0.0, double width = 0.0);
OpResult editNote(const BoardDocument &doc, const Id &noteId, const QString &text);
OpResult setNoteWidth(const BoardDocument &doc, const Id &noteId, double width);
// Width change from a resize gesture, with the note's new local transform
// (dragging the left edge keeps the right edge in place).
OpResult resizeNote(const BoardDocument &doc, const Id &noteId, double width, const Affine2D &localTransform);
OpResult setNoteStyle(const BoardDocument &doc, const QList<Id> &noteIds, double fontSize, quint32 textColor,
                      quint32 backgroundColor);

OpResult setBackground(const BoardDocument &doc, quint32 argb);
OpResult setGrayscale(const BoardDocument &doc, bool enabled);

// Inserts a fragment with fresh IDs. Assets are matched by digest. The
// fragment is centred on `anchor`, or offset by `offset` when anchor is null.
OpResult paste(const BoardDocument &doc, const Snapshot &fragment, std::optional<QPointF> anchor,
               QPointF offset = {});
OpResult duplicate(const BoardDocument &doc, const QList<Id> &ids, QPointF offset = {20.0, 20.0});

// Records to put on the clipboard for a selection: top-level entities as they
// are (groups with their children); selected group children become loose items
// with board transforms, so the fragment is self-contained.
Snapshot clipboardSnapshot(const BoardDocument &doc, const QList<Id> &ids);

} // namespace ops
} // namespace glacier::editing
