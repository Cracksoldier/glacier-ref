#include "glacier/core/SessionAssetStore.h"

#include "glacier/core/BoardDocument.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStorageInfo>

using namespace Qt::StringLiterals;

namespace glacier {

namespace {

const QString LockFileName = u"session.lock"_s;
const QString AssetDirName = u"assets"_s;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

qint64 defaultFreeSpace(const QString &path)
{
    return QStorageInfo(path).bytesAvailable();
}

} // namespace

QByteArray SessionAssetStore::sha256Hex(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

std::unique_ptr<SessionAssetStore> SessionAssetStore::create(Options options, QString *error)
{
    if (options.rootDir.isEmpty()) {
        setError(error, u"no asset store directory configured"_s);
        return nullptr;
    }
    if (!options.freeSpaceProvider)
        options.freeSpaceProvider = &defaultFreeSpace;

    const Id sessionId = Id::generate();
    const QString sessionDir = QDir(options.rootDir).filePath(sessionId.toString());
    if (!QDir().mkpath(QDir(sessionDir).filePath(AssetDirName))) {
        setError(error, u"cannot create the asset store directory"_s);
        return nullptr;
    }

    auto lock = std::make_unique<QLockFile>(QDir(sessionDir).filePath(LockFileName));
    lock->setStaleLockTime(0); // stale only when the owning process is gone
    if (!lock->tryLock(0)) {
        setError(error, u"cannot lock the asset store session"_s);
        QDir(sessionDir).removeRecursively();
        return nullptr;
    }

    return std::unique_ptr<SessionAssetStore>(
        new SessionAssetStore(std::move(options), sessionId, sessionDir, std::move(lock)));
}

SessionAssetStore::SessionAssetStore(Options options, Id sessionId, QString sessionDir,
                                     std::unique_ptr<QLockFile> lock)
    : m_options(std::move(options))
    , m_sessionId(sessionId)
    , m_sessionDir(std::move(sessionDir))
    , m_assetDir(QDir(m_sessionDir).filePath(AssetDirName))
    , m_lock(std::move(lock))
{
}

SessionAssetStore::~SessionAssetStore()
{
    QDir(m_assetDir).removeRecursively();
    m_lock->unlock();
    QDir(m_sessionDir).removeRecursively();
}

int SessionAssetStore::cleanupStaleSessions(const QString &rootDir)
{
    int removed = 0;
    const QDir root(rootDir);
    const QStringList entries = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : entries) {
        if (!Id::fromString(name))
            continue; // not a session directory; never delete unknown content
        const QString sessionDir = root.filePath(name);
        QLockFile lock(QDir(sessionDir).filePath(LockFileName));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            continue; // alive (or unreadable): leave it alone
        lock.unlock();
        if (QDir(sessionDir).removeRecursively())
            ++removed;
    }
    return removed;
}

std::optional<SessionAssetStore::PutResult> SessionAssetStore::put(const QByteArray &bytes, QString *error)
{
    const QByteArray digest = sha256Hex(bytes);
    const QString target = path(digest);

    QMutexLocker locker(&m_writeMutex);
    if (QFileInfo::exists(target))
        return PutResult{digest, false};

    const qint64 available = m_options.freeSpaceProvider(m_assetDir);
    if (available >= 0 && available - bytes.size() < m_options.freeSpaceMargin) {
        setError(error, u"not enough free disk space for the image"_s);
        return std::nullopt;
    }

    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setError(error, u"cannot write the image to the asset store"_s);
        return std::nullopt;
    }
    return PutResult{digest, true};
}

bool SessionAssetStore::contains(const QByteArray &sha256) const
{
    return BoardDocument::isValidDigest(sha256) && QFileInfo::exists(path(sha256));
}

QString SessionAssetStore::path(const QByteArray &sha256) const
{
    if (!BoardDocument::isValidDigest(sha256))
        return {};
    return QDir(m_assetDir).filePath(QString::fromLatin1(sha256));
}

std::unique_ptr<QIODevice> SessionAssetStore::open(const QByteArray &sha256) const
{
    if (!BoardDocument::isValidDigest(sha256))
        return nullptr;
    auto file = std::make_unique<QFile>(path(sha256));
    if (!file->open(QIODevice::ReadOnly))
        return nullptr;
    return file;
}

std::optional<QByteArray> SessionAssetStore::read(const QByteArray &sha256, qint64 maxBytes) const
{
    const auto device = open(sha256);
    if (!device || device->size() > maxBytes)
        return std::nullopt;
    QByteArray bytes = device->readAll();
    if (bytes.size() != device->size())
        return std::nullopt;
    return bytes;
}

} // namespace glacier
