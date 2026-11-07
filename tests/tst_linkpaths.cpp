#include "GlacierTestMain.h"

#include "glacier/core/LinkPaths.h"

#include <QCoreApplication>
#include <QObject>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::link_paths;

Q_DECLARE_METATYPE(glacier::link_paths::StoredPath)

class TestLinkPaths : public QObject
{
    Q_OBJECT

private slots:
    void roots_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("root");
        QTest::newRow("unix") << u"/home/a/x.png"_s << u"/"_s;
        QTest::newRow("drive") << u"C:/Users/a.png"_s << u"C:/"_s;
        QTest::newRow("drive backslash") << u"d:\\pics\\a.png"_s << u"d:/"_s;
        QTest::newRow("unc") << u"//server/share/a.png"_s << u"//server/share/"_s;
        QTest::newRow("unc root only") << u"//server/share"_s << u"//server/share/"_s;
        QTest::newRow("relative") << u"pics/a.png"_s << QString();
        QTest::newRow("drive relative") << u"C:a.png"_s << QString();
        QTest::newRow("bad unc") << u"//server"_s << QString();
        QTest::newRow("empty") << QString() << QString();
    }

    void roots()
    {
        QFETCH(QString, path);
        QFETCH(QString, root);
        QCOMPARE(link_paths::root(path), root);
        QCOMPARE(isAbsolute(path), !root.isEmpty());
    }

    void normalizeIsPlatformIndependent_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("dots") << u"/a/./b/../c.png"_s << u"/a/c.png"_s;
        QTest::newRow("double slash") << u"/a//b.png"_s << u"/a/b.png"_s;
        QTest::newRow("above root") << u"/../a.png"_s << u"/a.png"_s;
        QTest::newRow("windows") << u"C:\\a\\..\\b\\c.png"_s << u"C:/b/c.png"_s;
        QTest::newRow("unc kept") << u"//srv/share/x/../y.png"_s << u"//srv/share/y.png"_s;
        QTest::newRow("relative up") << u"../../a/./b.png"_s << u"../../a/b.png"_s;
        QTest::newRow("relative collapse") << u"a/../../b.png"_s << u"../b.png"_s;
        QTest::newRow("unicode and spaces") << u"/home/ä ö/Référence 1.png"_s << u"/home/ä ö/Référence 1.png"_s;
    }

    void normalizeIsPlatformIndependent()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(normalize(input), expected);
        QCOMPARE(normalize(expected), expected);
    }

    void storedPaths_data()
    {
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("boardDir");
        QTest::addColumn<StoredPath>("stored");
        QTest::newRow("same dir") << u"/home/a/boards/x.png"_s << u"/home/a/boards"_s
                                  << StoredPath{u"x.png"_s, true};
        QTest::newRow("subdir") << u"/home/a/boards/refs/x.png"_s << u"/home/a/boards"_s
                                << StoredPath{u"refs/x.png"_s, true};
        QTest::newRow("sibling") << u"/home/a/pics/x.png"_s << u"/home/a/boards"_s
                                 << StoredPath{u"../pics/x.png"_s, true};
        QTest::newRow("only root shared") << u"/mnt/usb/x.png"_s << u"/home/a/boards"_s
                                          << StoredPath{u"/mnt/usb/x.png"_s, false};
        QTest::newRow("unsaved") << u"/home/a/x.png"_s << QString() << StoredPath{u"/home/a/x.png"_s, false};
        QTest::newRow("windows same drive") << u"C:/Users/a/pics/x.png"_s << u"c:/Users/a/boards"_s
                                            << StoredPath{u"../pics/x.png"_s, true};
        QTest::newRow("windows case") << u"C:/Users/A/x.png"_s << u"C:/users/a"_s << StoredPath{u"x.png"_s, true};
        QTest::newRow("windows other drive") << u"D:/pics/x.png"_s << u"C:/Users/a"_s
                                             << StoredPath{u"D:/pics/x.png"_s, false};
        QTest::newRow("unc vs drive") << u"//srv/share/x.png"_s << u"C:/boards"_s
                                      << StoredPath{u"//srv/share/x.png"_s, false};
        QTest::newRow("unc same share") << u"//srv/share/team/pics/x.png"_s << u"//srv/share/team/boards"_s
                                        << StoredPath{u"../pics/x.png"_s, true};
        QTest::newRow("unc only share shared") << u"//srv/share/pics/x.png"_s << u"//srv/share/boards"_s
                                               << StoredPath{u"//srv/share/pics/x.png"_s, false};
        QTest::newRow("unix is case sensitive") << u"/home/A/x.png"_s << u"/home/a"_s
                                                << StoredPath{u"../A/x.png"_s, true};
        QTest::newRow("unicode") << u"/home/ä/Bilder/Ref 1.png"_s << u"/home/ä/Boards"_s
                                 << StoredPath{u"../Bilder/Ref 1.png"_s, true};
    }

    void storedPaths()
    {
        QFETCH(QString, target);
        QFETCH(QString, boardDir);
        QFETCH(StoredPath, stored);
        const StoredPath actual = toStored(target, boardDir);
        QCOMPARE(actual.path, stored.path);
        QCOMPARE(actual.relative, stored.relative);
        // Resolving gives the target back (case of the board dir aside).
        QCOMPARE(resolve(actual, boardDir).compare(normalize(target), Qt::CaseInsensitive), 0);
    }

    void saveAsRebasesToTheSameTarget()
    {
        const QString target = u"/home/a/pics/x.png"_s;
        const StoredPath first = toStored(target, u"/home/a/boards"_s);
        QCOMPARE(first.path, u"../pics/x.png"_s);
        // Load from the old place, then Save As into another folder.
        const QString absolute = resolve(first, u"/home/a/boards"_s);
        const StoredPath rebased = toStored(absolute, u"/home/a/archive/2026/november"_s);
        QCOMPARE(rebased.path, u"../../../pics/x.png"_s);
        QCOMPARE(resolve(rebased, u"/home/a/archive/2026/november"_s), target);
    }

    void resolveRejectsInvalidCombinations()
    {
        QCOMPARE(resolve({u"x.png"_s, true}, QString()), QString());           // unsaved board
        QCOMPARE(resolve({u"/x.png"_s, true}, u"/home"_s), QString());         // kind mismatch
        QCOMPARE(resolve({u"x.png"_s, false}, u"/home"_s), QString());         // kind mismatch
        QCOMPARE(resolve({QString(), false}, u"/home"_s), QString());
        QCOMPARE(resolve({u"C:/pics/x.png"_s, false}, u"/home"_s), u"C:/pics/x.png"_s); // foreign, kept
        QCOMPARE(resolve({u"../../../../x.png"_s, true}, u"/a/b"_s), u"/x.png"_s);
    }
};

GLACIER_TEST_CORE_MAIN(TestLinkPaths)
#include "tst_linkpaths.moc"
