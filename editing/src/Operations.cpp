#include "glacier/editing/Operations.h"

#include "glacier/core/BatchLayout.h"
#include "glacier/editing/Geometry.h"
#include "glacier/editing/NoteLayout.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace glacier::editing::ops {

namespace {

QString tr(const char *text, int n = -1)
{
    return QCoreApplication::translate("glacier::editing", text, nullptr, n);
}

OpResult fail(const QString &error)
{
    OpResult r;
    r.error = error;
    return r;
}

OpResult done(DocumentChange change, int skipped = 0)
{
    OpResult r;
    if (!change.isEmpty())
        r.change = std::move(change);
    r.skippedLocked = skipped;
    return r;
}

// Removes duplicates while keeping order.
QList<Id> unique(const QList<Id> &ids)
{
    QList<Id> out;
    QSet<Id> seen;
    for (const Id &id : ids)
        if (!seen.contains(id)) {
            seen.insert(id);
            out.append(id);
        }
    return out;
}

// Entities that can be edited: existing images, notes and groups.
QList<Id> editableEntities(const BoardDocument &doc, const QList<Id> &ids, int *skippedLocked)
{
    QList<Id> out;
    int skipped = 0;
    for (const Id &id : unique(ids)) {
        const EntityKind k = doc.kind(id);
        if (k != EntityKind::Image && k != EntityKind::Note && k != EntityKind::Group)
            continue;
        if (isLocked(doc, id)) {
            ++skipped;
            continue;
        }
        out.append(id);
    }
    if (skippedLocked)
        *skippedLocked = skipped;
    return out;
}

// Records the replacement of an entity's local transform.
void setLocalTransform(const BoardDocument &doc, const Id &id, const Affine2D &local, DocumentChange &change)
{
    if (const ImageItemData *i = doc.image(id)) {
        ImageItemData copy = *i;
        copy.transform = local;
        change.update.images.append(copy);
    } else if (const NoteData *n = doc.note(id)) {
        NoteData copy = *n;
        copy.transform = local;
        change.update.notes.append(copy);
    } else if (const GroupData *g = doc.group(id)) {
        GroupData copy = *g;
        copy.transform = local;
        change.update.groups.append(copy);
    }
}

// Local transform of `id` in its parent, after applying board delta G.
Affine2D movedLocal(const BoardDocument &doc, const Id &id, const Affine2D &g)
{
    const Id parent = doc.groupOf(id);
    const Affine2D world = doc.worldTransform(id) * g;
    if (parent.isNull())
        return world;
    return world * doc.group(parent)->transform.inverted();
}

QRectF unionBounds(const BoardDocument &doc, const QList<Id> &ids)
{
    QRectF bounds;
    for (const Id &id : ids) {
        const QRectF b = doc.worldBounds(id);
        bounds = bounds.isNull() ? b : bounds.united(b);
    }
    return bounds;
}

// All selected entities must be top-level, or all children of the same group.
bool sameParent(const BoardDocument &doc, const QList<Id> &ids, Id *parent)
{
    if (ids.isEmpty())
        return false;
    const Id first = doc.groupOf(ids.first());
    for (const Id &id : ids)
        if (doc.groupOf(id) != first)
            return false;
    if (parent)
        *parent = first;
    return true;
}

double zOf(const BoardDocument &doc, const Id &id)
{
    if (const ImageItemData *i = doc.image(id))
        return i->z;
    if (const NoteData *n = doc.note(id))
        return n->z;
    if (const GroupData *g = doc.group(id))
        return g->z;
    return 0.0;
}

void setZ(const BoardDocument &doc, const Id &id, double z, DocumentChange &change)
{
    if (const ImageItemData *i = doc.image(id)) {
        ImageItemData copy = *i;
        copy.z = z;
        change.update.images.append(copy);
    } else if (const NoteData *n = doc.note(id)) {
        NoteData copy = *n;
        copy.z = z;
        change.update.notes.append(copy);
    } else if (const GroupData *g = doc.group(id)) {
        GroupData copy = *g;
        copy.z = z;
        change.update.groups.append(copy);
    }
}

// Writes z = 1..n for the given top-level order (only where it changes).
void renumberTopLevel(const BoardDocument &doc, const QList<Id> &order, DocumentChange &change)
{
    for (qsizetype i = 0; i < order.size(); ++i) {
        const double z = double(i + 1);
        if (zOf(doc, order[i]) != z)
            setZ(doc, order[i], z, change);
    }
}

QList<Id> readingOrder(const BoardDocument &doc, QList<Id> ids)
{
    std::sort(ids.begin(), ids.end(), [&doc](const Id &a, const Id &b) {
        const QRectF ra = doc.worldBounds(a), rb = doc.worldBounds(b);
        if (ra.top() != rb.top())
            return ra.top() < rb.top();
        if (ra.left() != rb.left())
            return ra.left() < rb.left();
        return a.toString() < b.toString();
    });
    return ids;
}

QString lockedMessage(int n)
{
    return tr("%n locked item(s) were not changed.", n);
}

} // namespace

