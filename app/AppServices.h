#pragma once

#include "glacier/core/Limits.h"
#include "glacier/persistence/RecoveryStore.h"
#include "SettingsService.h"

#include <QList>
#include <QString>

#include <memory>

class QThreadPool;

namespace glacier {

class SessionAssetStore;

namespace canvas {
class ImageCache;
}

// Process-wide services shared by all windows (spec §6): the session asset
// store, the bounded worker pool, the decoded-image cache, and the recovery
// store with the boards found from sessions that ended abnormally. Created in
// main() after QApplication and destroyed after all windows; the destructor
// waits for running jobs before the store goes away.
class AppServices
{
public:
    struct Options
    {
        QString storeRoot; // empty: <AppDataLocation>/sessions
        Limits limits;
        QString recoveryRoot; // empty: <AppDataLocation>/recovery, or <storeRoot>/recovery
    };


    static std::unique_ptr<AppServices> create(const Options &options, QString *error = nullptr);
    ~AppServices();
    AppServices(const AppServices &) = delete;
    AppServices &operator=(const AppServices &) = delete;

    const Limits &limits() const { return m_limits; }
    const std::shared_ptr<SessionAssetStore> &store() const { return m_store; }
    QThreadPool *pool() const { return m_pool.get(); }
    canvas::ImageCache *cache() const { return m_cache.get(); }
    SettingsService *settings() const { return m_settings.get(); }
    // Folders used by this process (Preferences, Copy Diagnostics).
    QString sessionRoot() const { return m_sessionRoot; }
    static QString logDirectory();
    // Null when recovery data can't be written (then boards are not protected).
    const std::shared_ptr<persistence::RecoveryStore> &recovery() const { return m_recovery; }
    QString recoveryRoot() const { return m_recoveryRoot; }
    // Boards of ended sessions, found at startup.
    QList<persistence::RecoveryStore::Entry> recoverable() const { return m_recoverable; }

private:
    AppServices() = default;

    Limits m_limits;
    std::unique_ptr<SettingsService> m_settings;
    QString m_sessionRoot;
    std::shared_ptr<SessionAssetStore> m_store;
    std::unique_ptr<QThreadPool> m_pool;
    std::unique_ptr<canvas::ImageCache> m_cache;
    std::shared_ptr<persistence::RecoveryStore> m_recovery;
    QString m_recoveryRoot;
    QList<persistence::RecoveryStore::Entry> m_recoverable;
};

} // namespace glacier
