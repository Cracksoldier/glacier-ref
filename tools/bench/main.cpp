// Glacier Ref benchmark (milestone M1.11, acceptance A11 groundwork).
//
// Imports a dataset folder into a real MainWindow, then drives a scripted
// zoom/pan sequence twice: pass 1 while the image cache is still filling,
// pass 2 with a warm cache. Each frame is a synchronous viewport repaint, timed
// with QElapsedTimer (paint cost, not compositor latency). Reports import time,
// peak/steady RSS (Linux /proc), cache statistics and frame-time percentiles as
// JSON and a short summary.
//
// Then the persistence phase (M3): saves the board, writes a recovery snapshot
// of an edit, and opens the saved board in a new window, each timed together
// with the longest GUI stall (spec §9: no sustained stalls above 200 ms).
//
// M4.07 adds: the longest stall during import, selection and move-preview
// feedback on the whole board (budget under 100 ms), and for the opened board
// the time to the first decoded image and to "usable" (every visible level
// decoded). With --cold the bench pauses before opening, so the page cache can
// be dropped (sudo) for a cold measurement.

#include "AppServices.h"
#include "MainWindow.h"
#include "glacier/canvas/BoardScene.h"
#include "glacier/canvas/BoardView.h"
#include <QMouseEvent>
#include "glacier/canvas/ImageCache.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/editing/CommandController.h"
#include "glacier/editing/Operations.h"
#include "glacier/io/MimeRouter.h"
#include "glacier/platform/PlatformInfo.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUndoStack>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;
using namespace glacier;

namespace {

qint64 procStatusKiB(const char *field)
{
    QFile status(u"/proc/self/status"_s);
    if (!status.open(QIODevice::ReadOnly))
        return -1;
    const QList<QByteArray> lines = status.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith(field))
            return line.mid(qstrlen(field)).trimmed().split(' ').first().toLongLong();
    }
    return -1;
}

QJsonObject percentiles(QList<double> ms)
{
    std::sort(ms.begin(), ms.end());
    auto at = [&ms](double q) {
        if (ms.isEmpty())
            return 0.0;
        const auto i = std::min<qsizetype>(ms.size() - 1, qsizetype(std::ceil(q * double(ms.size()))) - 1);
        return ms[std::max<qsizetype>(0, i)];
    };
    double sum = 0;
    for (double v : ms)
        sum += v;
    return {{u"frames"_s, ms.size()}, {u"p50_ms"_s, at(0.50)}, {u"p95_ms"_s, at(0.95)},
            {u"max_ms"_s, ms.isEmpty() ? 0.0 : ms.last()}, {u"mean_ms"_s, ms.isEmpty() ? 0.0 : sum / double(ms.size())}};
}

void waitFor(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Longest gap between ticks of a 5 ms timer: how long the GUI thread was busy.
class StallMeter
{
public:
    StallMeter()
    {
        m_timer.setInterval(5);
        QObject::connect(&m_timer, &QTimer::timeout, [this] {
            m_longest = std::max(m_longest, double(m_clock.nsecsElapsed()) / 1e6);
            m_clock.restart();
        });
    }
    void start()
    {
        m_longest = 0;
        m_clock.start();
        m_timer.start();
    }
    double stop()
    {
        m_timer.stop();
        return std::max(m_longest, double(m_clock.nsecsElapsed()) / 1e6);
    }

private:
    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_longest = 0;
};

// Runs the event loop until `done` is set.
void until(const bool &done)
{
    while (!done)
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
}

// Zoom/pan script over the board bounds: overview sweeps, then close-ups.
QList<double> runPass(canvas::BoardView *view, const QRectF &bounds, int frames)
{
    QList<double> times;
    times.reserve(frames);
    for (int f = 0; f < frames; ++f) {
        const double t = double(f) / std::max(1, frames - 1);
        const double angle = t * 6.0 * std::numbers::pi;
        const QPointF centre = bounds.center()
                             + QPointF(std::cos(angle) * bounds.width() * 0.35, std::sin(angle) * bounds.height() * 0.35);
        // Zoom oscillates between the overview and roughly 100%.
        const double fitZoom = std::min(view->viewport()->width() / bounds.width(),
                                        view->viewport()->height() / bounds.height());
        const double zoom = fitZoom * std::pow(1.0 / std::max(fitZoom, 1e-3), 0.5 - 0.5 * std::cos(angle * 0.5));
        view->setZoom(zoom, view->viewport()->rect().center());
        view->centerOn(centre);

        QElapsedTimer timer;
        timer.start();
        view->viewport()->repaint();
        times.append(double(timer.nsecsElapsed()) / 1e6);
        QCoreApplication::processEvents(); // deliver finished decodes between frames
    }
    return times;
}

} // namespace

