#include "glacier/core/BoardDocument.h"

#include "glacier/core/LinkPaths.h"

#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace glacier {

namespace {

constexpr qsizetype MaxGroupNameLength = 256;
constexpr double MaxNoteExtent = 1.0e6;
constexpr double MinFontSize = 1.0;
constexpr double MaxFontSize = 1000.0;

bool isFinite(double v)
{
    return std::isfinite(v);
}

bool withinCoordinateLimit(const QRectF &r)
{
    const double limit = Limits::CoordinateLimit;
    return isFinite(r.left()) && isFinite(r.top()) && isFinite(r.right()) && isFinite(r.bottom())
        && r.left() >= -limit && r.right() <= limit && r.top() >= -limit && r.bottom() <= limit;
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

QString BoardDocument::validateTitle(const QString &title)
{
    if (title.size() > BoardDocument::MaxTitleLength)
        return u"the board title is too long"_s;
    for (const QChar c : title)
        if (c.category() == QChar::Other_Control)
            return u"the board title contains control characters"_s;
    return {};
}

namespace {

bool validCrop(const QRectF &c)
{
    return isFinite(c.x()) && isFinite(c.y()) && isFinite(c.width()) && isFinite(c.height()) && c.x() >= 0.0
        && c.y() >= 0.0 && c.width() > 0.0 && c.height() > 0.0 && c.right() <= 1.0 + 1e-12
        && c.bottom() <= 1.0 + 1e-12;
}

template<typename T>
void sortById(QList<T> &list)
{
    std::sort(list.begin(), list.end(), [](const T &a, const T &b) { return a.id.toString() < b.id.toString(); });
}

} // namespace

BoardDocument::BoardDocument(Limits limits, QObject *parent)
    : QObject(parent)
    , m_limits(limits)
    , m_token(Id::generate())
{
    m_metadata.documentId = Id::generate();
    m_metadata.created = QDateTime::currentDateTimeUtc();
    m_metadata.modified = m_metadata.created;
}

bool BoardDocument::setMetadata(const BoardMetadata &metadata, QString *error)
{
    if (metadata.documentId.isNull()) {
        setError(error, u"the document ID is missing"_s);
        return false;
    }
    if (const QString e = validateTitle(metadata.title); !e.isEmpty()) {
        setError(error, e);
        return false;
    }
    if (metadata == m_metadata)
        return true;
    m_metadata = metadata;
    emit metadataChanged();
    return true;
}

bool BoardDocument::isValidDigest(const QByteArray &sha256)
{
    if (sha256.size() != 64)
        return false;
    return std::all_of(sha256.cbegin(), sha256.cend(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

bool BoardDocument::isSupportedMediaType(const QString &mediaType)
{
    static const QSet<QString> types = {
        u"image/png"_s, u"image/jpeg"_s, u"image/webp"_s, u"image/bmp"_s,
        u"image/gif"_s, u"image/tiff"_s, u"image/svg+xml"_s,
    };
    return types.contains(mediaType);
}

// ---------------------------------------------------------------------------
// Validation of a complete state

QString BoardDocument::validateState(const State &s, const Limits &limits)
{
    if (s.images.size() + s.notes.size() > limits.maxItems)
        return u"a board can hold at most %1 items"_s.arg(limits.maxItems);

    QSet<Id> allIds;
    auto claim = [&allIds](const Id &id) {
        if (id.isNull() || allIds.contains(id))
            return false;
        allIds.insert(id);
        return true;
    };

    QSet<QByteArray> digests;
    for (auto it = s.assets.cbegin(); it != s.assets.cend(); ++it) {
        const Asset &a = *it;
        if (a.id != it.key() || !claim(a.id))
            return u"duplicate or mismatched asset ID"_s;
        if (!isValidDigest(a.sha256) || digests.contains(a.sha256))
            return u"invalid or duplicate asset digest"_s;
        digests.insert(a.sha256);
        if (!isSupportedMediaType(a.mediaType))
            return u"asset has an unsupported media type"_s;
        if (a.orientedSize.width() <= 0 || a.orientedSize.height() <= 0
            || qint64(a.orientedSize.width()) * a.orientedSize.height() > limits.maxDecodedPixels)
            return u"asset dimensions are invalid or exceed the limit"_s;
        if (a.byteLength <= 0 || a.byteLength > limits.maxAssetBytes)
            return u"asset size is invalid or exceeds the limit"_s;
        if (a.linked != !a.linkPath.isEmpty())
            return u"a linked asset needs a path, and only linked assets have one"_s;
        if (a.linked && (!link_paths::isAbsolute(a.linkPath) || link_paths::normalize(a.linkPath) != a.linkPath))
            return u"a linked asset path must be absolute and normalized"_s;
    }

    // Groups first: children lists define membership.
    QHash<Id, Id> memberOf; // child -> group
    for (auto it = s.groups.cbegin(); it != s.groups.cend(); ++it) {
        const GroupData &g = *it;
        if (g.id != it.key() || !claim(g.id))
            return u"duplicate or mismatched group ID"_s;
        if (g.children.isEmpty())
            return u"a group must have at least one member"_s;
        if (g.name.size() > MaxGroupNameLength)
            return u"group name is too long"_s;
        if (!g.transform.isInvertible())
            return u"group transform is not finite or not invertible"_s;
        if (!isFinite(g.z))
            return u"group z-order must be finite"_s;
        for (const Id &child : g.children) {
            if (memberOf.contains(child))
                return u"an item belongs to more than one group or is listed twice"_s;
            if (!s.images.contains(child) && !s.notes.contains(child))
                return u"group member is not an image or note (groups cannot be nested)"_s;
            memberOf.insert(child, g.id);
        }
    }

    auto checkPlacement = [&](const Id &id, const Id &groupId, const Affine2D &transform, const QRectF &local,
                              double z) -> QString {
        if (!transform.isInvertible())
            return u"item transform is not finite or not invertible"_s;
        if (!isFinite(z))
            return u"item z-order must be finite"_s;
        if (memberOf.value(id) != groupId)
            return u"group membership is inconsistent"_s;
        Affine2D world = transform;
        if (!groupId.isNull())
            world = transform * s.groups.value(groupId).transform;
        if (!world.isInvertible() || !withinCoordinateLimit(world.mapRect(local)))
            return u"item lies outside the supported coordinate range"_s;
        return {};
    };

    for (auto it = s.images.cbegin(); it != s.images.cend(); ++it) {
        const ImageItemData &item = *it;
        if (item.id != it.key() || !claim(item.id))
            return u"duplicate or mismatched item ID"_s;
        if (!s.assets.contains(item.assetId))
            return u"item references a missing asset"_s;
        if (!isFinite(item.displaySize.width()) || !isFinite(item.displaySize.height())
            || item.displaySize.width() <= 0.0 || item.displaySize.height() <= 0.0)
            return u"item display size must be finite and positive"_s;
        if (!validCrop(item.crop))
            return u"item crop must be a non-empty rectangle inside the image"_s;
        if (const QString e = checkPlacement(item.id, item.groupId, item.transform, item.localBounds(), item.z);
            !e.isEmpty())
            return e;
    }

    for (auto it = s.notes.cbegin(); it != s.notes.cend(); ++it) {
        const NoteData &note = *it;
        if (note.id != it.key() || !claim(note.id))
            return u"duplicate or mismatched note ID"_s;
        if (note.text.size() > limits.maxNoteCharacters)
            return u"a note can hold at most %1 characters"_s.arg(limits.maxNoteCharacters);
        if (!isFinite(note.width) || !isFinite(note.height) || note.width <= 0.0 || note.height <= 0.0
            || note.width > MaxNoteExtent || note.height > MaxNoteExtent)
            return u"note size must be finite and positive"_s;
        if (!isFinite(note.fontSize) || note.fontSize < MinFontSize || note.fontSize > MaxFontSize)
            return u"note font size is out of range"_s;
        if (const QString e = checkPlacement(note.id, note.groupId, note.transform, note.localBounds(), note.z);
            !e.isEmpty())
            return e;
    }
    return {};
}

QString BoardDocument::validate() const
{
    State s{m_assets, m_images, m_notes, m_groups, m_view};
    if (const QString e = validateState(s, m_limits); !e.isEmpty())
        return e;
    if (m_assetsByDigest.size() != m_assets.size())
        return u"asset digest index is inconsistent"_s;
    for (const Asset &a : m_assets) {
        if (m_assetsByDigest.value(a.sha256) != a.id)
            return u"asset digest index is inconsistent"_s;
    }
    return {};
}

QString BoardDocument::validateSnapshot(const Snapshot &snapshot, const ViewSettings &view, const Limits &limits)
{
    if (snapshot.images.size() + snapshot.notes.size() > limits.maxItems)
        return u"a board can hold at most %1 items"_s.arg(limits.maxItems);
    State s;
    s.view = view;
    QSet<Id> seen;
    QString problem;
    auto collect = [&](auto &target, const auto &records) {
        for (const auto &record : records) {
            if (record.id.isNull() || seen.contains(record.id)) {
                problem = u"an ID is missing or used more than once"_s;
                return false;
            }
            seen.insert(record.id);
            target.insert(record.id, record);
        }
        return true;
    };
    if (!collect(s.assets, snapshot.assets) || !collect(s.images, snapshot.images)
        || !collect(s.notes, snapshot.notes) || !collect(s.groups, snapshot.groups))
        return problem;
    return validateState(s, limits);
}

// ---------------------------------------------------------------------------
// The single mutation primitive

std::optional<DocumentChange> BoardDocument::apply(const DocumentChange &change, QString *error)
{
    if (!change.update.assets.isEmpty()) {
        setError(error, u"assets are immutable"_s);
        return std::nullopt;
    }
    if (change.isEmpty())
        return DocumentChange{};

    State s{m_assets, m_images, m_notes, m_groups, m_view};
    DocumentChange inverse;
    DocumentDelta delta;

    // 1. Removals.
    QSet<Id> removed;
    for (const Id &id : change.remove) {
        if (removed.contains(id)) {
            setError(error, u"an entity is removed twice"_s);
            return std::nullopt;
        }
        removed.insert(id);
        if (auto it = s.images.find(id); it != s.images.end()) {
            inverse.insert.images.append(*it);
            s.images.erase(it);
        } else if (auto nt = s.notes.find(id); nt != s.notes.end()) {
            inverse.insert.notes.append(*nt);
            s.notes.erase(nt);
        } else if (auto gt = s.groups.find(id); gt != s.groups.end()) {
            inverse.insert.groups.append(*gt);
            s.groups.erase(gt);
        } else if (auto at = s.assets.find(id); at != s.assets.end()) {
            inverse.insert.assets.append(*at);
            s.assets.erase(at);
        } else {
            setError(error, u"cannot remove an entity that does not exist"_s);
            return std::nullopt;
        }
        delta.removed.append(id);
    }

    // 2. Insertions (IDs must be new).
    auto exists = [&s](const Id &id) {
        return s.images.contains(id) || s.notes.contains(id) || s.groups.contains(id) || s.assets.contains(id);
    };
    QString problem;
    auto insertAll = [&](auto &target, const auto &records) {
        for (const auto &record : records) {
            if (record.id.isNull() || exists(record.id)) {
                problem = u"inserted entity ID is null or already in use"_s;
                return false;
            }
            target.insert(record.id, record);
            inverse.remove.append(record.id);
            delta.inserted.append(record.id);
        }
        return true;
    };
    if (!insertAll(s.assets, change.insert.assets) || !insertAll(s.images, change.insert.images)
        || !insertAll(s.notes, change.insert.notes) || !insertAll(s.groups, change.insert.groups)) {
        setError(error, problem);
        return std::nullopt;
    }

    // 3. Updates (records replace existing ones with the same ID).
    QSet<Id> updated;
    auto updateAll = [&](auto &target, const auto &records, auto &inverseList) {
        for (const auto &record : records) {
            auto it = target.find(record.id);
            if (it == target.end() || updated.contains(record.id) || delta.inserted.contains(record.id)) {
                problem = u"cannot update an entity that does not exist (or twice)"_s;
                return false;
            }
            updated.insert(record.id);
            inverseList.append(*it);
            *it = record;
            delta.updated.append(record.id);
        }
        return true;
    };
    if (!updateAll(s.images, change.update.images, inverse.update.images)
        || !updateAll(s.notes, change.update.notes, inverse.update.notes)
        || !updateAll(s.groups, change.update.groups, inverse.update.groups)) {
        setError(error, problem);
        return std::nullopt;
    }

    // 4. View settings and title.
    if (change.view && *change.view != s.view) {
        inverse.view = s.view;
        s.view = *change.view;
        delta.viewChanged = true;
    }
    if (change.title && *change.title != m_metadata.title) {
        if (const QString e = validateTitle(*change.title); !e.isEmpty()) {
            setError(error, e);
            return std::nullopt;
        }
        inverse.title = m_metadata.title;
        delta.titleChanged = true;
    }

    if (const QString e = validateState(s, m_limits); !e.isEmpty()) {
        setError(error, e);
        return std::nullopt;
    }

    // Commit.
    m_assets = std::move(s.assets);
    m_images = std::move(s.images);
    m_notes = std::move(s.notes);
    m_groups = std::move(s.groups);
    m_view = s.view;
    if (delta.titleChanged)
        m_metadata.title = *change.title;
    m_assetsByDigest.clear();
    for (const Asset &a : std::as_const(m_assets))
        m_assetsByDigest.insert(a.sha256, a.id);

    ++m_revision;
    emit revisionChanged(m_revision);
    emit changed(delta);
    if (delta.titleChanged)
        emit metadataChanged();
    return inverse;
}

// ---------------------------------------------------------------------------
// Convenience mutations

std::optional<DocumentChange> BoardDocument::addImagesChange(const QList<NewImage> &images, AddImagesResult *result,
                                                             QString *error) const
{
    DocumentChange change;
    AddImagesResult r;
    QHash<QByteArray, Id> pending;
    double z = maxTopLevelZ();
    for (const NewImage &image : images) {
        if (!isValidDigest(image.sha256)) {
            setError(error, u"invalid asset digest"_s);
            return std::nullopt;
        }
        Id assetId;
        if (const Asset *existing = assetByDigest(image.sha256)) {
            assetId = existing->id;
        } else if (const auto it = pending.constFind(image.sha256); it != pending.cend()) {
            assetId = *it;
        } else {
            Asset asset{Id::generate(), image.sha256, image.mediaType, image.orientedSize,
                        image.byteLength, image.linked, image.linked ? image.linkPath : QString()};
            assetId = asset.id;
            pending.insert(asset.sha256, asset.id);
            change.insert.assets.append(asset);
            r.newAssetIds.append(asset.id);
        }
        if (!isFinite(image.position.x()) || !isFinite(image.position.y())) {
            setError(error, u"item position must be finite"_s);
            return std::nullopt;
        }
        ImageItemData item;
        item.id = Id::generate();
        item.assetId = assetId;
        item.transform = Affine2D::translation(image.position.x(), image.position.y());
        item.displaySize = image.displaySize.isEmpty() ? QSizeF(image.orientedSize) : image.displaySize;
        z += 1.0;
        item.z = z;
        change.insert.images.append(item);
        r.itemIds.append(item.id);
    }
    if (result)
        *result = r;
    return change;
}

std::optional<AddImagesResult> BoardDocument::addImages(const QList<NewImage> &images, QString *error)
{
    AddImagesResult result;
    const auto change = addImagesChange(images, &result, error);
    if (!change || !apply(*change, error))
        return std::nullopt;
    return result;
}

bool BoardDocument::removeItems(const QList<Id> &ids, QString *error)
{
    DocumentChange change;
    change.remove = ids;
    return apply(change, error).has_value();
}

bool BoardDocument::removeAssets(const QList<Id> &assetIds, QString *error)
{
    for (const Id &id : assetIds) {
        if (!m_assets.contains(id)) {
            setError(error, u"asset does not exist"_s);
            return false;
        }
        for (const ImageItemData &item : std::as_const(m_images)) {
            if (item.assetId == id) {
                setError(error, u"asset is still referenced"_s);
                return false;
            }
        }
    }
    DocumentChange change;
    change.remove = assetIds;
    return apply(change, error).has_value();
}

// ---------------------------------------------------------------------------
// Queries

EntityKind BoardDocument::kind(const Id &id) const
{
    if (m_images.contains(id))
        return EntityKind::Image;
    if (m_notes.contains(id))
        return EntityKind::Note;
    if (m_groups.contains(id))
        return EntityKind::Group;
    if (m_assets.contains(id))
        return EntityKind::Asset;
    return EntityKind::None;
}

const Asset *BoardDocument::asset(const Id &id) const
{
    const auto it = m_assets.constFind(id);
    return it == m_assets.cend() ? nullptr : &*it;
}

const Asset *BoardDocument::assetByDigest(const QByteArray &sha256) const
{
    const auto it = m_assetsByDigest.constFind(sha256);
    return it == m_assetsByDigest.cend() ? nullptr : asset(*it);
}

const ImageItemData *BoardDocument::image(const Id &id) const
{
    const auto it = m_images.constFind(id);
    return it == m_images.cend() ? nullptr : &*it;
}

const NoteData *BoardDocument::note(const Id &id) const
{
    const auto it = m_notes.constFind(id);
    return it == m_notes.cend() ? nullptr : &*it;
}

const GroupData *BoardDocument::group(const Id &id) const
{
    const auto it = m_groups.constFind(id);
    return it == m_groups.cend() ? nullptr : &*it;
}

Id BoardDocument::groupOf(const Id &id) const
{
    if (const ImageItemData *i = image(id))
        return i->groupId;
    if (const NoteData *n = note(id))
        return n->groupId;
    return {};
}

double BoardDocument::maxTopLevelZ() const
{
    double z = 0.0;
    for (const ImageItemData &i : m_images)
        if (i.groupId.isNull())
            z = std::max(z, i.z);
    for (const NoteData &n : m_notes)
        if (n.groupId.isNull())
            z = std::max(z, n.z);
    for (const GroupData &g : m_groups)
        z = std::max(z, g.z);
    return z;
}

QList<Id> BoardDocument::topLevelIdsByZ() const
{
    QList<std::pair<double, Id>> entries;
    for (const ImageItemData &i : m_images)
        if (i.groupId.isNull())
            entries.append({i.z, i.id});
    for (const NoteData &n : m_notes)
        if (n.groupId.isNull())
            entries.append({n.z, n.id});
    for (const GroupData &g : m_groups)
        entries.append({g.z, g.id});
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first < b.first : a.second.toString() < b.second.toString();
    });
    QList<Id> ids;
    ids.reserve(entries.size());
    for (const auto &e : std::as_const(entries))
        ids.append(e.second);
    return ids;
}

QList<Id> BoardDocument::itemIdsByZ() const
{
    QList<Id> ids;
    for (const Id &id : topLevelIdsByZ()) {
        if (const GroupData *g = group(id)) {
            for (const Id &child : g->children)
                if (m_images.contains(child))
                    ids.append(child);
        } else if (m_images.contains(id)) {
            ids.append(id);
        }
    }
    return ids;
}

Affine2D BoardDocument::worldTransform(const Id &id) const
{
    auto withGroup = [this](const Affine2D &t, const Id &groupId) {
        if (const GroupData *g = group(groupId))
            return t * g->transform;
        return t;
    };
    if (const ImageItemData *i = image(id))
        return withGroup(i->transform, i->groupId);
    if (const NoteData *n = note(id))
        return withGroup(n->transform, n->groupId);
    if (const GroupData *g = group(id))
        return g->transform;
    return {};
}

QRectF BoardDocument::worldBounds(const Id &id) const
{
    if (const ImageItemData *i = image(id))
        return worldTransform(id).mapRect(i->localBounds());
    if (const NoteData *n = note(id))
        return worldTransform(id).mapRect(n->localBounds());
    if (const GroupData *g = group(id)) {
        QRectF bounds;
        for (const Id &child : g->children) {
            const QRectF b = worldBounds(child);
            bounds = bounds.isNull() ? b : bounds.united(b);
        }
        return bounds;
    }
    return {};
}

QRectF BoardDocument::boundingRect() const
{
    QRectF bounds;
    auto unite = [&bounds](const QRectF &b) { bounds = bounds.isNull() ? b : bounds.united(b); };
    for (const ImageItemData &i : m_images)
        unite(worldBounds(i.id));
    for (const NoteData &n : m_notes)
        unite(worldBounds(n.id));
    return bounds;
}

Snapshot BoardDocument::snapshot(const QList<Id> &ids) const
{
    Snapshot snap;
    QSet<Id> seen;
    auto addImage = [&](const Id &id) {
        if (seen.contains(id))
            return;
        if (const ImageItemData *i = image(id)) {
            seen.insert(id);
            snap.images.append(*i);
            if (!seen.contains(i->assetId)) {
                seen.insert(i->assetId);
                snap.assets.append(m_assets.value(i->assetId));
            }
        }
    };
    auto addNote = [&](const Id &id) {
        if (seen.contains(id))
            return;
        if (const NoteData *n = note(id)) {
            seen.insert(id);
            snap.notes.append(*n);
        }
    };
    for (const Id &id : ids) {
        if (const GroupData *g = group(id)) {
            if (seen.contains(id))
                continue;
            seen.insert(id);
            snap.groups.append(*g);
            for (const Id &child : g->children) {
                addImage(child);
                addNote(child);
            }
        } else {
            addImage(id);
            addNote(id);
        }
    }
    return snap;
}

Snapshot BoardDocument::snapshotAll() const
{
    Snapshot snap;
    snap.assets = m_assets.values();
    snap.images = m_images.values();
    snap.notes = m_notes.values();
    snap.groups = m_groups.values();
    sortById(snap.assets);
    sortById(snap.images);
    sortById(snap.notes);
    sortById(snap.groups);
    return snap;
}

} // namespace glacier
