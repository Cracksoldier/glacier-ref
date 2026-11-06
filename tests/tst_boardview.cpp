#include "GlacierTestMain.h"

#include "glacier/canvas/BoardScene.h"
#include "glacier/canvas/BoardView.h"
#include "glacier/canvas/ImageItem.h"
#include "glacier/core/BoardDocument.h"

#include <QApplication>
#include <QMimeData>
#include <QObject>
#include <QSignalSpy>
#include <QWheelEvent>

using namespace Qt::StringLiterals;
using namespace glacier;
using glacier::canvas::BoardScene;
using glacier::canvas::BoardView;

namespace {

NewImage fakeImage(int n, QPointF pos, QSize size = {100, 100})
{
    NewImage image;
    image.sha256 = QByteArray::number(n).rightJustified(64, '0');
    image.mediaType = u"image/png"_s;
    image.orientedSize = size;
    image.byteLength = 10;
    image.position = pos;
    return image;
}

void sendMouse(QWidget *target, QEvent::Type type, QPointF pos, Qt::MouseButton button,
               Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(type, pos, target->mapToGlobal(pos), button, buttons, modifiers);
    QApplication::sendEvent(target, &event);
}

bool fuzzy(QPointF a, QPointF b, double tolerance)
{
    return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

} // namespace

class TestBoardView : public QObject
{
    Q_OBJECT

    std::unique_ptr<BoardDocument> m_doc;
    std::unique_ptr<BoardScene> m_scene;
    std::unique_ptr<BoardView> m_view;
    QList<Id> m_ids;

private slots:
    void init()
    {
        m_doc = std::make_unique<BoardDocument>();
        const auto added = m_doc->addImages({fakeImage(1, {0, 0}), fakeImage(2, {300, 0}), fakeImage(3, {0, 300})});
        QVERIFY(added);
        m_ids = added->itemIds;
        m_scene = std::make_unique<BoardScene>(m_doc.get(), nullptr);
        m_view = std::make_unique<BoardView>();
        m_view->setBoardScene(m_scene.get());
        m_view->resize(800, 600);
        m_view->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
    }

    void cleanup()
    {
        m_view.reset();
        m_scene.reset();
        m_doc.reset();
    }

    void sceneMirrorsDocument()
    {
        QCOMPARE(m_scene->items().size(), 3);
        QVERIFY(m_scene->itemFor(m_ids[1]));
        QCOMPARE(m_scene->itemFor(m_ids[1])->sceneBoundingRect(), QRectF(300, 0, 100, 100));
        QVERIFY(m_doc->addImages({fakeImage(4, {600, 600})}));
        QCOMPARE(m_scene->items().size(), 4);
        QVERIFY(m_doc->removeItems({m_ids[0]}));
        QCOMPARE(m_scene->items().size(), 3);
        QVERIFY(!m_scene->itemFor(m_ids[0]));
    }

    void zoomIsClamped()
    {
        const QPointF centre = m_view->viewport()->rect().center();
        m_view->setZoom(1000, centre);
        QCOMPARE(m_view->zoom(), BoardView::MaxZoom);
        m_view->setZoom(0.00001, centre);
        QCOMPARE(m_view->zoom(), BoardView::MinZoom);
        m_view->setZoom(std::numeric_limits<double>::quiet_NaN(), centre);
        QCOMPARE(m_view->zoom(), BoardView::MinZoom);
    }

    void zoomKeepsPointUnderCursor_data()
    {
        QTest::addColumn<double>("factor");
        QTest::newRow("in x3") << 3.0;
        QTest::newRow("out x0.2") << 0.2;
        QTest::newRow("deep in") << 40.0;
    }

    void zoomKeepsPointUnderCursor()
    {
        QFETCH(double, factor);
        const QPointF cursor(130, 410);
        const QPointF before = m_view->mapToScene(cursor.toPoint());
        m_view->zoomBy(factor, cursor);
        const QPointF after = m_view->mapToScene(cursor.toPoint());
        // Within two device pixels, expressed in scene units.
        QVERIFY2(fuzzy(before, after, 2.0 / m_view->zoom()),
                 qPrintable(u"%1,%2 vs %3,%4"_s.arg(before.x()).arg(before.y()).arg(after.x()).arg(after.y())));
    }

