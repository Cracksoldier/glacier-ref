#include "GlacierTestMain.h"
#include "TestBoards.h"

#include "glacier/canvas/BoardItems.h"
#include "glacier/canvas/BoardScene.h"
#include "glacier/canvas/BoardView.h"
#include "glacier/core/BoardDocument.h"
#include "glacier/editing/CommandController.h"
#include "glacier/editing/Geometry.h"
#include "glacier/editing/Operations.h"

#include <QAction>
#include <QApplication>
#include <QMimeData>
#include <QObject>
#include <QSignalSpy>
#include <QWheelEvent>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::canvas;
using namespace glacier::editing;
using glacier::test::fakeImage;

namespace {

void sendMouse(QWidget *target, QEvent::Type type, QPointF pos, Qt::MouseButton button,
               Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QMouseEvent event(type, pos, target->mapToGlobal(pos), button, buttons, modifiers);
    QApplication::sendEvent(target, &event);
}

void drag(QWidget *target, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    sendMouse(target, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
    sendMouse(target, QEvent::MouseMove, (from + to) / 2, Qt::NoButton, Qt::LeftButton, modifiers);
    sendMouse(target, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, modifiers);
    sendMouse(target, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
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
    std::unique_ptr<CommandController> m_controller;
    std::unique_ptr<BoardScene> m_scene;
    std::unique_ptr<BoardView> m_view;
    QList<Id> m_ids;

    QPointF toView(QPointF scenePos) const { return m_view->viewportTransform().map(scenePos); }
    QUndoStack *stack() const { return m_controller->undoStack(); }

private slots:
    void initTestCase() { qRegisterMetaType<const QMimeData *>(); }

    void init()
    {
        m_doc = std::make_unique<BoardDocument>();
        m_ids = m_doc->addImages({fakeImage(1, {0, 0}), fakeImage(2, {300, 0}), fakeImage(3, {0, 300})})->itemIds;
        m_controller = std::make_unique<CommandController>(m_doc.get());
        m_scene = std::make_unique<BoardScene>(m_doc.get(), nullptr);
        m_view = std::make_unique<BoardView>();
        m_view->setBoardScene(m_scene.get());
        m_view->setController(m_controller.get());
        m_view->resize(800, 600);
        m_view->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
        m_view->fitBoard();
    }

    void cleanup()
    {
        m_view.reset();
        m_scene.reset();
        m_controller.reset();
        m_doc.reset();
    }

    // --- M1 navigation and selection (unchanged behaviour) ---------------------

    void sceneMirrorsDocument()
    {
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
    }

    void zoomKeepsPointUnderCursor()
    {
        const QPointF cursor(130, 410);
        const QPointF before = m_view->mapToScene(cursor.toPoint());
        m_view->zoomBy(3.0, cursor);
        QVERIFY(fuzzy(before, m_view->mapToScene(cursor.toPoint()), 2.0 / m_view->zoom()));
    }

    void wheelZoomsAtCursor()
    {
        const double before = m_view->zoom();
        const QPointF pos(200, 150);
        QWheelEvent wheel(pos, m_view->viewport()->mapToGlobal(pos), QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(m_view->viewport(), &wheel);
        QVERIFY(std::abs(m_view->zoom() / before - BoardView::WheelStep) < 1e-9);
    }

    void fitBoardAndSelection()
    {
        const QRectF visible = m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
        QVERIFY(visible.contains(m_doc->boundingRect()));
        m_scene->itemFor(m_ids[1])->setSelected(true);
        m_view->fitSelection();
        QVERIFY(fuzzy(m_view->viewCenter(), QPointF(350, 50), 2.0 / m_view->zoom()));
    }

    void shiftClickAndRubberBand()
    {
        QWidget *vp = m_view->viewport();
        QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, toView({50, 50}).toPoint());
        QCOMPARE(m_scene->selectedIds(), QList<Id>{m_ids[0]});
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ShiftModifier, toView({350, 50}).toPoint());
        QCOMPARE(m_scene->selectedIds().size(), 2);
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ShiftModifier, toView({50, 50}).toPoint());
        QCOMPARE(m_scene->selectedIds(), QList<Id>{m_ids[1]});
        QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, toView({250, 250}).toPoint());
        QVERIFY(m_scene->selectedIds().isEmpty());
        drag(vp, toView({-20, -20}), toView({420, 60}));
        QCOMPARE(m_scene->selectedIds().size(), 2);
        drag(vp, toView({-20, 280}), toView({60, 420}), Qt::ShiftModifier);
        QCOMPARE(m_scene->selectedIds().size(), 3);
    }

