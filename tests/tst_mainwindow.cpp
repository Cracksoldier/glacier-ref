#include "GlacierTestMain.h"
#include "TestImages.h"

#include "AppServices.h"
#include "MainWindow.h"
#include "glacier/canvas/BoardScene.h"
#include "glacier/canvas/BoardView.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/io/MimeRouter.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

using namespace Qt::StringLiterals;
using namespace glacier;

Q_DECLARE_METATYPE(glacier::io::ImportBatchResult)

class TestMainWindow : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;
    std::unique_ptr<AppServices> m_services;

    QString writePng(const QDir &dir, const QString &name, QColor color, QSize size = {120, 80})
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(color);
        const QString path = dir.filePath(name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(test::encode(image, "png"));
        return path;
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<io::ImportBatchResult>();
        m_services = AppServices::create({m_root.path(), {}});
        QVERIFY(m_services);
    }

    void cleanupTestCase() { m_services.reset(); }

    void showsEmptyCanvas()
    {
        MainWindow window(*m_services);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(window.windowTitle(), u"Glacier Ref"_s);
        QCOMPARE(window.centralWidget(), window.boardView());
        QCOMPARE(window.boardView()->boardScene(), window.boardScene());
        QVERIFY(window.boardScene()->items().isEmpty());
        QCOMPARE(window.document()->itemCount(), 0);
    }

    void exposesActionsWithSpecShortcuts_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QKeySequence>("shortcut");
        QTest::newRow("import") << u"actionImportImages"_s << QKeySequence(Qt::CTRL | Qt::Key_I);
        QTest::newRow("paste") << u"actionPaste"_s << QKeySequence(Qt::CTRL | Qt::Key_V);
        QTest::newRow("select all") << u"actionSelectAll"_s << QKeySequence(Qt::CTRL | Qt::Key_A);
        QTest::newRow("fit board") << u"actionFitBoard"_s << QKeySequence(Qt::Key_F);
        QTest::newRow("fit selection") << u"actionFitSelection"_s << QKeySequence(Qt::SHIFT | Qt::Key_F);
        QTest::newRow("zoom 100%") << u"actionZoomReset"_s << QKeySequence(Qt::CTRL | Qt::Key_0);
        QTest::newRow("always on top") << u"actionAlwaysOnTop"_s << QKeySequence(Qt::CTRL | Qt::Key_T);
        QTest::newRow("quit") << u"actionQuit"_s << QKeySequence(Qt::CTRL | Qt::Key_Q);
    }

    void exposesActionsWithSpecShortcuts()
    {
        QFETCH(QString, name);
        QFETCH(QKeySequence, shortcut);
        MainWindow window(*m_services);
        const QAction *action = window.findChild<QAction *>(name);
        QVERIFY2(action, qPrintable(name));
        QCOMPARE(action->shortcut(), shortcut);
        for (const auto &other : {u"actionImportFolder"_s, u"actionZoomIn"_s, u"actionZoomOut"_s,
                                  u"actionAbout"_s, u"actionAboutQt"_s})
            QVERIFY2(window.findChild<QAction *>(other), qPrintable(other));
    }

    void importsDroppedFilesEndToEnd()
    {
        QTemporaryDir dir;
        const QDir d(dir.path());
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(writePng(d, u"red.png"_s, Qt::red)),
                      QUrl::fromLocalFile(writePng(d, u"green.png"_s, Qt::green)),
                      QUrl::fromLocalFile(writePng(d, u"blue.png"_s, Qt::blue, {60, 200}))});

        MainWindow window(*m_services);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QSignalSpy applied(&window, &MainWindow::importApplied);
        emit window.boardView()->dropReceived(&mime, QPointF(500, -300));
        QVERIFY(applied.wait(30000));

        QCOMPARE(window.document()->itemCount(), 3);
        QCOMPARE(window.boardScene()->items().size(), 3);
        QCOMPARE(window.boardScene()->selectedItemIds().size(), 3); // new items are selected
        QCOMPARE(window.document()->boundingRect().center().x(), 500.0); // centred on the drop point
        QVERIFY(window.boardView()->zoom() != 0.0);
        QCOMPARE(window.document()->validate(), QString());
    }

    void onlineOnlyDropShowsOfflineMessage()
    {
        MainWindow window(*m_services);
        QMimeData mime;
        mime.setUrls({QUrl(u"https://example.com/a.jpg"_s)});
        emit window.boardView()->dropReceived(&mime, QPointF());
        const auto boxes = window.findChildren<QMessageBox *>();
        QCOMPARE(boxes.size(), 1);
        QCOMPARE(boxes.first()->text(), io::onlineImagesNotSupportedMessage());
        QCOMPARE(window.document()->itemCount(), 0);
    }

    void quitClosesWindowCleanly()
    {
        auto *window = new MainWindow(*m_services);
        window->setAttribute(Qt::WA_DeleteOnClose);
        QPointer<MainWindow> guard(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->findChild<QAction *>(u"actionQuit"_s)->trigger();
        QTRY_VERIFY(guard.isNull());
    }
};

GLACIER_TEST_WIDGETS_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
