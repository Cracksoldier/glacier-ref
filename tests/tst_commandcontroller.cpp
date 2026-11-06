// Undo/redo (spec A02): a long random editing session is undone step by step,
// comparing the complete document with the state recorded before each step,
// then redone to the final state.

#include "GlacierTestMain.h"
#include "TestBoards.h"

#include "glacier/editing/CommandController.h"
#include "glacier/editing/Operations.h"

#include <QGuiApplication>
#include <QObject>
#include <QRandomGenerator>
#include <QSignalSpy>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::editing;
using namespace glacier::test;

namespace {

struct State
{
    Snapshot snapshot;
    ViewSettings view;
    bool operator==(const State &) const = default;
};

State capture(const BoardDocument &doc)
{
    return {doc.snapshotAll(), doc.view()};
}

} // namespace

class TestCommandController : public QObject
{
    Q_OBJECT

private slots:
    void runUndoRedo()
    {
        BoardDocument doc;
        CommandController controller(&doc);
        QSignalSpy dirty(&controller, &CommandController::dirtyChanged);
        QVERIFY(!controller.isDirty());

        const auto change = doc.addImagesChange({fakeImage(1, {0, 0})});
        QVERIFY(controller.run(*change, u"Import"_s));
        QCOMPARE(doc.itemCount(), 1);
        QVERIFY(controller.isDirty());
        QCOMPARE(dirty.size(), 1);
        QCOMPARE(controller.undoStack()->undoText(), u"Import"_s);

        controller.undoStack()->undo();
        QCOMPARE(doc.itemCount(), 0);
        QVERIFY(!controller.isDirty());
        controller.undoStack()->redo();
        QCOMPARE(doc.itemCount(), 1);
        controller.markClean();
        QVERIFY(!controller.isDirty());
    }

    void rejectedEditsAreNotRecorded()
    {
        BoardDocument doc;
        CommandController controller(&doc);
        const auto ids = doc.addImages({fakeImage(1, {0, 0}), fakeImage(2, {200, 0})})->itemIds;
        QVERIFY(doc.apply(*ops::setLocked(doc, {ids[0]}, true).change));
        QString error;
        QVERIFY(!controller.run(ops::remove(doc, {ids[0]}), u"Delete"_s, &error));
        QVERIFY(error.contains(u"locked"_s));
        QVERIFY(!controller.run(ops::group(doc, {ids[0]}, {}), u"Group"_s, &error));
        QVERIFY(!controller.run(OpResult{}, u"Nothing"_s));
        QCOMPARE(controller.undoStack()->count(), 0);
    }

    void undoLimitIs200()
    {
        BoardDocument doc;
        CommandController controller(&doc);
        QCOMPARE(controller.undoStack()->undoLimit(), 200);
        const Id id = doc.addImages({fakeImage(1, {0, 0})})->itemIds.first();
        for (int i = 0; i < 250; ++i)
            QVERIFY(controller.run(ops::transform(doc, {id}, Affine2D::translation(1, 0)), u"Move"_s));
        QCOMPARE(controller.undoStack()->count(), 200);
        while (controller.undoStack()->canUndo())
            controller.undoStack()->undo();
        QCOMPARE(doc.worldBounds(id).left(), 50.0); // the oldest 50 steps are gone
    }

    void randomSessionUndoesExactly_data()
    {
        QTest::addColumn<quint32>("seed");
        for (quint32 seed : {1u, 7u, 42u, 2026u})
            QTest::newRow(qPrintable(u"seed %1"_s.arg(seed))) << seed;
    }

    void randomSessionUndoesExactly()
    {
        QFETCH(quint32, seed);
        QRandomGenerator rng(seed);
        BoardDocument doc;
        CommandController controller(&doc);
        controller.undoStack()->setUndoLimit(0); // unlimited for this test

        QList<NewImage> images;
        for (int i = 0; i < 8; ++i)
            images.append(fakeImage(i + 1, {double(i % 4) * 160, double(i / 4) * 160},
                                    {80 + int(rng.bounded(80)), 80 + int(rng.bounded(80))}));
        QVERIFY(controller.run(*doc.addImagesChange(images), u"Import"_s));
        QVERIFY(controller.run(ops::createNote(doc, {300, 500}, u"note"_s), u"Note"_s));

        auto pick = [&](const QList<Id> &from) { return from[int(rng.bounded(int(from.size())))]; };
        QList<State> history{capture(doc)};
        int applied = 0;
        for (int step = 0; step < 300; ++step) {
            const QList<Id> top = doc.topLevelIdsByZ();
            if (top.isEmpty())
                break;
            const Id a = pick(top), b = pick(top);
            OpResult r;
            switch (rng.bounded(18)) {
            case 0: r = ops::transform(doc, {a, b}, Affine2D::translation(rng.bounded(200) - 100, rng.bounded(200) - 100)); break;
            case 1: r = ops::rotate(doc, {a}, rng.bounded(360)); break;
            case 2: r = ops::flip(doc, {a, b}, rng.bounded(2) ? Qt::Horizontal : Qt::Vertical); break;
            case 3: r = ops::setCrop(doc, a, QRectF(rng.bounded(0.4), rng.bounded(0.4), 0.3 + rng.bounded(0.3), 0.3 + rng.bounded(0.3))); break;
            case 4: r = ops::resetCrop(doc, {a}); break;
            case 5: r = ops::group(doc, {a, b}, u"g"_s); break;
            case 6: r = ops::ungroup(doc, {a}); break;
            case 7: r = ops::setLocked(doc, {a}, rng.bounded(3) == 0); break;
            case 8: r = ops::zOrder(doc, {a}, ZOrder(rng.bounded(4))); break;
            case 9: r = ops::align(doc, {a, b}, Align(rng.bounded(6))); break;
            case 10: r = ops::arrange(doc, rng.bounded(2) ? QList<Id>{} : QList<Id>{a, b}, 10 + rng.bounded(30)); break;
            case 11: r = ops::remove(doc, {a}); break;
            case 12: r = ops::createNote(doc, {rng.bounded(1000.0), rng.bounded(1000.0)}, u"n%1"_s.arg(step)); break;
            case 13: r = ops::editNote(doc, a, u"edited %1\nsecond line"_s.arg(step)); break;
            case 14: r = ops::setNoteWidth(doc, a, 60 + rng.bounded(300)); break;
            case 15: r = ops::setBackground(doc, rng.generate()); break;
            case 16: r = ops::setGrayscale(doc, !doc.view().grayscale); break;
            case 17: r = ops::duplicate(doc, {a}); break;
            }
            if (controller.run(r, u"step"_s)) {
                ++applied;
                QCOMPARE(doc.validate(), QString());
                history.append(capture(doc));
            }
        }
        QVERIFY2(applied > 100, qPrintable(QString::number(applied)));
        QCOMPARE(controller.undoStack()->count(), applied + 2);

        // Undo every step: each must restore the recorded previous state exactly.
        for (qsizetype i = history.size() - 1; i > 0; --i) {
            QVERIFY(capture(doc) == history[i]);
            controller.undoStack()->undo();
            QVERIFY2(capture(doc) == history[i - 1], qPrintable(u"undo mismatch at step %1"_s.arg(i)));
        }
        // Redo everything back to the final state.
        for (qsizetype i = 1; i < history.size(); ++i) {
            controller.undoStack()->redo();
            QVERIFY2(capture(doc) == history[i], qPrintable(u"redo mismatch at step %1"_s.arg(i)));
        }
        QCOMPARE(doc.validate(), QString());
    }
};

GLACIER_TEST_GUI_MAIN(TestCommandController)
#include "tst_commandcontroller.moc"
