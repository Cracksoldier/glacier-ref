#include "GlacierTestMain.h"
#include "TestImages.h"

#include "glacier/core/SessionAssetStore.h"
#include "glacier/io/ImportService.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::io;

namespace {

QString writeFile(const QDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(bytes);
    return path;
}

QByteArray distinctPng(int seed, int size = 64)
{
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(QColor::fromHsv(seed * 7 % 360, 200, 200));
    image.setPixel(seed % size, (seed / size) % size, qRgb(seed & 0xff, 0, 0));
    return test::encode(image, "png");
}

ImportBatchResult waitForBatch(QSignalSpy &spy)
{
    if (spy.isEmpty() && !spy.wait(30000))
        return {};
    return spy.takeFirst().first().value<ImportBatchResult>();
}

} // namespace

Q_DECLARE_METATYPE(glacier::io::ImportBatchResult)

class TestImportService : public QObject
{
    Q_OBJECT

    QTemporaryDir m_storeRoot;
    std::shared_ptr<SessionAssetStore> m_store;
    QThreadPool m_pool;

private slots:
    void initTestCase()
    {
        qRegisterMetaType<ImportBatchResult>();
        configureImageAllocationLimit(Limits{});
        SessionAssetStore::Options options;
        options.rootDir = m_storeRoot.path();
        m_store = SessionAssetStore::create(options);
        QVERIFY(m_store);
        m_pool.setMaxThreadCount(4);
    }

    void cleanupTestCase() { m_pool.waitForDone(); }

    void importsFilesAndReportsFailures()
    {
        QTemporaryDir dir;
        const QDir d(dir.path());
        QStringList paths{
            writeFile(d, u"one.png"_s, distinctPng(1)),
            writeFile(d, u"two.png"_s, distinctPng(2)),
            writeFile(d, u"broken.png"_s, distinctPng(3).left(40)),
            writeFile(d, u"photo.psd"_s, test::psdHeader()),
            writeFile(d, u"anim.gif"_s, test::gifFrames(2)),
        };
        ImportService service(m_store, &m_pool);
        QSignalSpy finished(&service, &ImportService::batchFinished);
        const Id token = Id::generate();
        const quint64 id = service.start(planForPaths(paths), token, QPointF(10, 20));
        QVERIFY(id != 0);
        QVERIFY(service.isBusy());

        const ImportBatchResult result = waitForBatch(finished);
        QCOMPARE(result.batchId, id);
        QCOMPARE(result.documentToken, token);
        QCOMPARE(result.anchor, QPointF(10, 20));
        QVERIFY(!result.cancelled);
        QCOMPARE(result.images.size(), 3);
        QCOMPARE(result.images[0].displayName, u"one.png"_s); // source order
        QCOMPARE(result.images[2].displayName, u"anim.gif"_s);
        QCOMPARE(result.images[2].notices.size(), 1);
        for (const ImportedImage &image : result.images) {
            QVERIFY(m_store->contains(image.sha256));
            QVERIFY(!image.thumbnail.isNull());
            QVERIFY(std::max(image.thumbnail.width(), image.thumbnail.height()) <= ImportService::ThumbnailSide);
        }
        QCOMPARE(result.failures.size(), 2);
        QCOMPARE(result.failures[0].displayName, u"broken.png"_s);
        QCOMPARE(result.failures[1].displayName, u"photo.psd"_s);
        QVERIFY(!service.isBusy());
    }

    void encodesClipboardImagesAsPng()
    {
        ImportPlan plan;
        QImage image(8, 6, QImage::Format_ARGB32);
        image.fill(qRgba(1, 2, 3, 128));
        plan.sources.append({ImportSource::Kind::ClipboardImage, {}, image, u"Pasted image"_s});
        ImportService service(m_store, &m_pool);
        QSignalSpy finished(&service, &ImportService::batchFinished);
        service.start(plan, Id::generate(), {});
        const ImportBatchResult result = waitForBatch(finished);
        QCOMPARE(result.images.size(), 1);
        QCOMPARE(result.images.first().mediaType, u"image/png"_s);
        QCOMPARE(result.images.first().orientedSize, QSize(8, 6));
        const auto stored = m_store->read(result.images.first().sha256, 1 << 20);
        QVERIFY(stored && stored->startsWith("\x89PNG"));
    }

    void cancelKeepsFinishedResults()
    {
        QTemporaryDir dir;
        QStringList paths;
        for (int i = 0; i < 120; ++i)
            paths.append(writeFile(QDir(dir.path()), u"img%1.png"_s.arg(i), distinctPng(1000 + i, 256)));
        QThreadPool slowPool;
        slowPool.setMaxThreadCount(1);
        ImportService service(m_store, &slowPool);
        QSignalSpy finished(&service, &ImportService::batchFinished);
        quint64 id = 0;
        connect(&service, &ImportService::progress, &service, [&](quint64 batch, int done, int) {
            if (done >= 3)
                service.cancel(batch);
        });
        id = service.start(planForPaths(paths), Id::generate(), {});
        const ImportBatchResult result = waitForBatch(finished);
        QCOMPARE(result.batchId, id);
        QVERIFY(result.cancelled);
        QVERIFY2(result.images.size() >= 3 && result.images.size() < 120,
                 qPrintable(QString::number(result.images.size())));
        QVERIFY(result.failures.isEmpty()); // cancelled jobs are not failures
        slowPool.waitForDone();
    }

    void destroyingServiceDuringJobsIsSafe()
    {
        QTemporaryDir dir;
        QStringList paths;
        for (int i = 0; i < 60; ++i)
            paths.append(writeFile(QDir(dir.path()), u"x%1.png"_s.arg(i), distinctPng(5000 + i, 256)));
        {
            ImportService service(m_store, &m_pool);
            service.start(planForPaths(paths), Id::generate(), {});
            // Destroyed immediately while jobs are queued and running.
        }
        m_pool.waitForDone();
        QCoreApplication::processEvents(); // no result may reach the dead service
    }

    void emptyPlanStartsNothing()
    {
        ImportService service(m_store, &m_pool);
        QCOMPARE(service.start(ImportPlan{}, Id::generate(), {}), 0u);
        QVERIFY(!service.isBusy());
    }
};

GLACIER_TEST_GUI_MAIN(TestImportService)
#include "tst_importservice.moc"
