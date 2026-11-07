// Compatibility fixtures (M3.11, A03): the committed boards in
// tests/fixtures/boards were written once by glacier_test_boardfixtures and
// are loaded on every CI platform (Linux, Windows, macOS). They must keep
// loading identically for as long as schema version 1 is supported.
#include "GlacierTestMain.h"
#include "TestBoardFiles.h"

#include "glacier/core/LinkPaths.h"
#include "glacier/persistence/BoardSerializer.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QObject>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::persistence;
using namespace glacier::test;

class TestCompatibility : public QObject
{
    Q_OBJECT

    QString fixture(const QString &name) const { return QDir(QStringLiteral(GLACIER_FIXTURE_DIR)).filePath(name); }

    // The expected content: richBoard's structure. Asset digests and lengths
    // come from the fixture, because image encoders differ between Qt builds.
    static BoardContent expected(const BoardContent &loaded, SessionAssetStore &store, const QString &linkedFile)
    {
        BoardContent e = richBoard(store, linkedFile);
        for (Asset &a : e.snapshot.assets) {
            for (const Asset &l : loaded.snapshot.assets) {
                if (l.id == a.id) {
                    a.sha256 = l.sha256;
                    a.byteLength = l.byteLength;
                }
            }
        }
        return e;
    }

    static QImage decoded(SessionAssetStore &store, const Asset &asset)
    {
        const auto bytes = store.read(asset.sha256, asset.byteLength);
        if (!bytes)
            return {};
        io::ImageInfo info;
        info.format = *io::formatFromMediaType(asset.mediaType);
        info.orientedSize = asset.orientedSize;
        return io::decodeImage(*bytes, info, 0);
    }

private slots:
    void initTestCase()
    {
        io::configureImageAllocationLimit(Limits{});
        QVERIFY2(QFile::exists(fixture(u"v1-rich.glacierref"_s)), "fixtures missing");
    }

    void richBoardLoadsIdentically()
    {
        StoreFixture f;
        const LoadResult loaded = loadBoard(fixture(u"v1-rich.glacierref"_s), f.store, Limits{});
        QVERIFY2(loaded.content, qPrintable(loaded.error));
        const BoardContent &c = *loaded.content;
        QCOMPARE(sorted(c), sorted(expected(c, *f.store, {})));
        QCOMPARE(c.metadata.title, u"Référence — 参考 board"_s);
        QCOMPARE(c.snapshot.notes.size(), 2);

        // Pixels, including the orientation marker.
        for (const Asset &a : c.snapshot.assets) {
            const QImage image = decoded(*f.store, a);
            QVERIFY2(!image.isNull(), qPrintable(a.mediaType));
            QCOMPARE(image.size(), a.orientedSize);
            if (a.mediaType == u"image/png"_s || a.mediaType == u"image/bmp"_s) {
                QCOMPARE(QColor(image.pixel(0, 0)), QColor(255, 255, 255));
                const QColor body(image.pixel(image.width() - 1, image.height() - 1));
                QCOMPARE(body, a.mediaType == u"image/png"_s ? QColor(200, 30, 30) : QColor(30, 200, 30));
            }
        }
    }

    void linkedBoardResolvesRelativeLinks()
    {
        StoreFixture f;
        const LoadResult loaded = loadBoard(fixture(u"v1-linked.glacierref"_s), f.store, Limits{});
        QVERIFY2(loaded.content, qPrintable(loaded.error));
        const QString linkedFile = link_paths::normalize(fixture(u"refs/linked.png"_s));
        QCOMPARE(sorted(*loaded.content), sorted(expected(*loaded.content, *f.store, linkedFile)));
        QCOMPARE(loaded.links.value(fixedId(14)).state, LinkState::Available);
    }

    void futureSchemaIsRefused()
    {
        StoreFixture f;
        const LoadResult loaded = loadBoard(fixture(u"v2-future.glacierref"_s), f.store, Limits{});
        QVERIFY(!loaded.content);
        QVERIFY2(loaded.error.contains(u"newer version of Glacier Ref"_s), qPrintable(loaded.error));
    }

    // Loading and saving again keeps the board readable and unchanged.
    void resaveRoundTrip()
    {
        StoreFixture f;
        const LoadResult loaded = loadBoard(fixture(u"v1-rich.glacierref"_s), f.store, Limits{});
        QVERIFY(loaded.content);
        QTemporaryDir dir;
        SaveRequest request;
        request.content = *loaded.content;
        request.path = dir.filePath(u"resaved.glacierref"_s);
        request.store = f.store;
        QVERIFY(saveBoard(request).ok);
        StoreFixture g;
        const LoadResult again = loadBoard(request.path, g.store, Limits{});
        QVERIFY(again.content);
        QCOMPARE(sorted(*again.content), sorted(*loaded.content));
    }
};

GLACIER_TEST_GUI_MAIN(TestCompatibility)
#include "tst_compatibility.moc"