bool isLocked(const BoardDocument &doc, const Id &id)
{
    if (const ImageItemData *i = doc.image(id)) {
        if (i->locked)
            return true;
        if (const GroupData *g = doc.group(i->groupId))
            return g->locked;
        return false;
    }
    if (const NoteData *n = doc.note(id)) {
        if (n->locked)
            return true;
        if (const GroupData *g = doc.group(n->groupId))
            return g->locked;
        return false;
    }
    if (const GroupData *g = doc.group(id))
        return g->locked;
    return false;
}

// ---------------------------------------------------------------------------
// Transforms

OpResult transform(const BoardDocument &doc, const QList<Id> &ids, const Affine2D &g)
{
    if (!g.isInvertible())
        return fail(tr("The transformation is not valid."));
    int skipped = 0;
    const QList<Id> targets = editableEntities(doc, ids, &skipped);
    if (targets.isEmpty())
        return fail(skipped > 0 ? lockedMessage(skipped) : QString());
    if (!sameParent(doc, targets, nullptr))
        return fail(tr("Items inside and outside a group cannot be transformed together."));
    DocumentChange change;
    for (const Id &id : targets)
        setLocalTransform(doc, id, movedLocal(doc, id, g), change);
    return done(std::move(change), skipped);
}

OpResult flip(const BoardDocument &doc, const QList<Id> &ids, Qt::Orientation orientation)
{
    const QList<Id> targets = editableEntities(doc, ids, nullptr);
    const QRectF bounds = unionBounds(doc, targets);
    if (bounds.isNull())
        return transform(doc, ids, Affine2D()); // reports locked items, if any
    return transform(doc, ids, flipAbout(bounds.center(), orientation));
}

OpResult rotate(const BoardDocument &doc, const QList<Id> &ids, double degrees)
{
    const QList<Id> targets = editableEntities(doc, ids, nullptr);
    const QRectF bounds = unionBounds(doc, targets);
    if (bounds.isNull())
        return transform(doc, ids, Affine2D());
    return transform(doc, ids, rotationAbout(bounds.center(), degrees));
}

// ---------------------------------------------------------------------------
// Crop

OpResult setCrop(const BoardDocument &doc, const Id &imageId, const QRectF &crop)
{
    const ImageItemData *image = doc.image(imageId);
    if (!image)
        return fail({});
    if (isLocked(doc, imageId))
        return fail(lockedMessage(1));
    const QRectF clamped = crop.normalized().intersected(QRectF(0, 0, 1, 1));
    if (clamped.width() <= 0.0 || clamped.height() <= 0.0)
        return fail(tr("The crop area is empty."));
    if (clamped == image->crop)
        return done({});
    ImageItemData copy = *image;
    copy.crop = clamped;
    DocumentChange change;
    change.update.images.append(copy);
    return done(std::move(change));
}

OpResult resetCrop(const BoardDocument &doc, const QList<Id> &ids)
{
    int skipped = 0;
    DocumentChange change;
    for (const Id &id : editableEntities(doc, ids, &skipped)) {
        QList<Id> images{id};
        if (const GroupData *g = doc.group(id))
            images = g->children;
        for (const Id &imageId : images) {
            const ImageItemData *image = doc.image(imageId);
            if (image && image->crop != QRectF(0, 0, 1, 1)) {
                ImageItemData copy = *image;
                copy.crop = QRectF(0, 0, 1, 1);
                change.update.images.append(copy);
            }
        }
    }
    return done(std::move(change), skipped);
}

// ---------------------------------------------------------------------------
// Groups

