#include "MainWindow.h"

#include "AppServices.h"
#include "FramelessController.h"
#include "DiagnosticLog.h"
#include "NoteStyleDialog.h"
#include "PreferencesDialog.h"
#include "glacier/canvas/BoardScene.h"
#include "glacier/canvas/BoardView.h"
#include "glacier/canvas/ImageCache.h"
#include "glacier/core/AppInfo.h"
#include "glacier/core/BatchLayout.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/core/Diagnostics.h"
#include "glacier/core/LinkPaths.h"
#include "glacier/core/SessionAssetStore.h"
#include "glacier/editing/ClipboardFragment.h"
#include "glacier/editing/CommandController.h"
#include "glacier/editing/CopyImage.h"
#include "glacier/editing/Geometry.h"
#include "glacier/editing/Operations.h"
#include "glacier/io/ImageDecoder.h"
#include "glacier/io/ImageFormats.h"
#include "glacier/io/MimeRouter.h"
#include "glacier/persistence/BoardSerializer.h"
#include "glacier/persistence/DocumentLock.h"
#include "glacier/platform/PlatformInfo.h"
#include "glacier/platform/PlatformWindowService.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QImageReader>
#include <QShowEvent>
#include <QToolButton>
#include <QColorDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPromise>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

using namespace Qt::StringLiterals;
using namespace glacier::editing;

namespace glacier {

namespace {

const QString ArrangeSpacingKey = u"layout/arrangeSpacing"_s;
constexpr int DefaultArrangeSpacing = 20;
const QString LastBoardDirectoryKey = u"files/lastDirectory"_s;
const QString BoardSuffix = u"glacierref"_s;
constexpr int PreviewSide = 512;
constexpr int ProgressDelayMs = 300;

QString boardFileFilter()
{
    return QCoreApplication::translate("glacier::MainWindow", "Glacier Ref boards (*.glacierref)");
}

// Ends a progress dialog when its job finishes. It must not be deleted here:
// a window-modal QProgressDialog runs a nested event loop inside setValue(),
// and the job's "finished" can arrive in that loop, so the dialog is still
// executing (deleting it crashed the benchmark). Nor may it stay pending: a
// message box shown next runs its own loop, where deleteLater doesn't run,
// and the dialog would appear over the box after its delay (M3 demo, step 23).
// So: stop its updates, cancel it (hidden, and it never shows itself again),
// and delete it later.
void retireProgressDialog(QProgressDialog *dialog, const QObject *progressSource = nullptr)
{
    if (!dialog)
        return;
    if (progressSource)
        QObject::disconnect(progressSource, nullptr, dialog, nullptr); // no more progress updates
    dialog->blockSignals(true);                              // no "canceled" (import would treat it as Cancel)
    dialog->cancel();
    dialog->blockSignals(false);
    dialog->deleteLater();
}

bool isBoardFile(const QString &path)
{
    return QFileInfo(path).suffix().compare(BoardSuffix, Qt::CaseInsensitive) == 0;
}

bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return QFileInfo(a).absoluteFilePath().compare(QFileInfo(b).absoluteFilePath(), Qt::CaseInsensitive) == 0;
#else
    return QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
#endif
}

QKeySequence key(QKeyCombination combination)
{
    return QKeySequence(combination);
}

} // namespace

MainWindow::MainWindow(AppServices &services, QWidget *parent)
    : QMainWindow(parent)
    , m_services(services)
    , m_document(new BoardDocument(services.limits(), this))
    , m_controller(new CommandController(m_document, this))
    , m_scene(new canvas::BoardScene(m_document, services.cache(), this))
    , m_view(new canvas::BoardView(this))
    , m_import(new io::ImportService(services.store(), services.pool(), services.limits(), this))
{
    m_view->setBoardScene(m_scene);
    m_view->setController(m_controller);
    setCentralWidget(m_view);
    // Size and state of the last closed window (spec §7; positions are best-effort).
    if (const QByteArray geometry = services.settings()->windowGeometry(); geometry.isEmpty() || !restoreGeometry(geometry))
        resize(1280, 800);
    createActions();
    updateTitle();

    // The ≡ button: the whole menu while the menu bar is hidden (spec §3).
    m_menuButton = new QToolButton(m_view);
    m_menuButton->setObjectName(u"menuButton"_s);
    m_menuButton->setText(u"≡"_s);
    m_menuButton->setToolTip(tr("Menu (Tab shows the menu bar)"));
    m_menuButton->setAutoRaise(true);
    m_menuButton->setFocusPolicy(Qt::NoFocus);
    m_menuButton->setPopupMode(QToolButton::InstantPopup);
    m_menuButton->setStyleSheet(u"QToolButton { background: rgba(20, 22, 25, 150); color: #e6e6e6; border-radius: 4px;"
                                " font-size: 16px; padding: 1px 7px; } QToolButton:hover { background: rgba(20, 22, 25, 220); }"
                                " QToolButton::menu-indicator { image: none; }"_s);
    auto *buttonMenu = new QMenu(m_menuButton);
    connect(buttonMenu, &QMenu::aboutToShow, this, [this, buttonMenu] {
        finishModes();
        buttonMenu->clear();
        for (QAction *top : menuBar()->actions())
            buttonMenu->addAction(top); // the menu bar's own submenus and actions
    });
    m_menuButton->setMenu(buttonMenu);
    m_menuButton->move(6, FramelessController::GripHeight + 2);

    // Frameless mode (M4.04) with its ways back: context menu, ≡ menu, Escape.
    m_frameless = new FramelessController(this, m_view, m_view->viewport(), this);
    connect(m_frameless, &FramelessController::unsupported, this, [this] {
        setFrameless(false);
        m_view->showToast(tr("This system can't move or resize frameless windows, so the window frame is back."), 5000);
    });
    connect(m_view, &canvas::BoardView::escapeUnhandled, this, [this] {
        if (isFrameless())
            setFrameless(false);
    });
    if (services.settings()->frameless())
        setFrameless(true);
    applyMenuBarVisibility(m_services.settings()->menuBarVisible());
    connect(m_services.settings(), &SettingsService::menuBarVisibleChanged, this, &MainWindow::applyMenuBarVisibility);

    connect(m_view, &canvas::BoardView::dropReceived, this, &MainWindow::handleDrop);
    updateTextEditingPassThrough();
    connect(m_services.settings(), &SettingsService::shortcutsChanged, this, &MainWindow::applyShortcuts);
    // New boards get the preferred background (not an edit: no undo step, not dirty).
    if (const quint32 background = m_services.settings()->newBoardBackground();
        background != m_document->view().backgroundColor) {
        DocumentChange change;
        change.view = ViewSettings{background, m_document->view().grayscale};
        m_document->apply(change);
    }
    connect(m_view, &canvas::BoardView::contextMenuRequested, this, [this](QPoint globalPos) {
        QMenu *menu = createContextMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(globalPos);
    });
    connect(m_import, &io::ImportService::progress, this, &MainWindow::onImportProgress);
    connect(m_import, &io::ImportService::batchFinished, this, &MainWindow::onBatchFinished);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &MainWindow::updateActionStates);
    connect(m_document, &BoardDocument::changed, this, &MainWindow::updateActionStates);
    connect(m_view, &canvas::BoardView::modeChanged, this, &MainWindow::updateActionStates);
    connect(m_controller, &CommandController::dirtyChanged, this, &MainWindow::updateTitle);

    // Recovery snapshots of dirty boards (spec §5): at most one per interval.
    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setSingleShot(true);
    m_recoveryTimer->setInterval(services.settings()->recoveryIntervalSeconds() * 1000);
    connect(services.settings(), &SettingsService::recoveryChanged, this, [this] {
        m_recoveryTimer->setInterval(m_services.settings()->recoveryIntervalSeconds() * 1000);
    });
    connect(m_recoveryTimer, &QTimer::timeout, this, &MainWindow::writeRecoverySnapshot);
    connect(m_document, &BoardDocument::revisionChanged, this, &MainWindow::scheduleRecoverySnapshot);
    connect(m_controller, &CommandController::dirtyChanged, this, [this](bool dirty) {
        if (dirty)
            scheduleRecoverySnapshot();
        else
            discardRecovery();
    });
    updateActionStates();
}

MainWindow::~MainWindow()
{
    m_import->cancelAll();
    // Child objects are destroyed after this destructor body; their signals
    // (e.g. the scene clearing its selection) must not reach this window.
    for (QObject *source : std::initializer_list<QObject *>{m_document, m_controller, m_scene, m_view, m_import})
        disconnect(source, nullptr, this, nullptr);
}

QList<Id> MainWindow::selection() const
{
    return m_scene->selectedIds();
}

QString MainWindow::displayFileName() const
{
    return m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).fileName();
}

void MainWindow::updateTitle()
{
    QString name = displayFileName() + u"[*]"_s;
    if (m_readOnly)
        name += u' ' + tr("(read-only)");
    setWindowTitle(name + u" — "_s + app_info::displayName());
    setWindowFilePath(m_filePath);
    setWindowModified(m_controller->isDirty());
}

bool MainWindow::run(const OpResult &result, const QString &text)
{
    finishModes();
    return m_view->runEdit(result, text);
}

void MainWindow::finishModes()
{
    if (m_view->isTextEditing())
        m_view->commitTextEdit();
    if (m_view->isCropping())
        m_view->commitCrop();
}

// --- Actions -----------------------------------------------------------------------

QAction *MainWindow::addAction(QMenu *menu, const QString &id, const QString &text, const QList<QKeySequence> &shortcuts)
{
    auto *action = new QAction(text, this);
    action->setObjectName(u"action"_s + id);
    m_defaultShortcuts.insert(id, shortcuts);
    const QList<QKeySequence> effective = m_services.settings()->shortcuts(id, shortcuts);
    if (!effective.isEmpty())
        action->setShortcuts(effective);
    action->setShortcutContext(Qt::WindowShortcut);
    if (menu)
        menu->addAction(action);
    QMainWindow::addAction(action); // shortcuts work while the menu bar is hidden
    m_actions.insert(id, action);
    return action;
}

