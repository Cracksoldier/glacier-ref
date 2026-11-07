#include "GlacierTestMain.h"
#include "TestBoards.h"

#include "glacier/editing/CopyImage.h"
#include "glacier/editing/Geometry.h"
#include "glacier/editing/Operations.h"

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QObject>
#include <QSet>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::editing;
using namespace glacier::test;

namespace {

bool applyOp(BoardDocument &doc, const OpResult &r)
{
    return r.change && doc.apply(*r.change).has_value();
}

} // namespace

class TestOperations : public QObject
{
    Q_OBJECT

    std::unique_ptr<BoardDocument> m_doc;
    QList<Id> m_ids; // a at (0,0), b at (200,0), c at (0,200); 100x100 each

private slots:
    void init()
    {
        m_doc = std::make_unique<BoardDocument>();
        m_ids = m_doc->addImages({fakeImage(1, {0, 0}), fakeImage(2, {200, 0}), fakeImage(3, {0, 200})})->itemIds;
    }

    void geometryHelpers()
    {
        QVERIFY(fuzzyPoint(rotationAbout({10, 10}, 90).map({20, 10}), {10, 20}));
        QCOMPARE(snapAngle(22.4), 15.0);
        QCOMPARE(snapAngle(22.6), 30.0);
        QCOMPARE(snapAngle(-7.6), -15.0);
        QVERIFY(fuzzyPoint(scalingAbout({100, 100}, 2, 3).map({110, 110}), {120, 130}));
        // Scaling in a rotated frame scales along the frame's axes.
        const Affine2D frame = rotationAbout({0, 0}, 90); // frame x axis points down
        const Affine2D g = scalingInFrame(frame, {0, 0}, 2, 1);
        QVERIFY(fuzzyPoint(g.map({0, 10}), {0, 20}));
        QVERIFY(fuzzyPoint(g.map({10, 0}), {10, 0}));
    }

    void orientationRemovesScaleAndTranslation()
    {
        QCOMPARE(orientationOf(Affine2D()), Affine2D());
        // Mirroring survives; the stretch does not.
        QCOMPARE(orientationOf(Affine2D::scaling(-2, 3)), Affine2D::scaling(-1, 1));
        QCOMPARE(orientationOf(Affine2D::scaling(2, -0.5)), Affine2D::scaling(1, -1));
        // Quarter turns are exact, whatever the scale and position.
        const Affine2D quarter = Affine2D::scaling(1.5, 1.5) * rotationAbout({10, 20}, 90) * Affine2D::translation(5, 7);
        QCOMPARE(orientationOf(quarter), (Affine2D{0, 1, -1, 0, 0, 0}));
        QCOMPARE(orientationOf(rotationAbout({0, 0}, -270)), (Affine2D{0, 1, -1, 0, 0, 0}));
        // Free angles, also after a stretch in the item's own frame.
        const Affine2D r30 = rotationAbout({0, 0}, 30);
        const Affine2D o = orientationOf(Affine2D::scaling(3, 1) * r30 * Affine2D::translation(4, 4));
        QVERIFY(qAbs(o.m11 - r30.m11) < 1e-12 && qAbs(o.m12 - r30.m12) < 1e-12);
        QVERIFY(qAbs(o.m21 - r30.m21) < 1e-12 && qAbs(o.m22 - r30.m22) < 1e-12);
        const Affine2D mirrored = orientationOf(Affine2D::scaling(-2, 2) * r30);
        QVERIFY(qAbs(mirrored.determinant() + 1.0) < 1e-12);
        QVERIFY(fuzzyPoint(mirrored.map({1, 0}), (Affine2D::scaling(-1, 1) * r30).map({1, 0})));
    }

    void copyImagePixelsQuarterTurnsAreLossless_data()
    {
        QTest::addColumn<int>("quarters");
        QTest::addColumn<bool>("mirror");
        for (int q = 0; q < 4; ++q)
            for (bool m : {false, true})
                QTest::addRow("%d deg%s", q * 90, m ? " mirrored" : "") << q << m;
    }

