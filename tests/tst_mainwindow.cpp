#include "GlacierTestMain.h"
#include "TestImages.h"

#include "AppServices.h"
#include "MainWindow.h"
#include "glacier/canvas/BoardScene.h"
#include "glacier/canvas/BoardView.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/editing/ClipboardFragment.h"
#include "glacier/editing/CommandController.h"
#include "glacier/editing/Operations.h"
#include "glacier/io/MimeRouter.h"
#include "glacier/persistence/BoardSerializer.h"
#include "glacier/persistence/DocumentLock.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::editing;

Q_DECLARE_METATYPE(glacier::io::ImportBatchResult)

class TestMainWindow : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;
    QTemporaryDir m_images;
    std::unique_ptr<AppServices> m_services;

    QString writePng(const QString &name, QColor color, QSize size)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(color);
        image.setPixel(0, 0, qRgb(255, 255, 255)); // marks the top-left corner
        const QString path = QDir(m_images.path()).filePath(name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(test::encode(image, "png"));
        return path;
    }

    // Answers the next message box with this object name (modal dialogs run
    // their own event loop, so the answer comes from a timer).
    static void answer(const QString &objectName, QMessageBox::StandardButton button,
                       const QString &customButton = {})
    {
        auto *timer = new QTimer;
        timer->setInterval(20);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, objectName, button, customButton] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget *w : widgets) {
                auto *box = qobject_cast<QMessageBox *>(w);
                if (!box || box->objectName() != objectName || !box->isVisible())
                    continue;
                timer->stop();
                timer->deleteLater();
                if (!customButton.isEmpty()) {
                    for (QAbstractButton *b : box->buttons())
                        if (b->text() == customButton)
                            b->click();
                } else {
                    box->button(button)->click();
                }
                return;
            }
        });
        timer->start();
    }

    static bool saveAndWait(MainWindow &window, const QString &path)
    {
        QSignalSpy finished(&window, &MainWindow::saveFinished);
        window.saveAs(path);
        return finished.wait(30000) && finished.first().first().toBool();
    }

    // Imports three real images into a window (through the undo stack).
    bool importThree(MainWindow &window)
    {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(writePng(u"red.png"_s, Qt::red, {120, 80})),
                      QUrl::fromLocalFile(writePng(u"green.png"_s, Qt::green, {120, 80})),
                      QUrl::fromLocalFile(writePng(u"blue.png"_s, Qt::blue, {60, 200}))});
        QSignalSpy applied(&window, &MainWindow::importApplied);
        emit window.boardView()->dropReceived(&mime, QPointF(500, -300));
        return applied.wait(30000) && window.document()->itemCount() == 3;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true); // settings and app data stay out of the user's profile
        qRegisterMetaType<io::ImportBatchResult>();
        m_services = AppServices::create({m_root.path(), {}});
        QVERIFY(m_services);
    }

    void cleanupTestCase()
    {
        const auto widgets = QApplication::topLevelWidgets();
        for (QWidget *w : widgets)
            if (qobject_cast<MainWindow *>(w))
                delete w;
        m_services.reset();
    }

    void showsEmptyCanvas()
    {
        MainWindow window(*m_services);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.windowTitle().contains(u"Glacier Ref"_s));
        QVERIFY(!window.isWindowModified());
        QCOMPARE(window.centralWidget(), window.boardView());
        QVERIFY(window.boardScene()->items().isEmpty());
    }

    void actionsHaveSpecShortcuts_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QKeySequence>("shortcut");
        QTest::newRow("new window") << u"NewWindow"_s << QKeySequence(Qt::CTRL | Qt::Key_N);
        QTest::newRow("open") << u"Open"_s << QKeySequence(Qt::CTRL | Qt::Key_O);
        QTest::newRow("save") << u"Save"_s << QKeySequence(Qt::CTRL | Qt::Key_S);
        QTest::newRow("save as") << u"SaveAs"_s << QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S);
        QTest::newRow("import") << u"ImportImages"_s << QKeySequence(Qt::CTRL | Qt::Key_I);
        QTest::newRow("import as link") << u"ImportAsLink"_s << QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I);
        QTest::newRow("close") << u"CloseWindow"_s << QKeySequence(Qt::CTRL | Qt::Key_W);
        QTest::newRow("undo") << u"Undo"_s << QKeySequence(Qt::CTRL | Qt::Key_Z);
        QTest::newRow("redo") << u"Redo"_s << QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z);
        QTest::newRow("cut") << u"Cut"_s << QKeySequence(Qt::CTRL | Qt::Key_X);
        QTest::newRow("copy") << u"Copy"_s << QKeySequence(Qt::CTRL | Qt::Key_C);
        QTest::newRow("copy image") << u"CopyImage"_s << QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C);
        QTest::newRow("paste") << u"Paste"_s << QKeySequence(Qt::CTRL | Qt::Key_V);
        QTest::newRow("duplicate") << u"Duplicate"_s << QKeySequence(Qt::CTRL | Qt::Key_D);
        QTest::newRow("delete") << u"Delete"_s << QKeySequence(Qt::Key_Delete);
        QTest::newRow("select all") << u"SelectAll"_s << QKeySequence(Qt::CTRL | Qt::Key_A);
        QTest::newRow("new note") << u"NewNote"_s << QKeySequence(Qt::Key_N);
        QTest::newRow("crop") << u"Crop"_s << QKeySequence(Qt::Key_C);
        QTest::newRow("flip h") << u"FlipHorizontal"_s << QKeySequence(Qt::Key_H);
        QTest::newRow("flip v") << u"FlipVertical"_s << QKeySequence(Qt::Key_V);
        QTest::newRow("group") << u"Group"_s << QKeySequence(Qt::CTRL | Qt::Key_G);
        QTest::newRow("ungroup") << u"Ungroup"_s << QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G);
        QTest::newRow("rename group") << u"RenameGroup"_s << QKeySequence(Qt::Key_F2);
        QTest::newRow("lock") << u"ToggleLock"_s << QKeySequence(Qt::CTRL | Qt::Key_L);
        QTest::newRow("raise") << u"Raise"_s << QKeySequence(Qt::CTRL | Qt::Key_PageUp);
        QTest::newRow("to front") << u"BringToFront"_s << QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_PageUp);
        QTest::newRow("fit board") << u"FitBoard"_s << QKeySequence(Qt::Key_F);
        QTest::newRow("fit selection") << u"FitSelection"_s << QKeySequence(Qt::SHIFT | Qt::Key_F);
        QTest::newRow("zoom 100%") << u"ZoomReset"_s << QKeySequence(Qt::CTRL | Qt::Key_0);
        QTest::newRow("grayscale") << u"Grayscale"_s << QKeySequence(Qt::Key_G);
        QTest::newRow("always on top") << u"AlwaysOnTop"_s << QKeySequence(Qt::CTRL | Qt::Key_T);
        QTest::newRow("quit") << u"Quit"_s << QKeySequence(Qt::CTRL | Qt::Key_Q);
    }

    void actionsHaveSpecShortcuts()
    {
        QFETCH(QString, id);
        QFETCH(QKeySequence, shortcut);
        MainWindow window(*m_services);
        QAction *action = window.action(id);
        QVERIFY2(action, qPrintable(id));
        QCOMPARE(action->shortcut(), shortcut);
    }

    void shortcutsAreUnique()
    {
        MainWindow window(*m_services);
        QHash<QKeySequence, QString> seen;
        for (const QString &id : window.actionIds()) {
            for (const QKeySequence &k : window.action(id)->shortcuts()) {
                QVERIFY2(!seen.contains(k), qPrintable(id + u" clashes with "_s + seen.value(k)));
                seen.insert(k, id);
            }
        }
    }

    void importIsUndoable()
    {
        MainWindow window(*m_services);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(importThree(window));
        QCOMPARE(window.boardScene()->selectedIds().size(), 3);
        QVERIFY(window.isWindowModified());
        window.action(u"Undo"_s)->trigger();
        QCOMPARE(window.document()->itemCount(), 0);
        QVERIFY(!window.isWindowModified());
        window.action(u"Redo"_s)->trigger();
        QCOMPARE(window.document()->itemCount(), 3);
    }

    void actionStatesFollowSelection()
    {
        MainWindow window(*m_services);
        QVERIFY(importThree(window));
        window.boardScene()->clearSelection();
        QVERIFY(!window.action(u"Delete"_s)->isEnabled());
        QVERIFY(!window.action(u"Group"_s)->isEnabled());
        const QList<Id> ids = window.document()->topLevelIdsByZ();
        window.boardScene()->selectIds({ids[0]});
        QVERIFY(window.action(u"Delete"_s)->isEnabled());
        QVERIFY(window.action(u"Crop"_s)->isEnabled());
        QVERIFY(window.action(u"CopyImage"_s)->isEnabled());
        QVERIFY(!window.action(u"Group"_s)->isEnabled());
        window.action(u"ToggleLock"_s)->trigger();
        QVERIFY(ops::isLocked(*window.document(), ids[0]));
        QVERIFY(!window.action(u"Delete"_s)->isEnabled());
        QVERIFY(!window.action(u"Crop"_s)->isEnabled());
        QVERIFY(window.action(u"ToggleLock"_s)->text().contains(u"lock"_s, Qt::CaseInsensitive));
        window.boardScene()->selectIds({ids[1], ids[2]});
        QVERIFY(window.action(u"Group"_s)->isEnabled());
        window.action(u"Group"_s)->trigger();
        QCOMPARE(window.document()->groups().size(), 1);
        QVERIFY(window.action(u"Ungroup"_s)->isEnabled());

        std::unique_ptr<QMenu> menu(window.createContextMenu(&window));
        QVERIFY(menu->actions().contains(window.action(u"Ungroup"_s)));
        QVERIFY(menu->actions().contains(window.action(u"Copy"_s)));
    }

    void newWindowIsIndependent()
    {
        MainWindow first(*m_services);
        QVERIFY(importThree(first));
        first.action(u"NewWindow"_s)->trigger();
        MainWindow *second = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets())
            if (auto *mw = qobject_cast<MainWindow *>(w); mw && mw != &first && mw->isVisible())
                second = mw;
        QVERIFY(second);
        QCOMPARE(second->document()->itemCount(), 0);
        QVERIFY(second->document()->documentToken() != first.document()->documentToken());
        QVERIFY(!second->controller()->undoStack()->canUndo());
        second->close(); // WA_DeleteOnClose
    }

    void copyPasteBetweenWindows()
    {
        MainWindow source(*m_services);
        QVERIFY(importThree(source));
        source.action(u"SelectAll"_s)->trigger();
        source.action(u"Copy"_s)->trigger();
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        QVERIFY(mime && mime->hasFormat(QString::fromLatin1(FragmentMimeType)));

        MainWindow target(*m_services);
        target.action(u"Paste"_s)->trigger();
        QCOMPARE(target.document()->itemCount(), 3);
        QCOMPARE(target.document()->assetCount(), 3);
        QCOMPARE(target.document()->validate(), QString());
        QCOMPARE(target.boardScene()->selectedIds().size(), 3);
        target.action(u"Paste"_s)->trigger(); // pasting again duplicates, sharing assets
        QCOMPARE(target.document()->itemCount(), 6);
        QCOMPARE(target.document()->assetCount(), 3);
        target.action(u"Undo"_s)->trigger();
        QCOMPARE(target.document()->itemCount(), 3);
    }

    void cutRemovesAndDuplicateOffsets()
    {
        MainWindow window(*m_services);
        QVERIFY(importThree(window));
        const QList<Id> ids = window.document()->topLevelIdsByZ();
        window.boardScene()->selectIds({ids[0]});
        window.action(u"Duplicate"_s)->trigger();
        QCOMPARE(window.document()->itemCount(), 4);
        window.boardScene()->selectIds({ids[1]});
        window.action(u"Cut"_s)->trigger();
        QCOMPARE(window.document()->itemCount(), 3);
        window.action(u"Paste"_s)->trigger();
        QCOMPARE(window.document()->itemCount(), 4);
    }

    void copyImageKeepsCropFlipAndRotation()
    {
        MainWindow window(*m_services);
        QVERIFY(importThree(window));
        const Id red = window.document()->topLevelIdsByZ().first(); // 120x80, white pixel top-left
        QVERIFY(window.controller()->run(ops::setCrop(*window.document(), red, QRectF(0, 0, 0.5, 1)), u"Crop"_s));
        QVERIFY(window.controller()->run(ops::flip(*window.document(), {red}, Qt::Horizontal), u"Flip"_s));
        QVERIFY(window.controller()->run(ops::rotate(*window.document(), {red}, 90), u"Rotate"_s));
        window.boardScene()->selectIds({red});
        QSignalSpy finished(&window, &MainWindow::copyImageFinished);
        window.action(u"CopyImage"_s)->trigger();
        QVERIFY(finished.wait(10000));
        QVERIFY(finished.first().first().toBool());
        const QImage copied = QGuiApplication::clipboard()->image();
        // Cropped to the left half (60x80), flipped horizontally (white moves to the
        // top-right corner), then turned 90 degrees clockwise (to the bottom-right).
        QCOMPARE(copied.size(), QSize(80, 60));
        QCOMPARE(QColor(copied.pixel(79, 59)), QColor(255, 255, 255));
        QVERIFY(QColor(copied.pixel(0, 0)) != QColor(255, 255, 255));
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
    }

    // --- Board files (M3) ---------------------------------------------------

    void saveAndOpenRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"moodboard.glacierref"_s);
        auto *first = new MainWindow(*m_services);
        QVERIFY(importThree(*first));
        QVERIFY(first->controller()->run(ops::createNote(*first->document(), {0, 0}, u"Notiz ✓"_s), u"Note"_s));
        QVERIFY(first->isWindowModified());
        QVERIFY(saveAndWait(*first, path));
        QVERIFY(!first->controller()->isDirty());
        QVERIFY(!first->isWindowModified());
        QCOMPARE(first->filePath(), path);
        QVERIFY(first->windowTitle().startsWith(u"moodboard.glacierref"_s));
        QCOMPARE(first->document()->title(), u"moodboard"_s); // defaults to the file name
        QVERIFY(QFile::exists(persistence::DocumentLock::lockPathFor(path)));
        const Snapshot saved = first->document()->snapshotAll();

        // Opening the open board activates its window.
        QCOMPARE(MainWindow::openBoard(*m_services, path, nullptr), first);

        // A second window for the same file (as another process would) is read-only.
        auto second = std::make_unique<MainWindow>(*m_services);
        answer(u"readOnlyNotice"_s, QMessageBox::Ok);
        QSignalSpy loaded(second.get(), &MainWindow::boardLoaded);
        second->loadBoard(path);
        QVERIFY(loaded.wait(30000));
        QVERIFY(loaded.first().first().toBool());
        QVERIFY(second->isReadOnly());
        QVERIFY(second->windowTitle().contains(u"read-only"_s));
        QVERIFY(!second->action(u"Save"_s)->isEnabled());
        QVERIFY(second->action(u"SaveAs"_s)->isEnabled());
        QCOMPARE(second->document()->snapshotAll(), saved);
        QVERIFY(!second->controller()->isDirty());
        QCOMPARE(second->controller()->undoStack()->count(), 0);

        // Closing the first window releases the lock; the next open is writable.
        delete first;
        QCOMPARE(MainWindow::openBoard(*m_services, path, nullptr), second.get()); // still shown there
        second.reset();
        QVERIFY(!QFile::exists(persistence::DocumentLock::lockPathFor(path)));
        MainWindow third(*m_services);
        QSignalSpy loadedThird(&third, &MainWindow::boardLoaded);
        QCOMPARE(MainWindow::openBoard(*m_services, path, &third), &third); // reuses the empty window
        QVERIFY(loadedThird.wait(30000));
        QVERIFY(!third.isReadOnly());
        QCOMPARE(third.document()->snapshotAll(), saved);
        QCOMPARE(third.document()->notes().begin()->text, u"Notiz ✓"_s);
    }

    void openFailureClosesTheWindowOpenedForIt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"broken.glacierref"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is not a zip archive");
        file.close();
        MainWindow busy(*m_services);
        QVERIFY(busy.controller()->run(ops::createNote(*busy.document(), {0, 0}, u"x"_s), u"Note"_s));
        QVERIFY(!busy.isReusable());
        answer(u"openFailedBox"_s, QMessageBox::Ok);
        QPointer<MainWindow> opened = MainWindow::openBoard(*m_services, path, &busy);
        QVERIFY(opened && opened.data() != &busy);
        QTRY_VERIFY_WITH_TIMEOUT(opened.isNull(), 30000); // closed after the error
        QCOMPARE(busy.document()->itemCount(), 1);
    }

    void editsDuringSaveStayDirty()
    {
        QTemporaryDir dir;
        MainWindow window(*m_services);
        QVERIFY(importThree(window));
        QSignalSpy finished(&window, &MainWindow::saveFinished);
        window.saveAs(dir.filePath(u"async.glacierref"_s));
        QVERIFY(window.isSaving());
        // An edit while the worker writes the snapshot.
        QVERIFY(window.controller()->run(ops::createNote(*window.document(), {0, 0}, u"later"_s), u"Note"_s));
        QVERIFY(finished.wait(30000));
        QVERIFY(finished.first().first().toBool());
        QVERIFY(window.controller()->isDirty());
        QVERIFY(window.isWindowModified());
        window.controller()->undoStack()->undo(); // back to the saved state
        QVERIFY(!window.controller()->isDirty());
    }

    void failedSaveKeepsChangesDirty()
    {
        MainWindow window(*m_services);
        QVERIFY(importThree(window));
        QTemporaryDir dir;
        // The lock can be taken, but the target is a folder: writing fails.
        QVERIFY(QDir(dir.path()).mkdir(u"board.glacierref"_s));
        answer(u"saveFailedBox"_s, QMessageBox::Ok);
        QSignalSpy finished(&window, &MainWindow::saveFinished);
        window.saveAs(dir.filePath(u"board.glacierref"_s));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QCOMPARE(finished.first().first().toBool(), false);
        QVERIFY(window.controller()->isDirty());
        QVERIFY(window.filePath().isEmpty());

        // A folder that doesn't exist can't even be locked: explained, nothing saved.
        answer(u"saveAsBlockedBox"_s, QMessageBox::Ok);
        bool outcome = true;
        window.saveAs(dir.filePath(u"missing/board.glacierref"_s), [&outcome](bool ok) { outcome = ok; });
        QVERIFY(!outcome);
        QVERIFY(window.controller()->isDirty());
    }

    void externalModificationIsNotOverwrittenSilently()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"shared.glacierref"_s);
        MainWindow window(*m_services);
        QVERIFY(importThree(window));
        QVERIFY(saveAndWait(window, path));
        QFile other(path);
        QVERIFY(other.open(QIODevice::WriteOnly | QIODevice::Append));
        other.write("changed by someone else");
        other.close();
        const persistence::FileFingerprint external = persistence::FileFingerprint::of(path);
        QVERIFY(window.controller()->run(ops::createNote(*window.document(), {0, 0}, u"x"_s), u"Note"_s));

        answer(u"externalModificationBox"_s, QMessageBox::Cancel);
        bool outcome = true;
        window.save([&outcome](bool ok) { outcome = ok; });
        QVERIFY(!outcome);
        QCOMPARE(persistence::FileFingerprint::of(path), external); // untouched

        answer(u"externalModificationBox"_s, QMessageBox::NoButton, u"Replace"_s);
        QSignalSpy finished(&window, &MainWindow::saveFinished);
        window.save();
        QVERIFY(finished.wait(30000));
        QVERIFY(finished.first().first().toBool());
        QVERIFY(!window.controller()->isDirty());
    }

    void closePromptsForUnsavedChanges()
    {
        auto *window = new MainWindow(*m_services);
        window->setAttribute(Qt::WA_DeleteOnClose);
        QPointer<MainWindow> guard(window);
        window->show();
        QVERIFY(window->controller()->run(ops::createNote(*window->document(), {0, 0}, u"x"_s), u"Note"_s));

        answer(u"unsavedChangesBox"_s, QMessageBox::Cancel);
        QVERIFY(!window->close());
        QVERIFY(guard);

        answer(u"unsavedChangesBox"_s, QMessageBox::Discard);
        QVERIFY(window->close());
        QTRY_VERIFY(guard.isNull());
    }

    void saveShortcutWorksWhileEditingANote()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"notes.glacierref"_s);
        MainWindow window(*m_services);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(saveAndWait(window, path));

        window.boardView()->setFocus();
        window.boardView()->beginNewNote({100, 100});
        QVERIFY(window.boardView()->isTextEditing());
        QTest::keyClicks(window.boardView(), u"fhs"_s); // canvas keys go to the text
        QSignalSpy finished(&window, &MainWindow::saveFinished);
        QTest::keyClick(window.boardView(), Qt::Key_S, Qt::ControlModifier);
        QVERIFY(finished.wait(30000));
        QVERIFY(!window.boardView()->isTextEditing()); // committed first
        QCOMPARE(window.document()->notes().size(), 1);
        QCOMPARE(window.document()->notes().begin()->text, u"fhs"_s);
        QVERIFY(!window.controller()->isDirty());

        const persistence::LoadResult loaded = persistence::loadBoard(path, m_services->store(), m_services->limits());
        QVERIFY(loaded.content);
        QCOMPARE(loaded.content->snapshot.notes.size(), 1);
    }

    void boardTitleIsUndoable()
    {
        MainWindow window(*m_services);
        DocumentChange change;
        change.title = u"Robots"_s;
        QVERIFY(window.controller()->run(change, u"Board Title"_s));
        QCOMPARE(window.document()->title(), u"Robots"_s);
        QVERIFY(window.controller()->isDirty());
        window.action(u"Undo"_s)->trigger();
        QCOMPARE(window.document()->title(), QString());
    }

    void quitClosesWindowCleanly()
    {
        auto *window = new MainWindow(*m_services);
        window->setAttribute(Qt::WA_DeleteOnClose);
        QPointer<MainWindow> guard(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->action(u"Quit"_s)->trigger();
        QTRY_VERIFY(guard.isNull());
    }
};

GLACIER_TEST_WIDGETS_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