void MainWindow::createActions()
{
    auto on = [this](const QString &id, auto handler) { connect(m_actions.value(id), &QAction::triggered, this, handler); };
    auto edit = [this](const QString &id, auto builder, const QString &text) {
        connect(m_actions.value(id), &QAction::triggered, this, [this, builder, text] {
            finishModes();
            run(builder(), text);
        });
    };

    // File
    QMenu *file = menuBar()->addMenu(tr("&File"));
    addAction(file, u"NewWindow"_s, tr("&New Window"), {key(Qt::CTRL | Qt::Key_N)});
    on(u"NewWindow"_s, &MainWindow::newWindow);
    addAction(file, u"Open"_s, tr("&Open…"), {key(Qt::CTRL | Qt::Key_O)});
    on(u"Open"_s, &MainWindow::openFromDialog);
    addAction(file, u"Save"_s, tr("&Save"), {key(Qt::CTRL | Qt::Key_S)});
    on(u"Save"_s, [this] { save(); });
    addAction(file, u"SaveAs"_s, tr("Save &As…"), {key(Qt::CTRL | Qt::SHIFT | Qt::Key_S)});
    on(u"SaveAs"_s, [this] { saveAsFromDialog({}); });
    addAction(file, u"BoardTitle"_s, tr("Board &Title…"));
    on(u"BoardTitle"_s, &MainWindow::editBoardTitle);
    file->addSeparator();
    addAction(file, u"ImportImages"_s, tr("&Import Images…"), {key(Qt::CTRL | Qt::Key_I)});
    on(u"ImportImages"_s, &MainWindow::importImagesFromDialog);
    addAction(file, u"ImportAsLink"_s, tr("Import as &Link…"), {key(Qt::CTRL | Qt::SHIFT | Qt::Key_I)});
    on(u"ImportAsLink"_s, &MainWindow::importLinkedFromDialog);
    addAction(file, u"ImportFolder"_s, tr("Import &Folder…"));
    on(u"ImportFolder"_s, &MainWindow::importFolderFromDialog);
    file->addSeparator();
    addAction(file, u"CloseWindow"_s, tr("&Close Window"), {key(Qt::CTRL | Qt::Key_W)});
    on(u"CloseWindow"_s, &QWidget::close);
    // Explicit default (spec §3): QKeySequence::Quit is empty on some platforms.
    QAction *quit = addAction(file, u"Quit"_s, tr("&Quit"), {key(Qt::CTRL | Qt::Key_Q)});
    quit->setMenuRole(QAction::QuitRole);
    connect(quit, &QAction::triggered, qApp, &QApplication::closeAllWindows);

    // Edit
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QUndoStack *stack = m_controller->undoStack();
    QAction *undo = addAction(editMenu, u"Undo"_s, tr("&Undo"), {key(Qt::CTRL | Qt::Key_Z)});
    QList<QKeySequence> redoKeys{key(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)};
#ifndef Q_OS_MACOS
    redoKeys.append(key(Qt::CTRL | Qt::Key_Y));
#endif
    QAction *redo = addAction(editMenu, u"Redo"_s, tr("&Redo"), redoKeys);
    // Leave text editing and crop mode first, so undo applies to the board.
    connect(undo, &QAction::triggered, this, [this, stack] {
        finishModes();
        stack->undo();
    });
    connect(redo, &QAction::triggered, this, [this, stack] {
        finishModes();
        stack->redo();
    });
    auto syncUndo = [undo, redo, stack] {
        undo->setEnabled(stack->canUndo());
        redo->setEnabled(stack->canRedo());
        undo->setText(stack->canUndo() ? tr("&Undo %1").arg(stack->undoText()) : tr("&Undo"));
        redo->setText(stack->canRedo() ? tr("&Redo %1").arg(stack->redoText()) : tr("&Redo"));
    };
    connect(stack, &QUndoStack::indexChanged, this, syncUndo);
    syncUndo();
    m_actions.insert(u"Redo"_s, redo);
    editMenu->addSeparator();
    addAction(editMenu, u"Cut"_s, tr("Cu&t"), {key(Qt::CTRL | Qt::Key_X)});
    on(u"Cut"_s, &MainWindow::cut);
    addAction(editMenu, u"Copy"_s, tr("&Copy"), {key(Qt::CTRL | Qt::Key_C)});
    on(u"Copy"_s, &MainWindow::copy);
    addAction(editMenu, u"CopyImage"_s, tr("Copy &Image"), {key(Qt::CTRL | Qt::SHIFT | Qt::Key_C)});
    on(u"CopyImage"_s, &MainWindow::copyImage);
    addAction(editMenu, u"Paste"_s, tr("&Paste"), {key(Qt::CTRL | Qt::Key_V)});
    on(u"Paste"_s, &MainWindow::paste);
    addAction(editMenu, u"Duplicate"_s, tr("D&uplicate"), {key(Qt::CTRL | Qt::Key_D)});
    edit(u"Duplicate"_s, [this] { return ops::duplicate(*m_document, selection()); }, tr("Duplicate"));
    QList<QKeySequence> deleteKeys{key(Qt::Key_Delete)};
#ifdef Q_OS_MACOS
    deleteKeys.append(key(Qt::Key_Backspace));
#endif
    addAction(editMenu, u"Delete"_s, tr("&Delete"), deleteKeys);
    edit(u"Delete"_s, [this] { return ops::remove(*m_document, selection()); }, tr("Delete"));
    editMenu->addSeparator();
    addAction(editMenu, u"SelectAll"_s, tr("Select &All"), {key(Qt::CTRL | Qt::Key_A)});
    on(u"SelectAll"_s, &MainWindow::selectAll);
    editMenu->addSeparator();
    QList<QKeySequence> preferenceKeys = QKeySequence::keyBindings(QKeySequence::Preferences);
    if (preferenceKeys.isEmpty())
        preferenceKeys = {key(Qt::CTRL | Qt::Key_Comma)};
    QAction *preferences = addAction(editMenu, u"Preferences"_s, tr("Pr&eferences…"), preferenceKeys);
    preferences->setMenuRole(QAction::PreferencesRole);
    on(u"Preferences"_s, &MainWindow::showPreferences);

    // Board
    QMenu *board = menuBar()->addMenu(tr("&Board"));
    addAction(board, u"NewNote"_s, tr("New &Note"), {key(Qt::Key_N)});
    on(u"NewNote"_s, [this] {
        finishModes();
        m_view->beginNewNote(m_view->cursorScenePos());
    });
    addAction(board, u"NoteStyle"_s, tr("Note &Style…"));
    on(u"NoteStyle"_s, &MainWindow::editNoteStyle);
    board->addSeparator();
    addAction(board, u"Crop"_s, tr("&Crop"), {key(Qt::Key_C)});
    on(u"Crop"_s, [this] {
        finishModes();
        m_view->beginCrop();
    });
    addAction(board, u"ResetCrop"_s, tr("&Reset Crop"));
    edit(u"ResetCrop"_s, [this] { return ops::resetCrop(*m_document, selection()); }, tr("Reset Crop"));
    addAction(board, u"FlipHorizontal"_s, tr("Flip &Horizontally"), {key(Qt::Key_H)});
    edit(u"FlipHorizontal"_s, [this] { return ops::flip(*m_document, selection(), Qt::Horizontal); }, tr("Flip"));
    addAction(board, u"FlipVertical"_s, tr("Flip &Vertically"), {key(Qt::Key_V)});
    edit(u"FlipVertical"_s, [this] { return ops::flip(*m_document, selection(), Qt::Vertical); }, tr("Flip"));
    board->addSeparator();
    addAction(board, u"Group"_s, tr("&Group"), {key(Qt::CTRL | Qt::Key_G)});
    connect(m_actions.value(u"Group"_s), &QAction::triggered, this, [this] {
        finishModes();
        const OpResult r = ops::group(*m_document, selection(), {});
        if (run(r, tr("Group")))
            m_scene->selectIds(r.created);
    });
    addAction(board, u"Ungroup"_s, tr("&Ungroup"), {key(Qt::CTRL | Qt::SHIFT | Qt::Key_G)});
    connect(m_actions.value(u"Ungroup"_s), &QAction::triggered, this, [this] {
        finishModes();
        const OpResult r = ops::ungroup(*m_document, selection());
        if (run(r, tr("Ungroup")))
            m_scene->selectIds(r.created);
    });
    addAction(board, u"RenameGroup"_s, tr("Re&name Group…"), {key(Qt::Key_F2)});
    on(u"RenameGroup"_s, &MainWindow::renameGroup);
    addAction(board, u"ToggleLock"_s, tr("&Lock"), {key(Qt::CTRL | Qt::Key_L)});
    connect(m_actions.value(u"ToggleLock"_s), &QAction::triggered, this, [this] {
        const QList<Id> ids = selection();
        bool allLocked = !ids.isEmpty();
        for (const Id &id : ids)
            allLocked = allLocked && ops::isLocked(*m_document, id);
        run(ops::setLocked(*m_document, ids, !allLocked), allLocked ? tr("Unlock") : tr("Lock"));
    });
    board->addSeparator();
    QMenu *align = board->addMenu(tr("&Align"));
    const QList<std::tuple<QString, QString, Align>> alignments{
        {u"AlignLeft"_s, tr("&Left Edges"), Align::Left},     {u"AlignHCenter"_s, tr("Horizontal &Centers"), Align::HCenter},
        {u"AlignRight"_s, tr("&Right Edges"), Align::Right},  {u"AlignTop"_s, tr("&Top Edges"), Align::Top},
        {u"AlignVCenter"_s, tr("&Vertical Centers"), Align::VCenter}, {u"AlignBottom"_s, tr("&Bottom Edges"), Align::Bottom},
    };
    for (const auto &[id, text, mode] : alignments) {
        addAction(align, id, text);
        const Align m = mode;
        edit(id, [this, m] { return ops::align(*m_document, selection(), m); }, tr("Align"));
    }
    addAction(board, u"Arrange"_s, tr("A&rrange"));
    edit(u"Arrange"_s, [this] {
        const int spacing = QSettings().value(ArrangeSpacingKey, DefaultArrangeSpacing).toInt();
        return ops::arrange(*m_document, selection(), spacing);
    }, tr("Arrange"));
    addAction(board, u"ArrangeSpacing"_s, tr("Arrange &Spacing…"));
    on(u"ArrangeSpacing"_s, &MainWindow::chooseArrangeSpacing);
    board->addSeparator();
    const QList<std::tuple<QString, QString, ZOrder, QKeySequence>> stacking{
        {u"Raise"_s, tr("Bring For&ward"), ZOrder::Raise, key(Qt::CTRL | Qt::Key_PageUp)},
        {u"Lower"_s, tr("Send Back&ward"), ZOrder::Lower, key(Qt::CTRL | Qt::Key_PageDown)},
        {u"BringToFront"_s, tr("Bring to &Front"), ZOrder::BringToFront, key(Qt::CTRL | Qt::SHIFT | Qt::Key_PageUp)},
        {u"SendToBack"_s, tr("Send to Bac&k"), ZOrder::SendToBack, key(Qt::CTRL | Qt::SHIFT | Qt::Key_PageDown)},
    };
    for (const auto &[id, text, op, shortcut] : stacking) {
        addAction(board, id, text, {shortcut});
        const ZOrder o = op;
        edit(id, [this, o] { return ops::zOrder(*m_document, selection(), o); }, tr("Change Stacking Order"));
    }
    board->addSeparator();
    QMenu *links = board->addMenu(tr("Lin&ks"));
    addAction(links, u"LocateLink"_s, tr("&Locate Linked File…"));
    on(u"LocateLink"_s, &MainWindow::locateLink);
    addAction(links, u"ReloadLink"_s, tr("&Reload Linked File"));
    on(u"ReloadLink"_s, &MainWindow::reloadLinks);
    addAction(links, u"AcceptLinkChange"_s, tr("&Accept Changed File"));
    on(u"AcceptLinkChange"_s, &MainWindow::acceptLinkChanges);
    links->addSeparator();
    addAction(links, u"EmbedLink"_s, tr("&Embed Linked Image"));
    on(u"EmbedLink"_s, [this] { embedLinks(false); });
    addAction(links, u"EmbedAllLinks"_s, tr("Embed A&ll Linked Images"));
    on(u"EmbedAllLinks"_s, [this] { embedLinks(true); });
    board->addSeparator();
    addAction(board, u"Background"_s, tr("Board &Background…"));
    on(u"Background"_s, &MainWindow::chooseBackground);

    // View
    QMenu *view = menuBar()->addMenu(tr("&View"));
    addAction(view, u"FitBoard"_s, tr("Fit &Board"), {key(Qt::Key_F)});
    connect(m_actions.value(u"FitBoard"_s), &QAction::triggered, m_view, &canvas::BoardView::fitBoard);
    addAction(view, u"FitSelection"_s, tr("Fit &Selection"), {key(Qt::SHIFT | Qt::Key_F)});
    connect(m_actions.value(u"FitSelection"_s), &QAction::triggered, m_view, &canvas::BoardView::fitSelection);
    view->addSeparator();
    addAction(view, u"ZoomIn"_s, tr("Zoom &In"), QKeySequence::keyBindings(QKeySequence::ZoomIn));
    connect(m_actions.value(u"ZoomIn"_s), &QAction::triggered, m_view, &canvas::BoardView::zoomIn);
    addAction(view, u"ZoomOut"_s, tr("Zoom &Out"), QKeySequence::keyBindings(QKeySequence::ZoomOut));
    connect(m_actions.value(u"ZoomOut"_s), &QAction::triggered, m_view, &canvas::BoardView::zoomOut);
    addAction(view, u"ZoomReset"_s, tr("Zoom &100%"), {key(Qt::CTRL | Qt::Key_0)});
    connect(m_actions.value(u"ZoomReset"_s), &QAction::triggered, m_view, &canvas::BoardView::resetZoom);
    view->addSeparator();
    QAction *gray = addAction(view, u"Grayscale"_s, tr("&Grayscale Preview"), {key(Qt::Key_G)});
    gray->setCheckable(true);
    connect(gray, &QAction::triggered, this, [this](bool checked) {
        run(ops::setGrayscale(*m_document, checked), tr("Grayscale Preview"));
    });
    QAction *onTop = addAction(view, u"AlwaysOnTop"_s, tr("Always on &Top"), {key(Qt::CTRL | Qt::Key_T)});
    onTop->setCheckable(true);
    connect(onTop, &QAction::toggled, this, &MainWindow::setAlwaysOnTop);
    // Window opacity where the platform applies it (spec §3; not on native Wayland).
    QMenu *opacity = view->addMenu(tr("Window &Opacity"));
    auto *opacityGroup = new QActionGroup(this);
    for (int percent : {100, 90, 80, 70, 60, 50}) {
        QAction *a = addAction(opacity, u"Opacity%1"_s.arg(percent), tr("%1 %").arg(percent));
        a->setCheckable(true);
        a->setChecked(percent == 100);
        opacityGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, percent] { setWindowOpacity(percent / 100.0); });
    }
    opacity->menuAction()->setVisible(m_capabilities.opacity);
    opacity->setEnabled(m_capabilities.opacity);
    QAction *frameless = addAction(view, u"FramelessWindow"_s, tr("&Frameless Window"));
    frameless->setCheckable(true);
    connect(frameless, &QAction::triggered, this, [this](bool enable) { setFrameless(enable); });
    addAction(nullptr, u"ShowWindowFrame"_s, tr("Show Window &Frame"));
    on(u"ShowWindowFrame"_s, [this] { setFrameless(false); });
    view->addSeparator();
    // Minimal UI (spec §3): Tab toggles the menu bar, except while editing text.
    QAction *menuToggle = addAction(view, u"ShowMenuBar"_s, tr("Show &Menu Bar"), {key(Qt::Key_Tab)});
    menuToggle->setCheckable(true);
    connect(menuToggle, &QAction::triggered, this,
            [this](bool visible) { m_services.settings()->setMenuBarVisible(visible); });

    // Help
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    QList<QKeySequence> helpKeys = QKeySequence::keyBindings(QKeySequence::HelpContents);
    if (helpKeys.isEmpty())
        helpKeys = {key(Qt::Key_F1)};
    addAction(help, u"Help"_s, tr("Glacier Ref &Help"), helpKeys);
    on(u"Help"_s, [this] { showHelp(HelpDialog::Page::Basics); });
    addAction(help, u"ShortcutReference"_s, tr("&Keyboard Shortcuts"));
    on(u"ShortcutReference"_s, [this] { showHelp(HelpDialog::Page::Shortcuts); });
    help->addSeparator();
    addAction(help, u"ProjectWebsite"_s, tr("Project &Website"));
    on(u"ProjectWebsite"_s, &MainWindow::openProjectWebsite);
    addAction(help, u"CopyDiagnostics"_s, tr("Copy &Diagnostics"));
    on(u"CopyDiagnostics"_s, [this] {
        QGuiApplication::clipboard()->setText(diagnosticsReport());
        m_view->showToast(tr("Diagnostics copied to the clipboard. They contain no file contents or personal paths."));
    });
    help->addSeparator();
    QAction *about = addAction(help, u"About"_s, tr("&About Glacier Ref"));
    about->setMenuRole(QAction::AboutRole);
    on(u"About"_s, [this] { showHelp(HelpDialog::Page::About); });
    QAction *aboutQt = addAction(help, u"AboutQt"_s, tr("About &Qt"));
    aboutQt->setMenuRole(QAction::AboutQtRole);
    connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
}

