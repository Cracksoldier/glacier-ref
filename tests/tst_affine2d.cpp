#include "GlacierTestMain.h"

#include "glacier/core/Affine2D.h"

#include <QCoreApplication>
#include <QObject>

#include <cmath>
#include <limits>

using glacier::Affine2D;

namespace {
bool fuzzyEqual(QPointF a, QPointF b)
{
    return std::abs(a.x() - b.x()) < 1e-9 && std::abs(a.y() - b.y()) < 1e-9;
}
} // namespace

class TestAffine2D : public QObject
{
    Q_OBJECT

private slots:
    void identityByDefault()
    {
        const Affine2D t;
        QCOMPARE(t.map(QPointF(3, -4)), QPointF(3, -4));
        QVERIFY(t.isInvertible());
    }

    void compositionAppliesLeftFirst()
    {
        // Same convention as QTransform: (a * b).map(p) == b.map(a.map(p)).
        const Affine2D scale = Affine2D::scaling(2, 3);
        const Affine2D move = Affine2D::translation(10, 20);
        const QPointF p(1, 1);
        QVERIFY(fuzzyEqual((scale * move).map(p), move.map(scale.map(p))));
        QCOMPARE((scale * move).map(p), QPointF(12, 23));
        QCOMPARE((move * scale).map(p), QPointF(22, 63));
    }

    void inverseRoundTrips()
    {
        const Affine2D t{0.8, 0.6, -0.6, 0.8, 120.5, -42.25}; // rotation + translation
        const QPointF p(17.0, -3.5);
        QVERIFY(fuzzyEqual(t.inverted().map(t.map(p)), p));
        QVERIFY(fuzzyEqual((t * t.inverted()).map(p), p));
    }

    void singularAndNonFiniteAreRejected()
    {
        QVERIFY(!Affine2D::scaling(0, 1).isInvertible());
        Affine2D nan;
        nan.dx = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!nan.isFinite());
        QVERIFY(!nan.isInvertible());
        Affine2D inf;
        inf.m11 = std::numeric_limits<double>::infinity();
        QVERIFY(!inf.isFinite());
    }

    void mapRectBoundsRotatedRect()
    {
        const Affine2D quarterTurn{0, 1, -1, 0, 0, 0}; // 90 degrees
        const QRectF bounds = quarterTurn.mapRect(QRectF(0, 0, 4, 2));
        QCOMPARE(bounds, QRectF(-2, 0, 2, 4));
    }
};

GLACIER_TEST_CORE_MAIN(TestAffine2D)
#include "tst_affine2d.moc"
