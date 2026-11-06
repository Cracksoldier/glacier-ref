#include "MainWindow.h"

#include "AppServices.h"
#include "glacier/canvas/BoardScene.h"
#include "glacier/canvas/BoardView.h"
#include "glacier/canvas/ImageCache.h"
#include "glacier/canvas/ImageItem.h"
#include "glacier/core/AppInfo.h"
#include "glacier/core/BatchLayout.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/io/ImageFormats.h"
#include "glacier/io/MimeRouter.h"
#include "glacier/platform/PlatformInfo.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QGuiApplication>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace glacier {

namespace {

QAction *makeAction(QMenu *menu, const QString &text, const QString &objectName, const QKeySequence &shortcut = {})
{
    QAction *action = menu->addAction(text);
    action->setObjectName(objectName);
    if (!shortcut.isEmpty())
        action->setShortcut(shortcut);
    return action;
}

} // namespace

MainWindow::MainWindow(AppServices &services, QWidget *parent)
    : QMainWindow(parent)
    , m_services(services)
    , m_document(new BoardDocument(services.limits(), this))
    , m_scene(new canvas::BoardScene(m_document, services.cache(), this))
    , m_view(new canvas::BoardView(this))
    , m_import(new io::ImportService(services.store(), services.pool(), services.limits(), this))
{
    m_view->setBoardScene(m_scene);
    setCentralWidget(m_view);
    setWindowTitle(app_info::displayName());
    resize(1280, 800);
    createActions();

    connect(m_view, &canvas::BoardView::dropReceived, this, [this](const QMimeData *mime, QPointF scenePos) {
        importPlan(io::routeMimeData(*mime), scenePos);
    });
    connect(m_import, &io::ImportService::progress, this, &MainWindow::onImportProgress);
    connect(m_import, &io::ImportService::batchFinished, this, &MainWindow::onBatchFinished);
}

void MainWindow::createActions()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *importImages = makeAction(fileMenu, tr("&Import Images…"), u"actionImportImages"_s,
                                      QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(importImages, &QAction::triggered, this, &MainWindow::importImagesFromDialog);
    QAction *importFolder = makeAction(fileMenu, tr("Import &Folder…"), u"actionImportFolder"_s);
    connect(importFolder, &QAction::triggered, this, &MainWindow::importFolderFromDialog);
    fileMenu->addSeparator();
    // Explicit default (spec §3): the platform's QKeySequence::Quit is empty on
    // Windows and on some Linux platform themes. Ctrl maps to Cmd on macOS.
    QAction *quit = makeAction(fileMenu, tr("&Quit"), u"actionQuit"_s, QKeySequence(Qt::CTRL | Qt::Key_Q));
    quit->setMenuRole(QAction::QuitRole);
    connect(quit, &QAction::triggered, qApp, &QApplication::closeAllWindows);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *paste = makeAction(editMenu, tr("&Paste"), u"actionPaste"_s, QKeySequence(Qt::CTRL | Qt::Key_V));
    connect(paste, &QAction::triggered, this, &MainWindow::pasteFromClipboard);
    QAction *all = makeAction(editMenu, tr("Select &All"), u"actionSelectAll"_s, QKeySequence(Qt::CTRL | Qt::Key_A));
    connect(all, &QAction::triggered, this, &MainWindow::selectAll);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *fitBoard = makeAction(viewMenu, tr("Fit &Board"), u"actionFitBoard"_s, QKeySequence(Qt::Key_F));
    connect(fitBoard, &QAction::triggered, m_view, &canvas::BoardView::fitBoard);
    QAction *fitSelection = makeAction(viewMenu, tr("Fit &Selection"), u"actionFitSelection"_s,
                                      QKeySequence(Qt::SHIFT | Qt::Key_F));
    connect(fitSelection, &QAction::triggered, m_view, &canvas::BoardView::fitSelection);
    viewMenu->addSeparator();
    QAction *zoomIn = makeAction(viewMenu, tr("Zoom &In"), u"actionZoomIn"_s);
    zoomIn->setShortcuts(QKeySequence::ZoomIn);
    connect(zoomIn, &QAction::triggered, m_view, &canvas::BoardView::zoomIn);
    QAction *zoomOut = makeAction(viewMenu, tr("Zoom &Out"), u"actionZoomOut"_s);
    zoomOut->setShortcuts(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, m_view, &canvas::BoardView::zoomOut);
    QAction *zoomReset = makeAction(viewMenu, tr("Zoom &100%"), u"actionZoomReset"_s, QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(zoomReset, &QAction::triggered, m_view, &canvas::BoardView::resetZoom);
    viewMenu->addSeparator();
    QAction *onTop = makeAction(viewMenu, tr("Always on &Top"), u"actionAlwaysOnTop"_s, QKeySequence(Qt::CTRL | Qt::Key_T));
    onTop->setCheckable(true);
    connect(onTop, &QAction::toggled, this, &MainWindow::setAlwaysOnTop);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *about = makeAction(helpMenu, tr("&About Glacier Ref"), u"actionAbout"_s);
    about->setMenuRole(QAction::AboutRole);
    connect(about, &QAction::triggered, this, &MainWindow::showAbout);
    QAction *aboutQt = makeAction(helpMenu, tr("About &Qt"), u"actionAboutQt"_s);
    aboutQt->setMenuRole(QAction::AboutQtRole);
    connect(aboutQt, &QAction::triggered, qApp, &QApplication::aboutQt);
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

void MainWindow::pasteFromClipboard()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime) {
        showMessage(tr("Nothing pasted"), tr("The clipboard is empty."));
        return;
    }
    importPlan(io::routeMimeData(*mime), m_view->viewCenter());
}

void MainWindow::selectAll()
{
    const auto items = m_scene->items();
    for (QGraphicsItem *item : items)
        item->setSelected(true);
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
        QString error;
        const auto added = m_document->addImages(newImages, &error);
        if (!added) {
            failures.prepend(tr("The images could not be added to the board: %1").arg(error));
        } else {
            m_scene->clearSelection();
            for (const Id &id : added->itemIds) {
                if (canvas::ImageItem *item = m_scene->itemFor(id))
                    item->setSelected(true);
            }
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