QMenu *MainWindow::createContextMenu(QWidget *parent)
{
    updateActionStates();
    auto *menu = new QMenu(parent);
    if (!menuBar()->isVisible() || isFrameless()) {
        if (!menuBar()->isVisible())
            menu->addAction(m_actions.value(u"ShowMenuBar"_s));
        if (isFrameless())
            menu->addAction(m_actions.value(u"ShowWindowFrame"_s));
        menu->addSeparator();
    }
    auto add = [this, menu](const QString &id) {
        if (QAction *a = m_actions.value(id); a && a->isEnabled())
            menu->addAction(a);
    };
    const bool hasSelection = !selection().isEmpty();
    if (hasSelection) {
        for (const QString &id : {u"Cut"_s, u"Copy"_s, u"CopyImage"_s, u"Paste"_s, u"Duplicate"_s, u"Delete"_s})
            add(id);
        menu->addSeparator();
        for (const QString &id : {u"Crop"_s, u"ResetCrop"_s, u"FlipHorizontal"_s, u"FlipVertical"_s, u"NoteStyle"_s})
            add(id);
        menu->addSeparator();
        for (const QString &id : {u"LocateLink"_s, u"ReloadLink"_s, u"AcceptLinkChange"_s, u"EmbedLink"_s})
            add(id);
        menu->addSeparator();
        for (const QString &id : {u"Group"_s, u"Ungroup"_s, u"RenameGroup"_s, u"ToggleLock"_s})
            add(id);
        menu->addSeparator();
        for (const QString &id : {u"BringToFront"_s, u"Raise"_s, u"Lower"_s, u"SendToBack"_s, u"Arrange"_s})
            add(id);
    } else {
        for (const QString &id : {u"Paste"_s, u"NewNote"_s, u"ImportImages"_s})
            add(id);
        menu->addSeparator();
        for (const QString &id : {u"Arrange"_s, u"FitBoard"_s, u"Background"_s, u"Grayscale"_s})
            add(id);
    }
    return menu;
}

void MainWindow::updateActionStates()
{
    if (m_actions.isEmpty())
        return;
    const QList<Id> ids = selection();
    const BoardDocument &doc = *m_document;
    int unlocked = 0, locked = 0, images = 0, notes = 0, groups = 0, cropped = 0, children = 0;
    for (const Id &id : ids) {
        (ops::isLocked(doc, id) ? locked : unlocked) += 1;
        if (!doc.groupOf(id).isNull())
            ++children;
        if (const ImageItemData *i = doc.image(id)) {
            ++images;
            if (i->crop != QRectF(0, 0, 1, 1))
                ++cropped;
        } else if (doc.note(id)) {
            ++notes;
        } else if (const GroupData *g = doc.group(id)) {
            ++groups;
            for (const Id &child : g->children)
                if (const ImageItemData *ci = doc.image(child); ci && ci->crop != QRectF(0, 0, 1, 1))
                    ++cropped;
        }
    }
    const bool any = !ids.isEmpty();
    const bool editable = unlocked > 0;
    auto set = [this](const QString &id, bool enabled) {
        if (QAction *a = m_actions.value(id))
            a->setEnabled(enabled);
    };
    set(u"Cut"_s, any && locked == 0);
    set(u"Copy"_s, any);
    set(u"CopyImage"_s, ids.size() == 1 && images == 1);
    set(u"Duplicate"_s, any);
    set(u"Delete"_s, editable);
    set(u"SelectAll"_s, doc.itemCount() > 0);
    set(u"NoteStyle"_s, notes > 0 && editable);
    set(u"Crop"_s, ids.size() == 1 && images == 1 && editable);
    set(u"ResetCrop"_s, cropped > 0 && editable);
    set(u"FlipHorizontal"_s, editable);
    set(u"FlipVertical"_s, editable);
    set(u"Group"_s, ids.size() >= 2 && groups == 0 && children == 0 && locked == 0);
    set(u"Ungroup"_s, groups > 0 && editable);
    set(u"RenameGroup"_s, (ids.size() == 1 && groups == 1) || !m_scene->activeGroup().isNull());
    set(u"ToggleLock"_s, any && children == 0);
    if (QAction *lock = m_actions.value(u"ToggleLock"_s))
        lock->setText(any && unlocked == 0 ? tr("Un&lock") : tr("&Lock"));
    for (const QString &id : {u"AlignLeft"_s, u"AlignHCenter"_s, u"AlignRight"_s, u"AlignTop"_s, u"AlignVCenter"_s,
                              u"AlignBottom"_s})
        set(id, ids.size() >= 2 && editable);
    set(u"Arrange"_s, doc.itemCount() > 0);
    for (const QString &id : {u"Raise"_s, u"Lower"_s, u"BringToFront"_s, u"SendToBack"_s})
        set(id, editable);
    set(u"FitSelection"_s, any);
    int linked = 0, linkedAvailable = 0, linkedChanged = 0;
    for (const Id &assetId : selectedLinkedAssets()) {
        ++linked;
        const persistence::LinkState state = linkState(assetId);
        linkedAvailable += state == persistence::LinkState::Available;
        linkedChanged += state == persistence::LinkState::Changed;
    }
    bool anyLinkedAvailable = false;
    for (const Asset &a : doc.assets())
        anyLinkedAvailable = anyLinkedAvailable || (a.linked && linkState(a.id) == persistence::LinkState::Available);
    set(u"LocateLink"_s, linked == 1);
    set(u"ReloadLink"_s, linked > 0);
    set(u"AcceptLinkChange"_s, linkedChanged > 0);
    set(u"EmbedLink"_s, linkedAvailable > 0);
    set(u"EmbedAllLinks"_s, anyLinkedAvailable);
    // Read-only boards can't be saved in place (spec §5); Save As can.
    set(u"Save"_s, !m_readOnly && !m_saving && !m_loading);
    set(u"SaveAs"_s, !m_saving && !m_loading);
    set(u"Open"_s, true);
    if (QAction *gray = m_actions.value(u"Grayscale"_s))
        gray->setChecked(doc.view().grayscale);
}