OpResult group(const BoardDocument &doc, const QList<Id> &ids, const QString &name)
{
    const QList<Id> members = unique(ids);
    if (members.size() < 2)
        return fail(tr("Select at least two items to group."));
    for (const Id &id : members) {
        const EntityKind k = doc.kind(id);
        if (k == EntityKind::Group || !doc.groupOf(id).isNull())
            return fail(tr("Grouped items cannot be grouped again; groups cannot be nested."));
        if (k != EntityKind::Image && k != EntityKind::Note)
            return fail({});
        if (isLocked(doc, id))
            return fail(tr("Locked items cannot be grouped. Unlock them first."));
    }

    const QRectF bounds = unionBounds(doc, members);
    GroupData g;
    g.id = Id::generate();
    g.name = name.trimmed().isEmpty() ? tr("Group") : name.trimmed();
    g.transform = Affine2D::translation(bounds.left(), bounds.top());
    QList<Id> ordered = members;
    std::sort(ordered.begin(), ordered.end(), [&doc](const Id &a, const Id &b) {
        return zOf(doc, a) != zOf(doc, b) ? zOf(doc, a) < zOf(doc, b) : a.toString() < b.toString();
    });
    g.children = ordered;
    g.z = zOf(doc, ordered.last()); // the group takes the frontmost member's slot

    DocumentChange change;
    change.insert.groups.append(g);
    const Affine2D toGroup = g.transform.inverted();
    for (const Id &id : ordered) {
        const Affine2D local = doc.worldTransform(id) * toGroup;
        if (const ImageItemData *i = doc.image(id)) {
            ImageItemData copy = *i;
            copy.transform = local;
            copy.groupId = g.id;
            change.update.images.append(copy);
        } else if (const NoteData *n = doc.note(id)) {
            NoteData copy = *n;
            copy.transform = local;
            copy.groupId = g.id;
            change.update.notes.append(copy);
        }
    }
    OpResult r = done(std::move(change));
    r.created.append(g.id);
    return r;
}

OpResult ungroup(const BoardDocument &doc, const QList<Id> &groupIds)
{
    int skipped = 0;
    QList<Id> groups;
    for (const Id &id : editableEntities(doc, groupIds, &skipped))
        if (doc.group(id))
            groups.append(id);
    if (groups.isEmpty())
        return fail(skipped > 0 ? lockedMessage(skipped) : QString());

    // New top-level order: each group replaced by its children, in order.
    QList<Id> order;
    const QSet<Id> dissolving(groups.cbegin(), groups.cend());
    for (const Id &id : doc.topLevelIdsByZ()) {
        if (dissolving.contains(id))
            order.append(doc.group(id)->children);
        else
            order.append(id);
    }

    DocumentChange change;
    OpResult r;
    for (const Id &groupId : groups) {
        const GroupData *g = doc.group(groupId);
        change.remove.append(groupId);
        for (const Id &child : g->children) {
            const Affine2D world = doc.worldTransform(child);
            if (const ImageItemData *i = doc.image(child)) {
                ImageItemData copy = *i;
                copy.transform = world;
                copy.groupId = Id();
                copy.z = double(order.indexOf(child) + 1);
                change.update.images.append(copy);
            } else if (const NoteData *n = doc.note(child)) {
                NoteData copy = *n;
                copy.transform = world;
                copy.groupId = Id();
                copy.z = double(order.indexOf(child) + 1);
                change.update.notes.append(copy);
            }
            r.created.append(child);
        }
    }
    // Renumber the remaining top-level entities around the dissolved groups.
    QSet<Id> handled;
    for (const ImageItemData &i : std::as_const(change.update.images))
        handled.insert(i.id);
    for (const NoteData &n : std::as_const(change.update.notes))
        handled.insert(n.id);
    for (qsizetype i = 0; i < order.size(); ++i)
        if (!handled.contains(order[i]) && zOf(doc, order[i]) != double(i + 1))
            setZ(doc, order[i], double(i + 1), change);

    r.change = std::move(change);
    r.skippedLocked = skipped;
    return r;
}

OpResult renameGroup(const BoardDocument &doc, const Id &groupId, const QString &name)
{
    const GroupData *g = doc.group(groupId);
    if (!g)
        return fail({});
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed == g->name)
        return done({});
    GroupData copy = *g;
    copy.name = trimmed.left(256);
    DocumentChange change;
    change.update.groups.append(copy);
    return done(std::move(change));
}

