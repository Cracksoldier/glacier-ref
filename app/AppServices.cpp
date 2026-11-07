#include "AppServices.h"

#include "glacier/canvas/ImageCache.h"
#include "glacier/core/SessionAssetStore.h"
#include "glacier/io/ImageDecoder.h"

#include <QDir>
#include <QSettings>
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

    // Recovery (spec §5): discover ended sessions before starting our own.
    QString recoveryRoot = options.recoveryRoot;
    if (recoveryRoot.isEmpty())
        recoveryRoot = options.storeRoot.isEmpty()
                         ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(u"recovery"_s)
                         : QDir(options.storeRoot).filePath(u"recovery"_s);
    QList<persistence::RecoveryStore::Entry> recoverable = persistence::RecoveryStore::findRecoverable(recoveryRoot);
    persistence::RecoveryStore::Options recoveryOptions;
    recoveryOptions.rootDir = recoveryRoot;
    recoveryOptions.budgetBytes =
        qint64(std::max(64, QSettings().value(u"recovery/budgetMiB"_s, DefaultRecoveryBudgetMiB).toInt())) * 1024 * 1024;
    QString recoveryError;
    std::shared_ptr<persistence::RecoveryStore> recovery = persistence::RecoveryStore::create(recoveryOptions, &recoveryError);
    if (recovery)
        recovery->cleanUp();
    else
        qWarning("glacier: recovery is disabled: %s", qPrintable(recoveryError));

    std::unique_ptr<AppServices> services(new AppServices);
    services->m_limits = options.limits;
    services->m_store = std::move(store);
    services->m_pool = std::make_unique<QThreadPool>();
    services->m_pool->setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2, 2, 4));
    services->m_cache = std::make_unique<canvas::ImageCache>(services->m_store, services->m_pool.get(),
                                                             canvas::ImageCache::DefaultBudgetBytes,
                                                             options.limits);
    services->m_recovery = std::move(recovery);
    services->m_recoveryRoot = recoveryRoot;
    services->m_recoverable = std::move(recoverable);
    return services;
}

int AppServices::recoveryIntervalSeconds()
{
    return std::max(5, QSettings().value(u"recovery/intervalSeconds"_s, DefaultRecoveryIntervalSeconds).toInt());
}

AppServices::~AppServices()
{
    if (m_pool)
        m_pool->waitForDone();
}

} // namespace glacier
