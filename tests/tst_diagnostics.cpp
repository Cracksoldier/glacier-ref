#include "GlacierTestMain.h"

#include "DiagnosticLog.h"
#include "glacier/core/Diagnostics.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace glacier::diagnostics;

class TestDiagnostics : public QObject
{
    Q_OBJECT

private slots:
    void redactsPersonalPaths_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("unix home") << u"cannot open /home/jdoe/Bilder/ref.png"_s << u"cannot open ~/Bilder/ref.png"_s;
        QTest::newRow("windows home") << u"C:\\Users\\JDoe\\Pictures\\a.png: denied"_s
                                      << u"~\\Pictures\\a.png: denied"_s;
        QTest::newRow("windows forward") << u"C:/Users/jdoe/x.glacierref"_s << u"~/x.glacierref"_s;
        QTest::newRow("user elsewhere") << u"/mnt/jdoe-backup/x and /srv/jdoe/y"_s
                                        << u"/mnt/jdoe-backup/x and /srv/<user>/y"_s;
        QTest::newRow("unrelated") << u"saved \"board.glacierref\" (12 bytes)"_s
                                   << u"saved \"board.glacierref\" (12 bytes)"_s;
    }

    void redactsPersonalPaths()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        const QString home = input.contains(u"Users"_s) ? u"C:/Users/jdoe"_s : u"/home/jdoe"_s;
        QCOMPARE(redact(input, home, u"jdoe"_s), expected);
        QCOMPARE(redact(u"Ali was here"_s, u"/home/al"_s, u"al"_s), u"Ali was here"_s); // too short to redact
    }

    void lineFormat()
    {
        const QDateTime t(QDate(2026, 11, 7), QTime(10, 0, 0, 123), QTimeZone::UTC);
        QCOMPARE(formatLine(QtWarningMsg, "glacier.files", u"saving failed"_s, t),
                 u"2026-11-07T10:00:00.123Z warning glacier.files: saving failed"_s);
        QCOMPARE(formatLine(QtInfoMsg, nullptr, u"x"_s, t), u"2026-11-07T10:00:00.123Z info default: x"_s);
    }

    void writesACappedPerProcessFile()
    {
        QTemporaryDir dir;
        // Older logs of earlier processes: only the newest ones are kept.
        for (int i = 0; i < 7; ++i) {
            QFile old(dir.filePath(u"glacier-ref-2026010%1-000000-1%1.log"_s.arg(i)));
            QVERIFY(old.open(QIODevice::WriteOnly));
            old.write("old");
            old.close();
            QThread::msleep(5); // distinct modification times
        }
        LogOptions options;
        options.directory = dir.path();
        options.maxFileBytes = 400;
        options.keepFiles = 3;
        options.toStderr = false;
        QVERIFY(installLog(options));
        const QString path = logFilePath();
        QVERIFY(path.startsWith(dir.path()));
        QVERIFY(path.contains(QString::number(QCoreApplication::applicationPid())));
        qCWarning(lcFiles, "could not open %s", qPrintable(QDir::homePath() + u"/secret/board.glacierref"_s));
        for (int i = 0; i < 20; ++i)
            qCInfo(lcApp, "message number %d with some padding to fill the file", i);
        uninstallLog();

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.contains(u"warning glacier.files: could not open ~/secret/board.glacierref"_s));
        QVERIFY(!text.contains(QDir::homePath()));
        QVERIFY(text.contains(u"log size limit reached"_s));
        QVERIFY(file.size() < 600);
        QCOMPARE(QDir(dir.path()).entryList({u"glacier-ref-*.log"_s}, QDir::Files).size(), 3);
    }
};

GLACIER_TEST_WIDGETS_MAIN(TestDiagnostics)
#include "tst_diagnostics.moc"
