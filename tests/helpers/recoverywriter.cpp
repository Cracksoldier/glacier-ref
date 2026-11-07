// Test helper: a process that keeps recovery snapshots and then "crashes".
//
//   glacier_test_recoverywriter <recovery root> <board path or "-">
//
// Writes two snapshots of the test board (titled "first" and "second") for
// the given board path ("-": an unnamed board), reports "written" on stdout,
// and waits to be killed (tst_recovery).
#include "TestBoardFiles.h"

#include "glacier/persistence/RecoveryStore.h"

#include <QGuiApplication>
#include <QTextStream>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::persistence;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    if (argc < 3)
        return 2;
    test::StoreFixture assets;
    RecoveryStore::Options options;
    options.rootDir = QString::fromLocal8Bit(argv[1]);
    auto store = RecoveryStore::create(options);
    if (!store)
        return 1;
    RecoveryStore::SnapshotRequest request;
    request.key = Id::generate();
    request.content = test::richBoard(*assets.store);
    const QString board = QString::fromLocal8Bit(argv[2]);
    request.boardPath = board == u"-"_s ? QString() : board;
    request.baseFingerprint = FileFingerprint{1234, 5678};
    for (const QString &title : {u"first"_s, u"second"_s}) {
        request.content.metadata.title = title;
        if (!store->writeSnapshot(request, *assets.store))
            return 1;
    }
    QTextStream(stdout) << "written" << Qt::endl;
    for (;;)
        QThread::sleep(1);
}
