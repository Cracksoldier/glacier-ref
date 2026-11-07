// A04 (M3.07-M3.08): linked assets survive Save As rebasing; missing and
// changed files show placeholders and can be located, reloaded, accepted or
// embedded; changed content is never accepted silently.
#include "GlacierTestMain.h"
#include "TestImages.h"

#include "AppServices.h"
#include "MainWindow.h"
#include "glacier/canvas/BoardItems.h"
#include "glacier/canvas/BoardScene.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/core/LinkPaths.h"
#include "glacier/editing/CommandController.h"
#include "glacier/io/MimeRouter.h"
#include "glacier/persistence/BoardSerializer.h"
#include "glacier/persistence/ZipArchive.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>

using namespace Qt::StringLiterals;
using namespace glacier;
using persistence::LinkState;

Q_DECLARE_METATYPE(glacier::io::ImportBatchResult)

class TestLinks : public QObject
{
    Q_OBJECT

    QTemporaryDir m_root;
    std::unique_ptr<AppServices> m_services;

    static bool writeImage(const QString &path, QColor color, QSize size)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(color);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(test::encode(image, "png")) > 0;
    }

    static void answer(const QString &objectName, QMessageBox::StandardButton button)
    {
        auto *timer = new QTimer;
        timer->setInterval(20);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, objectName, button] {
            const auto widgets = QApplication::topLevelWidgets();
            for (QWidget *w : widgets) {
                auto *box = qobject_cast<QMessageBox *>(w);
                if (box && box->objectName() == objectName && box->isVisible()) {
                    timer->stop();
                    timer->deleteLater();
                    box->button(button)->click();
                    return;
                }
            }
        });
        timer->start();
    }

    // Closes informational message boxes (link summaries) left open.
    static void closeMessages()
    {
        const auto widgets = QApplication::topLevelWidgets();
        for (QWidget *w : widgets)
            if (auto *box = qobject_cast<QMessageBox *>(w))
                box->close();
    }

    static bool importLinked(MainWindow &window, const QStringList &files)
    {
        io::ImportPlan plan = io::planForPaths(files);
        plan.linked = true;
        QSignalSpy applied(&window, &MainWindow::importApplied);
        window.importPlan(plan, QPointF(0, 0));
        return applied.wait(30000);
    }

    static bool save(MainWindow &window, const QString &path)
    {
        QSignalSpy finished(&window, &MainWindow::saveFinished);
        window.saveAs(path);
        return finished.wait(30000) && finished.first().first().toBool();
    }

    std::unique_ptr<MainWindow> open(const QString &path)
    {
        auto window = std::make_unique<MainWindow>(*m_services);
        QSignalSpy loaded(window.get(), &MainWindow::boardLoaded);
        window->loadBoard(path);
        if (!loaded.wait(30000) || !loaded.first().first().toBool())
            return nullptr;
        closeMessages();
        return window;
    }

    // The "path" of the linked asset in a board file's manifest.
    static QString storedLinkPath(const QString &board)
    {
        QFile file(board);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const persistence::ZipReader reader(&file);
        const auto entry = reader.find(u"manifest.json"_s);
        const auto json = entry ? reader.read(*entry, 1 << 20) : std::nullopt;
        if (!json)
            return {};
        for (const QJsonValue &v : QJsonDocument::fromJson(*json).object().value(u"assets"_s).toArray())
            if (v.toObject().value(u"kind"_s).toString() == u"linked"_s)
                return v.toObject().value(u"path"_s).toString();
        return {};
    }

    static const Asset &onlyAsset(MainWindow &window)
    {
        return *window.document()->assets().cbegin();
    }

    static const ImageItemData &onlyImage(MainWindow &window)
    {
        return *window.document()->images().cbegin();
    }

    static QString placeholder(MainWindow &window)
    {
        return window.boardScene()->imageItem(onlyImage(window).id)->placeholderLabel();
    }

    static bool waitForCheck(MainWindow &window, QAction *action)
    {
        QSignalSpy checked(&window, &MainWindow::linksChecked);
        action->trigger();
        return checked.wait(30000);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qRegisterMetaType<io::ImportBatchResult>();
        m_services = AppServices::create({m_root.path(), {}});
        QVERIFY(m_services);
    }

    void importAsLinkAndSaveAsRebasing()
    {
        QTemporaryDir dir;
        const QString image = dir.filePath(u"pics/ref one.png"_s);
        QVERIFY(writeImage(image, Qt::red, {40, 30}));
        auto window = std::make_unique<MainWindow>(*m_services);
        QVERIFY(importLinked(*window, {image}));
        QCOMPARE(window->document()->itemCount(), 1);
        QVERIFY(onlyAsset(*window).linked);
        QCOMPARE(onlyAsset(*window).linkPath, link_paths::normalize(image));

        // First save: relative to the board.
        const QString first = dir.filePath(u"boards/a.glacierref"_s);
        QVERIFY(QDir().mkpath(dir.filePath(u"boards"_s)));
        QVERIFY(save(*window, first));

        // Save As into a deeper folder: the stored path is rebased.
        const QString second = dir.filePath(u"archive/2026/b.glacierref"_s);
        QVERIFY(QDir().mkpath(dir.filePath(u"archive/2026"_s)));
        QVERIFY(save(*window, second));
        window.reset(); // releases the board's lock
        const auto reopened = open(second);
        QVERIFY(reopened);
        QCOMPARE(onlyAsset(*reopened).linkPath, link_paths::normalize(image));
        QCOMPARE(reopened->linkState(onlyAsset(*reopened).id), LinkState::Available);
        QVERIFY(placeholder(*reopened).isEmpty());

        // The manifests hold relative paths.
        for (const auto &[board, expected] : {std::pair{first, u"../pics/ref one.png"_s},
                                              std::pair{second, u"../../pics/ref one.png"_s}}) {
            QCOMPARE(storedLinkPath(board), expected);
        }
    }

    void missingFileShowsPlaceholderAndCanBeLocated()
    {
        QTemporaryDir dir;
        const QString image = dir.filePath(u"ref.png"_s);
        QVERIFY(writeImage(image, Qt::blue, {40, 30}));
        const QString board = dir.filePath(u"board.glacierref"_s);
        {
            MainWindow window(*m_services);
            QVERIFY(importLinked(window, {image}));
            QVERIFY(save(window, board));
        }
        const QString moved = dir.filePath(u"moved/ref.png"_s);
        QVERIFY(QDir().mkpath(dir.filePath(u"moved"_s)));
        QVERIFY(QFile::rename(image, moved));

        const auto window = open(board);
        QVERIFY(window);
        const Id missing = onlyAsset(*window).id;
        QCOMPARE(window->linkState(missing), LinkState::Missing);
        QVERIFY(placeholder(*window).contains(u"ref.png"_s));
        QCOMPARE(onlyImage(*window).displaySize, QSizeF(40, 30)); // keeps its size
        QVERIFY(!window->action(u"EmbedLink"_s)->isEnabled());     // nothing to embed yet

        window->boardScene()->selectIds({onlyImage(*window).id});
        QVERIFY(window->action(u"LocateLink"_s)->isEnabled());
        QSignalSpy checked(window.get(), &MainWindow::linksChecked);
        window->locateLinkTo(moved);
        QVERIFY(checked.wait(30000));
        QCOMPARE(onlyAsset(*window).linkPath, link_paths::normalize(moved));
        QCOMPARE(window->linkState(onlyAsset(*window).id), LinkState::Available);
        QVERIFY(placeholder(*window).isEmpty());
        QVERIFY(window->controller()->isDirty());

        // Undo brings back the missing link and its placeholder.
        window->action(u"Undo"_s)->trigger();
        QCOMPARE(onlyAsset(*window).id, missing);
        QVERIFY(!placeholder(*window).isEmpty());
        window->action(u"Redo"_s)->trigger();
        QVERIFY(placeholder(*window).isEmpty());
    }

    void changedFileIsNeverAcceptedSilently()
    {
        QTemporaryDir dir;
        const QString image = dir.filePath(u"ref.png"_s);
        QVERIFY(writeImage(image, Qt::green, {40, 30}));
        const QString board = dir.filePath(u"board.glacierref"_s);
        QByteArray original;
        {
            MainWindow window(*m_services);
            QVERIFY(importLinked(window, {image}));
            original = onlyAsset(window).sha256;
            QVERIFY(save(window, board));
        }
        QVERIFY(writeImage(image, Qt::yellow, {80, 30})); // edited elsewhere: new size too

        const auto window = open(board);
        QVERIFY(window);
        const Id assetId = onlyAsset(*window).id;
        QCOMPARE(window->linkState(assetId), LinkState::Changed);
        QCOMPARE(onlyAsset(*window).sha256, original); // the board still describes the old file
        QVERIFY(placeholder(*window).contains(u"changed"_s));

        // Reload doesn't accept it either.
        window->boardScene()->selectIds({onlyImage(*window).id});
        QVERIFY(waitForCheck(*window, window->action(u"ReloadLink"_s)));
        QCOMPARE(window->linkState(onlyAsset(*window).id), LinkState::Changed);
        QCOMPARE(onlyAsset(*window).sha256, original);

        // Accepting is an explicit, undoable step; the width stays.
        QVERIFY(window->action(u"AcceptLinkChange"_s)->isEnabled());
        window->action(u"AcceptLinkChange"_s)->trigger();
        QVERIFY(onlyAsset(*window).sha256 != original);
        QCOMPARE(onlyAsset(*window).orientedSize, QSize(80, 30));
        QCOMPARE(onlyImage(*window).displaySize, QSizeF(40, 15));
        QVERIFY(placeholder(*window).isEmpty());
        window->action(u"Undo"_s)->trigger();
        QCOMPARE(onlyAsset(*window).sha256, original);
        QVERIFY(!placeholder(*window).isEmpty());
    }

    void embedRemovesTheDependency()
    {
        QTemporaryDir dir;
        const QString image = dir.filePath(u"ref.png"_s);
        QVERIFY(writeImage(image, Qt::cyan, {40, 30}));
        const QString board = dir.filePath(u"board.glacierref"_s);
        {
            MainWindow window(*m_services);
            QVERIFY(importLinked(window, {image}));
            QVERIFY(window.action(u"EmbedAllLinks"_s)->isEnabled());
            window.action(u"EmbedAllLinks"_s)->trigger();
            QVERIFY(!onlyAsset(window).linked);
            QVERIFY(onlyAsset(window).linkPath.isEmpty());
            QVERIFY(save(window, board));
        }
        QVERIFY(QFile::remove(image));
        const auto window = open(board);
        QVERIFY(window);
        QVERIFY(!onlyAsset(*window).linked);
        QVERIFY(placeholder(*window).isEmpty());
    }

    void linkedImportOfEmbeddedBytesStaysEmbedded()
    {
        QTemporaryDir dir;
        const QString image = dir.filePath(u"same.png"_s);
        QVERIFY(writeImage(image, Qt::magenta, {20, 20}));
        MainWindow window(*m_services);
        QSignalSpy applied(&window, &MainWindow::importApplied);
        window.importPlan(io::planForPaths({image}), QPointF(0, 0)); // embedded
        QVERIFY(applied.wait(30000));
        QVERIFY(importLinked(window, {image}));
        closeMessages();
        QCOMPARE(window.document()->assetCount(), 1);
        QVERIFY(!onlyAsset(window).linked);
        QCOMPARE(window.document()->itemCount(), 2);
    }
};

GLACIER_TEST_WIDGETS_MAIN(TestLinks)
#include "tst_links.moc"
