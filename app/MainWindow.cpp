#include "MainWindow.h"

#include "AppServices.h"
#include "NoteStyleDialog.h"
#include "glacier/canvas/BoardScene.h"
#include "glacier/canvas/BoardView.h"
#include "glacier/canvas/ImageCache.h"
#include "glacier/core/AppInfo.h"
#include "glacier/core/BatchLayout.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/core/SessionAssetStore.h"
#include "glacier/editing/ClipboardFragment.h"
#include "glacier/editing/CommandController.h"
#include "glacier/editing/Geometry.h"
#include "glacier/editing/Operations.h"
#include "glacier/io/ImageFormats.h"
#include "glacier/io/MimeRouter.h"
#include "glacier/platform/PlatformInfo.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QSettings>
#include <QtConcurrent/QtConcurrentRun>

using namespace Qt::StringLiterals;
using namespace glacier::editing;

namespace glacier {

namespace {

const QString ArrangeSpacingKey = u"layout/arrangeSpacing"_s;
constexpr int DefaultArrangeSpacing = 20;

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
    resize(1280, 800);
    createActions();
    updateTitle();

    connect(m_view, &canvas::BoardView::dropReceived, this, [this](const QMimeData *mime, QPointF scenePos) {
        importPlan(io::routeMimeData(*mime), scenePos);
    });
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

void MainWindow::updateTitle()
{
    setWindowTitle(tr("Untitled[*] — %1").arg(app_info::displayName()));
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
    if (!shortcuts.isEmpty())
        action->setShortcuts(shortcuts);
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
    file->addSeparator();
    addAction(file, u"ImportImages"_s, tr("&Import Images…"), {key(Qt::CTRL | Qt::Key_I)});
    on(u"ImportImages"_s, &MainWindow::importImagesFromDialog);
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

    // Help
    QMenu *help = menuBar()->addMenu(tr("&Help"));
    QAction *about = addAction(help, u"About"_s, tr("&About Glacier Ref"));
    about->setMenuRole(QAction::AboutRole);
    on(u"About"_s, &MainWindow::showAbout);
    QAction *aboutQt = addAction(help, u"AboutQt"_s, tr("About &Qt"));
    aboutQt->setMenuRole(QAction::AboutQtRole);
    connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
}

QMenu *MainWindow::createContextMenu(QWidget *parent)
{
    updateActionStates();
    auto *menu = new QMenu(parent);
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
    const FlipState flip = flipState(m_document->worldTransform(image->id));
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
    watcher->setFuture(QtConcurrent::run(m_services.pool(), [store, limits, sha, info, crop, flip] {
        const auto bytes = store->read(sha, limits.maxAssetBytes);
        if (!bytes)
            return QImage();
        QImage decoded = io::decodeImage(*bytes, info, 0, limits);
        if (decoded.isNull())
            return decoded;
        const QRect pixels = QRectF(crop.x() * decoded.width(), crop.y() * decoded.height(),
                                    crop.width() * decoded.width(), crop.height() * decoded.height())
                                 .toAlignedRect()
                                 .intersected(decoded.rect());
        QImage result = decoded.copy(pixels);
        Qt::Orientations flips;
        if (flip.horizontal)
            flips |= Qt::Horizontal;
        if (flip.vertical)
            flips |= Qt::Vertical;
        return flips ? result.flipped(flips) : result;
    }));
}

void MainWindow::selectAll()
{
    finishModes();
    const auto items = m_scene->items();
    for (QGraphicsItem *item : items)
        if (item->flags().testFlag(QGraphicsItem::ItemIsSelectable))
            item->setSelected(true);
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
    setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    show(); // changing window flags hides the window
    if (enabled && QGuiApplication::platformName() == u"wayland"_s && !m_alwaysOnTopNoticeShown) {
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
        dialog->deleteLater();
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
        newImages.append({image.sha256, image.mediaType, image.orientedSize, image.byteLength, positions[i], {}});
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

void MainWindow::showAbout()
{
    const platform::PlatformInfo info = platform::collectPlatformInfo();
    QMessageBox box(this);
    box.setWindowTitle(tr("About Glacier Ref"));
    box.setTextFormat(Qt::PlainText);
    box.setText(tr("Glacier Ref %1\n\nAn offline reference board.\n%2")
                    .arg(app_info::version(), app_info::homepageUrl()));
    box.setDetailedText(info.toText());
    box.exec();
}

} // namespace glacier
