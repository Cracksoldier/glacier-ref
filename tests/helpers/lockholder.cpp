// Test helper: takes a QLockFile at argv[1], reports "locked" on stdout, and
// waits to be killed. Killing it leaves a stale lock (dead owner) behind.
#include <QCoreApplication>
#include <QLockFile>
#include <QTextStream>
#include <QThread>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    if (argc < 2)
        return 2;
    QLockFile lock(QString::fromLocal8Bit(argv[1]));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return 1;
    QTextStream(stdout) << "locked" << Qt::endl;
    for (;;)
        QThread::sleep(1);
}