// ---------------------------------------------------------------------------
// Locking and stacking

OpResult setLocked(const BoardDocument &doc, const QList<Id> &ids, bool locked)
{
    DocumentChange change;
    for (const Id &id : unique(ids)) {
        if (!doc.groupOf(id).isNull())
            return fail(tr("Items inside a group cannot be locked individually. Lock the group instead."));
        if (const ImageItemData *i = doc.image(id); i && i->locked != locked) {
            ImageItemData copy = *i;
            copy.locked = locked;
            change.update.images.append(copy);
        } else if (const NoteData *n = doc.note(id); n && n->locked != locked) {
            NoteData copy = *n;
            copy.locked = locked;
            change.update.notes.append(copy);
        } else if (const GroupData *g = doc.group(id); g && g->locked != locked) {
            GroupData copy = *g;
            copy.locked = locked;
            change.update.groups.append(copy);
        }
    }
    return done(std::move(change));
}

OpResult zOrder(const BoardDocument &doc, const QList<Id> &ids, ZOrder op)
{
    int skipped = 0;
    const QList<Id> targets = editableEntities(doc, ids, &skipped);
    Id parent;
    if (targets.isEmpty())
        return fail(skipped > 0 ? lockedMessage(skipped) : QString());
    if (!sameParent(doc, targets, &parent))
        return fail(tr("Items inside and outside a group cannot be reordered together."));

    QList<Id> order = parent.isNull() ? doc.topLevelIdsByZ() : doc.group(parent)->children;
    const QSet<Id> selected(targets.cbegin(), targets.cend());
    QList<Id> result;
    switch (op) {
    case ZOrder::BringToFront:
    case ZOrder::SendToBack: {
        QList<Id> rest, picked;
        for (const Id &id : order)
            (selected.contains(id) ? picked : rest).append(id);
        result = op == ZOrder::BringToFront ? rest + picked : picked + rest;
        break;
    }
    case ZOrder::Raise:
        result = order;
        for (qsizetype i = result.size() - 2; i >= 0; --i)
            if (selected.contains(result[i]) && !selected.contains(result[i + 1]))
                result.swapItemsAt(i, i + 1);
        break;
    case ZOrder::Lower:
        result = order;
        for (qsizetype i = 1; i < result.size(); ++i)
            if (selected.contains(result[i]) && !selected.contains(result[i - 1]))
                result.swapItemsAt(i, i - 1);
        break;
    }
    if (result == order)
        return done({}, skipped);

    DocumentChange change;
    if (parent.isNull()) {
        renumberTopLevel(doc, result, change);
    } else {
        GroupData copy = *doc.group(parent);
        copy.children = result;
        change.update.groups.append(copy);
    }
    return done(std::move(change), skipped);
}

// ---------------------------------------------------------------------------
// Layout

OpResult align(const BoardDocument &doc, const QList<Id> &ids, Align mode)
{
    const QList<Id> all = unique(ids);
    int skipped = 0;
    const QList<Id> targets = editableEntities(doc, all, &skipped);
    if (all.size() < 2 || targets.isEmpty())
        return fail(skipped > 0 ? lockedMessage(skipped) : tr("Select at least two items to align."));
    if (!sameParent(doc, all, nullptr))
        return fail(tr("Items inside and outside a group cannot be aligned together."));
    const QRectF ref = unionBounds(doc, all); // locked items count as reference but stay put
    DocumentChange change;
    for (const Id &id : targets) {
        const QRectF b = doc.worldBounds(id);
        QPointF d;
        switch (mode) {
        case Align::Left: d.setX(ref.left() - b.left()); break;
        case Align::HCenter: d.setX(ref.center().x() - b.center().x()); break;
        case Align::Right: d.setX(ref.right() - b.right()); break;
        case Align::Top: d.setY(ref.top() - b.top()); break;
        case Align::VCenter: d.setY(ref.center().y() - b.center().y()); break;
        case Align::Bottom: d.setY(ref.bottom() - b.bottom()); break;
        }
        if (d != QPointF())
            setLocalTransform(doc, id, movedLocal(doc, id, Affine2D::translation(d.x(), d.y())), change);
    }
    return done(std::move(change), skipped);
}

