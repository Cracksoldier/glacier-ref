#include "GlacierTestMain.h"
#include "TestBoardFiles.h"
#include "TestBoards.h"

#include "glacier/persistence/BoardSerializer.h"
#include "glacier/persistence/ZipArchive.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

#include <functional>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::persistence;
using namespace glacier::test;

namespace {

struct Entry
{
    QString name;
    QByteArray bytes;
    bool compress = false;
};

QByteArray makeArchive(const QList<Entry> &entries)
{
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    ZipWriter writer(&buffer);
    for (const Entry &e : entries)
        writer.add(e.name, e.bytes, e.compress);
    writer.finish();
    return out;
}

QJsonObject manifestObject(const BoardContent &content)
{
    return QJsonDocument::fromJson(writeManifest(content, {})).object();
}

QByteArray json(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

} // namespace

class TestBoardSerializer : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    // A small valid board: one PNG image.
    struct Small
    {
        BoardContent content;
        Asset asset;
        QByteArray bytes;
    };
    static Small smallBoard(SessionAssetStore &store)
    {
        Small s;
        s.bytes = encode(markedImage({32, 24}, QColor(10, 120, 200)), "png");
        s.asset = storeAsset(store, s.bytes, fixedId(10));
        s.content.metadata.documentId = fixedId(1);
        s.content.metadata.created = QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        s.content.metadata.modified = s.content.metadata.created;
        s.content.snapshot.assets = {s.asset};
        ImageItemData item;
        item.id = fixedId(20);
        item.assetId = s.asset.id;
        item.displaySize = QSizeF(s.asset.orientedSize);
        s.content.snapshot.images = {item};
        return s;
    }

    LoadResult loadBytes(const QByteArray &bytes, SessionAssetStore &store, const Limits &limits = {})
    {
        QByteArray copy = bytes;
        QBuffer buffer(&copy);
        buffer.open(QIODevice::ReadOnly);
        return loadBoardFromDevice(buffer, m_dir.path(), store, limits);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        io::configureImageAllocationLimit(Limits{});
    }

    // A03: everything survives save and load into a fresh session.
    void saveAndLoadRoundTrip()
    {
        StoreFixture source;
        const QString linkedFile = m_dir.filePath(u"refs/linked ä.png"_s);
        QVERIFY(QDir(m_dir.path()).mkpath(u"refs"_s));
        QVERIFY(writeFile(linkedFile, encode(markedImage({20, 10}, Qt::yellow), "png")));
        const BoardContent board = richBoard(*source.store, linkedFile);

        SaveRequest request;
        request.content = board;
        request.path = m_dir.filePath(u"round trip.glacierref"_s);
        request.store = source.store;
        request.previewPng = encode(markedImage({64, 32}, Qt::gray), "png");
        QList<int> progress;
        const SaveResult saved = saveBoard(request, [&progress](int done, int) { progress.append(done); });
        QVERIFY2(saved.ok, qPrintable(saved.error));
        QVERIFY(saved.fingerprint.exists());
        QCOMPARE(saved.fingerprint, FileFingerprint::of(request.path));
        QVERIFY(!progress.isEmpty());

        StoreFixture target; // like opening on another machine
        const LoadResult loaded = loadBoard(request.path, target.store, Limits{});
        QVERIFY2(loaded.content, qPrintable(loaded.error));
        QCOMPARE(sorted(*loaded.content), sorted(board));
        QCOMPARE(loaded.fingerprint, saved.fingerprint);
        for (const Asset &a : board.snapshot.assets) {
            if (a.linked)
                continue;
            QVERIFY(target.store->contains(a.sha256));
            QCOMPARE(target.store->read(a.sha256, a.byteLength), source.store->read(a.sha256, a.byteLength));
        }
        QCOMPARE(loaded.links.size(), 1);
        QCOMPARE(loaded.links.value(fixedId(14)).state, LinkState::Available);
        QVERIFY(target.store->contains(loaded.links.value(fixedId(14)).sha256));
    }

