#include "GlacierTestMain.h"

#include "glacier/core/SessionAssetStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>

using namespace Qt::StringLiterals;
using glacier::SessionAssetStore;

namespace {

std::unique_ptr<SessionAssetStore> makeStore(const QString &root, qint64 freeBytes = -1)
{
    SessionAssetStore::Options options;
    options.rootDir = root;
    if (freeBytes >= 0)
        options.freeSpaceProvider = [freeBytes](const QString &) { return freeBytes; };
    return SessionAssetStore::create(options);
}

} // namespace

class TestSessionAssetStore : public QObject
{
    Q_OBJECT

private slots:
    void putIsIdempotentAndContentAddressed()
    {
        QTemporaryDir root;
        auto store = makeStore(root.path());
        QVERIFY(store);

        const QByteArray bytes("hello asset");
        const auto first = store->put(bytes);
        QVERIFY(first);
        QVERIFY(first->created);
        QCOMPARE(first->sha256, SessionAssetStore::sha256Hex(bytes));
        QCOMPARE(first->sha256.size(), 64);

        const auto second = store->put(bytes);
        QVERIFY(second);
        QVERIFY(!second->created);
        QCOMPARE(second->sha256, first->sha256);

        QVERIFY(store->contains(first->sha256));
        QCOMPARE(QFileInfo(store->path(first->sha256)).fileName(), QString::fromLatin1(first->sha256));
        QCOMPARE(store->read(first->sha256, 1024).value(), bytes);
        QVERIFY(!store->read(first->sha256, 4).has_value()); // larger than allowed
        auto device = store->open(first->sha256);
        QVERIFY(device);
        QCOMPARE(device->readAll(), bytes);
    }

    void rejectsInvalidDigests()
    {
        QTemporaryDir root;
        auto store = makeStore(root.path());
        QVERIFY(store);
        for (const QByteArray &bad : {QByteArray("../../etc/passwd"), QByteArray("ABC"), QByteArray()}) {
            QVERIFY(!store->contains(bad));
            QVERIFY(store->path(bad).isEmpty());
            QVERIFY(!store->open(bad));
        }
    }

    void concurrentPutsOfSameContent()
    {
        QTemporaryDir root;
        auto store = makeStore(root.path());
        QVERIFY(store);
        const QByteArray bytes(256 * 1024, 'x');
        std::atomic<int> created{0}, failed{0};
        QThreadPool pool;
        pool.setMaxThreadCount(8);
        QList<QFuture<void>> futures;
        for (int i = 0; i < 32; ++i) {
            futures.append(QtConcurrent::run(&pool, [&] {
                const auto r = store->put(bytes);
                if (!r)
                    ++failed;
                else if (r->created)
                    ++created;
            }));
        }
        for (auto &f : futures)
            f.waitForFinished();
        QCOMPARE(failed.load(), 0);
        QCOMPARE(created.load(), 1);
        QCOMPARE(store->read(SessionAssetStore::sha256Hex(bytes), bytes.size()).value(), bytes);
    }

    void refusesWhenFreeSpaceIsLow()
    {
        QTemporaryDir root;
        auto store = makeStore(root.path(), /*freeBytes=*/1024);
        QVERIFY(store);
        QString error;
        QVERIFY(!store->put(QByteArray("data"), &error));
        QVERIFY(error.contains(u"disk space"_s));
    }

    void destructionRemovesSession()
    {
        QTemporaryDir root;
        QString dir;
        {
            auto store = makeStore(root.path());
            QVERIFY(store);
            QVERIFY(store->put(QByteArray("x")));
            dir = store->sessionDir();
            QVERIFY(QDir(dir).exists());
        }
        QVERIFY(!QDir(dir).exists());
    }

    void cleanupRemovesOnlyStaleSessions()
    {
        QTemporaryDir root;
        auto live = makeStore(root.path());
        QVERIFY(live);
        QVERIFY(live->put(QByteArray("live")));

        // A session whose owner process died: lock held by a helper, then killed.
        const QString staleDir = QDir(root.path()).filePath(glacier::Id::generate().toString());
        QVERIFY(QDir().mkpath(QDir(staleDir).filePath(u"assets"_s)));
        QFile asset(QDir(staleDir).filePath(u"assets/leftover"_s));
        QVERIFY(asset.open(QIODevice::WriteOnly));
        asset.write("old");
        asset.close();

        QProcess holder;
        holder.start(QString::fromLocal8Bit(GLACIER_LOCKHOLDER_PATH),
                     {QDir(staleDir).filePath(u"session.lock"_s)});
        QVERIFY(holder.waitForReadyRead(10000));
        QCOMPARE(holder.readAllStandardOutput().trimmed(), QByteArray("locked"));
        holder.kill();
        QVERIFY(holder.waitForFinished(10000));
        QVERIFY(QFile::exists(QDir(staleDir).filePath(u"session.lock"_s)));

        // Unrelated content in the root must never be touched.
        const QString foreign = QDir(root.path()).filePath(u"not-a-session"_s);
        QVERIFY(QDir().mkpath(foreign));

        QCOMPARE(SessionAssetStore::cleanupStaleSessions(root.path()), 1);
        QVERIFY(!QDir(staleDir).exists());
        QVERIFY(QDir(live->sessionDir()).exists());
        QVERIFY(live->contains(SessionAssetStore::sha256Hex("live")));
        QVERIFY(QDir(foreign).exists());
    }
};

GLACIER_TEST_CORE_MAIN(TestSessionAssetStore)
#include "tst_sessionassetstore.moc"
