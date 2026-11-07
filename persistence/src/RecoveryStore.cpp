#include "glacier/persistence/RecoveryStore.h"

#include "glacier/core/SessionAssetStore.h"
#include "glacier/persistence/Manifest.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <filesystem>
#include <system_error>

using namespace Qt::StringLiterals;

namespace glacier::persistence {

namespace {

const QString PoolDir = u"pool"_s;
const QString SessionsDir = u"sessions"_s;
const QString LockName = u"session.lock"_s;
const QString RecoveryMember = u"x-recovery"_s;
constexpr qint64 MaxPoolAssetBytes = qint64(2) * 1024 * 1024 * 1024;

QString poolPath(const QString &root, const QByteArray &sha256)
{
    return root + u'/' + PoolDir + u'/' + QString::fromLatin1(sha256);
}

// Snapshot files of a document directory, oldest first (names sort by sequence).
QStringList snapshotFiles(const QString &documentDir)
{
    QStringList files = QDir(documentDir).entryList({u"snapshot-*.json"_s}, QDir::Files, QDir::Name);
    for (QString &f : files)
        f = documentDir + u'/' + f;
    return files;
}

// Whether a session's process is still running (its lock is held).
bool isLive(const QString &sessionDir)
{
    QLockFile lock(sessionDir + u'/' + LockName);
    lock.setStaleLockTime(0);
    if (lock.tryLock(0)) {
        lock.unlock();
        return false;
    }
    return true; // held, or not accessible: treat conservatively as live
}

QList<QByteArray> embeddedDigests(const QString &snapshotPath)
{
    QFile file(snapshotPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QList<QByteArray> digests;
    const QJsonObject root = QJsonDocument::fromJson(file.read(Limits{}.maxManifestBytes)).object();
    for (const QJsonValue &v : root.value(u"assets"_s).toArray()) {
        const QJsonObject a = v.toObject();
        if (a.value(u"kind"_s).toString() == u"embedded"_s)
            digests.append(a.value(u"sha256"_s).toString().toLatin1());
    }
    return digests;
}

} // namespace

RecoveryStore::RecoveryStore(Options options, Id sessionId, QString sessionDir, std::unique_ptr<QLockFile> lock)
    : m_options(std::move(options))
    , m_sessionId(sessionId)
    , m_sessionDir(std::move(sessionDir))
    , m_lock(std::move(lock))
{
}

RecoveryStore::~RecoveryStore() = default; // keeps the data: it is only removed explicitly

std::unique_ptr<RecoveryStore> RecoveryStore::create(Options options, QString *error)
{
    const Id id = Id::generate();
    const QString dir = options.rootDir + u'/' + SessionsDir + u'/' + id.toString();
    if (!QDir().mkpath(dir) || !QDir().mkpath(options.rootDir + u'/' + PoolDir)) {
        if (error)
            *error = u"the recovery folder could not be created"_s;
        return nullptr;
    }
    auto lock = std::make_unique<QLockFile>(dir + u'/' + LockName);
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) {
        if (error)
            *error = u"the recovery session could not be locked"_s;
        return nullptr;
    }
    return std::unique_ptr<RecoveryStore>(new RecoveryStore(std::move(options), id, dir, std::move(lock)));
}

bool RecoveryStore::storeAsset(const QByteArray &sha256, SessionAssetStore &assets, QString *error)
{
    const QString target = poolPath(m_options.rootDir, sha256);
    if (QFileInfo::exists(target))
        return true; // written once, shared by every snapshot
    // A hard link costs no space or time; the session store's files are
    // immutable and were verified when stored.
    std::error_code ec;
    std::filesystem::create_hard_link(QFileInfo(assets.path(sha256)).filesystemAbsoluteFilePath(),
                                      QFileInfo(target).filesystemAbsoluteFilePath(), ec);
    if (!ec)
        return true;
    const auto bytes = assets.read(sha256, MaxPoolAssetBytes);
    if (!bytes || SessionAssetStore::sha256Hex(*bytes) != sha256) {
        if (error)
            *error = u"an image is missing from the session store"_s;
        return false;
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(*bytes) != bytes->size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool RecoveryStore::writeSnapshot(const SnapshotRequest &request, SessionAssetStore &assets, QString *error)
{
    QMutexLocker locker(&m_mutex);
    const BoardContent &content = request.content;
    if (request.key.isNull()) {
        if (error)
            *error = u"the snapshot has no key"_s;
        return false;
    }
    const QString documentDir = m_sessionDir + u'/' + request.key.toString();
    if (!QDir().mkpath(documentDir)) {
        if (error)
            *error = u"the recovery folder could not be created"_s;
        return false;
    }
    // 1. Assets first: a manifest is only written once everything it needs is there.
    QSet<Id> used;
    for (const ImageItemData &image : content.snapshot.images)
        used.insert(image.assetId);
    for (const Asset &a : content.snapshot.assets)
        if (!a.linked && used.contains(a.id) && !storeAsset(a.sha256, assets, error))
            return false;

    // 2. The manifest, atomically.
    ManifestWriteOptions options; // no board directory: links are written absolute
    options.extra.insert(RecoveryMember, QJsonObject{
                                             {u"boardPath"_s, request.boardPath},
                                             {u"baseSize"_s, double(request.baseFingerprint.size)},
                                             {u"baseModified"_s, double(request.baseFingerprint.modifiedMsecs)},
                                             {u"time"_s, QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                                         });
    const QString name = u"snapshot-%1.json"_s.arg(++m_sequence, 12, 10, QLatin1Char('0'));
    QSaveFile file(documentDir + u'/' + name);
    const QByteArray json = writeManifest(content, options);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }

    // 3. Retention and budget.
    applyRetention(documentDir);
    enforceBudget();
    collectPool();
    return true;
}

void RecoveryStore::applyRetention(const QString &documentDir)
{
    QStringList files = snapshotFiles(documentDir);
    while (files.size() > m_options.keepPerDocument)
        QFile::remove(files.takeFirst());
}

void RecoveryStore::enforceBudget()
{
    if (usedBytes(m_options.rootDir) <= m_options.budgetBytes)
        return;
    // Least recent first; never the newest snapshot of a live session's document.
    struct Candidate
    {
        QString path;
        QDateTime modified;
    };
    QList<Candidate> candidates;
    const QDir sessions(m_options.rootDir + u'/' + SessionsDir);
    for (const QString &session : sessions.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString sessionDir = sessions.filePath(session);
        const bool live = sessionDir == m_sessionDir || isLive(sessionDir);
        for (const QString &document : QDir(sessionDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QStringList files = snapshotFiles(sessionDir + u'/' + document);
            if (live && !files.isEmpty())
                files.removeLast();
            for (const QString &f : std::as_const(files))
                candidates.append({f, QFileInfo(f).lastModified()});
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.modified < b.modified; });
    for (const Candidate &c : std::as_const(candidates)) {
        if (usedBytes(m_options.rootDir) <= m_options.budgetBytes)
            break;
        QFile::remove(c.path);
        collectPool();
    }
}

void RecoveryStore::collectPool()
{
    QSet<QByteArray> referenced;
    const QDir sessions(m_options.rootDir + u'/' + SessionsDir);
    for (const QString &session : sessions.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        for (const QString &document : QDir(sessions.filePath(session)).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            for (const QString &snapshot : snapshotFiles(sessions.filePath(session) + u'/' + document))
                for (const QByteArray &sha : embeddedDigests(snapshot))
                    referenced.insert(sha);
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addSecs(-m_options.poolGraceSeconds);
    const QDir pool(m_options.rootDir + u'/' + PoolDir);
    for (const QFileInfo &info : pool.entryInfoList(QDir::Files)) {
        if (referenced.contains(info.fileName().toLatin1()))
            continue;
        if (m_options.poolGraceSeconds > 0 && info.lastModified().toUTC() > cutoff)
            continue;
        QFile::remove(info.absoluteFilePath());
    }
}

void RecoveryStore::discardDocument(const Id &key)
{
    QMutexLocker locker(&m_mutex);
    QDir(m_sessionDir + u'/' + key.toString()).removeRecursively();
    collectPool();
}

void RecoveryStore::setBudgetBytes(qint64 bytes)
{
    QMutexLocker locker(&m_mutex);
    m_options.budgetBytes = bytes;
}

void RecoveryStore::cleanUp()
{
    QMutexLocker locker(&m_mutex);
    enforceBudget();
    collectPool();
}

void RecoveryStore::removeSession()
{
    QMutexLocker locker(&m_mutex);
    if (m_lock)
        m_lock->unlock();
    m_lock.reset();
    QDir(m_sessionDir).removeRecursively();
    collectPool();
}

QList<RecoveryStore::Entry> RecoveryStore::findRecoverable(const QString &rootDir)
{
    QList<Entry> entries;
    const QDir sessions(rootDir + u'/' + SessionsDir);
    for (const QString &session : sessions.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString sessionDir = sessions.filePath(session);
        if (isLive(sessionDir))
            continue; // never offer boards of a running process
        bool any = false;
        for (const QString &document : QDir(sessionDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString documentDir = sessionDir + u'/' + document;
            QStringList files = snapshotFiles(documentDir);
            std::reverse(files.begin(), files.end()); // newest first
            bool found = false;
            for (const QString &path : std::as_const(files)) {
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly))
                    continue;
                const auto manifest = readManifest(file.read(Limits{}.maxManifestBytes + 1), QString(), Limits{}, nullptr);
                if (!manifest)
                    continue;
                bool complete = true;
                for (const Asset &a : manifest->content.snapshot.assets)
                    complete = complete && (a.linked || QFileInfo::exists(poolPath(rootDir, a.sha256)));
                if (!complete)
                    continue; // an interrupted snapshot: fall back to an older one
                const QJsonObject recovery = manifest->root.value(RecoveryMember).toObject();
                Entry e;
                e.sessionDir = sessionDir;
                e.documentId = manifest->content.metadata.documentId;
                e.snapshotPath = path;
                e.boardPath = recovery.value(u"boardPath"_s).toString();
                e.baseFingerprint.size = qint64(recovery.value(u"baseSize"_s).toDouble(-1));
                e.baseFingerprint.modifiedMsecs = qint64(recovery.value(u"baseModified"_s).toDouble());
                e.title = manifest->content.metadata.title;
                e.time = QDateTime::fromString(recovery.value(u"time"_s).toString(), Qt::ISODateWithMs).toUTC();
                entries.append(e);
                found = true;
                break;
            }
            if (found)
                any = true;
            else
                QDir(documentDir).removeRecursively();
        }
        if (!any)
            QDir(sessionDir).removeRecursively();
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.time > b.time; });
    return entries;
}

LoadResult RecoveryStore::loadSnapshot(const QString &rootDir, const Entry &entry, SessionAssetStore &assets,
                                       const Limits &limits)
{
    LoadResult result;
    QFile file(entry.snapshotPath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = u"the recovery data could not be read"_s;
        return result;
    }
    QString error;
    const auto manifest = readManifest(file.read(limits.maxManifestBytes + 1), QString(), limits, &error);
    if (!manifest) {
        result.error = error;
        return result;
    }
    for (const Asset &a : manifest->content.snapshot.assets) {
        if (a.linked) {
            result.links.insert(a.id, checkLink(a.linkPath, a.sha256, assets, limits));
            continue;
        }
        QFile pooled(poolPath(rootDir, a.sha256));
        const QByteArray bytes = pooled.open(QIODevice::ReadOnly) ? pooled.read(limits.maxAssetBytes + 1) : QByteArray();
        if (bytes.size() != a.byteLength || SessionAssetStore::sha256Hex(bytes) != a.sha256) {
            result.error = u"an image of the recovered board is damaged or missing"_s;
            return result;
        }
        if (!assets.put(bytes, &error)) {
            result.error = error;
            return result;
        }
    }
    result.content = manifest->content;
    result.manifest = manifest->root;
    result.fingerprint = entry.baseFingerprint;
    return result;
}

void RecoveryStore::discardEntry(const Entry &entry)
{
    QDir(QFileInfo(entry.snapshotPath).absolutePath()).removeRecursively();
    const QDir session(entry.sessionDir);
    if (session.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty())
        QDir(entry.sessionDir).removeRecursively();
}

qint64 RecoveryStore::usedBytes(const QString &rootDir)
{
    qint64 total = 0;
    QDirIterator it(rootDir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        total += it.nextFileInfo().size();
    return total;
}

} // namespace glacier::persistence
