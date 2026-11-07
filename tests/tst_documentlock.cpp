#include "GlacierTestMain.h"

#include "glacier/persistence/DocumentLock.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using glacier::persistence::DocumentLock;

class TestDocumentLock : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    QString board(const QString &name) const { return m_dir.filePath(name); }

private slots:
    void lockIsAHiddenSibling()
    {
        const QString path = board(u"Mood board.glacierref"_s);
        QCOMPARE(DocumentLock::lockPathFor(path), m_dir.path() + u"/.Mood board.glacierref.lock"_s);
        {
            const DocumentLock::Result r = DocumentLock::acquire(path);
            QCOMPARE(r.status, DocumentLock::Status::Acquired);
            QVERIFY(r.lock);
            QVERIFY(QFile::exists(DocumentLock::lockPathFor(path)));
#ifdef Q_OS_WIN
            QVERIFY(QFileInfo(DocumentLock::lockPathFor(path)).isHidden());
#endif
        }
        QVERIFY(!QFile::exists(DocumentLock::lockPathFor(path))); // released
    }

    void secondWriterGetsReadOnly()
    {
        const QString path = board(u"shared.glacierref"_s);
        const DocumentLock::Result first = DocumentLock::acquire(path);
        QCOMPARE(first.status, DocumentLock::Status::Acquired);
        const DocumentLock::Result second = DocumentLock::acquire(path); // another window
        QCOMPARE(second.status, DocumentLock::Status::HeldElsewhere);
        QVERIFY(!second.lock);
        QCOMPARE(second.holder.pid, QCoreApplication::applicationPid());
        QVERIFY(!second.holder.otherHost);
    }

    void otherProcessHoldsTheLockUntilItDies()
    {
        const QString path = board(u"process.glacierref"_s);
        QProcess holder;
        holder.start(QString::fromLocal8Bit(GLACIER_LOCKHOLDER_PATH), {DocumentLock::lockPathFor(path)});
        QVERIFY(holder.waitForReadyRead(10000));
        QCOMPARE(holder.readAllStandardOutput().trimmed(), QByteArray("locked"));

        DocumentLock::Result r = DocumentLock::acquire(path);
        QCOMPARE(r.status, DocumentLock::Status::HeldElsewhere);
        QCOMPARE(r.holder.pid, holder.processId());

        // The owner crashed: same host, process gone, so the lock is stale.
        holder.kill();
        QVERIFY(holder.waitForFinished(10000));
        QVERIFY(QFile::exists(DocumentLock::lockPathFor(path)));
        r = DocumentLock::acquire(path);
        QCOMPARE(r.status, DocumentLock::Status::Acquired);
    }

    void lockFromAnotherHostIsNeverTakenOver()
    {
        const QString path = board(u"network.glacierref"_s);
        QFile foreign(DocumentLock::lockPathFor(path));
        QVERIFY(foreign.open(QIODevice::WriteOnly));
        foreign.write("4242\nglacier-ref\nworkstation.elsewhere.example\n");
        foreign.close();

        DocumentLock::Result r = DocumentLock::acquire(path);
        QCOMPARE(r.status, DocumentLock::Status::HeldElsewhere);
        QVERIFY(r.holder.otherHost);
        QCOMPARE(r.holder.hostname, u"workstation.elsewhere.example"_s);
        QCOMPARE(r.holder.pid, 4242);
        // Still held on a later attempt: never stale by age.
        QCOMPARE(DocumentLock::acquire(path).status, DocumentLock::Status::HeldElsewhere);

        // Only an explicit removal (after the user confirms) frees it.
        QVERIFY(DocumentLock::removeLock(path));
        QCOMPARE(DocumentLock::acquire(path).status, DocumentLock::Status::Acquired);
    }

    void unwritableFolderIsReadOnly()
    {
#ifdef Q_OS_WIN
        QSKIP("Folder permissions are not enforced the same way on Windows");
#else
        QTemporaryDir dir;
        const QString path = dir.filePath(u"readonly.glacierref"_s);
        QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        const bool writable = QFileInfo(dir.path()).isWritable(); // e.g. running as root
        const DocumentLock::Result r = DocumentLock::acquire(path);
        QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        if (writable)
            QSKIP("The folder stays writable for this user");
        QCOMPARE(r.status, DocumentLock::Status::Unavailable);
        QVERIFY(!r.error.isEmpty());
#endif
    }
};

GLACIER_TEST_CORE_MAIN(TestDocumentLock)
#include "tst_documentlock.moc"