    void navigationDoesNotChangeGeometry()
    {
        const Snapshot before = m_doc->snapshotAll();
        const quint64 revision = m_doc->revision();
        m_view->zoomBy(4, {100, 100});
        m_view->fitBoard();
        sendMouse(m_view->viewport(), QEvent::MouseButtonPress, {400, 300}, Qt::MiddleButton, Qt::MiddleButton);
        sendMouse(m_view->viewport(), QEvent::MouseMove, {320, 250}, Qt::NoButton, Qt::MiddleButton);
        sendMouse(m_view->viewport(), QEvent::MouseButtonRelease, {320, 250}, Qt::MiddleButton, Qt::NoButton);
        QCOMPARE(m_doc->snapshotAll(), before);
        QCOMPARE(m_doc->revision(), revision);
        QCOMPARE(stack()->count(), 0);
    }

    // --- M2 gestures ----------------------------------------------------------

    void moveIsOneUndoStep()
    {
        const QPointF start = toView({50, 50});
        drag(m_view->viewport(), start, start + QPointF(80, 40));
        QCOMPARE(stack()->count(), 1);
        const QPointF moved = m_doc->worldBounds(m_ids[0]).topLeft();
        QVERIFY(fuzzy(moved, QPointF(80, 40) / m_view->zoom(), 2.0 / m_view->zoom()));
        QCOMPARE(m_scene->itemFor(m_ids[0])->sceneBoundingRect().topLeft(), moved);
        stack()->undo();
        QCOMPARE(m_doc->worldBounds(m_ids[0]).topLeft(), QPointF(0, 0));
        QCOMPARE(m_scene->itemFor(m_ids[0])->sceneBoundingRect().topLeft(), QPointF(0, 0));
    }

    void escapeCancelsGesture()
    {
        QWidget *vp = m_view->viewport();
        const QPointF start = toView({50, 50});
        sendMouse(vp, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseMove, start + QPointF(60, 60), Qt::NoButton, Qt::LeftButton);
        QVERIFY(m_view->isGestureActive());
        QVERIFY(m_scene->itemFor(m_ids[0])->sceneBoundingRect().topLeft() != QPointF(0, 0)); // previewing
        QTest::keyClick(m_view.get(), Qt::Key_Escape);
        QVERIFY(!m_view->isGestureActive());
        QCOMPARE(m_scene->itemFor(m_ids[0])->sceneBoundingRect().topLeft(), QPointF(0, 0));
        sendMouse(vp, QEvent::MouseButtonRelease, start + QPointF(60, 60), Qt::LeftButton, Qt::NoButton);
        QCOMPARE(stack()->count(), 0);
        QCOMPARE(m_doc->worldBounds(m_ids[0]).topLeft(), QPointF(0, 0));
    }

    void resizeKeepsAspectUnlessShift()
    {
        m_scene->selectIds({m_ids[0]});
        drag(m_view->viewport(), toView({100, 100}), toView({100, 100}) + QPointF(60, 20)); // bottom-right handle
        QCOMPARE(stack()->count(), 1);
        const QRectF b = m_doc->worldBounds(m_ids[0]);
        QVERIFY(b.width() > 100);
        QVERIFY(std::abs(b.width() - b.height()) < 1e-6); // aspect preserved
        QCOMPARE(b.topLeft(), QPointF(0, 0));             // anchored at the opposite corner

        drag(m_view->viewport(), toView({b.right(), b.center().y()}), toView({b.right(), b.center().y()}) + QPointF(50, 0),
             Qt::ShiftModifier); // right edge, non-uniform
        const QRectF c = m_doc->worldBounds(m_ids[0]);
        QVERIFY(c.width() > b.width());
        QVERIFY(std::abs(c.height() - b.height()) < 1e-6);
    }

