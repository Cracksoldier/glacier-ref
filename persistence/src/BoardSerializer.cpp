#include "glacier/persistence/BoardSerializer.h"

#include "glacier/core/SessionAssetStore.h"
#include "glacier/io/ImageDecoder.h"
#include "glacier/io/ImageFormats.h"
#include "glacier/persistence/ZipArchive.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

using namespace Qt::StringLiterals;

namespace glacier::persistence {

namespace {

const QString ManifestEntry = u"manifest.json"_s;
const QString PreviewEntry = u"preview.png"_s;
constexpr qsizetype MaxEntryNameLength = 1024;

// Already compressed formats are stored as they are (spec §5).
bool shouldDeflate(const QString &mediaType)
{
    return mediaType == u"image/bmp"_s || mediaType == u"image/tiff"_s || mediaType == u"image/svg+xml"_s;
}

bool isSafeEntryName(const QString &name)
{
    if (name.isEmpty() || name.size() > MaxEntryNameLength || name.startsWith(u'/') || name.contains(u'\\')
        || name.contains(u':'))
        return false;
    for (const QChar c : name)
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return false;
    const QStringList segments = name.split(u'/');
    for (qsizetype i = 0; i < segments.size(); ++i) {
        const QString &s = segments[i];
        const bool trailing = i == segments.size() - 1 && i > 0;
        if ((s.isEmpty() && !trailing) || s == u"."_s || s == u".."_s)
            return false;
    }
    return true;
}

// A device that forwards to another one and fails after a byte budget, to
// simulate a full disk in tests.
class LimitedDevice : public QIODevice
{
public:
    LimitedDevice(QIODevice *inner, qint64 budget) : m_inner(inner), m_budget(budget)
    {
        QIODevice::open(QIODevice::WriteOnly);
    }
    bool isSequential() const override { return false; }
    qint64 size() const override { return m_inner->size(); }
    bool seek(qint64 pos) override { return QIODevice::seek(pos) && m_inner->seek(pos); }

protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *data, qint64 len) override
    {
        if (m_written + len > m_budget) {
            setErrorString(u"No space left on device"_s);
            return -1;
        }
        const qint64 n = m_inner->write(data, len);
        if (n > 0)
            m_written += n;
        return n;
    }

private:
    QIODevice *m_inner;
    qint64 m_budget;
    qint64 m_written = 0;
};

void report(const ProgressFunction &progress, int done, int total)
{
    if (progress)
        progress(done, total);
}

} // namespace

FileFingerprint FileFingerprint::of(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return {};
    return {info.size(), info.lastModified().toMSecsSinceEpoch()};
}

// --- Saving ------------------------------------------------------------------

bool writeBoardArchive(QIODevice &device, const SaveRequest &request, QString *error, const ProgressFunction &progress)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (!request.store)
        return fail(u"no asset store"_s);

    QSet<Id> referenced;
    for (const ImageItemData &i : request.content.snapshot.images)
        referenced.insert(i.assetId);
    QList<Asset> embedded;
    for (const Asset &a : request.content.snapshot.assets)
        if (!a.linked && referenced.contains(a.id))
            embedded.append(a);

    ManifestWriteOptions options;
    options.boardDir = request.path.isEmpty() ? QString() : QFileInfo(request.path).absolutePath();
    options.generator = request.generator;
    const QByteArray manifest = writeManifest(request.content, options);

    const int total = int(embedded.size()) + 2;
    int done = 0;
    report(progress, done, total);

    ZipWriter writer(&device);
    if (!writer.isOpen() || !writer.add(ManifestEntry, manifest, true))
        return fail(device.errorString().isEmpty() ? writer.error() : device.errorString());
    report(progress, ++done, total);

    for (const Asset &a : std::as_const(embedded)) {
        const auto bytes = request.store->read(a.sha256, request.limits.maxAssetBytes);
        if (!bytes || bytes->size() != a.byteLength)
            return fail(u"the data of an image is missing from the session store"_s);
        if (!writer.add(assetEntryName(a.sha256, a.mediaType), *bytes, shouldDeflate(a.mediaType)))
            return fail(device.errorString().isEmpty() ? writer.error() : device.errorString());
        report(progress, ++done, total);
    }
    if (!request.previewPng.isEmpty() && !writer.add(PreviewEntry, request.previewPng, false))
        return fail(device.errorString().isEmpty() ? writer.error() : device.errorString());
    if (!writer.finish())
        return fail(device.errorString().isEmpty() ? writer.error() : device.errorString());
    report(progress, ++done, total);
    return true;
}

SaveResult saveBoard(const SaveRequest &request, const ProgressFunction &progress)
{
    SaveResult result;
    QSaveFile file(request.path);
    // Never fall back to writing the target directly: the old board must
    // survive any failure.
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        result.error = file.errorString();
        return result;
    }
    QString error;
    bool ok = false;
    if (request.failAfterBytes >= 0) {
        LimitedDevice limited(&file, request.failAfterBytes);
        ok = writeBoardArchive(limited, request, &error, progress);
    } else {
        ok = writeBoardArchive(file, request, &error, progress);
    }
    if (!ok) {
        file.cancelWriting();
        result.error = error;
        return result;
    }
    if (!file.commit()) {
        result.error = file.errorString();
        return result;
    }
    result.ok = true;
    result.fingerprint = FileFingerprint::of(request.path);
    return result;
}

// --- Loading -----------------------------------------------------------------

