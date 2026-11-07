#include "GlacierTestMain.h"

#include "SettingsService.h"

#include <QApplication>
#include <QObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>

using namespace Qt::StringLiterals;
using glacier::SettingsService;

class TestSettings : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QSettings().clear();
    }

    void defaults()
    {
        SettingsService s;
        QVERIFY(!s.menuBarVisible()); // canvas first (spec §3)
        QVERIFY(!s.firstRunHintShown());
        QCOMPARE(s.theme(), SettingsService::Theme::Glacier);
        QCOMPARE(s.newBoardBackground(), SettingsService::DefaultBoardBackground);
        QCOMPARE(s.recoveryIntervalSeconds(), 60);
        QCOMPARE(s.recoveryBudgetMiB(), 2048);
        QCOMPARE(s.cacheBudgetMiB(), 512);
        QVERIFY(!s.frameless());
        QVERIFY(s.shortcutOverrides().isEmpty());
    }

    void valuesPersistAndSignal()
    {
        {
            SettingsService s;
            QSignalSpy menu(&s, &SettingsService::menuBarVisibleChanged);
            QSignalSpy appearance(&s, &SettingsService::appearanceChanged);
            QSignalSpy cache(&s, &SettingsService::cacheBudgetChanged);
            QSignalSpy recovery(&s, &SettingsService::recoveryChanged);
            s.setMenuBarVisible(true);
            s.setMenuBarVisible(true); // unchanged: no second signal
            s.setTheme(SettingsService::Theme::System);
            s.setNewBoardBackground(0x00102030u); // made opaque
            s.setCacheBudgetMiB(1024);
            s.setRecoveryIntervalSeconds(30);
            s.setRecoveryBudgetMiB(4096);
            s.setFrameless(true);
            s.setFirstRunHintShown();
            QCOMPARE(menu.size(), 1);
            QCOMPARE(appearance.size(), 2);
            QCOMPARE(cache.size(), 1);
            QCOMPARE(cache.first().first().toInt(), 1024);
            QCOMPARE(recovery.size(), 2);
        }
        SettingsService again; // another window or the next start
        QVERIFY(again.menuBarVisible());
        QCOMPARE(again.theme(), SettingsService::Theme::System);
        QCOMPARE(again.newBoardBackground(), 0xFF102030u);
        QCOMPARE(again.cacheBudgetMiB(), 1024);
        QCOMPARE(again.recoveryIntervalSeconds(), 30);
        QCOMPARE(again.recoveryBudgetMiB(), 4096);
        QVERIFY(again.frameless());
        QVERIFY(again.firstRunHintShown());
    }

    void rangesAreClamped()
    {
        SettingsService s;
        s.setRecoveryIntervalSeconds(1);
        QCOMPARE(s.recoveryIntervalSeconds(), SettingsService::MinRecoveryIntervalSeconds);
        s.setCacheBudgetMiB(1 << 30);
        QCOMPARE(s.cacheBudgetMiB(), SettingsService::MaxCacheBudgetMiB);
        QSettings().setValue(u"recovery/budgetMiB"_s, u"lots"_s); // hand-edited garbage
        QCOMPARE(s.recoveryBudgetMiB(), SettingsService::DefaultRecoveryBudgetMiB);
    }

    void shortcutOverrides()
    {
        SettingsService s;
        QSignalSpy changed(&s, &SettingsService::shortcutsChanged);
        const QList<QKeySequence> saveDefault{QKeySequence(Qt::CTRL | Qt::Key_S)};
        QCOMPARE(s.shortcuts(u"Save"_s, saveDefault), saveDefault);

        SettingsService::ShortcutMap overrides;
        overrides.insert(u"Save"_s, {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S), QKeySequence(Qt::Key_F12)});
        overrides.insert(u"Grayscale"_s, {}); // no shortcut at all
        s.setShortcutOverrides(overrides);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(s.shortcuts(u"Save"_s, saveDefault), overrides.value(u"Save"_s));
        QVERIFY(s.shortcuts(u"Grayscale"_s, {QKeySequence(Qt::Key_G)}).isEmpty());
        QCOMPARE(SettingsService().shortcutOverrides(), overrides);

        s.setShortcutOverrides({}); // reset all
        QCOMPARE(s.shortcuts(u"Save"_s, saveDefault), saveDefault);
        QCOMPARE(changed.size(), 2);
    }

    void conflictsAreReported()
    {
        SettingsService::ShortcutMap effective;
        effective.insert(u"Save"_s, {QKeySequence(Qt::CTRL | Qt::Key_S)});
        effective.insert(u"Crop"_s, {QKeySequence(Qt::Key_C)});
        effective.insert(u"CopyImage"_s, {QKeySequence(Qt::Key_C), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)});
        effective.insert(u"Grayscale"_s, {});
        const auto conflicts = SettingsService::conflicts(effective);
        QCOMPARE(conflicts.size(), 1);
        QCOMPARE(conflicts.value(QKeySequence(Qt::Key_C)), (QStringList{u"CopyImage"_s, u"Crop"_s}));
    }
};

GLACIER_TEST_WIDGETS_MAIN(TestSettings)
#include "tst_settings.moc"
