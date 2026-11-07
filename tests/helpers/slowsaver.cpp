// Test helper: saves a board to argv[1] and, after the first asset has been
// written, reports "writing" on stdout and stalls, waiting to be killed in
// the middle of the save (tst_atomicsave).
#include "TestBoardFiles.h"

#include "glacier/persistence/BoardSerializer.h"

#include <QGuiApplication>
#include <QTextStream>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace glacier;

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    if (argc < 2)
        return 2;
    test::StoreFixture fixture;
    persistence::SaveRequest request;
    request.content = test::richBoard(*fixture.store);
    request.content.metadata.title = u"half written"_s;
    request.path = QString::fromLocal8Bit(argv[1]);
    request.store = fixture.store;
    persistence::saveBoard(request, [](int done, int) {
        if (done == 2) {
            QTextStream(stdout) << "writing" << Qt::endl;
            for (;;)
                QThread::sleep(1);
        }
    });
    return 0; // not reached when killed
}