OpResult arrange(const BoardDocument &doc, const QList<Id> &ids, double spacing)
{
    if (!std::isfinite(spacing) || spacing < 0.0)
        spacing = 20.0;
    QList<Id> candidates;
    if (ids.isEmpty()) {
        candidates = doc.topLevelIdsByZ();
    } else {
        for (const Id &id : unique(ids)) {
            // Children count via their group (groups are arranged as units).
            const Id parent = doc.groupOf(id);
            const Id unit = parent.isNull() ? id : parent;
            if (!candidates.contains(unit))
                candidates.append(unit);
        }
    }
    int skipped = 0;
    const QList<Id> units = readingOrder(doc, editableEntities(doc, candidates, &skipped));
    if (units.isEmpty())
        return fail(skipped > 0 ? lockedMessage(skipped) : QString());

    QList<QSizeF> sizes;
    for (const Id &id : units)
        sizes.append(doc.worldBounds(id).size());
    const QRectF before = unionBounds(doc, units);
    const QList<QPointF> positions = layoutRows(sizes, before.topLeft(), spacing);

    DocumentChange change;
    for (qsizetype i = 0; i < units.size(); ++i) {
        const QPointF d = positions[i] - doc.worldBounds(units[i]).topLeft();
        if (d != QPointF())
            setLocalTransform(doc, units[i], movedLocal(doc, units[i], Affine2D::translation(d.x(), d.y())), change);
    }
    return done(std::move(change), skipped);
}

// ---------------------------------------------------------------------------
// Removal

OpResult remove(const BoardDocument &doc, const QList<Id> &ids)
{
    int skipped = 0;
    const QList<Id> targets = editableEntities(doc, ids, &skipped);
    if (targets.isEmpty())
        return fail(skipped > 0 ? lockedMessage(skipped) : QString());

    QSet<Id> removing;
    for (const Id &id : targets) {
        removing.insert(id);
        if (const GroupData *g = doc.group(id))
            for (const Id &child : g->children)
                removing.insert(child);
    }
    DocumentChange change;
    // Groups losing children: update their lists, or remove them when empty.
    for (const GroupData &g : doc.groups()) {
        if (removing.contains(g.id))
            continue;
        QList<Id> remaining;
        for (const Id &child : g.children)
            if (!removing.contains(child))
                remaining.append(child);
        if (remaining.size() == g.children.size())
            continue;
        if (remaining.isEmpty()) {
            removing.insert(g.id);
        } else {
            GroupData copy = g;
            copy.children = remaining;
            change.update.groups.append(copy);
        }
    }
    change.remove = QList<Id>(removing.cbegin(), removing.cend());
    std::sort(change.remove.begin(), change.remove.end(),
              [](const Id &a, const Id &b) { return a.toString() < b.toString(); });
    return done(std::move(change), skipped);
}

// ---------------------------------------------------------------------------
// Notes

OpResult createNote(const BoardDocument &doc, QPointF centre, const QString &text, double fontSize, double width)
{
    NoteData note;
    note.id = Id::generate();
    note.text = text.left(doc.limits().maxNoteCharacters);
    note.fontSize = fontSize > 0.0 ? fontSize : NoteLayout::DefaultFontSize;
    note.width = width > 0.0 ? std::clamp(width, NoteLayout::MinWidth, NoteLayout::MaxWidth) : NoteLayout::DefaultWidth;
    note.height = NoteLayout::height(note.text, note.width, note.fontSize);
    note.transform = Affine2D::translation(centre.x() - note.width / 2.0, centre.y() - note.height / 2.0);
    note.z = doc.maxTopLevelZ() + 1.0;
    DocumentChange change;
    change.insert.notes.append(note);
    OpResult r = done(std::move(change));
    r.created.append(note.id);
    return r;
}

OpResult editNote(const BoardDocument &doc, const Id &noteId, const QString &text)
{
    const NoteData *n = doc.note(noteId);
    if (!n)
        return fail({});
    if (isLocked(doc, noteId))
        return fail(lockedMessage(1));
    if (text.size() > doc.limits().maxNoteCharacters)
        return fail(tr("A note can hold at most %1 characters.").arg(doc.limits().maxNoteCharacters));
    if (text == n->text)
        return done({});
    NoteData copy = *n;
    copy.text = text;
    copy.height = NoteLayout::height(text, copy.width, copy.fontSize);
    DocumentChange change;
    change.update.notes.append(copy);
    return done(std::move(change));
}