Q_DECLARE_METATYPE(glacier::io::ImportBatchResult)

int main(int argc, char *argv[])
{
    QApplication::setOrganizationName(u"GlacierRef"_s);
    QApplication::setApplicationName(u"GlacierRefBench"_s);
    QApplication app(argc, argv);
    qRegisterMetaType<io::ImportBatchResult>();

    QCommandLineParser parser;
    parser.setApplicationDescription(u"Glacier Ref import and navigation benchmark."_s);
    parser.addHelpOption();
    const QCommandLineOption datasetOption(u"dataset"_s, u"Folder of images to import."_s, u"dir"_s);
    const QCommandLineOption framesOption(u"frames"_s, u"Frames per pass (default 300)."_s, u"n"_s, u"300"_s);
    const QCommandLineOption jsonOption(u"json"_s, u"Write results as JSON to this file."_s, u"file"_s);
    const QCommandLineOption screenOption(u"screen"_s, u"Screen name to run on full screen (e.g. HDMI-A-1)."_s, u"name"_s);
    const QCommandLineOption sizeOption(u"size"_s, u"Window size WxH instead of maximized (e.g. 2560x1440)."_s, u"WxH"_s);
    const QCommandLineOption storeOption(u"store-root"_s, u"Asset store root (default: next to the dataset)."_s, u"dir"_s);
    const QCommandLineOption coldOption(u"cold"_s, u"Pause before opening the saved board, to drop the page cache."_s);
    const QCommandLineOption notesOption(u"notes"_s, u"Notes added beside the images (default 50, spec §9)."_s, u"n"_s, u"50"_s);
    parser.addOptions({datasetOption, framesOption, jsonOption, screenOption, sizeOption, storeOption, coldOption, notesOption});
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
    const QString dataset = parser.value(datasetOption);
    if (dataset.isEmpty() || !QDir(dataset).exists()) {
        err << "--dataset <dir> is required" << Qt::endl;
        return 2;
    }
    // Keep the store on the dataset's filesystem (not a RAM-backed /tmp).
    QTemporaryDir storeDir(parser.isSet(storeOption) ? parser.value(storeOption) + u"/bench-XXXXXX"_s
                                                     : QDir(dataset).absoluteFilePath(u"../.bench-store-XXXXXX"_s));
    if (!storeDir.isValid()) {
        err << "cannot create the asset store directory" << Qt::endl;
        return 2;
    }

    QString error;
    auto services = AppServices::create({storeDir.path(), {}, {}}, &error);
    if (!services) {
        err << "cannot start: " << error << Qt::endl;
        return 2;
    }

    QJsonObject result;
    {
        MainWindow window(*services);
        QScreen *screen = nullptr;
        if (parser.isSet(screenOption)) {
            const auto screens = QGuiApplication::screens();
            for (QScreen *s : screens)
                if (s->name() == parser.value(screenOption))
                    screen = s;
            if (!screen) {
                err << "unknown screen " << parser.value(screenOption) << Qt::endl;
                return 2;
            }
            window.winId(); // create the native window so the screen can be set
            window.windowHandle()->setScreen(screen);
            window.showFullScreen();
        } else if (parser.isSet(sizeOption)) {
            const QStringList parts = parser.value(sizeOption).split(u'x');
            if (parts.size() != 2 || parts[0].toInt() <= 0 || parts[1].toInt() <= 0) {
                err << "--size expects WxH" << Qt::endl;
                return 2;
            }
            window.resize(parts[0].toInt(), parts[1].toInt());
            window.show();
        } else {
            window.showMaximized();
        }
        waitFor(1500); // let the compositor map and size the window

        // Import.
        QElapsedTimer importTimer;
        importTimer.start();
        io::ImportBatchResult batch;
        bool done = false;
        QObject::connect(&window, &MainWindow::importApplied, &window, [&](const io::ImportBatchResult &r) {
            batch = r;
            done = true;
        });
        StallMeter importMeter;
        importMeter.start();
        window.importPlan(io::planForPaths({dataset}), QPointF(0, 0));
        while (!done)
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 100);
        const double importSeconds = double(importTimer.nsecsElapsed()) / 1e9;
        const double importStall = importMeter.stop();
        // The spec's standard dataset has 50 notes beside the images (§9).
        const QRectF imageBounds = window.document()->boundingRect();
        const int notes = parser.value(notesOption).toInt();
        for (int i = 0; i < notes; ++i) {
            const QPointF at(imageBounds.left() + (i % 10) * imageBounds.width() / 10.0,
                             imageBounds.bottom() + 200.0 + (i / 10) * 260.0);
            window.controller()->run(editing::ops::createNote(*window.document(), at,
                                                              u"Reference note %1: lighting, palette, shapes."_s.arg(i + 1)),
                                     u"Note"_s);
        }
        const qint64 rssAfterImport = procStatusKiB("VmRSS:");

        canvas::BoardView *view = window.boardView();
        const QRectF bounds = window.document()->boundingRect();
        view->fitBoard();
        QElapsedTimer firstContent;
        firstContent.start();
        view->viewport()->repaint();
        const double firstPaintMs = double(firstContent.nsecsElapsed()) / 1e6;

        const int frames = parser.value(framesOption).toInt();
        const QList<double> pass1 = runPass(view, bounds, frames);
        // Settle: wait until outstanding decodes are done (bounded).
        for (int i = 0; i < 300; ++i) {
            const auto s = services->cache()->stats();
            if (s.decodesStarted == s.decodesFinished + s.decodesFailed)
                break;
            waitFor(100);
        }
        const QList<double> pass2 = runPass(view, bounds, frames);
        const auto cache = services->cache()->stats();
        const qint64 rssSteady = procStatusKiB("VmRSS:"); // comparable with the M1-M3 records

        // Selection and transform feedback on the whole board (spec §9: < 100 ms).
        view->fitBoard();
        view->viewport()->repaint();
        const QList<Id> all = window.document()->topLevelIdsByZ();
        QElapsedTimer feedback;
        feedback.start();
        window.boardScene()->selectIds(all);
        view->viewport()->repaint();
        const double selectMs = double(feedback.nsecsElapsed()) / 1e6;
        const QPointF grab = view->mapFromScene(window.document()->worldBounds(all.last()).center());
        auto mouse = [view](QEvent::Type type, QPointF pos, Qt::MouseButtons buttons) {
            QMouseEvent e(type, pos, view->viewport()->mapToGlobal(pos), Qt::LeftButton, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(view->viewport(), &e);
        };
        mouse(QEvent::MouseButtonPress, grab, Qt::LeftButton);
        mouse(QEvent::MouseMove, grab + QPointF(6, 0), Qt::LeftButton); // past the drag threshold
        view->viewport()->repaint();
        feedback.restart();
        mouse(QEvent::MouseMove, grab + QPointF(40, 25), Qt::LeftButton);
        view->viewport()->repaint();
        const double moveMs = double(feedback.nsecsElapsed()) / 1e6;
        feedback.restart();
        mouse(QEvent::MouseButtonRelease, grab + QPointF(40, 25), Qt::NoButton);
        view->viewport()->repaint();
        const double commitMs = double(feedback.nsecsElapsed()) / 1e6;
        const bool moved = window.controller()->undoStack()->count() == 1;
        if (moved)
            window.controller()->undoStack()->undo();
        window.boardScene()->clearSelection();

        const QSize viewport = view->viewport()->size();
        result = QJsonObject{
            {u"dataset"_s, QDir(dataset).absolutePath()},
            {u"items"_s, int(window.document()->itemCount())},
            {u"import_failures"_s, int(batch.failures.size())},
            {u"import_seconds"_s, importSeconds},
            {u"import_longest_stall_ms"_s, importStall},
            {u"feedback"_s, QJsonObject{{u"selected"_s, int(all.size())},
                                        {u"select_all_ms"_s, selectMs},
                                        {u"move_preview_ms"_s, moveMs},
                                        {u"move_commit_ms"_s, commitMs},
                                        {u"move_was_one_step"_s, moved}}},
            {u"first_paint_ms"_s, firstPaintMs},
            {u"viewport"_s, u"%1x%2"_s.arg(viewport.width()).arg(viewport.height())},
            {u"device_pixel_ratio"_s, view->devicePixelRatioF()},
            {u"pass1_filling_cache"_s, percentiles(pass1)},
            {u"pass2_warm_cache"_s, percentiles(pass2)},
            {u"rss_after_import_mib"_s, rssAfterImport / 1024.0},
            {u"rss_steady_mib"_s, rssSteady / 1024.0},
            {u"rss_after_feedback_mib"_s, procStatusKiB("VmRSS:") / 1024.0},
            {u"rss_peak_mib"_s, procStatusKiB("VmHWM:") / 1024.0},
            {u"cache"_s, QJsonObject{{u"budget_mib"_s, cache.budgetBytes / 1048576.0},
                                     {u"used_mib"_s, cache.bytesUsed / 1048576.0},
                                     {u"entries"_s, cache.entries},
                                     {u"hits"_s, qint64(cache.hits)},
                                     {u"misses"_s, qint64(cache.misses)},
                                     {u"decodes"_s, qint64(cache.decodesFinished)},
                                     {u"decode_failures"_s, qint64(cache.decodesFailed)}}},
            {u"platform"_s, platform::collectPlatformInfo().toText()},
        };

        // Persistence: save, then a recovery snapshot of an edit.
        StallMeter meter;
        const QString boardPath = QDir(storeDir.path()).filePath(u"bench.glacierref"_s);
        bool saved = false;
        bool saveOk = false;
        QObject::connect(&window, &MainWindow::saveFinished, &window, [&](bool ok) {
            saved = true;
            saveOk = ok;
        });
        QElapsedTimer persistence;
        persistence.start();
        meter.start();
        window.saveAs(boardPath);
        until(saved);
        const double saveStall = meter.stop();
        const double saveSeconds = double(persistence.nsecsElapsed()) / 1e9;

        window.controller()->run(editing::ops::createNote(*window.document(), bounds.center(), u"bench"_s), u"Note"_s);
        bool snapshot = false;
        QObject::connect(&window, &MainWindow::recoverySnapshotWritten, &window, [&](bool) { snapshot = true; });
        persistence.restart();
        meter.start();
        window.writeRecoverySnapshot();
        until(snapshot);
        const double snapshotStall = meter.stop();
        result.insert(u"save"_s, QJsonObject{{u"ok"_s, saveOk},
                                             {u"seconds"_s, saveSeconds},
                                             {u"longest_stall_ms"_s, saveStall},
                                             {u"file_mib"_s, QFileInfo(boardPath).size() / 1048576.0}});
        result.insert(u"recovery_snapshot"_s, QJsonObject{{u"seconds"_s, double(persistence.nsecsElapsed()) / 1e9},
                                                          {u"longest_stall_ms"_s, snapshotStall}});
        window.controller()->undoStack()->undo(); // back to the saved state: no close prompt
    }

    // Open the saved board in a new window (the first one has released it),
    // with an empty image cache, as after a fresh start.
    if (parser.isSet(coldOption)) {
        out << "Saved. For a cold open, drop the page cache in another terminal:\n"
               "  sudo sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches'\n"
               "then press Enter here." << Qt::endl;
        QTextStream(stdin).readLine();
    }
    services->cache()->setBudget(1); // evicts everything
    services->cache()->setBudget(qint64(services->settings()->cacheBudgetMiB()) * 1024 * 1024);
    {
        MainWindow window(*services);
        if (parser.isSet(sizeOption)) {
            const QStringList parts = parser.value(sizeOption).split(u'x');
            window.resize(parts[0].toInt(), parts[1].toInt());
        } else {
            window.resize(1280, 800);
        }
        window.show();
        waitFor(500);
        StallMeter meter;
        double firstImageMs = -1;
        QElapsedTimer sinceOpen;
        QObject::connect(services->cache(), &canvas::ImageCache::levelReady, &window, [&] {
            if (firstImageMs < 0)
                firstImageMs = double(sinceOpen.nsecsElapsed()) / 1e6;
        });
        bool loaded = false;
        bool loadOk = false;
        QObject::connect(&window, &MainWindow::boardLoaded, &window, [&](bool ok) {
            loaded = true;
            loadOk = ok;
        });
        QElapsedTimer timer;
        timer.start();
        sinceOpen.start();
        meter.start();
        window.loadBoard(QDir(storeDir.path()).filePath(u"bench.glacierref"_s));
        until(loaded);
        const double loadSeconds = double(timer.nsecsElapsed()) / 1e9;
        // Usable: the visible board is drawn from decoded levels, none pending.
        window.boardView()->fitBoard();
        double usableMs = -1;
        for (int i = 0; i < 6000 && usableMs < 0; ++i) { // at most 60 s
            window.boardView()->viewport()->repaint(); // requests the levels it needs
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            const auto s = services->cache()->stats();
            if (firstImageMs >= 0 && s.decodesStarted == s.decodesFinished + s.decodesFailed) {
                window.boardView()->viewport()->repaint();
                const auto again = services->cache()->stats();
                if (again.decodesStarted == s.decodesStarted)
                    usableMs = double(sinceOpen.nsecsElapsed()) / 1e6;
            }
            waitFor(10);
        }
        const double stall = meter.stop();
        result.insert(u"open"_s, QJsonObject{{u"ok"_s, loadOk},
                                             {u"cold"_s, parser.isSet(coldOption)},
                                             {u"seconds"_s, loadSeconds},
                                             {u"first_image_ms"_s, firstImageMs},
                                             {u"usable_ms"_s, usableMs},
                                             {u"longest_stall_ms"_s, stall},
                                             {u"items"_s, int(window.document()->itemCount())}});
    }

    const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Indented);
    if (parser.isSet(jsonOption)) {
        QFile file(parser.value(jsonOption));
        if (file.open(QIODevice::WriteOnly))
            file.write(json);
    }
    const QJsonObject warm = result.value(u"pass2_warm_cache"_s).toObject();
    out << "items " << result.value(u"items"_s).toInt() << ", import " << result.value(u"import_seconds"_s).toDouble()
        << " s, warm p50/p95/max " << warm.value(u"p50_ms"_s).toDouble() << "/" << warm.value(u"p95_ms"_s).toDouble()
        << "/" << warm.value(u"max_ms"_s).toDouble() << " ms, RSS steady/peak "
        << result.value(u"rss_steady_mib"_s).toDouble() << "/" << result.value(u"rss_peak_mib"_s).toDouble() << " MiB"
        << Qt::endl;
    const QJsonObject save = result.value(u"save"_s).toObject();
    const QJsonObject open = result.value(u"open"_s).toObject();
    const QJsonObject snapshot = result.value(u"recovery_snapshot"_s).toObject();
    const QJsonObject feedback = result.value(u"feedback"_s).toObject();
    out << "import stall " << result.value(u"import_longest_stall_ms"_s).toDouble() << " ms; select all "
        << feedback.value(u"select_all_ms"_s).toDouble() << " ms, move preview "
        << feedback.value(u"move_preview_ms"_s).toDouble() << " ms, commit "
        << feedback.value(u"move_commit_ms"_s).toDouble() << " ms (" << feedback.value(u"selected"_s).toInt()
        << " items)" << Qt::endl;
    out << "save " << save.value(u"seconds"_s).toDouble() << " s (stall " << save.value(u"longest_stall_ms"_s).toDouble()
        << " ms, " << save.value(u"file_mib"_s).toDouble() << " MiB), snapshot " << snapshot.value(u"seconds"_s).toDouble()
        << " s (stall " << snapshot.value(u"longest_stall_ms"_s).toDouble() << " ms), open "
        << open.value(u"seconds"_s).toDouble() << " s (stall " << open.value(u"longest_stall_ms"_s).toDouble()
        << " ms, " << open.value(u"items"_s).toInt() << " items), first image "
        << open.value(u"first_image_ms"_s).toDouble() << " ms, usable " << open.value(u"usable_ms"_s).toDouble()
        << " ms" << (open.value(u"cold"_s).toBool() ? " (cold)" : "") << Qt::endl;
    if (!parser.isSet(jsonOption))
        out << json;
    return 0;
}
