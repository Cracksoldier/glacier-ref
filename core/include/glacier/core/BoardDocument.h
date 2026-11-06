#pragma once

#include "glacier/core/Affine2D.h"
#include "glacier/core/Id.h"
#include "glacier/core/Limits.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

#include <optional>

namespace glacier {

// An image asset referenced by board items. Embedded assets are identified by
// the SHA-256 of their stored bytes (lowercase hex) and deduplicated by it.
// Assets are immutable.
struct Asset
{
    Id id;
    QByteArray sha256;   // 64 lowercase hex characters
    QString mediaType;   // e.g. "image/png"
    QSize orientedSize;  // pixel size after EXIF orientation (SVG: intrinsic size)
    qint64 byteLength = 0;

    friend bool operator==(const Asset &, const Asset &) = default;
};

// An image placed on the board. `displaySize` is the natural (oriented) size;
// all resizing, rotation and flipping is in `transform`, which maps the
// item-local frame (0,0)-(displaySize) to the parent: the board for top-level
// items, the group's local frame for group children.
struct ImageItemData
{
    Id id;
    Id assetId;
    Affine2D transform;
    QSizeF displaySize;
    QRectF crop{0.0, 0.0, 1.0, 1.0}; // normalized, image-local
    double z = 0.0;
    bool locked = false;
    Id groupId; // null: top level

    // Visible (cropped) rectangle in item-local coordinates.
    QRectF localBounds() const
    {
        return QRectF(crop.x() * displaySize.width(), crop.y() * displaySize.height(),
                      crop.width() * displaySize.width(), crop.height() * displaySize.height());
    }

    friend bool operator==(const ImageItemData &, const ImageItemData &) = default;
};

// A plain-text note. `width` is the wrap width; `height` is computed from the
// text layout by the editing layer and stored.
struct NoteData
{
    Id id;
    QString text;
    double width = 240.0;
    double height = 64.0;
    double fontSize = 16.0;
    quint32 textColor = 0xFF1E1E1Eu;       // ARGB
    quint32 backgroundColor = 0xFFFFF1B0u; // ARGB
    Affine2D transform;
    double z = 0.0;
    bool locked = false;
    Id groupId;

    QRectF localBounds() const { return QRectF(0.0, 0.0, width, height); }

    friend bool operator==(const NoteData &, const NoteData &) = default;
};

// A flat group (spec §3): ordered children (images and notes) whose
// transforms are relative to the group's transform. Groups cannot nest.
struct GroupData
{
    Id id;
    QString name;
    Affine2D transform;
    QList<Id> children;
    double z = 0.0;
    bool locked = false;

    friend bool operator==(const GroupData &, const GroupData &) = default;
};

// Board-wide view preferences persisted with the board (spec §3).
struct ViewSettings
{
    quint32 backgroundColor = 0xFF3A3D41u; // ARGB, neutral default
    bool grayscale = false;

    friend bool operator==(const ViewSettings &, const ViewSettings &) = default;
};

// A set of entity records, used for inserts, updates, snapshots and the
// clipboard. Lists are in no particular order unless stated.
struct Snapshot
{
    QList<Asset> assets;
    QList<ImageItemData> images;
    QList<NoteData> notes;
    QList<GroupData> groups;

    bool isEmpty() const { return assets.isEmpty() && images.isEmpty() && notes.isEmpty() && groups.isEmpty(); }
    friend bool operator==(const Snapshot &, const Snapshot &) = default;
};

// One atomic document mutation: removals, then insertions, then updates
// (replacements of existing records by ID), then the view settings.
struct DocumentChange
{
    QList<Id> remove;
    Snapshot insert;
    Snapshot update; // assets are immutable and cannot be updated
    std::optional<ViewSettings> view;

    bool isEmpty() const { return remove.isEmpty() && insert.isEmpty() && update.isEmpty() && !view; }
};

// What a successful apply() changed, for presentation layers.
struct DocumentDelta
{
    QList<Id> removed;
    QList<Id> inserted;
    QList<Id> updated;
    bool viewChanged = false;
};

// Input for BoardDocument::addImages.
struct NewImage
{
    QByteArray sha256;
    QString mediaType;
    QSize orientedSize;
    qint64 byteLength = 0;
    QPointF position;     // top-left on the board
    QSizeF displaySize;   // empty: use orientedSize
};

struct AddImagesResult
{
    QList<Id> itemIds;     // in input order
    QList<Id> newAssetIds; // assets created by this call (not previously present)
};

enum class EntityKind { None, Asset, Image, Note, Group };

// Authoritative board model (spec §6). Independent of graphics items; the only
// source for serialization. All mutations go through apply(), which validates
// the complete resulting state before committing and returns the exact
// inverse change (the basis of undo/redo).
class BoardDocument : public QObject
{
    Q_OBJECT

public:
    explicit BoardDocument(Limits limits = {}, QObject *parent = nullptr);