    void rotateSnapsWithShift()
    {
        m_scene->selectIds({m_ids[0]});
        const auto handles = TransformTool::handles(TransformTool::frameFor(*m_scene, {m_ids[0]}), *m_view);
        QPointF rotate;
        for (const auto &[h, pos] : handles)
            if (h == TransformTool::Handle::Rotate)
                rotate = pos;
        const QPointF centre = toView({50, 50});
        const QPointF target = centre + QPointF(70, -12); // roughly 80 degrees clockwise
        drag(m_view->viewport(), rotate, target, Qt::ShiftModifier);
        QCOMPARE(stack()->count(), 1);
        const double angle = rotationDegrees(m_doc->worldTransform(m_ids[0]));
        QVERIFY2(std::abs(std::remainder(angle, 15.0)) < 1e-6, qPrintable(QString::number(angle)));
        QVERIFY(std::abs(angle) > 10);
        QVERIFY(fuzzy(m_doc->worldBounds(m_ids[0]).center(), QPointF(50, 50), 1e-6)); // about the centre
    }

    void lockedItemsDoNotMove()
    {
        QVERIFY(m_controller->run(ops::setLocked(*m_doc, {m_ids[0]}, true), u"Lock"_s));
        const QPointF start = toView({50, 50});
        drag(m_view->viewport(), start, start + QPointF(80, 40));
        QCOMPARE(m_doc->worldBounds(m_ids[0]).topLeft(), QPointF(0, 0));
        QCOMPARE(stack()->count(), 1); // only the lock
    }

    void groupEnterAndLeave()
    {
        const OpResult grouped = ops::group(*m_doc, {m_ids[0], m_ids[1]}, {});
        QVERIFY(m_controller->run(grouped, u"Group"_s));
        const Id g = grouped.created.first();
        QWidget *vp = m_view->viewport();
        QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, toView({50, 50}).toPoint());
        QCOMPARE(m_scene->selectedIds(), QList<Id>{g}); // clicking a member selects the group

        QTest::mouseDClick(vp, Qt::LeftButton, Qt::NoModifier, toView({50, 50}).toPoint());
        QCOMPARE(m_scene->activeGroup(), g);
        QTest::mouseClick(vp, Qt::LeftButton, Qt::NoModifier, toView({350, 50}).toPoint());
        QCOMPARE(m_scene->selectedIds(), QList<Id>{m_ids[1]}); // members are editable now

        // Moving a member inside the group changes only its local transform.
        drag(vp, toView({350, 50}), toView({350, 50}) + QPointF(0, 50));
        QCOMPARE(m_doc->groupOf(m_ids[1]), g);
        QVERIFY(m_doc->worldBounds(m_ids[1]).top() > 0);

