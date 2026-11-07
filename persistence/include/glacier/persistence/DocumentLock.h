#pragma once

#include <QString>

#include <memory>

class QLockFile;

namespace glacier::persistence {

// Per-document lock (spec §5, M3.06): a hidden sibling QLockFile,
// ".<board file name>.lock", held while a board is open for editing in any
// window of any process. It also protects boards in shared network folders.
//
// Stale locks are handled conservatively: Qt treats a lock as stale only when
// it was taken on this host by a process that no longer runs (the lock never
// expires by age). A lock held on another host is never taken over
// automatically; removing it requires an explicit user decision.
class DocumentLock
{
public:
    enum class Status {
        Acquired,      // this process may write the board
        HeldElsewhere, // another live process (or host) holds the lock
        Unavailable,   // the lock file can't be created (e.g. read-only folder)
    };

    struct Holder
    {
        qint64 pid = 0;
        QString hostname;
        QString application;
        bool otherHost = false;
    };

    struct Result
    {
        Status status = Status::Unavailable;
        std::unique_ptr<DocumentLock> lock; // set when Acquired
        Holder holder;                      // for HeldElsewhere
        QString error;                      // for Unavailable
    };

    static QString lockPathFor(const QString &boardPath);

    static Result acquire(const QString &boardPath);

    // Removes the lock of a board after the user confirmed that it is stale
    // (e.g. left by a crashed session on another computer).
    static bool removeLock(const QString &boardPath);

    ~DocumentLock();
    DocumentLock(const DocumentLock &) = delete;
    DocumentLock &operator=(const DocumentLock &) = delete;

    QString boardPath() const { return m_boardPath; }

private:
    DocumentLock(QString boardPath, std::unique_ptr<QLockFile> lock);

    QString m_boardPath;
    std::unique_ptr<QLockFile> m_lock;
};

} // namespace glacier::persistence
