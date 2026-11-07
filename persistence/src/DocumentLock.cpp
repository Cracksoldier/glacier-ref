#include "glacier/persistence/DocumentLock.h"

#include <QFileInfo>
#include <QLockFile>
#include <QSysInfo>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <QDir>
#endif

using namespace Qt::StringLiterals;

namespace glacier::persistence {

namespace {

void hide(const QString &path)
{
#ifdef Q_OS_WIN
    const std::wstring native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();
    const DWORD attributes = GetFileAttributesW(native.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
        SetFileAttributesW(native.c_str(), attributes | FILE_ATTRIBUTE_HIDDEN);
#else
    Q_UNUSED(path); // the leading dot hides it
#endif
}

} // namespace

QString DocumentLock::lockPathFor(const QString &boardPath)
{
    const QFileInfo info(boardPath);
    return info.absolutePath() + u"/."_s + info.fileName() + u".lock"_s;
}

DocumentLock::DocumentLock(QString boardPath, std::unique_ptr<QLockFile> lock)
    : m_boardPath(std::move(boardPath))
    , m_lock(std::move(lock))
{
}

DocumentLock::~DocumentLock() = default; // QLockFile unlocks on destruction

DocumentLock::Result DocumentLock::acquire(const QString &boardPath)
{
    Result result;
    const QString path = lockPathFor(boardPath);
    auto lock = std::make_unique<QLockFile>(path);
    lock->setStaleLockTime(0); // never stale by age
    if (lock->tryLock(0)) {
        hide(path);
        result.status = Status::Acquired;
        result.lock.reset(new DocumentLock(QFileInfo(boardPath).absoluteFilePath(), std::move(lock)));
        return result;
    }
    switch (lock->error()) {
    case QLockFile::LockFailedError:
        result.status = Status::HeldElsewhere;
        lock->getLockInfo(&result.holder.pid, &result.holder.hostname, &result.holder.application);
        result.holder.otherHost = !result.holder.hostname.isEmpty()
                               && result.holder.hostname != QSysInfo::machineHostName();
        break;
    case QLockFile::PermissionError:
        result.status = Status::Unavailable;
        result.error = u"the folder is not writable, so the board can't be locked for editing"_s;
        break;
    default:
        result.status = Status::Unavailable;
        result.error = u"the board's lock file could not be created"_s;
        break;
    }
    return result;
}

bool DocumentLock::removeLock(const QString &boardPath)
{
    QLockFile lock(lockPathFor(boardPath));
    return lock.removeStaleLockFile();
}

} // namespace glacier::persistence
