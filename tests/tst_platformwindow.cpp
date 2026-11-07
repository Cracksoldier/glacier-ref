#include "GlacierTestMain.h"

#include "glacier/platform/PlatformWindowService.h"

#include <QGuiApplication>
#include <QObject>

using namespace Qt::StringLiterals;
using namespace glacier::platform;

class TestPlatformWindow : public QObject
{
    Q_OBJECT

private slots:
    void capabilities_data()
    {
        QTest::addColumn<QString>("platform");
        QTest::addColumn<bool>("opacity");
        QTest::addColumn<bool>("onTopConfirmed");
        QTest::addColumn<bool>("moveResize");
        QTest::addColumn<bool>("positioning");
        QTest::newRow("wayland") << u"wayland"_s << false << false << true << false;
        QTest::newRow("wayland-egl") << u"wayland-egl"_s << false << false << true << false;
        QTest::newRow("x11") << u"xcb"_s << true << true << true << true;
        QTest::newRow("windows") << u"windows"_s << true << true << true << true;
        QTest::newRow("macos") << u"cocoa"_s << true << true << true << true;
        QTest::newRow("offscreen") << u"offscreen"_s << true << true << false << true;
    }

    void capabilities()
    {
        QFETCH(QString, platform);
        QFETCH(bool, opacity);
        QFETCH(bool, onTopConfirmed);
        QFETCH(bool, moveResize);
        QFETCH(bool, positioning);
        const WindowCapabilities c = capabilitiesFor(platform);
        QCOMPARE(c.opacity, opacity);
        QCOMPARE(c.alwaysOnTopConfirmed, onTopConfirmed);
        QCOMPARE(c.systemMoveResize, moveResize);
        QCOMPARE(c.positioning, positioning);
    }

    void stayOnTopIsNeverClaimedOnWayland()
    {
        QCOMPARE(stayOnTopStatus(false, capabilitiesFor(u"wayland"_s)), StayOnTop::Off);
        QCOMPARE(stayOnTopStatus(true, capabilitiesFor(u"wayland"_s)), StayOnTop::Requested);
        QCOMPARE(stayOnTopStatus(true, capabilitiesFor(u"xcb"_s)), StayOnTop::On);
    }

    void systemMoveWithoutAWindowFails()
    {
        QVERIFY(!startSystemMove(nullptr));
        QVERIFY(!startSystemResize(nullptr, Qt::LeftEdge));
    }
};

GLACIER_TEST_GUI_MAIN(TestPlatformWindow)
#include "tst_platformwindow.moc"