// --- File --------------------------------------------------------------------------

void MainWindow::newWindow()
{
    auto *window = new MainWindow(m_services);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

bool MainWindow::isReusable() const
{
    return m_filePath.isEmpty() && !m_loading && !m_saving && m_document->itemCount() == 0
        && m_controller->undoStack()->count() == 0 && !m_controller->isDirty() && !m_import->isBusy();
}

MainWindow *MainWindow::openBoard(AppServices &services, const QString &path, MainWindow *current)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget *widget : widgets) {
        auto *window = qobject_cast<MainWindow *>(widget);
        if (window && !window->filePath().isEmpty() && samePath(window->filePath(), absolute)) {
            window->show();
            window->raise();
            window->activateWindow();
            return window;
        }
    }
    MainWindow *target = current && current->isReusable() ? current : nullptr;
    if (!target) {
        target = new MainWindow(services);
        target->setAttribute(Qt::WA_DeleteOnClose);
        target->m_closeIfLoadFails = true;
        target->show();
    }
    target->loadBoard(absolute);
    return target;
}

void MainWindow::openFromDialog()
{
    finishModes();
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Board"),
                                                      settings.value(LastBoardDirectoryKey).toString(),
                                                      boardFileFilter());
    if (path.isEmpty())
        return;
    settings.setValue(LastBoardDirectoryKey, QFileInfo(path).absolutePath());
    openBoard(m_services, path, this);
}

void MainWindow::loadBoard(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    m_loading = true;
    updateActionStates();
    // Lock first (spec §5): a board locked elsewhere opens read-only.
    auto lock = std::make_shared<persistence::DocumentLock::Result>(persistence::DocumentLock::acquire(absolute));

    auto *dialog = new QProgressDialog(tr("Opening “%1”…").arg(QFileInfo(absolute).fileName()), QString(), 0, 0, this);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setMinimumDuration(ProgressDelayMs);
    dialog->setCancelButton(nullptr);
    auto *watcher = new QFutureWatcher<persistence::LoadResult>(this);
    connect(watcher, &QFutureWatcherBase::progressRangeChanged, dialog, &QProgressDialog::setRange);
    connect(watcher, &QFutureWatcherBase::progressValueChanged, dialog, &QProgressDialog::setValue);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, dialog, absolute, lock] {
        retireProgressDialog(dialog, watcher);
        watcher->deleteLater();
        applyLoaded(absolute, watcher->result(), lock);
    });
    const auto store = m_services.store();
    const Limits limits = m_services.limits();
    watcher->setFuture(QtConcurrent::run(m_services.pool(), [absolute, store, limits](QPromise<persistence::LoadResult> &promise) {
        promise.addResult(persistence::loadBoard(absolute, store, limits, [&promise](int done, int total) {
            promise.setProgressRange(0, total);
            promise.setProgressValue(done);
        }));
    }));
}

void MainWindow::applyLoaded(const QString &path, persistence::LoadResult result,
                             std::shared_ptr<persistence::DocumentLock::Result> lock,
                             std::optional<persistence::RecoveryStore::Entry> recovered)
{
    m_loading = false;
    const QString name = recovered ? (recovered->title.isEmpty() ? tr("Untitled") : recovered->title)
                                   : QFileInfo(path).fileName();
    QString error = result.error;
    if (result.content && !isReusable())
        error = tr("the window is no longer empty"); // e.g. an import finished meanwhile
    if (result.content && error.isEmpty()) {
        DocumentChange change;
        change.insert = result.content->snapshot;
        change.view = result.content->view;
        if (!m_document->apply(change, &error) || !m_document->setMetadata(result.content->metadata, &error))
            result.content.reset();
    }
    if (!result.content || !error.isEmpty()) {
        qCWarning(lcFiles, "%s failed for \"%s\": %s", recovered ? "recovery" : "opening", qPrintable(name),
                  qPrintable(error));
        updateActionStates();
        QMessageBox box(QMessageBox::Critical, recovered ? tr("Recover Board") : tr("Open Board"),
                        recovered ? tr("“%1” could not be recovered.").arg(name)
                                  : tr("“%1” could not be opened.").arg(name),
                        QMessageBox::Ok, this);
        box.setObjectName(u"openFailedBox"_s);
        box.setInformativeText(error.isEmpty() ? tr("Unknown error.") : error);
        box.exec();
        emit boardLoaded(false);
        if (m_closeIfLoadFails)
            close();
        return;
    }

    qCInfo(lcFiles, "%s \"%s\": %lld items, %lld assets", recovered ? "recovered" : "opened", qPrintable(name),
           qlonglong(m_document->itemCount()), qlonglong(m_document->assetCount()));
    m_closeIfLoadFails = false;
    m_controller->resetHistory();
    m_filePath = path;
    m_fingerprint = result.fingerprint;
    if (!lock) { // a recovered board that was never saved
        m_lock.reset();
        m_readOnly = false;
    } else if (lock->status == persistence::DocumentLock::Status::Acquired) {
        m_lock = std::shared_ptr<persistence::DocumentLock>(std::move(lock->lock));
        m_readOnly = false;
    } else {
        m_lock.reset();
        m_readOnly = true;
    }
    m_linkStates.clear();
    for (auto it = result.links.cbegin(); it != result.links.cend(); ++it)
        setLinkState(it.key(), it.value());
    m_view->setZoom(result.content->zoom, QPointF(m_view->viewport()->rect().center()));
    m_view->centerOn(result.content->viewCenter);
    if (!path.isEmpty())
        QSettings().setValue(LastBoardDirectoryKey, QFileInfo(path).absolutePath());
    if (recovered) {
        // Recovered boards are dirty until saved (spec §5); the old recovery
        // data is dropped once this window has its own snapshot.
        m_controller->markNeverClean();
        m_recoveredFrom = recovered;
        writeRecoverySnapshot();
        m_view->showToast(tr("Unsaved changes were recovered. Save the board to keep them."));
    }
    updateTitle();
    updateActionStates();
    emit boardLoaded(true);
    if (m_readOnly && lock)
        showReadOnlyNotice(lock->holder, lock->status == persistence::DocumentLock::Status::Unavailable
                                             ? lock->error : QString());
    showLinkSummary();
}

void MainWindow::showReadOnlyNotice(const persistence::DocumentLock::Holder &holder, const QString &reason)
{
    QMessageBox box(QMessageBox::Information, tr("Opened Read-Only"),
                    tr("“%1” was opened read-only.").arg(displayFileName()), QMessageBox::Ok, this);
    box.setObjectName(u"readOnlyNotice"_s);
    QString details;
    if (!reason.isEmpty()) {
        details = tr("The board can't be locked for editing: %1.").arg(reason);
    } else if (holder.otherHost) {
        details = tr("It is open in %1 on the computer “%2”.").arg(holder.application, holder.hostname);
    } else {
        details = tr("It is open in another Glacier Ref window or program.");
    }
    box.setInformativeText(details + u"\n\n"_s
                           + tr("You can edit it, but changes can only be kept with Save As."));
    QPushButton *removeLock = nullptr;
    if (holder.otherHost)
        removeLock = box.addButton(tr("Remove Lock…"), QMessageBox::DestructiveRole);
    box.exec();
    if (removeLock && box.clickedButton() == removeLock) {
        const auto answer = QMessageBox::warning(
            this, tr("Remove Lock"),
            tr("Only remove the lock if “%1” is not open on “%2” anymore (for example after a crash). "
               "Two people editing the same board can overwrite each other's work.")
                .arg(displayFileName(), holder.hostname),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer == QMessageBox::Yes && persistence::DocumentLock::removeLock(m_filePath)) {
            persistence::DocumentLock::Result again = persistence::DocumentLock::acquire(m_filePath);
            if (again.status == persistence::DocumentLock::Status::Acquired) {
                m_lock = std::shared_ptr<persistence::DocumentLock>(std::move(again.lock));
                m_readOnly = false;
                updateTitle();
                updateActionStates();
            }
        }
    }
}

persistence::BoardContent MainWindow::currentContent() const
{
    persistence::BoardContent content;
    content.metadata = m_document->metadata();
    content.snapshot = m_document->snapshotAll();
    content.view = m_document->view();
    content.viewCenter = m_view->viewCenter();
    content.zoom = m_view->zoom();
    return content;
}

QByteArray MainWindow::renderPreview() const
{
    const QRectF bounds = m_document->boundingRect();
    if (bounds.isEmpty())
        return {};
    const double scale = PreviewSide / std::max(bounds.width(), bounds.height());
    const QSize size = (bounds.size() * scale).toSize().expandedTo(QSize(1, 1));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor::fromRgba(m_document->view().backgroundColor));
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        m_scene->render(&painter, QRectF(QPointF(0, 0), QSizeF(size)), bounds);
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "png");
    return png;
}

