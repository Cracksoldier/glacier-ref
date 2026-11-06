#pragma once

#include "glacier/io/ImportService.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>

class QProgressDialog;

namespace glacier {

class AppServices;
class BoardDocument;

namespace canvas {
class BoardScene;
class BoardView;
} // namespace canvas

// Top-level window. Each window owns one board document, its scene and view,
// and its import service (spec §3, §6).
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AppServices &services, QWidget *parent = nullptr);

    BoardDocument *document() const { return m_document; }
    canvas::BoardScene *boardScene() const { return m_scene; }
    canvas::BoardView *boardView() const { return m_view; }
    io::ImportService *importService() const { return m_import; }

    // Starts importing a plan, placing results around `anchor` (board coords).
    void importPlan(const io::ImportPlan &plan, QPointF anchor);

signals:
    // Emitted after a finished batch was applied (tests, benchmarks).
    void importApplied(const glacier::io::ImportBatchResult &result);

private:
    void createActions();
    void importImagesFromDialog();
    void importFolderFromDialog();
    void pasteFromClipboard();
    void selectAll();
    void setAlwaysOnTop(bool enabled);
    void onImportProgress(quint64 batchId, int done, int total);
    void onBatchFinished(const io::ImportBatchResult &result);
    void showMessage(const QString &title, const QString &text, const QString &details = {});
    void showAbout();

    AppServices &m_services;
    BoardDocument *m_document = nullptr;     // owned via QObject parent
    canvas::BoardScene *m_scene = nullptr;   // owned via QObject parent
    canvas::BoardView *m_view = nullptr;     // owned via QObject parent
    io::ImportService *m_import = nullptr;   // owned via QObject parent
    QHash<quint64, QPointer<QProgressDialog>> m_progress;
    bool m_alwaysOnTopNoticeShown = false;
};

} // namespace glacier