    void wheelZoomsAtCursor()
    {
        const QPointF pos(200, 150);
        const QPointF before = m_view->mapToScene(pos.toPoint());
        QWheelEvent wheel(pos, m_view->viewport()->mapToGlobal(pos), QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(m_view->viewport(), &wheel);
        QVERIFY(std::abs(m_view->zoom() - BoardView::WheelStep) < 1e-9);
        QVERIFY(fuzzy(before, m_view->mapToScene(pos.toPoint()), 2.0));
    }

    void fitBoardShowsEverything()
    {
        m_view->setZoom(10, m_view->viewport()->rect().center());
        m_view->fitBoard();
        const QRectF visible = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
        QVERIFY(visible.contains(m_doc->boundingRect()));
        QVERIFY(fuzzy(m_view->viewCenter(), m_doc->boundingRect().center(), 2.0 / m_view->zoom()));
        // 400x400 content in an 800x600 viewport with 5% margins: height-bound.
        QVERIFY(std::abs(m_view->zoom() - 600 * 0.9 / 400) < 0.01);
    }

    void fitSelectionCentresSelection()
    {
        m_scene->itemFor(m_ids[1])->setSelected(true);
        m_view->fitSelection();
        QVERIFY(fuzzy(m_view->viewCenter(), QPointF(350, 50), 2.0 / m_view->zoom()));
        QVERIFY(m_view->zoom() > 4.0);
    }

    void fitEmptyBoardResets()
    {
        QVERIFY(m_doc->removeItems(m_ids));
        m_view->setZoom(5, QPointF(10, 10));
        m_view->fitBoard();
        QCOMPARE(m_view->zoom(), 1.0);
        QVERIFY(fuzzy(m_view->viewCenter(), QPointF(0, 0), 2.0));
    }

    void middleDragPans()
    {
        m_view->resetZoom();
        const QPointF before = m_view->viewCenter();
        QWidget *vp = m_view->viewport();
        sendMouse(vp, QEvent::MouseButtonPress, {400, 300}, Qt::MiddleButton, Qt::MiddleButton);
        sendMouse(vp, QEvent::MouseMove, {350, 260}, Qt::NoButton, Qt::MiddleButton);
        sendMouse(vp, QEvent::MouseButtonRelease, {350, 260}, Qt::MiddleButton, Qt::NoButton);
        // Content follows the cursor: dragging left/up moves the view right/down.
        QVERIFY(fuzzy(m_view->viewCenter(), before + QPointF(50, 40), 1.5));
    }

    void shiftClickTogglesSelection()
    {
        m_view->fitBoard();
        QWidget *vp = m_view->viewport();
        const QPoint a = m_view->mapFromScene(QPointF(50, 50));
        const QPoint b = m_view->mapFromScene(QPointF(350, 50));

        QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, a);
        QCOMPARE(m_scene->selectedItemIds(), QList<Id>{m_ids[0]});
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ShiftModifier, b);
        QCOMPARE(m_scene->selectedItemIds().size(), 2);
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ShiftModifier, a);
        QCOMPARE(m_scene->selectedItemIds(), QList<Id>{m_ids[1]});
        QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, a); // plain click replaces
        QCOMPARE(m_scene->selectedItemIds(), QList<Id>{m_ids[0]});
        QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, m_view->mapFromScene(QPointF(250, 250)));
        QVERIFY(m_scene->selectedItemIds().isEmpty()); // click on empty canvas clears
    }

    void rubberBandSelects()
    {
        m_view->fitBoard();
        QWidget *vp = m_view->viewport();
        const QPointF from = m_view->mapFromScene(QPointF(-20, -20));
        const QPointF to = m_view->mapFromScene(QPointF(420, 60)); // covers items 1 and 2
        sendMouse(vp, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(m_scene->selectedItemIds().size(), 2);
        QVERIFY(!m_scene->itemFor(m_ids[2])->isSelected());

        // Shift adds the third item without dropping the others.
        const QPointF from2 = m_view->mapFromScene(QPointF(-20, 280));
        const QPointF to2 = m_view->mapFromScene(QPointF(60, 420));
        sendMouse(vp, QEvent::MouseButtonPress, from2, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
        sendMouse(vp, QEvent::MouseMove, to2, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
        sendMouse(vp, QEvent::MouseButtonRelease, to2, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
        QCOMPARE(m_scene->selectedItemIds().size(), 3);
    }

    void itemsAreNotMovableInM1()
    {
        m_view->fitBoard();
        QWidget *vp = m_view->viewport();
        const QPointF a = m_view->mapFromScene(QPointF(50, 50));
        sendMouse(vp, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseMove, a + QPointF(80, 80), Qt::NoButton, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseButtonRelease, a + QPointF(80, 80), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(m_scene->itemFor(m_ids[0])->sceneBoundingRect(), QRectF(0, 0, 100, 100));
        QCOMPARE(m_doc->item(m_ids[0])->transform.translationPart(), QPointF(0, 0));
    }

    void dropEmitsScenePosition()
    {
        QSignalSpy dropped(m_view.get(), &BoardView::dropReceived);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(u"/tmp/x.png"_s)});
        const QPointF pos(100, 100);
        QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(m_view->viewport(), &enter);
        QDropEvent drop(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(m_view->viewport(), &drop);
        QCOMPARE(dropped.size(), 1);
        QVERIFY(fuzzy(dropped.first().at(1).toPointF(), m_view->mapToScene(pos.toPoint()), 1e-6));
    }
};

GLACIER_TEST_WIDGETS_MAIN(TestBoardView)
#include "tst_boardview.moc"
