#include "GlacierTestMain.h"

#include "MainWindow.h"
#include "glacier/canvas/BoardView.h"

#include <QAction>
#include <QApplication>
#include <QGraphicsScene>
#include <QMenuBar>
#include <QObject>
#include <QPointer>

using namespace Qt::StringLiterals;

class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void showsEmptyCanvas()
    {
        glacier::MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QCOMPARE(window.windowTitle(), u"Glacier Ref"_s);
        QVERIFY(window.boardView() != nullptr);
        QCOMPARE(window.centralWidget(), window.boardView());
        QVERIFY(window.boardView()->boardScene()->items().isEmpty());
    }

    void exposesCoreActions()
    {
        glacier::MainWindow window;
        for (const auto &name : {u"actionQuit"_s, u"actionAbout"_s, u"actionAboutQt"_s})
            QVERIFY2(window.findChild<QAction *>(name) != nullptr, qPrintable(name));
        QVERIFY(!window.findChild<QAction *>(u"actionQuit"_s)->shortcut().isEmpty());
    }

    void quitClosesWindowCleanly()
    {
        auto *window = new glacier::MainWindow;
        window->setAttribute(Qt::WA_DeleteOnClose);
        QPointer<glacier::MainWindow> guard(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));

        window->findChild<QAction *>(u"actionQuit"_s)->trigger();
        QTRY_VERIFY(guard.isNull());
    }
};

GLACIER_TEST_WIDGETS_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
