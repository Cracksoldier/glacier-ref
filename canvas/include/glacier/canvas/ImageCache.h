#pragma once

#include "glacier/core/Limits.h"
#include "glacier/io/ImageDecoder.h"

#include <QCache>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>

#include <memory>

class QThreadPool;

namespace glacier {
class SessionAssetStore;
}

namespace glacier::canvas {

// Process-wide decoded-image cache (spec §6, M1.09). GUI thread only.
//
// Each asset can be cached at resolution levels whose longest side is 256,
// 512, ..., 8192 px (capped at the asset's own size). Entries are QPixmaps in
// an LRU (QCache) bounded by a byte budget. best() returns the best level that
// is available right now and schedules the requested level on the worker pool;
// levelReady() announces new levels so items can repaint.
class ImageCache : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 DefaultBudgetBytes = qint64(512) * 1024 * 1024;
    static constexpr int MaxPendingDecodes = 64;

    struct Stats
    {
        qint64 bytesUsed = 0;
        qint64 budgetBytes = 0;
        int entries = 0;
        quint64 hits = 0;
        quint64 misses = 0;
        quint64 decodesStarted = 0;
        quint64 decodesFinished = 0;
        quint64 decodesFailed = 0;
    };

    ImageCache(std::shared_ptr<SessionAssetStore> store, QThreadPool *pool,
               qint64 budgetBytes = DefaultBudgetBytes, Limits limits = {}, QObject *parent = nullptr);
    ~ImageCache() override;

    // Makes an asset known to the cache (format and size are needed to decode).
    void registerAsset(const QByteArray &sha256, const QString &mediaType, QSize orientedSize);

    // Seeds a level from an image decoded elsewhere (e.g. import thumbnails).
    void insert(const QByteArray &sha256, const QImage &image);

    // Best available pixmap for drawing at `neededSide` device pixels (longest
    // side). Null if nothing is cached yet; schedules the needed level. With
    // `grayscale`, returns a grayscale rendering (spec §3 preview), converted
    // once per level and cached in the same budget; assets are never changed.
    QPixmap best(const QByteArray &sha256, int neededSide, bool grayscale = false);

    // Level side that would be decoded for a request (for tests/benchmarks).
    int levelFor(const QByteArray &sha256, int neededSide) const;

    Stats stats() const;
    void setBudget(qint64 budgetBytes);

signals:
    void levelReady(const QByteArray &sha256);

private:
    struct AssetInfo
    {
        io::ImageInfo info;
        int maxSide = 0;
    };
    static QString key(const QByteArray &sha256, int side);
    QPixmap bestColor(const QByteArray &sha256, int neededSide);
    void schedule(const QByteArray &sha256, int side);

    std::shared_ptr<SessionAssetStore> m_store;
    QThreadPool *m_pool;
    Limits m_limits;
    QCache<QString, QPixmap> m_cache; // cost in KiB
    QHash<QByteArray, AssetInfo> m_assets;
    QSet<QString> m_pending;
    Stats m_stats;
};

} // namespace glacier::canvas
