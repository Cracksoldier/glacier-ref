#include "glacier/core/BoardDocument.h"

#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace glacier {

namespace {

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

BoardDocument::BoardDocument(Limits limits, QObject *parent)
    : QObject(parent)
    , m_limits(limits)
    , m_token(Id::generate())
{
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

QString BoardDocument::checkItem(const ImageItemData &item) const
{
    if (item.id.isNull())
        return u"item has a null ID"_s;
    if (!m_assets.contains(item.assetId))
        return u"item references a missing asset"_s;
    if (!item.transform.isInvertible())
        return u"item transform is not finite or not invertible"_s;
    if (!isFinite(item.displaySize.width()) || !isFinite(item.displaySize.height())
        || item.displaySize.width() <= 0.0 || item.displaySize.height() <= 0.0)
        return u"item display size must be finite and positive"_s;
    if (!withinCoordinateLimit(item.boardBounds()))
        return u"item lies outside the supported coordinate range"_s;
    const QRectF &c = item.crop;
    if (!isFinite(c.x()) || !isFinite(c.y()) || !isFinite(c.width()) || !isFinite(c.height())
        || c.x() < 0.0 || c.y() < 0.0 || c.width() <= 0.0 || c.height() <= 0.0
        || c.right() > 1.0 || c.bottom() > 1.0)
        return u"item crop must be a non-empty rectangle inside the image"_s;
    if (!isFinite(item.z))
        return u"item z-order must be finite"_s;
    return {};
}

std::optional<AddImagesResult> BoardDocument::addImages(const QList<NewImage> &images, QString *error)
{
    if (m_items.size() + images.size() > m_limits.maxItems) {
        setError(error, u"a board can hold at most %1 items"_s.arg(m_limits.maxItems));
        return std::nullopt;
    }

    AddImagesResult result;
    QHash<QByteArray, Asset> newAssets; // by digest, for assets not yet in the document
    QList<ImageItemData> newItems;
    newItems.reserve(images.size());

    double z = m_maxZ;
    for (const NewImage &image : images) {
        if (!isValidDigest(image.sha256)) {
            setError(error, u"invalid asset digest"_s);
            return std::nullopt;
        }

        Id assetId;
        if (const auto it = m_assetsByDigest.constFind(image.sha256); it != m_assetsByDigest.cend()) {
            assetId = *it;
        } else if (const auto pending = newAssets.constFind(image.sha256); pending != newAssets.cend()) {
            assetId = pending->id;
        } else {
            if (!isSupportedMediaType(image.mediaType)) {
                setError(error, u"unsupported media type"_s);
                return std::nullopt;
            }
            const QSize size = image.orientedSize;
            if (size.width() <= 0 || size.height() <= 0
                || qint64(size.width()) * size.height() > m_limits.maxDecodedPixels) {
                setError(error, u"asset dimensions are invalid or exceed the limit"_s);
                return std::nullopt;
            }
            if (image.byteLength <= 0 || image.byteLength > m_limits.maxAssetBytes) {
                setError(error, u"asset size is invalid or exceeds the limit"_s);
                return std::nullopt;
            }
            Asset asset{Id::generate(), image.sha256, image.mediaType, size, image.byteLength};
            assetId = asset.id;
            newAssets.insert(asset.sha256, asset);
            result.newAssetIds.append(asset.id);
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
        newItems.append(item);
    }

    // Validate items against existing plus pending assets before committing.
    for (const Asset &asset : std::as_const(newAssets))
        m_assets.insert(asset.id, asset);
    for (const ImageItemData &item : std::as_const(newItems)) {
        const QString problem = checkItem(item);
        if (!problem.isEmpty()) {
            for (const Asset &asset : std::as_const(newAssets))
                m_assets.remove(asset.id);
            setError(error, problem);
            return std::nullopt;
        }
    }

    for (const Asset &asset : std::as_const(newAssets))
        m_assetsByDigest.insert(asset.sha256, asset.id);
    for (const ImageItemData &item : std::as_const(newItems)) {
        m_items.insert(item.id, item);
        result.itemIds.append(item.id);
    }
    m_maxZ = z;

    if (!result.itemIds.isEmpty()) {
        bumpRevision();
        emit itemsAdded(result.itemIds);
    }
    return result;
}

bool BoardDocument::removeItems(const QList<Id> &itemIds, QString *error)
{
    for (const Id &id : itemIds) {
        if (!m_items.contains(id)) {
            setError(error, u"item does not exist"_s);
            return false;
        }
    }
    if (itemIds.isEmpty())
        return true;
    for (const Id &id : itemIds)
        m_items.remove(id);
    bumpRevision();
    emit itemsRemoved(itemIds);
    return true;
}

bool BoardDocument::removeAssets(const QList<Id> &assetIds, QString *error)
{
    QSet<Id> referenced;
    for (const ImageItemData &item : std::as_const(m_items))
        referenced.insert(item.assetId);
    for (const Id &id : assetIds) {
        if (!m_assets.contains(id)) {
            setError(error, u"asset does not exist"_s);
            return false;
        }
        if (referenced.contains(id)) {
            setError(error, u"asset is still referenced"_s);
            return false;
        }
    }
    if (assetIds.isEmpty())
        return true;
    for (const Id &id : assetIds) {
        m_assetsByDigest.remove(m_assets.value(id).sha256);
        m_assets.remove(id);
    }
    bumpRevision();
    return true;
}

const ImageItemData *BoardDocument::item(const Id &id) const
{
    const auto it = m_items.constFind(id);
    return it == m_items.cend() ? nullptr : &*it;
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

QList<Id> BoardDocument::itemIdsByZ() const
{
    QList<const ImageItemData *> items;
    items.reserve(m_items.size());
    for (const ImageItemData &item : m_items)
        items.append(&item);
    std::sort(items.begin(), items.end(), [](const ImageItemData *a, const ImageItemData *b) {
        return a->z != b->z ? a->z < b->z : a->id.toString() < b->id.toString();
    });
    QList<Id> ids;
    ids.reserve(items.size());
    for (const ImageItemData *item : std::as_const(items))
        ids.append(item->id);
    return ids;
}

QRectF BoardDocument::boundingRect() const
{
    QRectF bounds;
    for (const ImageItemData &item : m_items)
        bounds = bounds.isNull() ? item.boardBounds() : bounds.united(item.boardBounds());
    return bounds;
}

QString BoardDocument::validate() const
{
    if (m_items.size() > m_limits.maxItems)
        return u"too many items"_s;
    for (auto it = m_assets.cbegin(); it != m_assets.cend(); ++it) {
        const Asset &asset = *it;
        if (asset.id.isNull() || asset.id != it.key())
            return u"asset ID mismatch"_s;
        if (!isValidDigest(asset.sha256) || m_assetsByDigest.value(asset.sha256) != asset.id)
            return u"asset digest index is inconsistent"_s;
        if (!isSupportedMediaType(asset.mediaType))
            return u"asset has an unsupported media type"_s;
    }
    if (m_assetsByDigest.size() != m_assets.size())
        return u"duplicate asset digests"_s;
    QSet<Id> seen;
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        if (it->id != it.key() || seen.contains(it->id) || m_assets.contains(it->id))
            return u"duplicate or mismatched item ID"_s;
        seen.insert(it->id);
        const QString problem = checkItem(*it);
        if (!problem.isEmpty())
            return problem;
    }
    return {};
}

void BoardDocument::bumpRevision()
{
    ++m_revision;
    emit revisionChanged(m_revision);
}

} // namespace glacier