    void copyImagePixelsQuarterTurnsAreLossless()
    {
        QFETCH(int, quarters);
        QFETCH(bool, mirror);
        QImage source(5, 3, QImage::Format_RGB32);
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                source.setPixel(x, y, qRgb(x * 50, y * 100, 7));
        // Scale and translation on the board must not matter.
        const Affine2D world = (mirror ? Affine2D::scaling(-1, 1) : Affine2D()) * Affine2D::scaling(2.5, 2.5)
            * rotationAbout({3, 4}, quarters * 90.0) * Affine2D::translation(100, -50);
        const QImage result = copyImagePixels(source, QRectF(0, 0, 1, 1), world, 8192);
        const Affine2D u = orientationOf(world);
        const QRectF bounds = u.mapRect(QRectF(source.rect()));
        QCOMPARE(result.size(), bounds.size().toSize());
        for (int y = 0; y < source.height(); ++y) {
            for (int x = 0; x < source.width(); ++x) {
                const QPointF q = u.map({x + 0.5, y + 0.5}) - bounds.topLeft();
                QCOMPARE(QColor(result.pixel(int(q.x()), int(q.y()))), QColor(source.pixel(x, y)));
            }
        }
    }

    void copyImagePixelsCropsFreeAnglesAndLimits()
    {
        QImage source(4, 2, QImage::Format_RGB32);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x)
                source.setPixel(x, y, qRgb(x * 60, y * 120, 0));
        const QImage cropped = copyImagePixels(source, QRectF(0.5, 0, 0.5, 1), Affine2D(), 8192);
        QCOMPARE(cropped.size(), QSize(2, 2));
        QCOMPARE(QColor(cropped.pixel(0, 1)), QColor(source.pixel(2, 1)));

        QImage plain(100, 50, QImage::Format_RGB32);
        plain.fill(qRgb(200, 40, 40));
        const QImage turned = copyImagePixels(plain, QRectF(0, 0, 1, 1), rotationAbout({0, 0}, 30), 8192);
        const QSizeF expected = rotationAbout({0, 0}, 30).mapRect(QRectF(0, 0, 100, 50)).size();
        QVERIFY(qAbs(turned.width() - expected.width()) <= 2 && qAbs(turned.height() - expected.height()) <= 2);
        QVERIFY(turned.hasAlphaChannel());
        QCOMPARE(qAlpha(turned.pixel(0, 0)), 0); // transparent corner
        const QColor centre = QColor::fromRgba(turned.pixel(turned.width() / 2, turned.height() / 2));
        QCOMPARE(centre.alpha(), 255);
        QVERIFY(qAbs(centre.red() - 200) <= 1 && qAbs(centre.green() - 40) <= 1);

