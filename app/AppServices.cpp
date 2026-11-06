#include "AppServices.h"

#include "glacier/canvas/ImageCache.h"
#include "glacier/core/SessionAssetStore.h"
#include "glacier/io/ImageDecoder.h"

#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace glacier {

std::unique_ptr<AppServices> AppServices::create(const Options &options, QString *error)
{
    QString root = options.storeRoot;
    if (root.isEmpty())
        root = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(u"sessions"_s);
    QDir().mkpath(root);
    SessionAssetStore::cleanupStaleSessions(root);

    SessionAssetStore::Options storeOptions;
    storeOptions.rootDir = root;
    std::shared_ptr<SessionAssetStore> store = SessionAssetStore::create(storeOptions, error);
    if (!store)
        return nullptr;

    io::configureImageAllocationLimit(options.limits);

    std::unique_ptr<AppServices> services(new AppServices);
    services->m_limits = options.limits;
    services->m_store = std::move(store);
    services->m_pool = std::make_unique<QThreadPool>();
    services->m_pool->setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2, 2, 4));
    services->m_cache = std::make_unique<canvas::ImageCache>(services->m_store, services->m_pool.get(),
                                                             canvas::ImageCache::DefaultBudgetBytes,
                                                             options.limits);
    return services;
}

AppServices::~AppServices()
{
    if (m_pool)
        m_pool->waitForDone();
}

} // namespace glacier