OpResult setNoteWidth(const BoardDocument &doc, const Id &noteId, double width)
{
    const NoteData *n = doc.note(noteId);
    if (!n)
        return fail({});
    if (isLocked(doc, noteId))
        return fail(lockedMessage(1));
    width = std::clamp(width, NoteLayout::MinWidth, NoteLayout::MaxWidth);
    if (!std::isfinite(width) || width == n->width)
        return done({});
    NoteData copy = *n;
    copy.width = width;
    copy.height = NoteLayout::height(copy.text, width, copy.fontSize);
    DocumentChange change;
    change.update.notes.append(copy);
    return done(std::move(change));
}

OpResult resizeNote(const BoardDocument &doc, const Id &noteId, double width, const Affine2D &localTransform)
{
    const NoteData *n = doc.note(noteId);
    if (!n)
        return fail({});
    if (isLocked(doc, noteId))
        return fail(lockedMessage(1));
    if (!std::isfinite(width) || !localTransform.isInvertible())
        return fail({});
    NoteData copy = *n;
    copy.width = std::clamp(width, NoteLayout::MinWidth, NoteLayout::MaxWidth);
    copy.height = NoteLayout::height(copy.text, copy.width, copy.fontSize);
    copy.transform = localTransform;
    if (copy == *n)
        return done({});
    DocumentChange change;
    change.update.notes.append(copy);
    return done(std::move(change));
}

OpResult setNoteStyle(const BoardDocument &doc, const QList<Id> &noteIds, double fontSize, quint32 textColor,
                      quint32 backgroundColor)
{
    int skipped = 0;
    DocumentChange change;
    for (const Id &id : editableEntities(doc, noteIds, &skipped)) {
        const NoteData *n = doc.note(id);
        if (!n)
            continue;
        NoteData copy = *n;
        copy.fontSize = std::clamp(fontSize, 4.0, 400.0);
        copy.textColor = textColor;
        copy.backgroundColor = backgroundColor;
        copy.height = NoteLayout::height(copy.text, copy.width, copy.fontSize);
        if (!(copy == *n))
            change.update.notes.append(copy);
    }
    return done(std::move(change), skipped);
}

// ---------------------------------------------------------------------------
// Board

OpResult setBackground(const BoardDocument &doc, quint32 argb)
{
    argb |= 0xFF000000u; // the board background is always opaque
    ViewSettings view = doc.view();
    if (view.backgroundColor == argb)
        return done({});
    view.backgroundColor = argb;
    DocumentChange change;
    change.view = view;
    return done(std::move(change));
}

OpResult setGrayscale(const BoardDocument &doc, bool enabled)
{
    ViewSettings view = doc.view();
    if (view.grayscale == enabled)
        return done({});
    view.grayscale = enabled;
    DocumentChange change;
    change.view = view;
    return done(std::move(change));
}

// ---------------------------------------------------------------------------
// Paste / duplicate

