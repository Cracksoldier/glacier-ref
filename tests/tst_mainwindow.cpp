#include "GlacierTestMain.h"
#include "TestBoardFiles.h"
#include "TestImages.h"

#include "AppServices.h"
#include "FramelessController.h"
#include "HelpDialog.h"
#include "MainWindow.h"
#include "PreferencesDialog.h"
#include "SettingsService.h"
#include "Theme.h"
#include "glacier/canvas/ImageCache.h"
#include "RecoveryDialog.h"
#include "glacier/canvas/BoardScene.h"
#include "glacier/canvas/BoardView.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/editing/ClipboardFragment.h"
#include "glacier/editing/CommandController.h"
#include "glacier/editing/Operations.h"
#include "glacier/io/MimeRouter.h"
#include "glacier/persistence/BoardSerializer.h"
#include "glacier/persistence/DocumentLock.h"
#include "glacier/persistence/RecoveryStore.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QStyle>
#include <QMenuBar>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QTableWidget>
#include <QToolButton>
#include <QMessageBox>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <QProgressDialog>
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
        m_services = AppServices::create({m_root.path(), {}, {}});
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
        QTest::newRow("menu bar") << u"ShowMenuBar"_s << QKeySequence(Qt::Key_Tab);
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
        // Keep the error box open longer than the progress dialog's delay: the
        // progress dialog must already be deleted, so it can't pop up over the
        // box (M3 demo step 23; the app's main loop doesn't run a pending
        // deleteLater while the box runs its own loop).
        bool progressShown = false;
        auto *timer = new QTimer;
        timer->setInterval(20);
        int ticks = 0;
        connect(timer, &QTimer::timeout, timer, [timer, &ticks, &progressShown] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget *w : widgets) {
                auto *box = qobject_cast<QMessageBox *>(w);
                if (!box || box->objectName() != u"openFailedBox"_s || !box->isVisible())
                    continue;
                if (++ticks < 30) // about 600 ms
                    return;
                for (QWidget *other : widgets)
                    if (qobject_cast<QProgressDialog *>(other))
                        progressShown = true; // must already be deleted, not just hidden
                timer->stop();
                timer->deleteLater();
                box->button(QMessageBox::Ok)->click();
                return;
            }
        });
        timer->start();
        QPointer<MainWindow> opened = MainWindow::openBoard(*m_services, path, &busy);
        QVERIFY(opened && opened.data() != &busy);
        QTRY_VERIFY_WITH_TIMEOUT(opened.isNull(), 30000); // closed after the error
        QVERIFY(!progressShown);
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

    // --- Recovery (M3.09-M3.10) -------------------------------------------

    void recoverySnapshotsFollowTheBoard()
    {
        QTemporaryDir dir;
        auto window = std::make_unique<MainWindow>(*m_services);
        const QString snapshots = m_services->recovery()->sessionDir() + u'/' + window->recoveryKey().toString();
        QSignalSpy written(window.get(), &MainWindow::recoverySnapshotWritten);
        window->writeRecoverySnapshot(); // clean: nothing to protect
        QVERIFY(!QDir(snapshots).exists());

        QVERIFY(importThree(*window));
        window->writeRecoverySnapshot();
        QVERIFY(written.wait(30000));
        QVERIFY(written.first().first().toBool());
        QCOMPARE(QDir(snapshots).entryList({u"snapshot-*.json"_s}, QDir::Files).size(), 1);
        window->writeRecoverySnapshot(); // unchanged since: no new snapshot
        QVERIFY(!written.wait(200));

        // Saving makes the board clean: its recovery data goes.
        QVERIFY(saveAndWait(*window, dir.filePath(u"saved.glacierref"_s)));
        QVERIFY(!QDir(snapshots).exists());

        // Closing without saving (Discard) removes it too.
        QVERIFY(window->controller()->run(ops::createNote(*window->document(), {0, 0}, u"x"_s), u"Note"_s));
        window->writeRecoverySnapshot();
        QVERIFY(written.wait(30000));
        QVERIFY(QDir(snapshots).exists());
        answer(u"unsavedChangesBox"_s, QMessageBox::Discard);
        QVERIFY(window->close());
        QVERIFY(!QDir(snapshots).exists());
    }

    // A06 in the app: a recovered board opens dirty, linked to its file.
    void recoveredBoardOpensDirty()
    {
        QTemporaryDir dir;
        const QString original = dir.filePath(u"original.glacierref"_s);
        {
            MainWindow saved(*m_services);
            QVERIFY(importThree(saved));
            QVERIFY(saveAndWait(saved, original));
        }
        // A session that ended without closing its windows.
        persistence::RecoveryStore::SnapshotRequest request;
        request.key = Id::generate();
        request.content = test::richBoard(*m_services->store());
        request.content.metadata.title = u"unsaved work"_s;
        request.boardPath = original;
        request.baseFingerprint = persistence::FileFingerprint::of(original);
        {
            auto crashed = persistence::RecoveryStore::create({m_services->recoveryRoot()});
            QVERIFY(crashed);
            QVERIFY(crashed->writeSnapshot(request, *m_services->store()));
        } // its lock is released, as if the process had died
        const auto entries = persistence::RecoveryStore::findRecoverable(m_services->recoveryRoot());
        QCOMPARE(entries.size(), 1);

        MainWindow window(*m_services);
        QSignalSpy loaded(&window, &MainWindow::boardLoaded);
        QSignalSpy written(&window, &MainWindow::recoverySnapshotWritten);
        QCOMPARE(MainWindow::recoverBoard(*m_services, entries.first(), &window), &window);
        QVERIFY(loaded.wait(30000));
        QVERIFY(loaded.first().first().toBool());
        QVERIFY(window.controller()->isDirty());
        QVERIFY(window.isWindowModified());
        QCOMPARE(window.filePath(), original);
        QVERIFY(!window.isReadOnly());
        QCOMPARE(window.document()->title(), u"unsaved work"_s);
        QCOMPARE(test::sorted(persistence::BoardContent{window.document()->metadata(), window.document()->snapshotAll(),
                                           window.document()->view(), {}, 1.0})
                     .snapshot,
                 test::sorted(request.content).snapshot);
        window.controller()->undoStack()->setIndex(0);
        QVERIFY(window.controller()->isDirty()); // dirty until saved

        // Once the new window has its own snapshot, the old data is gone.
        QTRY_VERIFY_WITH_TIMEOUT(!written.isEmpty(), 30000);
        QVERIFY(!QFile::exists(entries.first().snapshotPath));
        QVERIFY(persistence::RecoveryStore::findRecoverable(m_services->recoveryRoot()).isEmpty());

        // Saving goes to the original (whose content was unchanged meanwhile).
        QSignalSpy saved(&window, &MainWindow::saveFinished);
        window.save();
        QVERIFY(saved.wait(30000));
        QVERIFY(saved.first().first().toBool());
        QVERIFY(!window.controller()->isDirty());
    }

    void recoveryDialogOffersEachBoard()
    {
        persistence::RecoveryStore::Entry named;
        named.title = u"Board A"_s;
        named.boardPath = u"/tmp/a.glacierref"_s;
        named.time = QDateTime::currentDateTimeUtc();
        persistence::RecoveryStore::Entry unnamed;
        unnamed.time = named.time;
        QStringList calls;
        RecoveryDialog dialog(
            {named, unnamed}, [&calls](const auto &e) { calls.append(u"recover "_s + e.title); },
            [&calls](const auto &e) { calls.append(u"original "_s + e.title); },
            [&calls](const auto &e) { calls.append(u"discard "_s + e.title); });
        QCOMPARE(dialog.remaining(), 2);
        dialog.recoverAll();
        QCOMPARE(calls, (QStringList{u"recover Board A"_s, u"recover "_s}));
        QCOMPARE(dialog.remaining(), 0);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }

    // --- Minimal UI (M4.01) ------------------------------------------------

    void menuBarIsHiddenByDefaultAndTabTogglesIt()
    {
        QSettings().remove(u"ui/menuBarVisible"_s);
        MainWindow window(*m_services);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(!window.menuBar()->isVisible());
        QVERIFY(window.menuButton()->isVisible());

        MainWindow other(*m_services);
        window.boardView()->setFocus();
        QTest::keyClick(window.boardView(), Qt::Key_Tab);
        QVERIFY(window.menuBar()->isVisible());
        QVERIFY(!window.menuButton()->isVisible());
        QVERIFY(m_services->settings()->menuBarVisible()); // remembered for new windows
        QVERIFY(!other.menuButton()->isVisibleTo(&other)); // every window follows

        // While a note is edited, Tab goes to the text.
        window.boardView()->beginNewNote({0, 0});
        QTest::keyClick(window.boardView(), Qt::Key_Tab);
        QVERIFY(window.menuBar()->isVisible());
        QTest::keyClick(window.boardView(), Qt::Key_Escape);

        QTest::keyClick(window.boardView(), Qt::Key_Tab);
        QVERIFY(!window.menuBar()->isVisible());
        QVERIFY(!m_services->settings()->menuBarVisible());
    }

    void menuButtonAndContextMenuReachEveryCommand()
    {
        m_services->settings()->setMenuBarVisible(false);
        MainWindow window(*m_services);
        QMenu *menu = window.menuButton()->menu();
        QVERIFY(menu);
        emit menu->aboutToShow();
        QCOMPARE(menu->actions(), window.menuBar()->actions()); // File, Edit, Board, View, Help
        std::unique_ptr<QMenu> context(window.createContextMenu(nullptr));
        QCOMPARE(context->actions().first(), window.action(u"ShowMenuBar"_s));
    }

    void firstRunHintIsShownOnce()
    {
        QSettings().remove(u"ui/firstRunHintShown"_s);
        {
            MainWindow window(*m_services);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QVERIFY(window.boardView()->toastText().contains(u"Tab"_s));
        }
        MainWindow again(*m_services);
        again.show();
        QVERIFY(QTest::qWaitForWindowExposed(&again));
        QVERIFY(!again.boardView()->toastText().contains(u"Tab"_s));
    }

    void helpShowsShortcutsAndOpensTheWebsiteOnlyOnClick()
    {
        QList<QUrl> opened;
        MainWindow::setUrlOpenerForTesting([&opened](const QUrl &url) { opened.append(url); });
        MainWindow window(*m_services);
        const QList<HelpDialog::ShortcutRow> rows = window.shortcutRows();
        const auto save = std::find_if(rows.cbegin(), rows.cend(), [](const auto &r) { return r.command == u"Save"_s; });
        QVERIFY(save != rows.cend());
        QCOMPARE(save->menu, u"File"_s);
        QCOMPARE(save->shortcuts, window.action(u"Save"_s)->shortcuts());

        int tableRows = -1;
        auto *timer = new QTimer;
        timer->setInterval(20);
        connect(timer, &QTimer::timeout, timer, [timer, &tableRows] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget *w : widgets) {
                if (w->objectName() != u"helpDialog"_s || !w->isVisible())
                    continue;
                timer->stop();
                timer->deleteLater();
                tableRows = w->findChild<QTableWidget *>(u"shortcutTable"_s)->rowCount();
                w->findChild<QPushButton *>(u"websiteButton"_s)->click();
                static_cast<QDialog *>(w)->reject();
                return;
            }
        });
        timer->start();
        QVERIFY(opened.isEmpty()); // nothing until asked
        window.action(u"Help"_s)->trigger();
        QCOMPARE(tableRows, int(rows.size()));
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.first(), QUrl(u"https://github.com/Cracksoldier/glacier-ref"_s));
        window.action(u"ProjectWebsite"_s)->trigger();
        QCOMPARE(opened.size(), 2);
        MainWindow::setUrlOpenerForTesting({});
    }

    void copyDiagnosticsHasNoPersonalPaths()
    {
        MainWindow window(*m_services);
        window.action(u"CopyDiagnostics"_s)->trigger();
        const QString report = QGuiApplication::clipboard()->text();
        QVERIFY(report.contains(u"Data folders:"_s));
        QVERIFY(report.contains(u"image cache:"_s));
        QVERIFY(!report.contains(QDir::homePath()));
    }

    // --- Preferences and theme (M4.02) --------------------------------------

    void shortcutChangesApplyToEveryWindow()
    {
        MainWindow first(*m_services);
        MainWindow second(*m_services);
        const QKeySequence custom(Qt::CTRL | Qt::ALT | Qt::Key_S);
        m_services->settings()->setShortcutOverrides({{u"Save"_s, {custom}}, {u"Grayscale"_s, {}}});
        for (MainWindow *w : {&first, &second}) {
            QCOMPARE(w->action(u"Save"_s)->shortcuts(), QList<QKeySequence>{custom});
            QVERIFY(w->action(u"Grayscale"_s)->shortcuts().isEmpty());
            QVERIFY(w->boardView()->textEditingPassThrough().contains(custom)); // still works in notes
            QVERIFY(!w->boardView()->textEditingPassThrough().contains(QKeySequence(Qt::CTRL | Qt::Key_S)));
        }
        MainWindow later(*m_services); // new windows too
        QCOMPARE(later.action(u"Save"_s)->shortcuts(), QList<QKeySequence>{custom});
        m_services->settings()->setShortcutOverrides({});
        QCOMPARE(first.action(u"Save"_s)->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_S));
        QCOMPARE(first.action(u"Grayscale"_s)->shortcut(), QKeySequence(Qt::Key_G));
    }

    void preferencesRefuseConflictsAndStoreOnlyChanges()
    {
        MainWindow window(*m_services);
        const QList<PreferencesDialog::ShortcutEntry> entries = window.shortcutEntries();
        QCOMPARE(entries.size(), window.shortcutRows().size());
        PreferencesDialog dialog(m_services->settings(), entries, {u"s"_s, u"t"_s, u"r"_s, u"l"_s});
        QVERIFY(dialog.okButton()->isEnabled());

        dialog.setShortcutForTesting(u"Crop"_s, {QKeySequence(Qt::CTRL | Qt::Key_S)});
        QVERIFY(!dialog.okButton()->isEnabled());
        QCOMPARE(dialog.conflicts().value(QKeySequence(Qt::CTRL | Qt::Key_S)), (QStringList{u"Crop"_s, u"Save"_s}));
        QVERIFY(dialog.findChild<QLabel *>(u"conflictLabel"_s)->text().contains(u"Crop"_s));
        dialog.accept(); // refused while conflicting
        QVERIFY(m_services->settings()->shortcutOverrides().isEmpty());

        dialog.setShortcutForTesting(u"Crop"_s, {QKeySequence(Qt::Key_C)}); // back to the default
        dialog.setShortcutForTesting(u"Grayscale"_s, {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Y)});
        QVERIFY(dialog.okButton()->isEnabled());
        dialog.accept();
        const SettingsService::ShortcutMap stored = m_services->settings()->shortcutOverrides();
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored.value(u"Grayscale"_s), QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Y)});
        QCOMPARE(window.action(u"Grayscale"_s)->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Y));
        m_services->settings()->setShortcutOverrides({});
    }

    void cacheBudgetAppliesLive()
    {
        m_services->settings()->setCacheBudgetMiB(1024);
        QCOMPARE(m_services->cache()->stats().budgetBytes, qint64(1024) * 1024 * 1024);
        m_services->settings()->setCacheBudgetMiB(SettingsService::DefaultCacheBudgetMiB);
        QCOMPARE(m_services->cache()->stats().budgetBytes, qint64(512) * 1024 * 1024);
    }

    void themeSwitchesLive()
    {
        theme::rememberSystemStyle();
        theme::apply(SettingsService::Theme::Glacier);
        QCOMPARE(QApplication::style()->name().toLower(), u"fusion"_s);
        QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(0x20, 0x23, 0x27));
        theme::apply(SettingsService::Theme::System);
        QVERIFY(QApplication::palette().color(QPalette::Window) != QColor(0x20, 0x23, 0x27));
    }

    void newBoardsUseThePreferredBackground()
    {
        m_services->settings()->setNewBoardBackground(0xFF112233u);
        MainWindow window(*m_services);
        QCOMPARE(window.document()->view().backgroundColor, 0xFF112233u);
        QVERIFY(!window.controller()->isDirty());
        QCOMPARE(window.controller()->undoStack()->count(), 0);
        QVERIFY(window.isReusable());
        m_services->settings()->setNewBoardBackground(SettingsService::DefaultBoardBackground);
    }

    // --- Window integration (M4.03, M4.04) ---------------------------------

    void geometryIsRememberedForNewWindows()
    {
        // Smaller than the screen at any scale factor (the .scaled run uses 1.5).
        const QSize wanted = (QGuiApplication::primaryScreen()->availableSize() * 0.75).boundedTo(QSize(640, 480));
        {
            auto *window = new MainWindow(*m_services);
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->resize(wanted);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window));
            QVERIFY(window->close());
        }
        MainWindow next(*m_services);
        QCOMPARE(next.size(), wanted);
        m_services->settings()->setWindowGeometry({});
    }

    void windowOpacityWhereSupported()
    {
        MainWindow window(*m_services);
        QAction *eighty = window.action(u"Opacity80"_s);
        QVERIFY(eighty);
        // Offscreen supports it; on native Wayland the menu is hidden (tst_platformwindow).
        QVERIFY(eighty->isEnabled());
        eighty->trigger();
        QCOMPARE(window.windowOpacity(), 0.8);
        QVERIFY(eighty->isChecked());
        window.action(u"Opacity100"_s)->trigger();
        QCOMPARE(window.windowOpacity(), 1.0);
    }

    void alwaysOnTopReportsHonestly()
    {
        MainWindow window(*m_services);
        window.show();
        window.action(u"AlwaysOnTop"_s)->trigger();
        QVERIFY(window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QCOMPARE(window.boardView()->toastText(), u"Always on top"_s); // offscreen honours the hint
        window.action(u"AlwaysOnTop"_s)->trigger();
        QVERIFY(!window.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    }

    void framelessModeAlwaysHasAWayBack()
    {
        MainWindow window(*m_services);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.action(u"FramelessWindow"_s)->trigger();
        QVERIFY(window.isFrameless());
        QVERIFY(window.framelessController()->grip()->isVisible());
        QVERIFY(m_services->settings()->frameless());
        std::unique_ptr<QMenu> context(window.createContextMenu(nullptr));
        QVERIFY(context->actions().contains(window.action(u"ShowWindowFrame"_s)));

        // Escape with no canvas mode active restores the frame.
        window.activateWindow();
        window.boardView()->setFocus();
        QTest::keyClick(window.boardView(), Qt::Key_Escape);
        QVERIFY(!window.isFrameless());
        QVERIFY(!window.framelessController()->grip()->isVisible());

        // A platform that refuses system moves gets its frame back (offscreen refuses).
        window.setFrameless(true);
        QVERIFY(!window.framelessController()->beginMove());
        QVERIFY(!window.isFrameless());
        QVERIFY(window.boardView()->toastText().contains(u"frame is back"_s));
        QVERIFY(!m_services->settings()->frameless());
    }

    void framelessEdgesAreDetected()
    {
        const QSize size(800, 600);
        QCOMPARE(FramelessController::edgesAt({2, 300}, size), Qt::Edges(Qt::LeftEdge));
        QCOMPARE(FramelessController::edgesAt({798, 599}, size), Qt::Edges(Qt::RightEdge | Qt::BottomEdge));
        QCOMPARE(FramelessController::edgesAt({400, 1}, size), Qt::Edges(Qt::TopEdge));
        QCOMPARE(FramelessController::edgesAt({1, 1}, size), Qt::Edges(Qt::TopEdge | Qt::LeftEdge));
        QCOMPARE(FramelessController::edgesAt({400, 300}, size), Qt::Edges());
        QCOMPARE(FramelessController::edgesAt({400, 6}, size), Qt::Edges()); // the grip below the top edge moves
    }

    // --- DPI, input and paths (M4.06) ---------------------------------------

    void unicodePathsWithSpaces()
    {
        QTemporaryDir root;
        const QString dir = root.filePath(u"Réf ä/Board 1 ✓"_s);
        QVERIFY(QDir().mkpath(dir + u"/Bilder ✓"_s));
        const QString image = dir + u"/Bilder ✓/ref ö 1.png"_s;
        {
            QImage pixels(30, 20, QImage::Format_RGB32);
            pixels.fill(Qt::darkCyan);
            QFile f(image);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(test::encode(pixels, "png"));
        }
        const QString board = dir + u"/Mood ✓ 1.glacierref"_s;
        {
            MainWindow window(*m_services);
            io::ImportPlan plan = io::planForPaths({image});
            plan.linked = true;
            QSignalSpy applied(&window, &MainWindow::importApplied);
            window.importPlan(plan, {});
            QVERIFY(applied.wait(30000));
            QVERIFY(window.document()->assets().cbegin()->linked);
            QVERIFY(saveAndWait(window, board));
            QSignalSpy written(&window, &MainWindow::recoverySnapshotWritten);
            QVERIFY(window.controller()->run(ops::createNote(*window.document(), {0, 0}, u"ünïcödé ✓"_s), u"Note"_s));
            window.writeRecoverySnapshot();
            QVERIFY(written.wait(30000));
            QVERIFY(written.first().first().toBool());
            window.controller()->undoStack()->undo(); // clean again: no close prompt
        }
        MainWindow reopened(*m_services);
        QSignalSpy loaded(&reopened, &MainWindow::boardLoaded);
        reopened.loadBoard(board);
        QVERIFY(loaded.wait(30000));
        QVERIFY(loaded.first().first().toBool());
        QCOMPARE(reopened.filePath(), QFileInfo(board).absoluteFilePath());
        QCOMPARE(reopened.linkState(reopened.document()->assets().cbegin()->id), persistence::LinkState::Available);
        QVERIFY(reopened.windowTitle().startsWith(u"Mood ✓ 1.glacierref"_s));
    }

    void canvasShortcutsDontFireInDialogs()
    {
        MainWindow window(*m_services);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        bool typed = false;
        auto *timer = new QTimer;
        timer->setInterval(20);
        connect(timer, &QTimer::timeout, timer, [timer, &typed] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget *w : widgets) {
                if (w->objectName() != u"helpDialog"_s || !w->isVisible())
                    continue;
                timer->stop();
                timer->deleteLater();
                auto *filter = w->findChild<QLineEdit *>();
                filter->setFocus();
                QTest::keyClicks(filter, u"nchvg"_s); // New Note, Crop, Flip, Grayscale on the canvas
                typed = filter->text() == u"nchvg"_s;
                static_cast<QDialog *>(w)->reject();
                return;
            }
        });
        timer->start();
        window.action(u"Help"_s)->trigger();
        QVERIFY(typed);
        QCOMPARE(window.document()->notes().size(), 0);
        QVERIFY(!window.document()->view().grayscale);
        QVERIFY(!window.boardView()->isTextEditing());
        QVERIFY(window.boardView()->hasFocus() || !window.isActiveWindow()); // focus goes back to the canvas
    }

    // M4.07: one selectionChanged per selection change, not one per item
    // (listeners look at the whole selection: n items cost n^2 before).
    void selectingManyItemsSignalsOnce()
    {
        MainWindow window(*m_services);
        QVERIFY(importThree(window));
        QSignalSpy changed(window.boardScene(), &QGraphicsScene::selectionChanged);
        window.boardScene()->selectIds(window.document()->topLevelIdsByZ());
        QCOMPARE(changed.size(), 1);
        QCOMPARE(window.boardScene()->selectedIds().size(), 3);
        QVERIFY(window.action(u"Delete"_s)->isEnabled()); // action states follow at once
        window.boardScene()->clearSelection();
        changed.clear();
        window.action(u"SelectAll"_s)->trigger();
        QCOMPARE(changed.size(), 1);
        QCOMPARE(window.boardScene()->selectedIds().size(), 3);
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
