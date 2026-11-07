// A06 and M3.09: crash recovery keeps the latest complete snapshot of named
// and unnamed boards, offers only sessions of ended processes, and bounds its
// storage.
#include "GlacierTestMain.h"
#include "TestBoardFiles.h"

#include "glacier/persistence/RecoveryStore.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QGuiApplication>
#include <QObject>
#include <QProcess>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::persistence;
using namespace glacier::test;

class TestRecovery : public QObject
{
    Q_OBJECT

    StoreFixture m_assets;

    // Starts a writer process; it holds its session until killed.
    static bool startWriter(QProcess &process, const QString &root, const QString &board)
    {
        process.start(QString::fromLocal8Bit(GLACIER_RECOVERYWRITER_PATH), {root, board});
        return process.waitForReadyRead(30000) && process.readAllStandardOutput().trimmed() == "written";
    }

    static void crash(QProcess &process)
    {
        process.kill();
        process.waitForFinished(10000);
    }

    static QStringList files(const QString &dir, const QString &pattern)
    {
        QStringList out;
        QDirIterator it(dir, {pattern}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            out.append(it.next());
        return out;
    }

    RecoveryStore::SnapshotRequest request(const QString &title)
    {
        RecoveryStore::SnapshotRequest r;
        r.key = fixedId(100);
        r.content = richBoard(*m_assets.store);
        r.content.metadata.title = title;
        return r;
    }

private slots:
    void initTestCase()
    {
        io::configureImageAllocationLimit(Limits{});
        QVERIFY(m_assets.store);
    }

    // A06: the latest complete snapshot of a named board after a crash.
    void namedBoardIsRecoveredAfterACrash()
    {
        QTemporaryDir root;
        QProcess writer;
        QVERIFY(startWriter(writer, root.path(), u"/home/a/boards/moodboard.glacierref"_s));
        QVERIFY(RecoveryStore::findRecoverable(root.path()).isEmpty()); // still running
        crash(writer);

        const QList<RecoveryStore::Entry> entries = RecoveryStore::findRecoverable(root.path());
        QCOMPARE(entries.size(), 1);
        const RecoveryStore::Entry &e = entries.first();
        QCOMPARE(e.title, u"second"_s);
        QCOMPARE(e.boardPath, u"/home/a/boards/moodboard.glacierref"_s);
        QCOMPARE(e.documentId, fixedId(1));
        QCOMPARE(e.baseFingerprint, (FileFingerprint{1234, 5678}));
        QVERIFY(e.time.isValid());

        StoreFixture fresh;
        const LoadResult loaded = RecoveryStore::loadSnapshot(root.path(), e, *fresh.store, Limits{});
        QVERIFY2(loaded.content, qPrintable(loaded.error));
        BoardContent expected = richBoard(*m_assets.store);
        expected.metadata.title = u"second"_s;
        QCOMPARE(sorted(*loaded.content), sorted(expected));
        for (const Asset &a : expected.snapshot.assets)
            QVERIFY(fresh.store->contains(a.sha256));
        QCOMPARE(loaded.fingerprint, e.baseFingerprint);

        RecoveryStore::discardEntry(e);
        QVERIFY(RecoveryStore::findRecoverable(root.path()).isEmpty());
        QVERIFY(!QDir(e.sessionDir).exists());
    }

    void unnamedBoardIsRecoveredToo()
    {
        QTemporaryDir root;
        QProcess writer;
        QVERIFY(startWriter(writer, root.path(), u"-"_s));
        crash(writer);
        const QList<RecoveryStore::Entry> entries = RecoveryStore::findRecoverable(root.path());
        QCOMPARE(entries.size(), 1);
        QVERIFY(entries.first().boardPath.isEmpty());
        QCOMPARE(entries.first().title, u"second"_s);
    }

    void interruptedSnapshotsFallBackToTheLastCompleteOne()
    {
        QTemporaryDir root;
        QProcess writer;
        QVERIFY(startWriter(writer, root.path(), u"-"_s));
        crash(writer);
        const QStringList snapshots = [&] {
            QStringList s = files(root.path(), u"snapshot-*.json"_s);
            s.sort();
            return s;
        }();
        QCOMPARE(snapshots.size(), 2);

        // A newer manifest whose asset never reached the pool (the process
        // died between the two steps) is skipped...
        QFile newest(QFileInfo(snapshots.last()).absolutePath() + u"/snapshot-999999999999.json"_s);
        QFile source(snapshots.last());
        QVERIFY(source.open(QIODevice::ReadOnly) && newest.open(QIODevice::WriteOnly));
        newest.write(source.readAll()
                         .replace(QByteArray("\"title\": \"second\""), QByteArray("\"title\": \"third\""))
                         .replace(SessionAssetStore::sha256Hex(svgBytes()), QByteArray(64, 'a')));
        newest.close();
        // ...and so is a damaged one.
        QFile damaged(QFileInfo(snapshots.last()).absolutePath() + u"/snapshot-999999999998.json"_s);
        QVERIFY(damaged.open(QIODevice::WriteOnly));
        damaged.write("{\"format\": \"glacier-ref\", \"schemaVer");
        damaged.close();

        const QList<RecoveryStore::Entry> entries = RecoveryStore::findRecoverable(root.path());
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().title, u"second"_s);
        QCOMPARE(entries.first().snapshotPath, snapshots.last());
    }