void MainWindow::save(std::function<void(bool)> done)
{
    finishModes();
    if (m_filePath.isEmpty() || m_readOnly) {
        saveAsFromDialog(std::move(done));
        return;
    }
    // External modification (spec §5): never overwrite silently.
    const persistence::FileFingerprint now = persistence::FileFingerprint::of(m_filePath);
    if (now.exists() && m_fingerprint.exists() && now != m_fingerprint) {
        QMessageBox box(QMessageBox::Warning, tr("Save"),
                        tr("“%1” was changed by another program since it was opened or last saved.")
                            .arg(displayFileName()),
                        QMessageBox::Cancel, this);
        box.setObjectName(u"externalModificationBox"_s);
        box.setInformativeText(tr("Replace it with this board, or save this board as a new file?"));
        QPushButton *saveAs = box.addButton(tr("Save As…"), QMessageBox::AcceptRole);
        QPushButton *replace = box.addButton(tr("Replace"), QMessageBox::DestructiveRole);
        box.setDefaultButton(saveAs);
        box.exec();
        if (box.clickedButton() == saveAs) {
            saveAsFromDialog(std::move(done));
            return;
        }
        if (box.clickedButton() != replace) {
            if (done)
                done(false);
            return;
        }
    }
    startSave(m_filePath, nullptr, std::move(done));
}

void MainWindow::saveAsFromDialog(std::function<void(bool)> done)
{
    finishModes();
    QSettings settings;
    QString base = m_document->title().trimmed();
    if (base.isEmpty())
        base = m_filePath.isEmpty() ? tr("Untitled") : QFileInfo(m_filePath).completeBaseName();
    const QString dir = m_filePath.isEmpty() ? settings.value(LastBoardDirectoryKey).toString()
                                             : QFileInfo(m_filePath).absolutePath();
    QString path = QFileDialog::getSaveFileName(this, tr("Save Board As"),
                                                QDir(dir).filePath(base + u'.' + BoardSuffix), boardFileFilter());
    if (path.isEmpty()) {
        if (done)
            done(false);
        return;
    }
    if (!isBoardFile(path))
        path += u'.' + BoardSuffix;
    saveAs(path, std::move(done));
}

void MainWindow::saveAs(const QString &path, std::function<void(bool)> done)
{
    finishModes();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if (!m_filePath.isEmpty() && samePath(absolute, m_filePath) && !m_readOnly) {
        startSave(m_filePath, nullptr, std::move(done));
        return;
    }
    // Take the new file's lock before writing it (spec §5).
    persistence::DocumentLock::Result lock = persistence::DocumentLock::acquire(absolute);
    if (lock.status != persistence::DocumentLock::Status::Acquired) {
        QMessageBox box(QMessageBox::Warning, tr("Save Board As"),
                        lock.status == persistence::DocumentLock::Status::HeldElsewhere
                            ? tr("“%1” is open in another window or program. Choose another name.")
                                  .arg(QFileInfo(absolute).fileName())
                            : tr("“%1” can't be written: %2.").arg(QFileInfo(absolute).fileName(), lock.error),
                        QMessageBox::Ok, this);
        box.setObjectName(u"saveAsBlockedBox"_s);
        box.exec();
        if (done)
            done(false);
        return;
    }
    startSave(absolute, std::shared_ptr<persistence::DocumentLock>(std::move(lock.lock)), std::move(done));
}

void MainWindow::startSave(const QString &path, std::shared_ptr<persistence::DocumentLock> newLock,
                           std::function<void(bool)> done)
{
    if (m_saving) {
        if (done)
            done(false);
        return;
    }
    finishModes();
    // An immutable snapshot (spec §5): edits made during the save stay dirty.
    persistence::SaveRequest request;
    request.content = currentContent();
    if (request.content.metadata.title.isEmpty())
        request.content.metadata.title = QFileInfo(path).completeBaseName().left(BoardDocument::MaxTitleLength);
    request.content.metadata.modified = QDateTime::currentDateTimeUtc();
    request.path = path;
    request.previewPng = renderPreview();
    request.generator = app_info::displayName() + u' ' + app_info::version();
    request.store = m_services.store();
    request.limits = m_services.limits();
    const CommandController::Marker marker = m_controller->currentMarker();
    m_saving = true;
    updateActionStates();

    auto *dialog = new QProgressDialog(tr("Saving “%1”…").arg(QFileInfo(path).fileName()), QString(), 0, 0, this);
    dialog->setWindowModality(Qt::NonModal);
    dialog->setMinimumDuration(ProgressDelayMs);
    dialog->setCancelButton(nullptr); // a save is never abandoned halfway
    auto *watcher = new QFutureWatcher<persistence::SaveResult>(this);
    connect(watcher, &QFutureWatcherBase::progressRangeChanged, dialog, &QProgressDialog::setRange);
    connect(watcher, &QFutureWatcherBase::progressValueChanged, dialog, &QProgressDialog::setValue);
    const persistence::BoardContent content = request.content;
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, dialog, path, content, marker, newLock, done = std::move(done)] {
                retireProgressDialog(dialog, watcher);
                watcher->deleteLater();
                finishSave(watcher->result(), path, content, marker, newLock, done);
            });
    watcher->setFuture(QtConcurrent::run(m_services.pool(), [request](QPromise<persistence::SaveResult> &promise) {
        promise.addResult(persistence::saveBoard(request, [&promise](int d, int total) {
            promise.setProgressRange(0, total);
            promise.setProgressValue(d);
        }));
    }));
}

void MainWindow::finishSave(const persistence::SaveResult &result, const QString &path,
                            const persistence::BoardContent &content, CommandController::Marker marker,
                            std::shared_ptr<persistence::DocumentLock> newLock, std::function<void(bool)> done)
{
    m_saving = false;
    if (result.ok)
        qCInfo(lcFiles, "saved \"%s\" (%lld bytes)", qPrintable(QFileInfo(path).fileName()),
               qlonglong(result.fingerprint.size));
    else
        qCWarning(lcFiles, "saving \"%s\" failed: %s", qPrintable(QFileInfo(path).fileName()),
                  qPrintable(result.error));
    if (result.ok) {
        if (newLock) {
            m_lock = std::move(newLock); // releases the previous file's lock
            m_readOnly = false;
        }
        m_filePath = path;
        m_fingerprint = result.fingerprint;
        BoardMetadata metadata = m_document->metadata();
        metadata.modified = content.metadata.modified;
        if (metadata.title.isEmpty())
            metadata.title = content.metadata.title;
        m_document->setMetadata(metadata);
        m_controller->markClean(marker);
        QSettings().setValue(LastBoardDirectoryKey, QFileInfo(path).absolutePath());
        warnAboutAbsoluteLinks(content, path);
    } else {
        QMessageBox box(QMessageBox::Critical, tr("Save"),
                        tr("“%1” could not be saved.").arg(QFileInfo(path).fileName()), QMessageBox::Ok, this);
        box.setObjectName(u"saveFailedBox"_s);
        box.setInformativeText(result.error + u"\n\n"_s
                               + tr("The previous version of the file was not changed. Your changes are still "
                                    "here; try saving again or use Save As to choose another location."));
        box.exec();
    }
    updateTitle();
    updateActionStates();
    emit saveFinished(result.ok);
    if (done)
        done(result.ok);
    if (m_closeWhenSaved) {
        m_closeWhenSaved = false;
        close();
    }
}

void MainWindow::editBoardTitle()
{
    finishModes();
    bool ok = false;
    const QString title = QInputDialog::getText(this, tr("Board Title"), tr("Title:"), QLineEdit::Normal,
                                                m_document->title(), &ok);
    if (!ok || title == m_document->title())
        return;
    DocumentChange change;
    change.title = title.left(BoardDocument::MaxTitleLength);
    QString error;
    if (!m_controller->run(change, tr("Board Title"), &error))
        m_view->showToast(error);
}

void MainWindow::handleDrop(const QMimeData *mime, QPointF scenePos)
{
    // Dropped boards open (spec §3); everything else is imported.
    QStringList boards;
    QStringList others;
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile() && isBoardFile(url.toLocalFile()))
            boards.append(url.toLocalFile());
        else if (url.isLocalFile())
            others.append(url.toLocalFile());
    }
    if (boards.isEmpty()) {
        importPlan(io::routeMimeData(*mime), scenePos);
        return;
    }
    for (const QString &board : std::as_const(boards))
        openBoard(m_services, board, this);
    if (!others.isEmpty())
        importPlan(io::planForPaths(others), scenePos);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    finishModes();
    if (m_saving) {
        m_closeWhenSaved = true; // close once the save has finished
        event->ignore();
        return;
    }
    if (m_closeConfirmed || !m_controller->isDirty()) {
        prepareToClose();
        event->accept();
        return;
    }
    QMessageBox box(QMessageBox::Warning, tr("Unsaved Changes"),
                    tr("Save the changes to “%1” before closing?").arg(displayFileName()),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setObjectName(u"unsavedChangesBox"_s);
    box.setInformativeText(tr("If you don't save, your changes will be lost."));
    box.setDefaultButton(QMessageBox::Save);
    const int answer = box.exec();
    if (answer == QMessageBox::Discard) {
        prepareToClose();
        event->accept();
        return;
    }
    event->ignore();
    if (answer == QMessageBox::Save) {
        save([this](bool ok) {
            if (ok && !m_controller->isDirty()) {
                m_closeConfirmed = true;
                close();
            }
        });
    }
}

void MainWindow::importPlan(const io::ImportPlan &plan, QPointF anchor)
{
    if (plan.sources.isEmpty()) {
        showMessage(tr("Nothing imported"), plan.blockingMessage);
        return;
    }
    m_import->start(plan, m_document->documentToken(), anchor);
}

void MainWindow::importImagesFromDialog()
{
    QSettings settings;
    const QString lastDir = settings.value(u"import/lastDirectory"_s).toString();
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Import Images"), lastDir, io::fileDialogFilter());
    if (files.isEmpty())
        return;
    settings.setValue(u"import/lastDirectory"_s, QFileInfo(files.first()).absolutePath());
    importPlan(io::planForPaths(files), m_view->viewCenter());
}

void MainWindow::importFolderFromDialog()
{
    QSettings settings;
    const QString lastDir = settings.value(u"import/lastDirectory"_s).toString();
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Import Folder"), lastDir);
    if (folder.isEmpty())
        return;
    settings.setValue(u"import/lastDirectory"_s, folder);
    importPlan(io::planForPaths({folder}), m_view->viewCenter());
}

// --- Recovery ------------------------------------------------------------------------

MainWindow *MainWindow::recoverBoard(AppServices &services, const persistence::RecoveryStore::Entry &entry,
                                     MainWindow *current)
{
    MainWindow *target = current && current->isReusable() ? current : nullptr;
    if (!target) {
        target = new MainWindow(services);
        target->setAttribute(Qt::WA_DeleteOnClose);
        target->m_closeIfLoadFails = true;
        target->show();
    }
    target->loadRecovery(entry);
    return target;
}

