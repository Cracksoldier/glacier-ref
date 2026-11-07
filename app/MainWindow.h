#pragma once

#include "glacier/editing/CommandController.h"
#include "glacier/io/ImportService.h"
#include "glacier/persistence/BoardSerializer.h"
#include "glacier/persistence/DocumentLock.h"
#include "glacier/persistence/RecoveryStore.h"

#include <QHash>
#include <QKeySequence>
#include <QMainWindow>
#include <QPointer>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QAction;
class QMenu;
class QProgressDialog;
class QTimer;
class QMimeData;

namespace glacier {

class AppServices;
class BoardDocument;

namespace canvas {
class BoardScene;
class BoardView;
} // namespace canvas


// Top-level window. Each window owns one board document with its undo
// history, scene, view, selection and import service (spec §3, §6), and the
// board's file: path, document lock, read-only state and save state (§5).
//
// All commands are QActions registered by ID (spec §3: menu commands are the
// canonical action registry); menus, the canvas context menu and shortcuts
// invoke the same actions, and their enabled state follows the selection.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AppServices &services, QWidget *parent = nullptr);
    ~MainWindow() override;

    BoardDocument *document() const { return m_document; }
    editing::CommandController *controller() const { return m_controller; }
    canvas::BoardScene *boardScene() const { return m_scene; }
    canvas::BoardView *boardView() const { return m_view; }
    io::ImportService *importService() const { return m_import; }

    QAction *action(const QString &id) const { return m_actions.value(id); }
    QStringList actionIds() const { return m_actions.keys(); }

    // --- Board files ---------------------------------------------------------
    QString filePath() const { return m_filePath; } // empty: untitled
    bool isReadOnly() const { return m_readOnly; }
    bool isSaving() const { return m_saving; }
    bool isLoading() const { return m_loading; }
    // Untitled, empty and unchanged: Open loads into this window.
    bool isReusable() const;

    // Opens a board (spec §3): activates the window that already shows it,
    // reuses `current` if it is reusable, or opens a new window. Loading is
    // asynchronous; the returned window emits boardLoaded().
    static MainWindow *openBoard(AppServices &services, const QString &path, MainWindow *current);
    // Loads a board into this window (asynchronous).
    void loadBoard(const QString &path);
    // Opens a board from recovery data (spec §5): dirty, linked to its
    // original file if it had one. Reuses `current` if it is reusable.
    static MainWindow *recoverBoard(AppServices &services, const persistence::RecoveryStore::Entry &entry,
                                    MainWindow *current);
    void loadRecovery(const persistence::RecoveryStore::Entry &entry);
    // Writes a recovery snapshot now if the board changed since the last one
    // (normally run by the recovery timer).
    void writeRecoverySnapshot();
    Id recoveryKey() const { return m_recoveryKey; }

    // Save: to the current file; untitled or read-only boards ask for a file.
    void save(std::function<void(bool)> done = {});
    // Save As without a dialog.
    void saveAs(const QString &path, std::function<void(bool)> done = {});

    // State of a linked asset's file (Available when not known otherwise).
    persistence::LinkState linkState(const Id &assetId) const;
    // Locate without the file dialog: links the selected image to `file`.
    void locateLinkTo(const QString &file);

    // Starts importing a plan, placing results around `anchor` (board coords).
    void importPlan(const io::ImportPlan &plan, QPointF anchor);
    // Builds the context menu for the current selection (also used by tests).
    QMenu *createContextMenu(QWidget *parent);

signals:
    // Emitted after a finished batch was applied (tests, benchmarks).
    void importApplied(const glacier::io::ImportBatchResult &result);
    void copyImageFinished(bool ok);
    void boardLoaded(bool ok);
    void saveFinished(bool ok);
    void linksChecked(); // a Reload or Locate check finished (tests)
    void recoverySnapshotWritten(bool ok);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QAction *addAction(QMenu *menu, const QString &id, const QString &text, const QList<QKeySequence> &shortcuts = {});
    void createActions();
    void updateActionStates();
    void updateTitle();
    bool run(const editing::OpResult &result, const QString &text);
    void finishModes(); // commits text editing and crop before other commands

