#pragma once

#include "glacier/core/Id.h"

#include <QByteArray>
#include <QLockFile>
#include <QMutex>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QIODevice;

namespace glacier {

// Per-process store of embedded asset bytes, organized by SHA-256 (spec §4).
//
// Layout: <root>/<sessionId>/session.lock and <root>/<sessionId>/assets/<sha256>.
// The session lock (QLockFile) marks the session as alive; directories whose
// lock is stale (owning process ended) are removed by cleanupStaleSessions().
// The session directory is removed when the store is destroyed.
//
// put() and the read functions are thread-safe.
class SessionAssetStore
{
public:
    struct Options
    {
        QString rootDir;                         // e.g. <AppDataLocation>/sessions
        qint64 freeSpaceMargin = qint64(256) * 1024 * 1024;
        // Returns available bytes for a path; defaults to QStorageInfo.
        std::function<qint64(const QString &path)> freeSpaceProvider;
    };

    struct PutResult
    {
        QByteArray sha256; // lowercase hex
        bool created = false;
    };

    // Creates the session directory and takes its lock.
    static std::unique_ptr<SessionAssetStore> create(Options options, QString *error = nullptr);

    // Removes session directories under rootDir whose lock is stale. Never
    // touches live sessions. Returns the number of sessions removed.
    static int cleanupStaleSessions(const QString &rootDir);

    static QByteArray sha256Hex(const QByteArray &bytes);

    ~SessionAssetStore();
    SessionAssetStore(const SessionAssetStore &) = delete;
    SessionAssetStore &operator=(const SessionAssetStore &) = delete;

    Id sessionId() const { return m_sessionId; }
    QString sessionDir() const { return m_sessionDir; }

    // Stores bytes (idempotent). Fails cleanly when free space would drop below
    // the configured margin.
    std::optional<PutResult> put(const QByteArray &bytes, QString *error = nullptr);

    bool contains(const QByteArray &sha256) const;
    QString path(const QByteArray &sha256) const;

    // Opens an asset for streaming reads; nullptr if missing or invalid digest.
    std::unique_ptr<QIODevice> open(const QByteArray &sha256) const;

    // Reads a whole asset if it is at most maxBytes long.
    std::optional<QByteArray> read(const QByteArray &sha256, qint64 maxBytes) const;

private:
    SessionAssetStore(Options options, Id sessionId, QString sessionDir, std::unique_ptr<QLockFile> lock);

    Options m_options;
    Id m_sessionId;
    QString m_sessionDir;
    QString m_assetDir;
    std::unique_ptr<QLockFile> m_lock;
    mutable QMutex m_writeMutex;
};

} // namespace glacier