QString checkArchive(const ZipReader &reader, const Limits &limits)
{
    const QList<ZipEntry> &entries = reader.entries();
    if (entries.size() > limits.maxArchiveEntries)
        return u"the archive has too many entries"_s;
    QSet<QString> names;
    qint64 total = 0;
    for (const ZipEntry &e : entries) {
        if (!isSafeEntryName(e.name))
            return u"the archive contains an unsafe entry name"_s;
        if (names.contains(e.name))
            return u"the archive contains two entries with the same name"_s;
        names.insert(e.name);
        if (e.size < 0 || e.compressedSize < 0)
            return u"the archive directory is damaged"_s;
        total += e.size;
        if (total > limits.maxUncompressedBytes)
            return u"the archive expands to more data than allowed"_s;
        if (e.size > limits.ratioCheckBytes && e.size > e.compressedSize * qint64(limits.maxCompressionRatio))
            return u"an archive entry is compressed suspiciously well"_s;
    }
    return {};
}

LinkCheck checkLink(const QString &absolutePath, const QByteArray &expectedSha256, SessionAssetStore &store,
                    const Limits &limits)
{
    LinkCheck check;
    const QFileInfo info(absolutePath);
    if (!info.isFile()) {
        check.reason = u"The file could not be found."_s;
        return check;
    }
    if (info.size() > limits.maxAssetBytes) {
        check.reason = u"The file is larger than the %1 MiB limit."_s.arg(limits.maxAssetBytes / (1024 * 1024));
        return check;
    }
    QFile file(absolutePath);
    if (!file.open(QIODevice::ReadOnly)) {
        check.reason = u"The file could not be opened."_s;
        return check;
    }
    const QByteArray bytes = file.read(limits.maxAssetBytes + 1);
    if (bytes.size() > limits.maxAssetBytes || bytes.isEmpty()) {
        check.reason = u"The file could not be read."_s;
        return check;
    }
    const QByteArray sha = SessionAssetStore::sha256Hex(bytes);
    if (sha != expectedSha256) {
        const io::ProbeOutcome probe = io::probeImage(bytes, limits);
        if (!probe.info) {
            check.reason = u"The file changed and is no longer a supported image: %1"_s.arg(probe.error);
            return check;
        }
        check.mediaType = io::mediaType(probe.info->format);
        check.orientedSize = probe.info->orientedSize;
    }
    QString error;
    if (!store.put(bytes, &error)) {
        check.reason = error;
        return check;
    }
    check.sha256 = sha;
    check.byteLength = bytes.size();
    check.state = sha == expectedSha256 ? LinkState::Available : LinkState::Changed;
    return check;
}

LoadResult loadBoardFromDevice(QIODevice &device, const QString &boardDir, SessionAssetStore &store,
                               const Limits &limits, const ProgressFunction &progress)
{
    LoadResult result;
    auto fail = [&result](const QString &message) {
        result.error = message;
        return result;
    };
    const ZipReader reader(&device);
    if (!reader.isOpen())
        return fail(u"the file is not a Glacier Ref board or is damaged"_s);
    if (const QString e = checkArchive(reader, limits); !e.isEmpty())
        return fail(e);
    const auto manifestEntry = reader.find(ManifestEntry);
    if (!manifestEntry)
        return fail(u"the file is not a Glacier Ref board (no manifest)"_s);
    QString error;
    const auto json = reader.read(*manifestEntry, limits.maxManifestBytes, &error);
    if (!json)
        return fail(error);
    auto manifest = readManifest(*json, boardDir, limits, &error);
    if (!manifest)
        return fail(error);

    const QList<Asset> &assets = manifest->content.snapshot.assets;
    const int total = int(assets.size());
    int done = 0;
    report(progress, done, total);
    for (const Asset &a : assets) {
        if (a.linked) {
            result.links.insert(a.id, checkLink(a.linkPath, a.sha256, store, limits));
            report(progress, ++done, total);
            continue;
        }
        const QString name = assetEntryName(a.sha256, a.mediaType);
        const auto entry = reader.find(name);
        if (!entry)
            return fail(u"an embedded image is missing from the board file"_s);
        if (entry->size != a.byteLength)
            return fail(u"an embedded image has the wrong size"_s);
        const auto bytes = reader.read(*entry, limits.maxAssetBytes, &error);
        if (!bytes)
            return fail(error);
        if (SessionAssetStore::sha256Hex(*bytes) != a.sha256)
            return fail(u"an embedded image is damaged (its checksum does not match)"_s);
        const io::ProbeOutcome probe = io::probeImage(*bytes, limits);
        if (!probe.info)
            return fail(u"an embedded image could not be read: %1"_s.arg(probe.error));
        if (io::mediaType(probe.info->format) != a.mediaType || probe.info->orientedSize != a.orientedSize)
            return fail(u"an embedded image does not match its description in the manifest"_s);
        if (!store.put(*bytes, &error))
            return fail(error);
        report(progress, ++done, total);
    }
    result.content = manifest->content;
    result.manifest = manifest->root;
    return result;
}

LoadResult loadBoard(const QString &path, const std::shared_ptr<SessionAssetStore> &store, const Limits &limits,
                     const ProgressFunction &progress)
{
    LoadResult result;
    const QFileInfo info(path);
    if (!info.isFile()) {
        result.error = u"the file could not be found"_s;
        return result;
    }
    if (info.size() > limits.maxBoardFileBytes) {
        result.error = u"the file is larger than the %1 GiB limit"_s.arg(limits.maxBoardFileBytes >> 30);
        return result;
    }
    if (!store) {
        result.error = u"no asset store"_s;
        return result;
    }
    const FileFingerprint fingerprint = FileFingerprint::of(path);
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            result.error = file.errorString();
            return result;
        }
        result = loadBoardFromDevice(file, info.absolutePath(), *store, limits, progress);
    } // the file is closed here
    result.fingerprint = fingerprint;
    return result;
}

} // namespace glacier::persistence