    void newWindow();
    void openFromDialog();
    void saveAsFromDialog(std::function<void(bool)> done);
    void startSave(const QString &path, std::shared_ptr<persistence::DocumentLock> newLock,
                   std::function<void(bool)> done);
    void finishSave(const persistence::SaveResult &result, const QString &path,
                    const persistence::BoardContent &content, editing::CommandController::Marker marker,
                    std::shared_ptr<persistence::DocumentLock> newLock, std::function<void(bool)> done);
    void applyLoaded(const QString &path, persistence::LoadResult result,
                     std::shared_ptr<persistence::DocumentLock::Result> lock,
                     std::optional<persistence::RecoveryStore::Entry> recovered = {});
    void scheduleRecoverySnapshot();
    void discardRecovery(); // the board is clean or was closed: drop its snapshots
    void prepareToClose();
    void showReadOnlyNotice(const persistence::DocumentLock::Holder &holder, const QString &reason);
    void editBoardTitle();
    void handleDrop(const QMimeData *mime, QPointF scenePos);
    persistence::BoardContent currentContent() const;
    QByteArray renderPreview() const;
    QString displayFileName() const;
    void importImagesFromDialog();
    void importLinkedFromDialog();
    // Linked assets (spec §4, M3.07-M3.08).
    void setLinkState(const Id &assetId, const persistence::LinkCheck &check);
    QList<Id> selectedLinkedAssets() const;
    void checkLinks(const QList<Id> &assetIds, const QStringList &paths,
                    std::function<void(QList<persistence::LinkCheck>)> done);
    void locateLink();
    void reloadLinks();
    void acceptLinkChanges();
    void embedLinks(bool all);
    void showLinkSummary();
    void warnAboutAbsoluteLinks(const persistence::BoardContent &content, const QString &path);
    void importFolderFromDialog();
    void cut();
    void copy();
    void copyImage();
    void paste();
    void selectAll();
    void renameGroup();
    void editNoteStyle();
    void chooseBackground();
    void chooseArrangeSpacing();
    void setAlwaysOnTop(bool enabled);
    void onImportProgress(quint64 batchId, int done, int total);
    void onBatchFinished(const io::ImportBatchResult &result);
    void showMessage(const QString &title, const QString &text, const QString &details = {});
    void showAbout();
    QList<glacier::Id> selection() const;

    AppServices &m_services;
    BoardDocument *m_document = nullptr;                // owned via QObject parent
    editing::CommandController *m_controller = nullptr; // owned via QObject parent
    canvas::BoardScene *m_scene = nullptr;              // owned via QObject parent
    canvas::BoardView *m_view = nullptr;                // owned via QObject parent
    io::ImportService *m_import = nullptr;              // owned via QObject parent
    QHash<QString, QAction *> m_actions;                // owned via QObject parent
    QHash<quint64, QPointer<QProgressDialog>> m_progress;
    bool m_alwaysOnTopNoticeShown = false;

    QString m_filePath;
    std::shared_ptr<persistence::DocumentLock> m_lock; // null: no lock (untitled or read-only)
    bool m_readOnly = false;
    persistence::FileFingerprint m_fingerprint; // at the last load or save
    bool m_saving = false;
    bool m_loading = false;
    bool m_closeWhenSaved = false;
    bool m_closeConfirmed = false;
    bool m_closeIfLoadFails = false; // a window opened just for loading a board
    QHash<Id, persistence::LinkCheck> m_linkStates; // linked assets; Available when absent

    // Recovery snapshots (spec §5, M3.09).
    QTimer *m_recoveryTimer = nullptr;
    Id m_recoveryKey = Id::generate(); // this window's snapshots
    bool m_snapshotInFlight = false;
    bool m_discardAfterSnapshot = false;
    std::optional<quint64> m_snapshotRevision; // document revision of the last snapshot
    std::optional<persistence::RecoveryStore::Entry> m_recoveredFrom; // dropped once replaced
    std::shared_ptr<std::atomic_bool> m_recoveryClosed = std::make_shared<std::atomic_bool>(false);
};

} // namespace glacier