OpResult paste(const BoardDocument &doc, const Snapshot &fragment, std::optional<QPointF> anchor, QPointF offset)
{
    if (fragment.images.isEmpty() && fragment.notes.isEmpty())
        return fail(tr("There is nothing to paste."));

    DocumentChange change;
    QHash<Id, Id> remap; // fragment ID -> new ID
    for (const Asset &asset : fragment.assets) {
        if (const Asset *existing = doc.assetByDigest(asset.sha256)) {
            remap.insert(asset.id, existing->id);
        } else {
            Asset copy = asset;
            copy.id = Id::generate();
            remap.insert(asset.id, copy.id);
            change.insert.assets.append(copy);
        }
    }
    for (const GroupData &g : fragment.groups)
        remap.insert(g.id, Id::generate());
    for (const ImageItemData &i : fragment.images)
        remap.insert(i.id, Id::generate());
    for (const NoteData &n : fragment.notes)
        remap.insert(n.id, Id::generate());

    // Fragment bounds (in the source's board coordinates).
    QHash<Id, Affine2D> groupTransforms;
    for (const GroupData &g : fragment.groups)
        groupTransforms.insert(g.id, g.transform);
    QRectF bounds;
    auto unite = [&](const Affine2D &local, const Id &groupId, const QRectF &rect) {
        const Affine2D world = groupId.isNull() ? local : local * groupTransforms.value(groupId);
        const QRectF b = world.mapRect(rect);
        bounds = bounds.isNull() ? b : bounds.united(b);
    };
    for (const ImageItemData &i : fragment.images)
        unite(i.transform, i.groupId, i.localBounds());
    for (const NoteData &n : fragment.notes)
        unite(n.transform, n.groupId, n.localBounds());
    const QPointF shift = anchor ? *anchor - bounds.center() : offset;
    const Affine2D move = Affine2D::translation(shift.x(), shift.y());

    // Keep the fragment's stacking order, above everything on the board.
    QList<std::pair<double, Id>> topLevel;
    for (const ImageItemData &i : fragment.images)
        if (i.groupId.isNull())
            topLevel.append({i.z, i.id});
    for (const NoteData &n : fragment.notes)
        if (n.groupId.isNull())
            topLevel.append({n.z, n.id});
    for (const GroupData &g : fragment.groups)
        topLevel.append({g.z, g.id});
    std::sort(topLevel.begin(), topLevel.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    QHash<Id, double> newZ;
    double z = doc.maxTopLevelZ();
    for (const auto &entry : std::as_const(topLevel))
        newZ.insert(entry.second, z += 1.0);

    OpResult r;
    for (GroupData g : fragment.groups) {
        const Id oldId = g.id;
        g.id = remap.value(oldId);
        g.transform = g.transform * move;
        g.z = newZ.value(oldId);
        g.locked = false;
        for (Id &child : g.children)
            child = remap.value(child);
        change.insert.groups.append(g);
        r.created.append(g.id);
    }
    for (ImageItemData i : fragment.images) {
        const Id oldId = i.id;
        i.id = remap.value(oldId);
        i.assetId = remap.value(i.assetId);
        if (i.groupId.isNull()) {
            i.transform = i.transform * move;
            i.z = newZ.value(oldId);
            i.locked = false;
            r.created.append(i.id);
        } else {
            i.groupId = remap.value(i.groupId);
        }
        change.insert.images.append(i);
    }
    for (NoteData n : fragment.notes) {
        const Id oldId = n.id;
        n.id = remap.value(oldId);
        if (n.groupId.isNull()) {
            n.transform = n.transform * move;
            n.z = newZ.value(oldId);
            n.locked = false;
            r.created.append(n.id);
        } else {
            n.groupId = remap.value(n.groupId);
        }
        change.insert.notes.append(n);
    }
    r.change = std::move(change);
    return r;
}

OpResult duplicate(const BoardDocument &doc, const QList<Id> &ids, QPointF offset)
{
    QList<Id> units;
    for (const Id &id : unique(ids)) {
        const Id parent = doc.groupOf(id);
        const Id unit = parent.isNull() ? id : parent; // duplicate whole groups
        if (!units.contains(unit))
            units.append(unit);
    }
    const Snapshot fragment = doc.snapshot(units);
    return paste(doc, fragment, std::nullopt, offset);
}

Snapshot clipboardSnapshot(const BoardDocument &doc, const QList<Id> &ids)
{
    QList<Id> topLevel;
    QList<Id> children;
    for (const Id &id : unique(ids))
        (doc.groupOf(id).isNull() ? topLevel : children).append(id);
    Snapshot snap = doc.snapshot(topLevel);
    QSet<Id> assets;
    for (const Asset &a : std::as_const(snap.assets))
        assets.insert(a.id);
    double z = 0.0;
    for (const ImageItemData &i : std::as_const(snap.images))
        z = std::max(z, i.z);
    for (const Id &id : children) {
        if (const ImageItemData *i = doc.image(id)) {
            ImageItemData copy = *i;
            copy.transform = doc.worldTransform(id);
            copy.groupId = Id();
            copy.z = (z += 1.0);
            snap.images.append(copy);
            if (!assets.contains(i->assetId)) {
                assets.insert(i->assetId);
                snap.assets.append(*doc.asset(i->assetId));
            }
        } else if (const NoteData *n = doc.note(id)) {
            NoteData copy = *n;
            copy.transform = doc.worldTransform(id);
            copy.groupId = Id();
            copy.z = (z += 1.0);
            snap.notes.append(copy);
        }
    }
    return snap;
}

} // namespace glacier::editing::ops