void MainWindow::loadRecovery(const persistence::RecoveryStore::Entry &entry)
{
    m_loading = true;
    updateActionStates();
    // The original file is locked as on a normal open; the crashed session's
    // lock is stale and is taken over.
    std::shared_ptr<persistence::DocumentLock::Result> lock;
    if (!entry.boardPath.isEmpty())
        lock = std::make_shared<persistence::DocumentLock::Result>(persistence::DocumentLock::acquire(entry.boardPath));
    auto *watcher = new QFutureWatcher<persistence::LoadResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, entry, lock] {
        watcher->deleteLater();
        applyLoaded(entry.boardPath, watcher->result(), lock, entry);
    });
    const auto store = m_services.store();
    const Limits limits = m_services.limits();
    const QString root = m_services.recoveryRoot();
    watcher->setFuture(QtConcurrent::run(m_services.pool(), [root, entry, store, limits] {
        return persistence::RecoveryStore::loadSnapshot(root, entry, *store, limits);
    }));
}

void MainWindow::scheduleRecoverySnapshot()
{
    if (m_services.recovery() && m_controller->isDirty() && !m_loading && !m_recoveryTimer->isActive())
        m_recoveryTimer->start();
}

void MainWindow::writeRecoverySnapshot()
{
    const std::shared_ptr<persistence::RecoveryStore> recovery = m_services.recovery();
    const quint64 revision = m_document->revision();
    if (!recovery || m_snapshotInFlight || !m_controller->isDirty() || m_snapshotRevision == revision)
        return;
    // A text edit in progress is not committed by a timer; the snapshot holds
    // the board as it is.
    persistence::RecoveryStore::SnapshotRequest request;
    request.key = m_recoveryKey;
    request.content = currentContent();
    request.boardPath = m_filePath;
    request.baseFingerprint = m_fingerprint;
    m_snapshotInFlight = true;
    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, revision] {
        watcher->deleteLater();
        m_snapshotInFlight = false;
        const bool ok = watcher->result();
        if (ok) {
            m_snapshotRevision = revision;
            if (m_recoveredFrom) {
                persistence::RecoveryStore::discardEntry(*m_recoveredFrom); // replaced by our snapshot
                m_recoveredFrom.reset();
            }
        }
        if (m_discardAfterSnapshot) {
            m_discardAfterSnapshot = false;
            if (!m_controller->isDirty())
                discardRecovery();
        }
        emit recoverySnapshotWritten(ok);
        if (m_controller->isDirty() && m_document->revision() != revision)
            scheduleRecoverySnapshot();
    });
    const auto store = m_services.store();
    const auto closed = m_recoveryClosed;
    watcher->setFuture(QtConcurrent::run(m_services.pool(), [recovery, request, store, closed] {
        if (closed->load())
            return false;
        QString error;
        const bool ok = recovery->writeSnapshot(request, *store, &error);
        if (!ok)
            qCWarning(lcRecovery, "recovery snapshot failed: %s", qPrintable(error));
        if (closed->load()) // the window closed meanwhile
            recovery->discardDocument(request.key);
        return ok;
    }));
}

void MainWindow::discardRecovery()
{
    m_recoveryTimer->stop();
    if (m_snapshotInFlight) {
        m_discardAfterSnapshot = true;
        return;
    }
    if (const auto recovery = m_services.recovery())
        recovery->discardDocument(m_recoveryKey);
    m_snapshotRevision.reset();
    if (m_recoveredFrom) {
        persistence::RecoveryStore::discardEntry(*m_recoveredFrom);
        m_recoveredFrom.reset();
    }
}

void MainWindow::prepareToClose()
{
    m_services.settings()->setWindowGeometry(saveGeometry());
    // Normal closure (spec §5): the chosen action is done, so the board's
    // recovery data goes, including what it was recovered from.
    m_recoveryClosed->store(true);
    m_recoveryTimer->stop();
    if (const auto recovery = m_services.recovery())
        recovery->discardDocument(m_recoveryKey);
    if (m_recoveredFrom) {
        persistence::RecoveryStore::discardEntry(*m_recoveredFrom);
        m_recoveredFrom.reset();
    }
}

// --- Linked assets -------------------------------------------------------------------

void MainWindow::importLinkedFromDialog()
{
    QSettings settings;
    const QString lastDir = settings.value(u"import/lastDirectory"_s).toString();
    const QStringList files =
        QFileDialog::getOpenFileNames(this, tr("Import as Link"), lastDir, io::fileDialogFilter());
    if (files.isEmpty())
        return;
    settings.setValue(u"import/lastDirectory"_s, QFileInfo(files.first()).absolutePath());
    io::ImportPlan plan = io::planForPaths(files);
    plan.linked = true;
    importPlan(plan, m_view->viewCenter());
}

persistence::LinkState MainWindow::linkState(const Id &assetId) const
{
    const auto it = m_linkStates.constFind(assetId);
    return it == m_linkStates.cend() ? persistence::LinkState::Available : it->state;
}

void MainWindow::setLinkState(const Id &assetId, const persistence::LinkCheck &check)
{
    m_linkStates.insert(assetId, check);
    const Asset *asset = m_document->asset(assetId);
    const QString name = asset ? QFileInfo(asset->linkPath).fileName() : QString();
    QString label;
    if (check.state == persistence::LinkState::Missing)
        label = tr("Missing linked file\n%1").arg(name);
    else if (check.state == persistence::LinkState::Changed)
        label = tr("Linked file changed on disk\n%1\n\nBoard ▸ Links ▸ Accept Changed File").arg(name);
    m_scene->setAssetPlaceholder(assetId, label);
    updateActionStates();
}

QList<Id> MainWindow::selectedLinkedAssets() const
{
    QList<Id> assets;
    auto addImage = [this, &assets](const Id &imageId) {
        if (const ImageItemData *image = m_document->image(imageId))
            if (const Asset *asset = m_document->asset(image->assetId); asset && asset->linked
                && !assets.contains(asset->id))
                assets.append(asset->id);
    };
    for (const Id &id : selection()) {
        addImage(id);
        if (const GroupData *g = m_document->group(id))
            for (const Id &child : g->children)
                addImage(child);
    }
    return assets;
}

void MainWindow::checkLinks(const QList<Id> &assetIds, const QStringList &paths,
                            std::function<void(QList<persistence::LinkCheck>)> done)
{
    QList<QByteArray> expected;
    for (const Id &id : assetIds)
        expected.append(m_document->asset(id) ? m_document->asset(id)->sha256 : QByteArray());
    const auto store = m_services.store();
    const Limits limits = m_services.limits();
    const Id token = m_document->documentToken();
    auto *watcher = new QFutureWatcher<QList<persistence::LinkCheck>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, token, done = std::move(done)] {
        watcher->deleteLater();
        if (token == m_document->documentToken())
            done(watcher->result());
        emit linksChecked();
    });
    watcher->setFuture(QtConcurrent::run(m_services.pool(), [paths, expected, store, limits] {
        QList<persistence::LinkCheck> checks;
        for (qsizetype i = 0; i < paths.size(); ++i)
            checks.append(persistence::checkLink(paths[i], expected[i], *store, limits));
        return checks;
    }));
}

void MainWindow::locateLink()
{
    finishModes();
    const QList<Id> assets = selectedLinkedAssets();
    if (assets.size() != 1)
        return;
    const Asset *asset = m_document->asset(assets.first());
    const QString start = QFileInfo(asset->linkPath).absolutePath();
    const QString chosen = QFileDialog::getOpenFileName(
        this, tr("Locate “%1”").arg(QFileInfo(asset->linkPath).fileName()),
        QFileInfo::exists(start) ? start : QSettings().value(u"import/lastDirectory"_s).toString(),
        io::fileDialogFilter());
    if (!chosen.isEmpty())
        locateLinkTo(chosen);
}

void MainWindow::locateLinkTo(const QString &file)
{
    const QList<Id> assets = selectedLinkedAssets();
    if (assets.size() != 1)
        return;
    const QString path = link_paths::normalize(QFileInfo(file).absoluteFilePath());
    const Id assetId = assets.first();
    checkLinks({assetId}, {path}, [this, assetId, path](const QList<persistence::LinkCheck> &checks) {
        const Asset *old = m_document->asset(assetId);
        if (!old || checks.isEmpty())
            return;
        const persistence::LinkCheck &check = checks.first();
        if (check.state == persistence::LinkState::Missing) {
            showMessage(tr("Locate Linked File"), tr("The file can't be used."), check.reason);
            return;
        }
        Asset replacement = *old;
        replacement.linkPath = path;
        bool refit = false;
        if (check.state == persistence::LinkState::Changed) {
            QMessageBox box(QMessageBox::Question, tr("Locate Linked File"),
                            tr("“%1” is a different image than the one on the board.")
                                .arg(QFileInfo(path).fileName()),
                            QMessageBox::Yes | QMessageBox::Cancel, this);
            box.setObjectName(u"locateDifferentBox"_s);
            box.setInformativeText(tr("Use it anyway? The images keep their position and width."));
            if (box.exec() != QMessageBox::Yes)
                return;
            replacement.sha256 = check.sha256;
            replacement.mediaType = check.mediaType;
            replacement.orientedSize = check.orientedSize;
            replacement.byteLength = check.byteLength;
            refit = true;
        }
        if (run(ops::replaceAssets(*m_document, {{assetId, replacement, refit}}), tr("Locate Linked File")))
            if (const Asset *now = m_document->assetByDigest(replacement.sha256); now && now->linked)
                setLinkState(now->id, persistence::LinkCheck{persistence::LinkState::Available, {}, {}, {}, {}, 0});
    });
}

void MainWindow::reloadLinks()
{
    finishModes();
    const QList<Id> assets = selectedLinkedAssets();
    QStringList paths;
    for (const Id &id : assets)
        paths.append(m_document->asset(id)->linkPath);
    checkLinks(assets, paths, [this, assets](const QList<persistence::LinkCheck> &checks) {
        int changed = 0;
        for (qsizetype i = 0; i < checks.size() && i < assets.size(); ++i) {
            if (!m_document->asset(assets[i]))
                continue;
            setLinkState(assets[i], checks[i]);
            changed += checks[i].state == persistence::LinkState::Changed;
        }
        if (changed > 0)
            m_view->showToast(tr("%n linked file(s) changed on disk. Use Board ▸ Links ▸ Accept Changed File "
                                 "to show the new version.", nullptr, changed));
    });
}