    // Changes when the document is replaced; tags asynchronous results.
    Id documentToken() const { return m_token; }
    // Incremented by every committed change.
    quint64 revision() const { return m_revision; }
    bool isDirty() const { return m_revision != m_cleanRevision; }
    void markClean() { m_cleanRevision = m_revision; }
    const Limits &limits() const { return m_limits; }

    // Applies a change atomically. On success returns the inverse change
    // (applying it restores the previous state exactly); on failure nothing
    // changes and std::nullopt is returned.
    std::optional<DocumentChange> apply(const DocumentChange &change, QString *error = nullptr);

    // Convenience: builds and applies the change for an import batch.
    std::optional<AddImagesResult> addImages(const QList<NewImage> &images, QString *error = nullptr);
    // Builds (without applying) the change for an import batch.
    std::optional<DocumentChange> addImagesChange(const QList<NewImage> &images, AddImagesResult *result = nullptr,
                                                  QString *error = nullptr) const;
    bool removeItems(const QList<Id> &ids, QString *error = nullptr);
    bool removeAssets(const QList<Id> &assetIds, QString *error = nullptr);

    EntityKind kind(const Id &id) const;
    const Asset *asset(const Id &id) const;
    const Asset *assetByDigest(const QByteArray &sha256) const;
    const ImageItemData *item(const Id &id) const { return image(id); }
    const ImageItemData *image(const Id &id) const;
    const NoteData *note(const Id &id) const;
    const GroupData *group(const Id &id) const;
    const ViewSettings &view() const { return m_view; }

    const QHash<Id, Asset> &assets() const { return m_assets; }
    const QHash<Id, ImageItemData> &images() const { return m_images; }
    const QHash<Id, NoteData> &notes() const { return m_notes; }
    const QHash<Id, GroupData> &groups() const { return m_groups; }

    // Group of an image or note (null if top level or not a child).
    Id groupOf(const Id &id) const;
    // Top-level entities (ungrouped images and notes, groups), back to front.
    QList<Id> topLevelIdsByZ() const;
    // Image IDs in paint order (back to front), including group children.
    QList<Id> itemIdsByZ() const;
    qsizetype itemCount() const { return m_images.size() + m_notes.size(); }
    qsizetype assetCount() const { return m_assets.size(); }
    double maxTopLevelZ() const;

    // Board transform of an image, note or group (group children include the
    // group transform), and the axis-aligned board bounds of what is visible.
    Affine2D worldTransform(const Id &id) const;
    QRectF worldBounds(const Id &id) const;
    // Union of all visible bounds; null if empty.
    QRectF boundingRect() const;

    // Records for the given entity IDs (groups include their children) and the
    // assets they reference.
    Snapshot snapshot(const QList<Id> &ids) const;
    // The complete document, sorted by ID (for comparisons in tests).
    Snapshot snapshotAll() const;

    // Full invariant check; returns an empty string when valid.
    QString validate() const;

    static bool isValidDigest(const QByteArray &sha256);
    static bool isSupportedMediaType(const QString &mediaType);

signals:
    void changed(const glacier::DocumentDelta &delta);
    void revisionChanged(quint64 revision);

private:
    struct State
    {
        QHash<Id, Asset> assets;
        QHash<Id, ImageItemData> images;
        QHash<Id, NoteData> notes;
        QHash<Id, GroupData> groups;
        ViewSettings view;
    };
    static QString validateState(const State &state, const Limits &limits);

    Limits m_limits;
    Id m_token;
    quint64 m_revision = 0;
    quint64 m_cleanRevision = 0;
    QHash<Id, Asset> m_assets;
    QHash<QByteArray, Id> m_assetsByDigest;
    QHash<Id, ImageItemData> m_images;
    QHash<Id, NoteData> m_notes;
    QHash<Id, GroupData> m_groups;
    ViewSettings m_view;
};

} // namespace glacier
