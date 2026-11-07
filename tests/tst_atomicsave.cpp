// A05 and M3.12: failed or interrupted saves keep the previous valid board.
#include "GlacierTestMain.h"
#include "TestBoardFiles.h"

#include "glacier/persistence/BoardSerializer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QObject>
#include <QProcess>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::persistence;
using namespace glacier::test;

class TestAtomicSave : public QObject
{
    Q_OBJECT

    StoreFixture m_fixture;

    SaveRequest request(const QString &path, const QString &title)
    {
        SaveRequest r;
        r.content = richBoard(*m_fixture.store);
        r.content.metadata.title = title;
        r.path = path;
        r.store = m_fixture.store;
        return r;
    }

    QString loadedTitle(const QString &path)
    {
        StoreFixture f;
        const LoadResult loaded = loadBoard(path, f.store, Limits{});
        return loaded.content ? loaded.content->metadata.title : u"<unreadable: %1>"_s.arg(loaded.error);
    }

private slots:
    void initTestCase()
    {
        io::configureImageAllocationLimit(Limits{});
        QVERIFY(m_fixture.store);
    }

    void diskFullKeepsThePreviousBoard()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"board.glacierref"_s);
        QVERIFY(saveBoard(request(path, u"original"_s)).ok);
        const FileFingerprint before = FileFingerprint::of(path);

        // Fail at the start, in the manifest, between assets, and on the very
        // last byte of the central directory.
        for (qint64 budget : {qint64(0), qint64(100), before.size / 2, before.size - 1}) {
            SaveRequest failing = request(path, u"replacement"_s);
            failing.failAfterBytes = budget;
            const SaveResult result = saveBoard(failing);
            QVERIFY2(!result.ok, qPrintable(QString::number(budget)));
            QVERIFY(!result.error.isEmpty());
            QCOMPARE(FileFingerprint::of(path), before);
            QCOMPARE(loadedTitle(path), u"original"_s);
        }
        // No temporary files are left behind.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{u"board.glacierref"_s});

        // A later successful save replaces it.
        QVERIFY(saveBoard(request(path, u"replacement"_s)).ok);
        QCOMPARE(loadedTitle(path), u"replacement"_s);
    }

    void firstSaveThatFailsLeavesNoFile()
    {
        QTemporaryDir dir;
        SaveRequest failing = request(dir.filePath(u"new.glacierref"_s), u"x"_s);
        failing.failAfterBytes = 500;
        QVERIFY(!saveBoard(failing).ok);
        QVERIFY(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).isEmpty());
    }

    void unwritableFolderKeepsThePreviousBoard()
    {
#ifdef Q_OS_WIN
        QSKIP("Folder permissions are not enforced the same way on Windows");
#else
        QTemporaryDir dir;
        const QString path = dir.filePath(u"board.glacierref"_s);
        QVERIFY(saveBoard(request(path, u"original"_s)).ok);
        QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        const bool writable = QFileInfo(dir.path()).isWritable();
        const SaveResult result = saveBoard(request(path, u"replacement"_s));
        QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        if (writable)
            QSKIP("The folder stays writable for this user");
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QCOMPARE(loadedTitle(path), u"original"_s);
#endif
    }

    void terminationDuringSaveKeepsThePreviousBoard()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"board.glacierref"_s);
        QVERIFY(saveBoard(request(path, u"original"_s)).ok);
        const FileFingerprint before = FileFingerprint::of(path);

        QProcess saver;
        saver.start(QString::fromLocal8Bit(GLACIER_SLOWSAVER_PATH), {path});
        QVERIFY(saver.waitForReadyRead(20000));
        QCOMPARE(saver.readAllStandardOutput().trimmed(), QByteArray("writing"));
        saver.kill(); // like a crash or power loss, in the middle of the archive
        QVERIFY(saver.waitForFinished(10000));

        QCOMPARE(FileFingerprint::of(path), before);
        QCOMPARE(loadedTitle(path), u"original"_s);
    }

    void fingerprintDetectsExternalModification()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"board.glacierref"_s);
        QVERIFY(!FileFingerprint::of(path).exists());
        const SaveResult saved = saveBoard(request(path, u"original"_s));
        QVERIFY(saved.ok);
        QCOMPARE(FileFingerprint::of(path), saved.fingerprint);

        // Another program rewrites the file.
        QFile other(path);
        QVERIFY(other.open(QIODevice::WriteOnly | QIODevice::Append));
        other.write("x");
        other.close();
        QVERIFY(FileFingerprint::of(path) != saved.fingerprint);
        QVERIFY(QFile::remove(path));
        QVERIFY(!FileFingerprint::of(path).exists());
    }
};

GLACIER_TEST_GUI_MAIN(TestAtomicSave)
#include "tst_atomicsave.moc"
