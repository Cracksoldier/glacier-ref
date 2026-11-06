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
struct Asset
{
    Id id;
    QByteArray sha256;   // 64 lowercase hex characters
    QString mediaType;   // e.g. "image/png"
    QSize orientedSize;  // pixel size after EXIF orientation (SVG: intrinsic size)
    qint64 byteLength = 0;
};

// An image placed on the board. Display geometry is in board units; the
// transform maps item-local coordinates (0,0)-(displaySize) to the board.
struct ImageItemData
{
    Id id;
    Id assetId;
    Affine2D transform;
    QSizeF displaySize;
    QRectF crop{0.0, 0.0, 1.0, 1.0}; // normalized, image-local
    double z = 0.0;
    bool locked = false;

    QRectF boardBounds() const { return transform.mapRect(QRectF(QPointF(0, 0), displaySize)); }
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

// Authoritative board model (spec §6). Independent of graphics items; the only
// source for serialization. Every committed mutation increments revision().
class BoardDocument : public QObject
{
    Q_OBJECT

public:
    explicit BoardDocument(Limits limits = {}, QObject *parent = nullptr);

    // Changes when the document is replaced; tags asynchronous results.
    Id documentToken() const { return m_token; }
    quint64 revision() const { return m_revision; }
    bool isDirty() const { return m_revision != m_cleanRevision; }
    void markClean() { m_cleanRevision = m_revision; }
    const Limits &limits() const { return m_limits; }

    // Adds all images or none. Existing assets with the same digest are reused.
    std::optional<AddImagesResult> addImages(const QList<NewImage> &images, QString *error = nullptr);

    // Removes the given items (all must exist). Assets stay until removeAssets.
    bool removeItems(const QList<Id> &itemIds, QString *error = nullptr);

    // Removes unreferenced assets (all must exist and be unreferenced).
    bool removeAssets(const QList<Id> &assetIds, QString *error = nullptr);

    const ImageItemData *item(const Id &id) const;
    const Asset *asset(const Id &id) const;
    const Asset *assetByDigest(const QByteArray &sha256) const;

    // Item IDs in ascending z-order (back to front).
    QList<Id> itemIdsByZ() const;
    qsizetype itemCount() const { return m_items.size(); }
    qsizetype assetCount() const { return m_assets.size(); }

    // Union of all item bounds; null if empty.
    QRectF boundingRect() const;

    // Full invariant check; returns an empty string when valid.
    QString validate() const;

    static bool isValidDigest(const QByteArray &sha256);
    static bool isSupportedMediaType(const QString &mediaType);

signals:
    void itemsAdded(const QList<glacier::Id> &itemIds);
    void itemsRemoved(const QList<glacier::Id> &itemIds);
    void revisionChanged(quint64 revision);

private:
    QString checkItem(const ImageItemData &item) const;
    void bumpRevision();

    Limits m_limits;
    Id m_token;
    quint64 m_revision = 0;
    quint64 m_cleanRevision = 0;
    double m_maxZ = 0.0;
    QHash<Id, Asset> m_assets;
    QHash<QByteArray, Id> m_assetsByDigest;
    QHash<Id, ImageItemData> m_items;
};

} // namespace glacier
