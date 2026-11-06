#pragma once

#include "glacier/core/Limits.h"

#include <QString>

#include <memory>

class QThreadPool;

namespace glacier {

class SessionAssetStore;

namespace canvas {
class ImageCache;
}

// Process-wide services shared by all windows (spec §6): the session asset
// store, the bounded worker pool, and the decoded-image cache. Created in
// main() after QApplication and destroyed after all windows; the destructor
// waits for running jobs before the store goes away.
class AppServices
{
public:
    struct Options
    {
        QString storeRoot; // empty: <AppDataLocation>/sessions
        Limits limits;
    };

    static std::unique_ptr<AppServices> create(const Options &options, QString *error = nullptr);
    ~AppServices();
    AppServices(const AppServices &) = delete;
    AppServices &operator=(const AppServices &) = delete;

    const Limits &limits() const { return m_limits; }
    const std::shared_ptr<SessionAssetStore> &store() const { return m_store; }
    QThreadPool *pool() const { return m_pool.get(); }
    canvas::ImageCache *cache() const { return m_cache.get(); }

private:
    AppServices() = default;

    Limits m_limits;
    std::shared_ptr<SessionAssetStore> m_store;
    std::unique_ptr<QThreadPool> m_pool;
    std::unique_ptr<canvas::ImageCache> m_cache;
};

} // namespace glacier
