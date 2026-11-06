#include "glacier/canvas/ImageCache.h"

#include "glacier/core/SessionAssetStore.h"
#include "glacier/io/ImageFormats.h"

#include <QFutureWatcher>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <limits>

namespace glacier::canvas {

namespace {

constexpr std::array<int, 6> Levels = {256, 512, 1024, 2048, 4096, io::MaxDecodeSide};

int costKiB(const QPixmap &pixmap)
{
    return std::max(1, int(qint64(pixmap.width()) * pixmap.height() * 4 / 1024));
}

} // namespace

ImageCache::ImageCache(std::shared_ptr<SessionAssetStore> store, QThreadPool *pool, qint64 budgetBytes,
                       Limits limits, QObject *parent)
    : QObject(parent)
    , m_store(std::move(store))
    , m_pool(pool)
    , m_limits(limits)
{
    setBudget(budgetBytes);
}

ImageCache::~ImageCache() = default; // watchers are children; running decodes finish harmlessly

void ImageCache::setBudget(qint64 budgetBytes)
{
    m_stats.budgetBytes = budgetBytes;
    m_cache.setMaxCost(int(std::clamp<qint64>(budgetBytes / 1024, 1, std::numeric_limits<int>::max())));
}

QString ImageCache::key(const QByteArray &sha256, int side)
{
    return QString::fromLatin1(sha256) + u':' + QString::number(side);
}

void ImageCache::registerAsset(const QByteArray &sha256, const QString &mediaType, QSize orientedSize)
{
    if (m_assets.contains(sha256))
        return;
    const auto format = io::formatFromMediaType(mediaType);
    if (!format || orientedSize.isEmpty())
        return;
    AssetInfo asset;
    asset.info.format = *format;
    asset.info.orientedSize = orientedSize;
    asset.maxSide = std::min(std::max(orientedSize.width(), orientedSize.height()), io::MaxDecodeSide);
    m_assets.insert(sha256, asset);
}

int ImageCache::levelFor(const QByteArray &sha256, int neededSide) const
{
    const auto it = m_assets.constFind(sha256);
    if (it == m_assets.cend())
        return 0;
    for (int level : Levels) {
        if (level >= neededSide || level >= it->maxSide)
            return std::min(level, it->maxSide);
    }
    return it->maxSide;
}

void ImageCache::insert(const QByteArray &sha256, const QImage &image)
{
    if (image.isNull() || !m_assets.contains(sha256))
        return;
    const int side = std::max(image.width(), image.height());
    const int level = levelFor(sha256, side);
    if (level != side)
        return; // not on a level boundary; ignore rather than mislabel
    auto *pixmap = new QPixmap(QPixmap::fromImage(image));
    m_cache.insert(key(sha256, level), pixmap, costKiB(*pixmap)); // QCache takes ownership
}

QPixmap ImageCache::best(const QByteArray &sha256, int neededSide)
{
    const int wanted = levelFor(sha256, std::max(1, neededSide));
    if (wanted == 0)
        return {};
    if (const QPixmap *hit = m_cache.object(key(sha256, wanted))) {
        ++m_stats.hits;
        return *hit;
    }
    ++m_stats.misses;
    schedule(sha256, wanted);

    // Fall back to the closest available level: prefer larger, then smaller.
    const int maxSide = m_assets.value(sha256).maxSide;
    for (int level : Levels) {
        const int side = std::min(level, maxSide);
        if (side > wanted)
            if (const QPixmap *p = m_cache.object(key(sha256, side)))
                return *p;
    }
    for (auto it = Levels.rbegin(); it != Levels.rend(); ++it) {
        const int side = std::min(*it, maxSide);
        if (side < wanted)
            if (const QPixmap *p = m_cache.object(key(sha256, side)))
                return *p;
    }
    return {};
}

void ImageCache::schedule(const QByteArray &sha256, int side)
{
    const QString k = key(sha256, side);
    if (m_pending.contains(k) || m_pending.size() >= MaxPendingDecodes)
        return; // a later paint will ask again
    m_pending.insert(k);
    ++m_stats.decodesStarted;

    const io::ImageInfo info = m_assets.value(sha256).info;
    auto store = m_store;
    const Limits limits = m_limits;
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, sha256, k] {
        m_pending.remove(k);
        const QImage image = watcher->result();
        watcher->deleteLater();
        if (image.isNull()) {
            ++m_stats.decodesFailed;
            return;
        }
        ++m_stats.decodesFinished;
        auto *pixmap = new QPixmap(QPixmap::fromImage(image)); // GUI thread
        m_cache.insert(k, pixmap, costKiB(*pixmap));
        emit levelReady(sha256);
    });
    watcher->setFuture(QtConcurrent::run(m_pool, [store, info, side, limits, sha256] {
        const auto bytes = store->read(sha256, limits.maxAssetBytes);
        if (!bytes)
            return QImage();
        return io::decodeImage(*bytes, info, side, limits);
    }));
}

ImageCache::Stats ImageCache::stats() const
{
    Stats s = m_stats;
    s.entries = int(m_cache.count());
    s.bytesUsed = qint64(m_cache.totalCost()) * 1024;
    return s;
}

} // namespace glacier::canvas
