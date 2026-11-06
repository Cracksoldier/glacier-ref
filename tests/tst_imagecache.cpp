#include "GlacierTestMain.h"
#include "TestImages.h"

#include "glacier/canvas/ImageCache.h"
#include "glacier/core/SessionAssetStore.h"

#include <QGuiApplication>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>

using namespace Qt::StringLiterals;
using namespace glacier;
using glacier::canvas::ImageCache;

class TestImageCache : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;
    std::shared_ptr<SessionAssetStore> m_store;
    QThreadPool m_pool;

    QByteArray storePng(QSize size, QColor color)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(color);
        return m_store->put(test::encode(image, "png"))->sha256;
    }

private slots:
    void initTestCase()
    {
        io::configureImageAllocationLimit(Limits{});
        SessionAssetStore::Options options;
        options.rootDir = m_root.path();
        m_store = SessionAssetStore::create(options);
        QVERIFY(m_store);
    }

    void cleanupTestCase() { m_pool.waitForDone(); }

    void levelSelection()
    {
        ImageCache cache(m_store, &m_pool);
        const QByteArray digest = storePng({1000, 500}, Qt::red);
        cache.registerAsset(digest, u"image/png"_s, {1000, 500});
        QCOMPARE(cache.levelFor(digest, 1), 256);
        QCOMPARE(cache.levelFor(digest, 256), 256);
        QCOMPARE(cache.levelFor(digest, 300), 512);
        QCOMPARE(cache.levelFor(digest, 900), 1000); // capped at the image size
        QCOMPARE(cache.levelFor(digest, 50000), 1000);
        QCOMPARE(cache.levelFor("0000000000000000000000000000000000000000000000000000000000000000", 10), 0);
    }

    void decodesRequestedLevelAsynchronously()
    {
        ImageCache cache(m_store, &m_pool);
        const QByteArray digest = storePng({1000, 500}, Qt::green);
        cache.registerAsset(digest, u"image/png"_s, {1000, 500});
        QSignalSpy ready(&cache, &ImageCache::levelReady);

        QVERIFY(cache.best(digest, 300).isNull()); // nothing yet; 512 scheduled
        QVERIFY(ready.wait(10000));
        QCOMPARE(ready.first().first().toByteArray(), digest);
        const QPixmap level = cache.best(digest, 300);
        QCOMPARE(level.size(), QSize(512, 256));

        // A larger request falls back to the cached level while decoding.
        QCOMPARE(cache.best(digest, 900).size(), QSize(512, 256));
        QVERIFY(ready.wait(10000));
        QCOMPARE(cache.best(digest, 900).size(), QSize(1000, 500));

        const ImageCache::Stats stats = cache.stats();
        QCOMPARE(stats.decodesFinished, 2u);
        QVERIFY(stats.hits >= 1);
        QCOMPARE(stats.entries, 2);
    }

    void seededThumbnailIsUsed()
    {
        ImageCache cache(m_store, &m_pool);
        const QByteArray digest = storePng({800, 400}, Qt::blue);
        cache.registerAsset(digest, u"image/png"_s, {800, 400});
        QImage thumb(256, 128, QImage::Format_RGB32);
        thumb.fill(Qt::blue);
        cache.insert(digest, thumb);
        QCOMPARE(cache.best(digest, 100).size(), QSize(256, 128));
        QCOMPARE(cache.stats().decodesStarted, 0u);
    }

    void evictsWithinBudget()
    {
        ImageCache cache(m_store, &m_pool, 600 * 1024); // ~600 KiB
        QList<QByteArray> digests;
        for (int i = 0; i < 4; ++i) {
            const QByteArray digest = storePng({256, 256}, QColor::fromHsv(i * 60, 255, 255));
            cache.registerAsset(digest, u"image/png"_s, {256, 256});
            QImage image(256, 256, QImage::Format_RGB32);
            image.fill(Qt::black);
            cache.insert(digest, image); // 256 KiB each
            digests.append(digest);
        }
        const ImageCache::Stats stats = cache.stats();
        QVERIFY2(stats.bytesUsed <= 600 * 1024, qPrintable(QString::number(stats.bytesUsed)));
        QCOMPARE(stats.entries, 2);
        QVERIFY(!cache.best(digests.last(), 256).isNull()); // most recent survives
    }

    void failedDecodeIsCounted()
    {
        ImageCache cache(m_store, &m_pool);
        const QByteArray missing(64, 'a'); // valid digest, not in the store
        cache.registerAsset(missing, u"image/png"_s, {100, 100});
        QVERIFY(cache.best(missing, 100).isNull());
        QTRY_COMPARE(cache.stats().decodesFailed, 1u);
    }
};

GLACIER_TEST_GUI_MAIN(TestImageCache)
#include "tst_imagecache.moc"