void MainWindow::acceptLinkChanges()
{
    finishModes();
    QList<ops::AssetReplacement> replacements;
    QList<QByteArray> digests;
    for (const Id &id : selectedLinkedAssets()) {
        const auto it = m_linkStates.constFind(id);
        if (it == m_linkStates.cend() || it->state != persistence::LinkState::Changed)
            continue;
        Asset replacement = *m_document->asset(id);
        replacement.sha256 = it->sha256;
        replacement.mediaType = it->mediaType;
        replacement.orientedSize = it->orientedSize;
        replacement.byteLength = it->byteLength;
        replacements.append({id, replacement, true});
        digests.append(it->sha256);
    }
    if (replacements.isEmpty())
        return;
    if (!run(ops::replaceAssets(*m_document, replacements), tr("Accept Changed File")))
        return;
    for (const QByteArray &sha : std::as_const(digests))
        if (const Asset *now = m_document->assetByDigest(sha); now && now->linked)
            setLinkState(now->id, persistence::LinkCheck{persistence::LinkState::Available, {}, {}, {}, {}, 0});
}

void MainWindow::embedLinks(bool all)
{
    finishModes();
    QList<Id> candidates;
    if (all) {
        for (const Asset &a : m_document->assets())
            if (a.linked)
                candidates.append(a.id);
    } else {
        candidates = selectedLinkedAssets();
    }
    QList<ops::AssetReplacement> replacements;
    int unavailable = 0;
    for (const Id &id : std::as_const(candidates)) {
        if (linkState(id) != persistence::LinkState::Available || !m_services.store()->contains(m_document->asset(id)->sha256)) {
            ++unavailable;
            continue;
        }
        Asset replacement = *m_document->asset(id);
        replacement.linked = false;
        replacement.linkPath.clear();
        replacements.append({id, replacement, false});
    }
    if (!replacements.isEmpty())
        run(ops::replaceAssets(*m_document, replacements),
            replacements.size() == 1 ? tr("Embed Linked Image") : tr("Embed Linked Images"));
    if (unavailable > 0)
        m_view->showToast(tr("%n missing or changed file(s) can't be embedded. Locate or reload them first.",
                             nullptr, unavailable));
}

void MainWindow::showLinkSummary()
{
    QStringList missing;
    QStringList changed;
    for (auto it = m_linkStates.cbegin(); it != m_linkStates.cend(); ++it) {
        const Asset *asset = m_document->asset(it.key());
        if (!asset)
            continue;
        if (it->state == persistence::LinkState::Missing)
            missing.append(asset->linkPath + u" — "_s + it->reason);
        else if (it->state == persistence::LinkState::Changed)
            changed.append(asset->linkPath);
    }
    if (missing.isEmpty() && changed.isEmpty())
        return;
    QStringList summary;
    if (!missing.isEmpty())
        summary.append(tr("%n linked image(s) could not be found.", nullptr, int(missing.size())));
    if (!changed.isEmpty())
        summary.append(tr("%n linked image(s) changed on disk and are not shown until you accept the change.",
                          nullptr, int(changed.size())));
    QStringList details;
    if (!missing.isEmpty())
        details << tr("Missing:") << missing;
    if (!changed.isEmpty())
        details << tr("Changed:") << changed;
    showMessage(tr("Linked Images"),
                summary.join(u' ') + u"\n\n"_s
                    + tr("Select an image and use Board ▸ Links to locate, reload, accept or embed it."),
                details.join(u'\n'));
}

void MainWindow::warnAboutAbsoluteLinks(const persistence::BoardContent &content, const QString &path)
{
    QSet<Id> used;
    for (const ImageItemData &image : content.snapshot.images)
        used.insert(image.assetId);
    int absolute = 0;
    for (const Asset &a : content.snapshot.assets)
        if (a.linked && used.contains(a.id) && !link_paths::toStored(a.linkPath, QFileInfo(path).absolutePath()).relative)
            ++absolute;
    if (absolute > 0)
        m_view->showToast(tr("%n linked image(s) are stored with an absolute path and won't be found if the board "
                             "is moved to another computer.", nullptr, absolute));
}

// --- Clipboard ---------------------------------------------------------------------

void MainWindow::copy()
{
    finishModes();
    const QList<Id> ids = selection();
    if (ids.isEmpty())
        return;
    ClipboardFragment fragment;
    fragment.snapshot = ops::clipboardSnapshot(*m_document, ids);
    qint64 total = 0;
    for (const Asset &asset : std::as_const(fragment.snapshot.assets)) {
        const auto bytes = m_services.store()->read(asset.sha256, m_services.limits().maxAssetBytes);
        if (!bytes) {
            m_view->showToast(tr("An image could not be read from the session store."));
            return;
        }
        total += bytes->size();
        if (total > ClipboardFragment::MaxTotalBytes) {
            m_view->showToast(tr("The selection is too large to copy (over 1 GiB of images)."));
            return;
        }
        fragment.assetBytes.insert(asset.sha256, *bytes);
    }
    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(FragmentMimeType), encodeFragment(fragment));
    QGuiApplication::clipboard()->setMimeData(mime); // clipboard takes ownership
}

void MainWindow::cut()
{
    finishModes();
    const QList<Id> ids = selection();
    for (const Id &id : ids) {
        if (ops::isLocked(*m_document, id)) {
            m_view->showToast(tr("Locked items cannot be cut. Unlock them first."));
            return;
        }
    }
    copy();
    run(ops::remove(*m_document, ids), tr("Cut"));
}

void MainWindow::paste()
{
    finishModes();
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) {
        m_view->showToast(tr("The clipboard is empty."));
        return;
    }
    const QString fragmentType = QString::fromLatin1(FragmentMimeType);
    if (mime->hasFormat(fragmentType)) {
        QString error;
        const auto fragment = decodeFragment(mime->data(fragmentType), m_services.limits(), &error);
        if (!fragment) {
            m_view->showToast(tr("The clipboard content could not be pasted: %1").arg(error));
            return;
        }
        for (auto it = fragment->assetBytes.cbegin(); it != fragment->assetBytes.cend(); ++it) {
            const auto stored = m_services.store()->put(it.value(), &error);
            if (!stored || stored->sha256 != it.key()) {
                m_view->showToast(tr("The pasted images could not be stored: %1").arg(error));
                return;
            }
        }
        const OpResult r = ops::paste(*m_document, fragment->snapshot, m_view->cursorScenePos());
        if (run(r, tr("Paste")))
            m_scene->selectIds(r.created);
        return;
    }
    importPlan(io::routeMimeData(*mime), m_view->cursorScenePos());
}

void MainWindow::copyImage()
{
    finishModes();
    const QList<Id> ids = selection();
    const ImageItemData *image = ids.size() == 1 ? m_document->image(ids.first()) : nullptr;
    const Asset *asset = image ? m_document->asset(image->assetId) : nullptr;
    const auto format = asset ? io::formatFromMediaType(asset->mediaType) : std::nullopt;
    if (!asset || !format)
        return;
    io::ImageInfo info;
    info.format = *format;
    info.orientedSize = asset->orientedSize;
    const QRectF crop = image->crop;
    const Affine2D world = m_document->worldTransform(image->id);
    auto store = m_services.store();
    const Limits limits = m_services.limits();
    const QByteArray sha = asset->sha256;

    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        const QImage result = watcher->result();
        watcher->deleteLater();
        if (result.isNull()) {
            m_view->showToast(tr("The image could not be copied."));
            emit copyImageFinished(false);
            return;
        }
        QGuiApplication::clipboard()->setImage(result);
        emit copyImageFinished(true);
    });
    watcher->setFuture(QtConcurrent::run(m_services.pool(), [store, limits, sha, info, crop, world] {
        const auto bytes = store->read(sha, limits.maxAssetBytes);
        if (!bytes)
            return QImage();
        const QImage decoded = io::decodeImage(*bytes, info, 0, limits);
        return copyImagePixels(decoded, crop, world, io::MaxDecodeSide);
    }));
}

void MainWindow::selectAll()
{
    finishModes();
    m_scene->selectAllSelectable();
}

// --- Dialogs -----------------------------------------------------------------------

void MainWindow::renameGroup()
{
    finishModes();
    const QList<Id> ids = selection();
    Id groupId = ids.size() == 1 && m_document->group(ids.first()) ? ids.first() : m_scene->activeGroup();
    const GroupData *g = m_document->group(groupId);
    if (!g)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Group"), tr("Name:"), QLineEdit::Normal, g->name, &ok);
    if (ok)
        run(ops::renameGroup(*m_document, groupId, name), tr("Rename Group"));
}

void MainWindow::editNoteStyle()
{
    finishModes();
    QList<Id> notes;
    for (const Id &id : selection())
        if (m_document->note(id))
            notes.append(id);
    if (notes.isEmpty())
        return;
    const NoteData *first = m_document->note(notes.first());
    NoteStyleDialog dialog(first->fontSize, first->textColor, first->backgroundColor, this);
    if (dialog.exec() == QDialog::Accepted)
        run(ops::setNoteStyle(*m_document, notes, dialog.fontSize(), dialog.textColor(), dialog.backgroundColor()),
            tr("Note Style"));
}

void MainWindow::chooseBackground()
{
    finishModes();
    const QColor color = QColorDialog::getColor(QColor::fromRgba(m_document->view().backgroundColor), this,
                                                tr("Board Background"));
    if (color.isValid())
        run(ops::setBackground(*m_document, color.rgba()), tr("Board Background"));
}

void MainWindow::chooseArrangeSpacing()
{
    QSettings settings;
    bool ok = false;
    const int spacing = QInputDialog::getInt(this, tr("Arrange Spacing"), tr("Spacing between items:"),
                                             settings.value(ArrangeSpacingKey, DefaultArrangeSpacing).toInt(), 0,
                                             1000, 5, &ok);
    if (ok)
        settings.setValue(ArrangeSpacingKey, spacing);
}

void MainWindow::setAlwaysOnTop(bool enabled)
{
    const bool visible = isVisible();
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    if (visible)
        show(); // changing window flags hides the window
    // Honest status (spec §7): never claim what the compositor may ignore.
    const platform::StayOnTop status = platform::stayOnTopStatus(enabled, m_capabilities);
    if (QAction *onTop = m_actions.value(u"AlwaysOnTop"_s))
        onTop->setToolTip(status == platform::StayOnTop::Requested
                              ? tr("Requested: your compositor decides whether the window stays on top")
                              : QString());
    if (status == platform::StayOnTop::On)
        m_view->showToast(tr("Always on top"));
    else if (status == platform::StayOnTop::Requested)
        m_view->showToast(tr("Always on top requested — your compositor decides"));
    if (status == platform::StayOnTop::Requested && !m_alwaysOnTopNoticeShown) {
        m_alwaysOnTopNoticeShown = true;
        showMessage(tr("Always on top"),
                    tr("On Wayland, the compositor decides window stacking. Glacier Ref asked to stay on top, "
                       "but this cannot be confirmed.\n\nIn KDE Plasma, use the window menu (Alt+F3) → "
                       "More Actions → Keep Above Others, or create a KWin window rule."));
    }
}