    void archiveLayout()
    {
        StoreFixture f;
        const BoardContent board = richBoard(*f.store);
        SaveRequest request;
        request.content = board;
        request.path = m_dir.filePath(u"layout.glacierref"_s);
        request.store = f.store;
        request.previewPng = encode(markedImage({8, 8}, Qt::gray), "png");
        QVERIFY(saveBoard(request).ok);

        QFile file(request.path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        ZipReader reader(&file);
        QVERIFY(reader.isOpen());
        // manifest + 4 distinct assets (the PNG is shared by two items) + preview
        QCOMPARE(reader.entries().size(), 6);
        QCOMPARE(reader.entries().first().name, u"manifest.json"_s);
        for (const Asset &a : board.snapshot.assets) {
            const auto entry = reader.find(assetEntryName(a.sha256, a.mediaType));
            QVERIFY(entry);
            const bool deflated = entry->compressedSize < entry->size;
            QCOMPARE(deflated, a.mediaType == u"image/bmp"_s || a.mediaType == u"image/svg+xml"_s);
        }
        QVERIFY(reader.find(u"preview.png"_s));
    }

    void previewIsOptional()
    {
        StoreFixture f;
        const Small s = smallBoard(*f.store);
        const QByteArray withoutPreview =
            makeArchive({{u"manifest.json"_s, writeManifest(s.content, {}), true},
                         {assetEntryName(s.asset.sha256, s.asset.mediaType), s.bytes}});
        QVERIFY(loadBytes(withoutPreview, *f.store).content);
        const QByteArray corruptPreview =
            makeArchive({{u"manifest.json"_s, writeManifest(s.content, {}), true},
                         {assetEntryName(s.asset.sha256, s.asset.mediaType), s.bytes},
                         {u"preview.png"_s, "not a png"},
                         {u"x-vendor/notes.txt"_s, "ignored"}});
        QVERIFY(loadBytes(corruptPreview, *f.store).content);
    }

    // A04 (detection part): missing and changed links are reported, never
    // silently accepted.
    void linkStates()
    {
        StoreFixture f;
        const QString path = m_dir.filePath(u"link.png"_s);
        QVERIFY(writeFile(path, encode(markedImage({20, 10}, Qt::yellow), "png")));
        BoardContent board = richBoard(*f.store, path);
        SaveRequest request;
        request.content = board;
        request.path = m_dir.filePath(u"links.glacierref"_s);
        request.store = f.store;
        QVERIFY(saveBoard(request).ok);
        const Asset linked = *std::find_if(board.snapshot.assets.cbegin(), board.snapshot.assets.cend(),
                                           [](const Asset &a) { return a.linked; });

        // Changed on disk: reported with the new content, the board keeps the old digest.
        QVERIFY(writeFile(path, encode(markedImage({30, 30}, Qt::cyan), "png")));
        StoreFixture other;
        LoadResult loaded = loadBoard(request.path, other.store, Limits{});
        QVERIFY(loaded.content);
        LinkCheck check = loaded.links.value(linked.id);
        QCOMPARE(check.state, LinkState::Changed);
        QVERIFY(check.sha256 != linked.sha256);
        QCOMPARE(check.orientedSize, QSize(30, 30));
        QVERIFY(other.store->contains(check.sha256));
        QVERIFY(!other.store->contains(linked.sha256));
        for (const Asset &a : loaded.content->snapshot.assets)
            if (a.id == linked.id)
                QCOMPARE(a.sha256, linked.sha256);

        // Missing: reported, the board still loads.
        QVERIFY(QFile::remove(path));
        loaded = loadBoard(request.path, other.store, Limits{});
        QVERIFY(loaded.content);
        check = loaded.links.value(linked.id);
        QCOMPARE(check.state, LinkState::Missing);
        QVERIFY(!check.reason.isEmpty());

        // Changed into something that is not an image: missing with a reason.
        QVERIFY(writeFile(path, "plain text"));
        check = checkLink(path, linked.sha256, *other.store, Limits{});
        QCOMPARE(check.state, LinkState::Missing);
        QVERIFY(check.reason.contains(u"no longer a supported image"_s));
    }

    void saveFailsWhenAssetBytesAreMissing()
    {
        StoreFixture f;
        Small s = smallBoard(*f.store);
        s.content.snapshot.assets.first().sha256 = fakeDigest(5); // not in the store
        SaveRequest request;
        request.content = s.content;
        request.path = m_dir.filePath(u"missing-bytes.glacierref"_s);
        request.store = f.store;
        const SaveResult result = saveBoard(request);
        QVERIFY(!result.ok);
        QVERIFY(!QFile::exists(request.path)); // nothing written
    }

    // A07: hostile or damaged archives fail safely with a message.
    void rejectsHostileArchives_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("message");
        QTest::newRow("traversal name") << 0 << u"unsafe entry name"_s;
        QTest::newRow("absolute name") << 1 << u"unsafe entry name"_s;
        QTest::newRow("backslash name") << 2 << u"unsafe entry name"_s;
        QTest::newRow("drive name") << 3 << u"unsafe entry name"_s;
        QTest::newRow("duplicate entries") << 4 << u"same name"_s;
        QTest::newRow("too many entries") << 5 << u"too many entries"_s;
        QTest::newRow("compression bomb") << 6 << u"compressed suspiciously well"_s;
        QTest::newRow("uncompressed total") << 7 << u"more data than allowed"_s;
        QTest::newRow("truncated") << 8 << u"damaged"_s;
        QTest::newRow("garbage") << 9 << u"not a Glacier Ref board"_s;
        QTest::newRow("no manifest") << 10 << u"no manifest"_s;
        QTest::newRow("tampered asset") << 11 << u"checksum does not match"_s;
        QTest::newRow("wrong byte length") << 12 << u"wrong size"_s;
        QTest::newRow("format mismatch") << 13 << u"does not match its description"_s;
        QTest::newRow("dimension mismatch") << 14 << u"does not match its description"_s;
        QTest::newRow("oversized image") << 15 << u"could not be read"_s;
        QTest::newRow("missing asset entry") << 16 << u"missing from the board file"_s;
        QTest::newRow("unsafe svg") << 17 << u"could not be read"_s;
        QTest::newRow("newer schema") << 18 << u"newer version"_s;
        QTest::newRow("oversized manifest") << 19 << u"too large"_s;
    }

