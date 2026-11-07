#include "AppServices.h"

#include "glacier/canvas/ImageCache.h"
#include "glacier/core/Diagnostics.h"
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
    auto settings = std::make_unique<SettingsService>();
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
    recoveryOptions.budgetBytes = qint64(settings->recoveryBudgetMiB()) * 1024 * 1024;
    QString recoveryError;
    std::shared_ptr<persistence::RecoveryStore> recovery = persistence::RecoveryStore::create(recoveryOptions, &recoveryError);
    if (recovery)
        recovery->cleanUp();
    else
        qCWarning(lcRecovery, "recovery is disabled: %s", qPrintable(recoveryError));

    std::unique_ptr<AppServices> services(new AppServices);
    services->m_limits = options.limits;
    services->m_store = std::move(store);
    services->m_pool = std::make_unique<QThreadPool>();
    services->m_pool->setMaxThreadCount(std::clamp(QThread::idealThreadCount() / 2, 2, 4));
    services->m_cache = std::make_unique<canvas::ImageCache>(services->m_store, services->m_pool.get(),
                                                             qint64(settings->cacheBudgetMiB()) * 1024 * 1024,
                                                             options.limits);
    // Preferences apply live (M4.02).
    canvas::ImageCache *cache = services->m_cache.get();
    QObject::connect(settings.get(), &SettingsService::cacheBudgetChanged, cache,
                     [cache](int mib) { cache->setBudget(qint64(mib) * 1024 * 1024); });
    if (recovery) {
        std::weak_ptr<persistence::RecoveryStore> weak = recovery;
        SettingsService *s = settings.get();
        QObject::connect(s, &SettingsService::recoveryChanged, s, [weak, s] {
            if (const auto r = weak.lock())
                r->setBudgetBytes(qint64(s->recoveryBudgetMiB()) * 1024 * 1024);
        });
    }
    services->m_settings = std::move(settings);
    services->m_sessionRoot = root;
    services->m_recovery = std::move(recovery);
    services->m_recoveryRoot = recoveryRoot;
    services->m_recoverable = std::move(recoverable);
    return services;
}

QString AppServices::logDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(u"logs"_s);
}

AppServices::~AppServices()
{
    if (m_pool)
        m_pool->waitForDone();
}

} // namespace glacier