// --- Import ------------------------------------------------------------------------

void MainWindow::onImportProgress(quint64 batchId, int done, int total)
{
    QPointer<QProgressDialog> &dialog = m_progress[batchId];
    if (!dialog) {
        dialog = new QProgressDialog(tr("Importing images…"), tr("Cancel"), 0, total, this);
        dialog->setWindowModality(Qt::NonModal);
        dialog->setMinimumDuration(500);
        dialog->setAutoClose(false);
        dialog->setAutoReset(false);
        connect(dialog, &QProgressDialog::canceled, this, [this, batchId] { m_import->cancel(batchId); });
    }
    dialog->setMaximum(total);
    dialog->setValue(done);
}

void MainWindow::onBatchFinished(const io::ImportBatchResult &result)
{
    if (QPointer<QProgressDialog> dialog = m_progress.take(result.batchId))
        retireProgressDialog(dialog.data());
    if (result.documentToken != m_document->documentToken())
        return; // stale: the document was replaced since the batch started

    QList<QSizeF> sizes;
    for (const io::ImportedImage &image : result.images)
        sizes.append(QSizeF(image.orientedSize));
    const QList<QPointF> positions = layoutBatch(sizes, result.anchor);

    QList<NewImage> newImages;
    QStringList notices = result.notices;
    for (qsizetype i = 0; i < result.images.size(); ++i) {
        const io::ImportedImage &image = result.images[i];
        m_services.cache()->registerAsset(image.sha256, image.mediaType, image.orientedSize);
        m_services.cache()->insert(image.sha256, image.thumbnail);
        NewImage newImage{image.sha256, image.mediaType, image.orientedSize, image.byteLength, positions[i], {}, false, {}};
        if (result.linked && !image.sourcePath.isEmpty()) {
            const Asset *existing = m_document->assetByDigest(image.sha256);
            if (existing && !existing->linked) {
                notices.append(image.displayName + u": "_s
                               + tr("the same image is already embedded in the board, so it stays embedded"));
            } else {
                newImage.linked = true;
                newImage.linkPath = link_paths::normalize(image.sourcePath);
            }
        }
        newImages.append(newImage);
        for (const QString &notice : image.notices)
            notices.append(image.displayName + u": "_s + notice);
    }

    const bool wasEmpty = m_document->itemCount() == 0;
    QStringList failures;
    for (const io::ImportFailure &failure : result.failures)
        failures.append(failure.displayName + u": "_s + failure.reason);

    if (!newImages.isEmpty()) {
        finishModes();
        QString error;
        AddImagesResult added;
        const auto change = m_document->addImagesChange(newImages, &added, &error);
        if (!change || !m_controller->run(*change, tr("Import Images"), &error)) {
            failures.prepend(tr("The images could not be added to the board: %1").arg(error));
        } else {
            m_scene->selectIds(added.itemIds);
            if (wasEmpty)
                m_view->fitBoard();
        }
    }

    if (!failures.isEmpty() || !notices.isEmpty()) {
        QString summary;
        if (!failures.isEmpty())
            summary = tr("%n image(s) could not be imported.", nullptr, int(failures.size()));
        if (result.cancelled)
            summary = (summary.isEmpty() ? QString() : summary + u' ') + tr("The import was cancelled.");
        if (summary.isEmpty())
            summary = tr("Some images were imported with notes.");
        showMessage(tr("Import"), summary, (failures + notices).join(u'\n'));
    }
    emit importApplied(result);
}

void MainWindow::showMessage(const QString &title, const QString &text, const QString &details)
{
    auto *box = new QMessageBox(QMessageBox::Information, title, text, QMessageBox::Ok, this);
    box->setTextFormat(Qt::PlainText);
    if (!details.isEmpty())
        box->setDetailedText(details);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open(); // non-blocking
}

// --- Window integration (M4.03, M4.04) -------------------------------------------------

bool MainWindow::isFrameless() const
{
    return windowFlags().testFlag(Qt::FramelessWindowHint);
}

void MainWindow::setFrameless(bool on)
{
    if (on != isFrameless()) {
        const bool visible = isVisible();
        setWindowFlag(Qt::FramelessWindowHint, on);
        if (visible)
            show(); // changing window flags hides the window
    }
    m_frameless->setEnabled(on);
    m_menuButton->raise();
    m_services.settings()->setFrameless(on);
    if (QAction *a = m_actions.value(u"FramelessWindow"_s))
        a->setChecked(on);
}

// --- Preferences (M4.02) ---------------------------------------------------------------

void MainWindow::updateTextEditingPassThrough()
{
    // File commands still work while a note is edited; they commit it first.
    QList<QKeySequence> passThrough;
    for (const QString &id : {u"Save"_s, u"SaveAs"_s, u"Open"_s, u"NewWindow"_s, u"CloseWindow"_s, u"Quit"_s})
        passThrough.append(m_actions.value(id)->shortcuts());
    m_view->setTextEditingPassThrough(passThrough);
}

void MainWindow::applyShortcuts()
{
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it)
        it.value()->setShortcuts(m_services.settings()->shortcuts(it.key(), m_defaultShortcuts.value(it.key())));
    updateTextEditingPassThrough();
}

QList<PreferencesDialog::ShortcutEntry> MainWindow::shortcutEntries() const
{
    QList<PreferencesDialog::ShortcutEntry> entries;
    for (const HelpDialog::ShortcutRow &row : shortcutRows())
        if (m_actions.contains(row.id))
            entries.append({row.id, row.menu, row.command, m_defaultShortcuts.value(row.id), row.shortcuts});
    return entries;
}

void MainWindow::showPreferences()
{
    finishModes();
    PreferencesDialog dialog(m_services.settings(), shortcutEntries(),
                             {QSettings().fileName(), m_services.sessionRoot(), m_services.recoveryRoot(),
                              AppServices::logDirectory()},
                             this);
    dialog.exec();
    m_view->setFocus();
}

// --- Minimal UI and help (M4.01) ----------------------------------------------------

namespace {
std::function<void(const QUrl &)> &urlOpener()
{
    static std::function<void(const QUrl &)> *opener =
        new std::function<void(const QUrl &)>([](const QUrl &url) { QDesktopServices::openUrl(url); });
    return *opener;
}
} // namespace

void MainWindow::setUrlOpenerForTesting(std::function<void(const QUrl &)> opener)
{
    urlOpener() = opener ? std::move(opener) : [](const QUrl &url) { QDesktopServices::openUrl(url); };
}

void MainWindow::openProjectWebsite()
{
    // The only way Glacier Ref reaches the network is through the user's
    // browser, on this explicit click (spec §3, A10).
    urlOpener()(QUrl(app_info::homepageUrl()));
}

void MainWindow::applyMenuBarVisibility(bool visible)
{
    menuBar()->setVisible(visible);
    m_menuButton->setVisible(!visible);
    m_menuButton->raise();
    if (QAction *toggle = m_actions.value(u"ShowMenuBar"_s))
        toggle->setChecked(visible);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!m_firstShowDone) {
        m_firstShowDone = true;
        if (!m_services.settings()->firstRunHintShown()) {
            m_services.settings()->setFirstRunHintShown();
            m_view->showToast(tr("Press Tab or click ≡ for the menu · right-click for commands"), 9000);
        }
    }
}

QList<HelpDialog::ShortcutRow> MainWindow::shortcutRows() const
{
    QList<HelpDialog::ShortcutRow> rows;
    std::function<void(QMenu *, const QString &)> collect = [&](QMenu *menu, const QString &title) {
        for (QAction *a : menu->actions()) {
            if (a->isSeparator())
                continue;
            if (a->menu()) {
                collect(a->menu(), title);
                continue;
            }
            QString text = a->text();
            text.remove(u'&');
            text.remove(u'…');
            rows.append({a->objectName().mid(6), title, text.trimmed(), a->shortcuts()}); // "action<ID>"
        }
    };
    for (QAction *top : menuBar()->actions()) {
        if (!top->menu())
            continue;
        QString title = top->text();
        title.remove(u'&');
        collect(top->menu(), title);
    }
    return rows;
}

void MainWindow::showHelp(HelpDialog::Page page)
{
    finishModes();
    const QString about =
        tr("Glacier Ref %1\nAn offline reference board. MIT license.\n\nQt %2 (%3)\n\n"
           "Glacier Ref is an independent project and is not affiliated with PureRef.\n"
           "It makes no network connections; the button below opens the project page in your browser.")
            .arg(app_info::version(), QString::fromLatin1(qVersion()), QGuiApplication::platformName());
    HelpDialog dialog(shortcutRows(), about, [this] { openProjectWebsite(); }, this);
    dialog.showPage(page);
    dialog.exec();
    m_view->setFocus();
}

QString MainWindow::diagnosticsReport() const
{
    const SettingsService *s = m_services.settings();
    QStringList lines;
    lines << platform::collectPlatformInfo().toText();
    lines << u"Data folders:"_s
          << u"  settings: "_s + QSettings().fileName()
          << u"  sessions: "_s + m_services.sessionRoot()
          << u"  recovery: "_s + m_services.recoveryRoot()
          << u"  logs: "_s + AppServices::logDirectory()
          << u"  current log: "_s + diagnostics::logFilePath();
    lines << u"Settings:"_s
          << u"  theme: "_s + (s->theme() == SettingsService::Theme::System ? u"system"_s : u"glacier"_s)
          << u"  menu bar: "_s + (s->menuBarVisible() ? u"shown"_s : u"hidden"_s)
          << u"  frameless: "_s + (s->frameless() ? u"yes"_s : u"no"_s)
          << u"  image cache: %1 MiB"_s.arg(s->cacheBudgetMiB())
          << u"  recovery: every %1 s, budget %2 MiB"_s.arg(s->recoveryIntervalSeconds()).arg(s->recoveryBudgetMiB())
          << u"  custom shortcuts: %1"_s.arg(s->shortcutOverrides().size());
    QStringList formats;
    for (const QByteArray &f : QImageReader::supportedImageFormats())
        formats.append(QString::fromLatin1(f));
    lines << u"Image plugins: "_s + formats.join(u' ');
    return diagnostics::redact(lines.join(u'\n'));
}

} // namespace glacier