    void keepsThreeSnapshotsPerDocument()
    {
        QTemporaryDir root;
        auto store = RecoveryStore::create({root.path()});
        QVERIFY(store);
        for (int i = 0; i < 5; ++i)
            QVERIFY(store->writeSnapshot(request(u"v%1"_s.arg(i)), *m_assets.store));
        QStringList snapshots = files(store->sessionDir(), u"snapshot-*.json"_s);
        QCOMPARE(snapshots.size(), 3);
        // Another document has its own three.
        RecoveryStore::SnapshotRequest other = request(u"other"_s);
        other.key = fixedId(101);
        QVERIFY(store->writeSnapshot(other, *m_assets.store));
        QCOMPARE(files(store->sessionDir(), u"snapshot-*.json"_s).size(), 4);
        QVERIFY(RecoveryStore::findRecoverable(root.path()).isEmpty()); // our own session is live
    }

    void poolAssetsAreSharedAndCollected()
    {
        QTemporaryDir root;
        RecoveryStore::Options options;
        options.rootDir = root.path();
        options.poolGraceSeconds = 0;
        auto store = RecoveryStore::create(options);
        QVERIFY(store);
        QVERIFY(store->writeSnapshot(request(u"a"_s), *m_assets.store));
        RecoveryStore::SnapshotRequest second = request(u"b"_s);
        second.key = fixedId(101);
        QVERIFY(store->writeSnapshot(second, *m_assets.store));
        const int assets = int(richBoard(*m_assets.store).snapshot.assets.size());
        QCOMPARE(files(root.filePath(u"pool"_s), u"*"_s).size(), assets); // written once, shared

        store->discardDocument(fixedId(100));
        QCOMPARE(files(root.filePath(u"pool"_s), u"*"_s).size(), assets); // still used by "b"
        store->discardDocument(fixedId(101));
        QVERIFY(files(root.filePath(u"pool"_s), u"*"_s).isEmpty());
        store->removeSession();
        QVERIFY(!QDir(store->sessionDir()).exists());
    }

    void budgetNeverDropsTheNewestSnapshotOfAnOpenBoard()
    {
        QTemporaryDir root;
        RecoveryStore::Options options;
        options.rootDir = root.path();
        options.budgetBytes = 1; // always over budget
        options.poolGraceSeconds = 0;
        auto store = RecoveryStore::create(options);
        QVERIFY(store);
        for (int i = 0; i < 3; ++i)
            QVERIFY(store->writeSnapshot(request(u"v%1"_s.arg(i)), *m_assets.store));
        const QStringList left = files(store->sessionDir(), u"snapshot-*.json"_s);
        QCOMPARE(left.size(), 1);
        QVERIFY(left.first().endsWith(u"000000000003.json"_s));
        QVERIFY(!files(root.filePath(u"pool"_s), u"*"_s).isEmpty()); // its assets stay
        QVERIFY(RecoveryStore::usedBytes(root.path()) > 0);
    }
};

GLACIER_TEST_GUI_MAIN(TestRecovery)
#include "tst_recovery.moc"