        QTest::keyClick(m_view.get(), Qt::Key_Escape);
        QVERIFY(m_scene->activeGroup().isNull());
        QCOMPARE(m_scene->selectedIds(), QList<Id>{g});
    }

    void cropDragAndCommit()
    {
        m_scene->selectIds({m_ids[0]});
        m_view->beginCrop();
        QVERIFY(m_view->isCropping());
        drag(m_view->viewport(), toView({100, 50}), toView({60, 50})); // right edge to x=60
        QTest::keyClick(m_view.get(), Qt::Key_Return);
        QVERIFY(!m_view->isCropping());
        QCOMPARE(stack()->count(), 1);
        const QRectF crop = m_doc->image(m_ids[0])->crop;
        QVERIFY2(std::abs(crop.width() - 0.6) < 0.02, qPrintable(QString::number(crop.width())));
        QCOMPARE(m_doc->worldBounds(m_ids[0]).left(), 0.0); // the visible part stays in place

        m_view->beginCrop();
        drag(m_view->viewport(), toView({0, 50}), toView({30, 50}));
        QTest::keyClick(m_view.get(), Qt::Key_Escape); // cancel
        QCOMPARE(m_doc->image(m_ids[0])->crop, crop);
        QCOMPARE(stack()->count(), 1);
    }

    void textEditingHasShortcutPriority()
    {
        // A window-level shortcut that must not fire while editing.
        QAction fit(u"Fit"_s, m_view.get());
        fit.setShortcut(QKeySequence(Qt::Key_F));
        m_view->addAction(&fit);
        int fired = 0;
        connect(&fit, &QAction::triggered, this, [&fired] { ++fired; });
        m_view->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_view.get()));
        QTest::keyClick(m_view.get(), Qt::Key_F);
        QCOMPARE(fired, 1);

        const OpResult created = ops::createNote(*m_doc, {600, 600}, u"abc"_s);
        QVERIFY(m_controller->run(created, u"Note"_s));
        const Id note = created.created.first();
        const int commands = stack()->count();
        m_view->editNote(note);
        QVERIFY(m_view->isTextEditing());
        QTest::keyClicks(m_view.get(), u"f"_s);
        QCOMPARE(fired, 1); // went to the editor, not the shortcut
        QTest::keyClick(m_view.get(), Qt::Key_Z, Qt::ControlModifier); // undoes typing in the editor
        QCOMPARE(stack()->count(), commands);
        QTest::keyClicks(m_view.get(), u"de"_s);
        QTest::keyClick(m_view.get(), Qt::Key_Escape);
        QVERIFY(!m_view->isTextEditing());
        QCOMPARE(m_doc->note(note)->text, u"abcde"_s);
        QCOMPARE(stack()->count(), commands + 1); // the whole session is one step
    }

    void newNoteCommitsOnceAndEmptyIsDiscarded()
    {
        const int before = stack()->count();
        m_view->beginNewNote({500, 500});
        QTest::keyClick(m_view.get(), Qt::Key_Escape); // empty: discarded
        QCOMPARE(m_doc->notes().size(), 0);
        QCOMPARE(stack()->count(), before);

        m_view->beginNewNote({500, 500});
        QTest::keyClicks(m_view.get(), u"Rim light"_s);
        QTest::keyClick(m_view.get(), Qt::Key_Escape);
        QCOMPARE(m_doc->notes().size(), 1);
        QCOMPARE(m_doc->notes().begin()->text, u"Rim light"_s);
        QCOMPARE(stack()->count(), before + 1);
        QCOMPARE(m_scene->selectedIds().size(), 1);
    }

    void undoWhileEditingIsSafe()
    {
        const OpResult created = ops::createNote(*m_doc, {600, 600}, u"x"_s);
        QVERIFY(m_controller->run(created, u"Note"_s));
        m_view->editNote(created.created.first());
        stack()->undo(); // removes the note being edited
        QVERIFY(!m_view->isTextEditing());
        QTest::keyClick(m_view.get(), Qt::Key_Escape); // no crash, no dangling editor
        QCOMPARE(m_doc->notes().size(), 0);
    }

    void dropEmitsScenePosition()
    {
        QSignalSpy dropped(m_view.get(), &BoardView::dropReceived);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(u"/tmp/x.png"_s)});
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
        const QPointF pos(100, 100);
#else
        const QPoint pos(100, 100); // QDragEnterEvent takes QPointF from Qt 6.12
#endif
        QDragEnterEvent enter(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(m_view->viewport(), &enter);
        QDropEvent drop(pos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(m_view->viewport(), &drop);
        QCOMPARE(dropped.size(), 1);
    }
};

GLACIER_TEST_WIDGETS_MAIN(TestBoardView)
#include "tst_boardview.moc"
