#pragma once

#include "glacier/io/ImportService.h"

#include <QHash>
#include <QKeySequence>
#include <QMainWindow>
#include <QPointer>

class QAction;
class QMenu;
class QProgressDialog;

namespace glacier {

class AppServices;
class BoardDocument;

namespace canvas {
class BoardScene;
class BoardView;
} // namespace canvas

namespace editing {
class CommandController;
struct OpResult;
} // namespace editing

// Top-level window. Each window owns one board document with its undo
// history, scene, view, selection and import service (spec §3, §6).
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

    // Starts importing a plan, placing results around `anchor` (board coords).
    void importPlan(const io::ImportPlan &plan, QPointF anchor);
    // Builds the context menu for the current selection (also used by tests).
    QMenu *createContextMenu(QWidget *parent);

signals:
    // Emitted after a finished batch was applied (tests, benchmarks).
    void importApplied(const glacier::io::ImportBatchResult &result);
    void copyImageFinished(bool ok);

private:
    QAction *addAction(QMenu *menu, const QString &id, const QString &text, const QList<QKeySequence> &shortcuts = {});
    void createActions();
    void updateActionStates();
    void updateTitle();
    bool run(const editing::OpResult &result, const QString &text);
    void finishModes(); // commits text editing and crop before other commands

    void newWindow();
    void importImagesFromDialog();
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
};

} // namespace glacier