    void rejectsHostileArchives()
    {
        QFETCH(int, kind);
        QFETCH(QString, message);
        StoreFixture f;
        const Small s = smallBoard(*f.store);
        const QString assetName = assetEntryName(s.asset.sha256, s.asset.mediaType);
        const QByteArray manifest = writeManifest(s.content, {});
        Limits limits;
        QByteArray archive;

        auto withManifest = [&](const QJsonObject &m, const QByteArray &assetBytes, const QString &name) {
            return makeArchive({{u"manifest.json"_s, json(m), true}, {name, assetBytes}});
        };
        auto editedAsset = [&](std::function<void(QJsonObject &)> edit) {
            QJsonObject m = manifestObject(s.content);
            QJsonArray assets = m.value(u"assets"_s).toArray();
            QJsonObject a = assets.at(0).toObject();
            edit(a);
            assets.replace(0, a);
            m.insert(u"assets"_s, assets);
            return m;
        };

        switch (kind) {
        case 0:
        case 1:
        case 2:
        case 3: {
            const QString names[] = {u"assets/../../evil.png"_s, u"_etc/evil"_s, u"assets\\evil.png"_s,
                                     u"C:/evil.png"_s};
            archive = makeArchive({{u"manifest.json"_s, manifest, true}, {assetName, s.bytes}, {names[kind], "x"}});
            // miniz refuses to write a leading '/': patch the name in the
            // local header and the central directory instead.
            if (kind == 1)
                archive.replace("_etc/evil", "/etc/evil");
            break;
        }
        case 4:
            archive = makeArchive({{u"manifest.json"_s, manifest, true}, {assetName, s.bytes}, {assetName, s.bytes}});
            break;
        case 5:
            limits.maxArchiveEntries = 2;
            archive = makeArchive({{u"manifest.json"_s, manifest, true}, {assetName, s.bytes}, {u"x"_s, "x"}});
            break;
        case 6:
            archive = makeArchive({{u"manifest.json"_s, manifest, true},
                                   {assetName, s.bytes},
                                   {u"x-bomb/zeros"_s, QByteArray(4 * 1024 * 1024, '\0'), true}});
            break;
        case 7:
            limits.maxUncompressedBytes = 100;
            archive = makeArchive({{u"manifest.json"_s, manifest, true}, {assetName, s.bytes}});
            break;
        case 8:
            archive = makeArchive({{u"manifest.json"_s, manifest, true}, {assetName, s.bytes}});
            archive.chop(archive.size() / 2);
            break;
        case 9:
            archive = QByteArray(4096, 'x');
            break;
        case 10:
            archive = makeArchive({{assetName, s.bytes}});
            break;
        case 11: {
            QByteArray tampered = s.bytes;
            tampered[tampered.size() - 20] = char(tampered[tampered.size() - 20] ^ 0x55);
            archive = makeArchive({{u"manifest.json"_s, manifest, true}, {assetName, tampered}});
            break;
        }
        case 12:
            archive = withManifest(editedAsset([&](QJsonObject &a) { a.insert(u"byteLength"_s, s.bytes.size() + 1); }),
                                   s.bytes, assetName);
            break;
        case 13: {
            // JPEG bytes described as PNG (digest and length match the bytes).
            const QByteArray jpeg = encode(markedImage({32, 24}, Qt::red), "jpeg");
            const QByteArray sha = SessionAssetStore::sha256Hex(jpeg);
            archive = withManifest(editedAsset([&](QJsonObject &a) {
                                       a.insert(u"sha256"_s, QString::fromLatin1(sha));
                                       a.insert(u"byteLength"_s, jpeg.size());
                                       a.insert(u"entry"_s, assetEntryName(sha, u"image/png"_s));
                                   }),
                                   jpeg, assetEntryName(sha, u"image/png"_s));
            break;
        }
        case 14:
            archive = withManifest(editedAsset([](QJsonObject &a) { a.insert(u"width"_s, 33); }), s.bytes, assetName);
            break;
        case 15: {
            // The manifest claims 32x24; the PNG header says 20000x20000.
            const QByteArray huge = pngHeaderOnly(20000, 20000);
            const QByteArray sha = SessionAssetStore::sha256Hex(huge);
            archive = withManifest(editedAsset([&](QJsonObject &a) {
                                       a.insert(u"sha256"_s, QString::fromLatin1(sha));
                                       a.insert(u"byteLength"_s, huge.size());
                                       a.insert(u"entry"_s, assetEntryName(sha, u"image/png"_s));
                                   }),
                                   huge, assetEntryName(sha, u"image/png"_s));
            break;
        }
        case 16:
            archive = makeArchive({{u"manifest.json"_s, manifest, true}});
            break;
        case 17: {
            const QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"24\">"
                                   "<image href=\"https://example.com/a.png\" width=\"32\" height=\"24\"/></svg>";
            const QByteArray sha = SessionAssetStore::sha256Hex(svg);
            archive = withManifest(editedAsset([&](QJsonObject &a) {
                                       a.insert(u"sha256"_s, QString::fromLatin1(sha));
                                       a.insert(u"mediaType"_s, u"image/svg+xml"_s);
                                       a.insert(u"byteLength"_s, svg.size());
                                       a.insert(u"entry"_s, assetEntryName(sha, u"image/svg+xml"_s));
                                   }),
                                   svg, assetEntryName(sha, u"image/svg+xml"_s));
            break;
        }
        case 18: {
            QJsonObject m = manifestObject(s.content);
            m.insert(u"schemaVersion"_s, 2);
            archive = withManifest(m, s.bytes, assetName);
            break;
        }
        case 19:
            limits.maxManifestBytes = 64;
            archive = makeArchive({{u"manifest.json"_s, manifest, true}, {assetName, s.bytes}});
            break;
        }

        StoreFixture fresh;
        const LoadResult result = loadBytes(archive, *fresh.store, limits);
        QVERIFY(!result.content);
        QVERIFY2(result.error.contains(message, Qt::CaseInsensitive), qPrintable(result.error));
    }

    void rejectsOversizedFilesBeforeReading()
    {
        StoreFixture f;
        const QString path = m_dir.filePath(u"big.glacierref"_s);
        QVERIFY(writeFile(path, QByteArray(2048, 'x')));
        Limits limits;
        limits.maxBoardFileBytes = 1024;
        const LoadResult result = loadBoard(path, f.store, limits);
        QVERIFY(!result.content);
        QVERIFY(result.error.contains(u"larger than"_s));
        QVERIFY(loadBoard(m_dir.filePath(u"absent.glacierref"_s), f.store, Limits{}).error.contains(u"not be found"_s));
    }
};

GLACIER_TEST_GUI_MAIN(TestBoardSerializer)
#include "tst_boardserializer.moc"
