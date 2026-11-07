// Glacier Ref benchmark (milestone M1.11, acceptance A11 groundwork).
//
// Imports a dataset folder into a real MainWindow, then drives a scripted
// zoom/pan sequence twice: pass 1 while the image cache is still filling,
// pass 2 with a warm cache. Each frame is a synchronous viewport repaint, timed
// with QElapsedTimer (paint cost, not compositor latency). Reports import time,
// peak/steady RSS (Linux /proc), cache statistics and frame-time percentiles as
// JSON and a short summary.

#include "AppServices.h"
#include "MainWindow.h"
#include "glacier/canvas/BoardScene.h"
#include "glacier/canvas/BoardView.h"
#include "glacier/canvas/ImageCache.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/io/MimeRouter.h"
#include "glacier/platform/PlatformInfo.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
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
    parser.addOptions({datasetOption, framesOption, jsonOption, screenOption, sizeOption, storeOption});
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
        window.importPlan(io::planForPaths({dataset}), QPointF(0, 0));
        while (!done)
            QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 100);
        const double importSeconds = double(importTimer.nsecsElapsed()) / 1e9;
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

        const QSize viewport = view->viewport()->size();
        result = QJsonObject{
            {u"dataset"_s, QDir(dataset).absolutePath()},
            {u"items"_s, int(window.document()->itemCount())},
            {u"import_failures"_s, int(batch.failures.size())},
            {u"import_seconds"_s, importSeconds},
            {u"first_paint_ms"_s, firstPaintMs},
            {u"viewport"_s, u"%1x%2"_s.arg(viewport.width()).arg(viewport.height())},
            {u"device_pixel_ratio"_s, view->devicePixelRatioF()},
            {u"pass1_filling_cache"_s, percentiles(pass1)},
            {u"pass2_warm_cache"_s, percentiles(pass2)},
            {u"rss_after_import_mib"_s, rssAfterImport / 1024.0},
            {u"rss_steady_mib"_s, procStatusKiB("VmRSS:") / 1024.0},
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
    if (!parser.isSet(jsonOption))
        out << json;
    return 0;
}
