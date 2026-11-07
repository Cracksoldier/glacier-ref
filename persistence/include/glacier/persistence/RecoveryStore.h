#pragma once

#include "glacier/core/Id.h"
#include "glacier/core/Limits.h"
#include "glacier/persistence/BoardSerializer.h"

#include <QDateTime>
#include <QList>
#include <QMutex>
#include <QString>

#include <memory>

class QLockFile;

namespace glacier {
class SessionAssetStore;
}

namespace glacier::persistence {

// Crash recovery data (spec §5, M3.09), in the application data directory:
//
//   <root>/pool/<sha256>                                   asset bytes, shared
//   <root>/sessions/<session>/session.lock                 held while the process runs
//   <root>/sessions/<session>/<key>/snapshot-<n>.json      manifest snapshots
//
// <key> identifies an open board within the session (one per window, so two
// windows showing the same file keep separate snapshots).
//
// A snapshot is a board manifest (docs/FORMAT.md) whose embedded assets live
// in the pool, plus an "x-recovery" member (board path, base fingerprint,
// time). It is complete once its manifest is committed atomically; assets are
// written and verified before that. Linked files are referenced by path only.
// Only sessions whose lock is stale (process ended) are offered for recovery.
//
// writeSnapshot() and the cleanup functions are thread-safe.
class RecoveryStore
{
public:
    struct Options
    {
        QString rootDir;
        qint64 budgetBytes = qint64(2048) * 1024 * 1024;
        int keepPerDocument = 3;
        // Pool files younger than this are never collected: another process
        // may be about to commit a snapshot that references them.
        int poolGraceSeconds = 3600;
    };

    // Creates this process's session directory and takes its lock.
    static std::unique_ptr<RecoveryStore> create(Options options, QString *error = nullptr);
    ~RecoveryStore();
    RecoveryStore(const RecoveryStore &) = delete;
    RecoveryStore &operator=(const RecoveryStore &) = delete;

    Id sessionId() const { return m_sessionId; }
    QString rootDir() const { return m_options.rootDir; }
    QString sessionDir() const { return m_sessionDir; }

    struct SnapshotRequest
    {
        Id key;                       // the open board (window) within the session
        BoardContent content;
        QString boardPath;            // empty for unnamed boards
        FileFingerprint baseFingerprint; // of the board file when last loaded or saved
    };

    // Writes a snapshot of a document, then applies retention and the budget.
    bool writeSnapshot(const SnapshotRequest &request, SessionAssetStore &assets, QString *error = nullptr);

    // Removes an open board's snapshots (saved, or closed without saving).
    void discardDocument(const Id &key);

    // Removes this session's directory (normal exit with every window closed).
    void removeSession();

    // --- Discovery (startup) -------------------------------------------------

    struct Entry
    {
        QString sessionDir;
        Id documentId;
        QString snapshotPath;     // newest complete snapshot
        QString boardPath;        // empty: the board was never saved
        FileFingerprint baseFingerprint;
        QString title;
        QDateTime time;           // UTC
    };

    // Boards from sessions whose process has ended, newest first. Sessions
    // without any complete snapshot are removed.
    static QList<Entry> findRecoverable(const QString &rootDir);

    // Loads a snapshot; embedded assets are copied from the pool into `assets`
    // and linked files are checked as on a normal load.
    static LoadResult loadSnapshot(const QString &rootDir, const Entry &entry, SessionAssetStore &assets,
                                   const Limits &limits);

    // Removes an entry's document directory, and its session once empty.
    static void discardEntry(const Entry &entry);

    // Applies the budget and removes unreferenced pool files (startup).
    void cleanUp();

    // Total bytes used (manifests and pool), for tests and diagnostics.
    static qint64 usedBytes(const QString &rootDir);

private:
    RecoveryStore(Options options, Id sessionId, QString sessionDir, std::unique_ptr<QLockFile> lock);
    bool storeAsset(const QByteArray &sha256, SessionAssetStore &assets, QString *error);
    void applyRetention(const QString &documentDir);
    void enforceBudget();
    void collectPool();

    Options m_options;
    Id m_sessionId;
    QString m_sessionDir;
    std::unique_ptr<QLockFile> m_lock;
    QMutex m_mutex;
    quint64 m_sequence = 0;
};

} // namespace glacier::persistence