        const QImage limited = copyImagePixels(plain, QRectF(0, 0, 1, 1), rotationAbout({0, 0}, 45), 64);
        QVERIFY(qMax(limited.width(), limited.height()) <= 65);
        QCOMPARE(copyImagePixels(plain, QRectF(0, 0, 1, 1), rotationAbout({0, 0}, 90), 40).size(), QSize(20, 40));
        QVERIFY(copyImagePixels(QImage(), QRectF(0, 0, 1, 1), Affine2D(), 8192).isNull());
        QVERIFY(copyImagePixels(plain, QRectF(2, 2, 1, 1), Affine2D(), 8192).isNull());
    }

    void transformAndRotateAboutSelection()
    {
        QVERIFY(applyOp(*m_doc, ops::rotate(*m_doc, {m_ids[0]}, 90)));
        // Rotating about its own centre keeps the bounds of a square.
        QVERIFY(fuzzyRect(m_doc->worldBounds(m_ids[0]), QRectF(0, 0, 100, 100)));
        QVERIFY(fuzzyPoint(worldPoint(*m_doc, m_ids[0], {0, 0}), {100, 0})); // top-left corner moved
    }

    void multiFlipMirrorsArrangement()
    {
        QVERIFY(applyOp(*m_doc, ops::flip(*m_doc, {m_ids[0], m_ids[1]}, Qt::Horizontal)));
        // Selection bounds 0..300: a moves to 200..300, b to 0..100, each mirrored.
        QVERIFY(fuzzyRect(m_doc->worldBounds(m_ids[0]), QRectF(200, 0, 100, 100)));
        QVERIFY(fuzzyRect(m_doc->worldBounds(m_ids[1]), QRectF(0, 0, 100, 100)));
        QVERIFY(m_doc->worldTransform(m_ids[0]).determinant() < 0);
        QCOMPARE(orientationOf(m_doc->worldTransform(m_ids[0])), Affine2D::scaling(-1, 1));
    }

    void cropKeepsImageFeaturesInPlace()
    {
        // Rotate and flip first; then crop. A picture feature (local point)
        // must stay at the same board position (non-destructive crop).
        QVERIFY(applyOp(*m_doc, ops::rotate(*m_doc, {m_ids[0]}, 37)));
        QVERIFY(applyOp(*m_doc, ops::flip(*m_doc, {m_ids[0]}, Qt::Vertical)));
        const QPointF feature = worldPoint(*m_doc, m_ids[0], {70, 60});
        QVERIFY(applyOp(*m_doc, ops::setCrop(*m_doc, m_ids[0], QRectF(0.5, 0.4, 0.4, 0.5))));
        QCOMPARE(m_doc->image(m_ids[0])->crop, QRectF(0.5, 0.4, 0.4, 0.5));
        QVERIFY(fuzzyPoint(worldPoint(*m_doc, m_ids[0], {70, 60}), feature));
        QVERIFY(m_doc->worldBounds(m_ids[0]).width() < 100); // only the cropped part is visible
        QVERIFY(applyOp(*m_doc, ops::resetCrop(*m_doc, {m_ids[0]})));
        QCOMPARE(m_doc->image(m_ids[0])->crop, QRectF(0, 0, 1, 1));
        QVERIFY(!ops::setCrop(*m_doc, m_ids[0], QRectF(2, 2, 1, 1))); // empty after clamping
    }

    void groupAndUngroupPreserveWorldTransforms()
    {
        QVERIFY(applyOp(*m_doc, ops::rotate(*m_doc, {m_ids[1]}, 25)));
        QVERIFY(applyOp(*m_doc, ops::setCrop(*m_doc, m_ids[1], QRectF(0.2, 0.2, 0.6, 0.6))));
        const Affine2D worldA = m_doc->worldTransform(m_ids[0]);
        const Affine2D worldB = m_doc->worldTransform(m_ids[1]);
        const QRectF boundsB = m_doc->worldBounds(m_ids[1]);

        const OpResult grouped = ops::group(*m_doc, {m_ids[0], m_ids[1]}, u"refs"_s);
        QVERIFY(applyOp(*m_doc, grouped));
        const Id g = grouped.created.first();
        QCOMPARE(m_doc->group(g)->name, u"refs"_s);
        QCOMPARE(m_doc->group(g)->children.size(), 2);
        QCOMPARE(m_doc->groupOf(m_ids[0]), g);
        QVERIFY(fuzzyPoint(m_doc->worldTransform(m_ids[1]).map({10, 10}), worldB.map({10, 10})));
        QVERIFY(fuzzyRect(m_doc->worldBounds(m_ids[1]), boundsB));

        // Moving the group moves both children; then ungroup keeps worlds.
        QVERIFY(applyOp(*m_doc, ops::transform(*m_doc, {g}, Affine2D::translation(50, 0))));
        QVERIFY(fuzzyPoint(m_doc->worldTransform(m_ids[0]).map({0, 0}), worldA.map({0, 0}) + QPointF(50, 0)));
        const Affine2D movedB = m_doc->worldTransform(m_ids[1]);
        QVERIFY(applyOp(*m_doc, ops::ungroup(*m_doc, {g})));
        QVERIFY(!m_doc->group(g));
        QVERIFY(m_doc->groupOf(m_ids[1]).isNull());
        QVERIFY(fuzzyPoint(m_doc->worldTransform(m_ids[1]).map({33, 77}), movedB.map({33, 77})));
        QCOMPARE(m_doc->validate(), QString());
    }

    void groupingRules()
    {
        const OpResult grouped = ops::group(*m_doc, {m_ids[0], m_ids[1]}, {});
        QVERIFY(applyOp(*m_doc, grouped));
        const Id g = grouped.created.first();
        QVERIFY(!ops::group(*m_doc, {g, m_ids[2]}, {}));        // nested
        QVERIFY(!ops::group(*m_doc, {m_ids[0], m_ids[2]}, {}));  // already grouped
        QVERIFY(!ops::group(*m_doc, {m_ids[2]}, {}));            // fewer than two
        QVERIFY(applyOp(*m_doc, ops::setLocked(*m_doc, {m_ids[2]}, true)));
        const OpResult withLocked = ops::group(*m_doc, {m_ids[2], g}, {});
        QVERIFY(!withLocked);
        QVERIFY(!ops::setLocked(*m_doc, {m_ids[0]}, true)); // children cannot be locked
        QVERIFY(!ops::setLocked(*m_doc, {m_ids[0]}, true).error.isEmpty());
    }

    void locksProtectEverything()
    {
        QVERIFY(applyOp(*m_doc, ops::setLocked(*m_doc, {m_ids[0]}, true)));
        const Snapshot before = m_doc->snapshotAll();
        QVERIFY(!ops::transform(*m_doc, {m_ids[0]}, Affine2D::translation(5, 5)));
        QVERIFY(!ops::flip(*m_doc, {m_ids[0]}, Qt::Horizontal));
        QVERIFY(!ops::setCrop(*m_doc, m_ids[0], QRectF(0, 0, 0.5, 0.5)));
        QVERIFY(!ops::remove(*m_doc, {m_ids[0]}));
        QVERIFY(!ops::resetCrop(*m_doc, {m_ids[0]}));
        QCOMPARE(m_doc->snapshotAll(), before);

        // Mixed selections change the unlocked part and report the rest.
        const OpResult moved = ops::transform(*m_doc, {m_ids[0], m_ids[1]}, Affine2D::translation(5, 0));
        QVERIFY(moved);
        QCOMPARE(moved.skippedLocked, 1);
        QCOMPARE(moved.change->update.images.size(), 1);
        const OpResult removed = ops::remove(*m_doc, {m_ids[0], m_ids[2]});
        QCOMPARE(removed.skippedLocked, 1);
        QCOMPARE(removed.change->remove, QList<Id>{m_ids[2]});

        // A locked group protects its children.
        QVERIFY(applyOp(*m_doc, ops::setLocked(*m_doc, {m_ids[0]}, false)));
        const OpResult grouped = ops::group(*m_doc, {m_ids[0], m_ids[1]}, {});
        QVERIFY(applyOp(*m_doc, grouped));
        QVERIFY(applyOp(*m_doc, ops::setLocked(*m_doc, grouped.created, true)));
        QVERIFY(ops::isLocked(*m_doc, m_ids[0]));
        QVERIFY(!ops::transform(*m_doc, {m_ids[0]}, Affine2D::translation(1, 1)));
        QVERIFY(!ops::ungroup(*m_doc, grouped.created));
        QVERIFY(!ops::arrange(*m_doc, grouped.created, 20));
    }

    void removingLastChildRemovesGroup()
    {
        const OpResult grouped = ops::group(*m_doc, {m_ids[0], m_ids[1]}, {});
        QVERIFY(applyOp(*m_doc, grouped));
        const Id g = grouped.created.first();
        QVERIFY(applyOp(*m_doc, ops::remove(*m_doc, {m_ids[0]})));
        QCOMPARE(m_doc->group(g)->children, QList<Id>{m_ids[1]});
        QVERIFY(applyOp(*m_doc, ops::remove(*m_doc, {m_ids[1]})));
        QVERIFY(!m_doc->group(g));
        QCOMPARE(m_doc->validate(), QString());
    }

    void zOrderOperations()
    {
        auto order = [this] { return m_doc->topLevelIdsByZ(); };
        QCOMPARE(order(), (QList<Id>{m_ids[0], m_ids[1], m_ids[2]}));
        QVERIFY(applyOp(*m_doc, ops::zOrder(*m_doc, {m_ids[0]}, ZOrder::BringToFront)));
        QCOMPARE(order(), (QList<Id>{m_ids[1], m_ids[2], m_ids[0]}));
        QVERIFY(applyOp(*m_doc, ops::zOrder(*m_doc, {m_ids[0]}, ZOrder::Lower)));
        QCOMPARE(order(), (QList<Id>{m_ids[1], m_ids[0], m_ids[2]}));
        QVERIFY(applyOp(*m_doc, ops::zOrder(*m_doc, {m_ids[2]}, ZOrder::SendToBack)));
        QCOMPARE(order(), (QList<Id>{m_ids[2], m_ids[1], m_ids[0]}));
        QVERIFY(applyOp(*m_doc, ops::zOrder(*m_doc, {m_ids[2]}, ZOrder::Raise)));
        QCOMPARE(order(), (QList<Id>{m_ids[1], m_ids[2], m_ids[0]}));
        QVERIFY(!ops::zOrder(*m_doc, {m_ids[0]}, ZOrder::BringToFront)); // already in front: no-op
    }

    void alignUsesVisibleBounds()
    {
        QVERIFY(applyOp(*m_doc, ops::align(*m_doc, {m_ids[0], m_ids[1], m_ids[2]}, Align::Right)));
        for (const Id &id : m_ids)
            QCOMPARE(m_doc->worldBounds(id).right(), 300.0);
        QVERIFY(applyOp(*m_doc, ops::align(*m_doc, {m_ids[0], m_ids[2]}, Align::Top)));
        QCOMPARE(m_doc->worldBounds(m_ids[2]).top(), 0.0);
        QVERIFY(!ops::align(*m_doc, {m_ids[0]}, Align::Left)); // needs two
    }

    void arrangeIsDeterministicAndRespectsLocksAndGroups()
    {
        QVERIFY(applyOp(*m_doc, ops::setLocked(*m_doc, {m_ids[2]}, true)));
        const QRectF locked = m_doc->worldBounds(m_ids[2]);
        const auto more = m_doc->addImages({fakeImage(4, {900, 900}, {50, 80}), fakeImage(5, {600, -300})});
        QVERIFY(more);
        const OpResult grouped = ops::group(*m_doc, more->itemIds, {});
        QVERIFY(applyOp(*m_doc, grouped));

        const OpResult a = ops::arrange(*m_doc, {}, 20);
        const OpResult b = ops::arrange(*m_doc, {}, 20);
        QVERIFY(a && b);
        QCOMPARE(a.skippedLocked, 1);
        QVERIFY(applyOp(*m_doc, a));
        QCOMPARE(m_doc->worldBounds(m_ids[2]), locked); // locked item stays
        // Units (a, b, the group) do not overlap each other after arranging.
        const QList<Id> units{m_ids[0], m_ids[1], grouped.created.first()};
        for (qsizetype i = 0; i < units.size(); ++i)
            for (qsizetype j = i + 1; j < units.size(); ++j)
                QVERIFY(!m_doc->worldBounds(units[i]).intersects(m_doc->worldBounds(units[j])));
        // Arranging again changes nothing (stable).
        QVERIFY(!ops::arrange(*m_doc, {}, 20));
    }

    void notes()
    {
        const OpResult created = ops::createNote(*m_doc, {500, 500}, u"Reference: rim light"_s);
        QVERIFY(applyOp(*m_doc, created));
        const Id note = created.created.first();
        const NoteData *n = m_doc->note(note);
        QVERIFY(n);
        QVERIFY(n->height > 0);
        QVERIFY(fuzzyPoint(m_doc->worldBounds(note).center(), {500, 500}, 0.5));

        const double singleLine = n->height;
        QVERIFY(applyOp(*m_doc, ops::editNote(*m_doc, note, u"line one\nline two\nline three"_s)));
        QVERIFY(m_doc->note(note)->height > singleLine);
        QVERIFY(applyOp(*m_doc, ops::setNoteWidth(*m_doc, note, 60))); // narrower: more lines
        QVERIFY(m_doc->note(note)->width == 60);
        QVERIFY(applyOp(*m_doc, ops::setNoteStyle(*m_doc, {note}, 30, 0xFF000000u, 0xFFFFFFFFu)));
        QCOMPARE(m_doc->note(note)->fontSize, 30.0);
        QVERIFY(!ops::editNote(*m_doc, note, QString(m_doc->limits().maxNoteCharacters + 1, u'x')));
        QCOMPARE(m_doc->validate(), QString());
    }

    void backgroundAndGrayscale()
    {
        QVERIFY(applyOp(*m_doc, ops::setBackground(*m_doc, 0x00112233u)));
        QCOMPARE(m_doc->view().backgroundColor, 0xFF112233u); // forced opaque
        QVERIFY(!ops::setBackground(*m_doc, 0xFF112233u));      // unchanged: no-op
        QVERIFY(applyOp(*m_doc, ops::setGrayscale(*m_doc, true)));
        QVERIFY(m_doc->view().grayscale);
    }

    void pasteAndDuplicateUseFreshIds()
    {
        const OpResult grouped = ops::group(*m_doc, {m_ids[0], m_ids[1]}, u"pair"_s);
        QVERIFY(applyOp(*m_doc, grouped));
        const Snapshot fragment = m_doc->snapshot({grouped.created.first(), m_ids[2]});

        const OpResult pasted = ops::paste(*m_doc, fragment, QPointF(1000, 1000));
        QVERIFY(applyOp(*m_doc, pasted));
        QCOMPARE(pasted.created.size(), 2); // the group and the loose image
        QCOMPARE(m_doc->itemCount(), 6);
        QCOMPARE(m_doc->assetCount(), 3);   // assets matched by digest, not duplicated
        QCOMPARE(m_doc->groups().size(), 2);
        QRectF pastedBounds;
        for (const Id &id : pasted.created)
            pastedBounds = pastedBounds.isNull() ? m_doc->worldBounds(id) : pastedBounds.united(m_doc->worldBounds(id));
        QVERIFY(fuzzyPoint(pastedBounds.center(), {1000, 1000}));
        QSet<Id> sourceIds;
        for (const ImageItemData &i : fragment.images)
            sourceIds.insert(i.id);
        for (const GroupData &g : fragment.groups)
            sourceIds.insert(g.id);
        for (const Id &id : pasted.created)
            QVERIFY(!sourceIds.contains(id)); // fresh IDs, originals untouched
        QVERIFY(m_doc->group(grouped.created.first())); // the source group still exists

        const OpResult dup = ops::duplicate(*m_doc, {m_ids[2]});
        QVERIFY(applyOp(*m_doc, dup));
        QCOMPARE(m_doc->worldBounds(dup.created.first()), m_doc->worldBounds(m_ids[2]).translated(20, 20));
        QCOMPARE(m_doc->validate(), QString());
    }
};

GLACIER_TEST_GUI_MAIN(TestOperations)
#include "tst_operations.moc"
