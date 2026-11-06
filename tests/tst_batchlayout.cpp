#include "GlacierTestMain.h"

#include "glacier/core/BatchLayout.h"
#include "glacier/core/Limits.h"

#include <QCoreApplication>
#include <QObject>
#include <QRectF>

using glacier::layoutBatch;

class TestBatchLayout : public QObject
{
    Q_OBJECT

private slots:
    void emptyBatch() { QVERIFY(layoutBatch({}, {}).isEmpty()); }

    void singleItemIsCentredOnAnchor()
    {
        const auto p = layoutBatch({QSizeF(100, 50)}, QPointF(500, 500));
        QCOMPARE(p, QList<QPointF>{QPointF(450, 475)});
    }

    void gridIsDeterministicAndNonOverlapping()
    {
        QList<QSizeF> sizes;
        for (int i = 0; i < 9; ++i)
            sizes.append(QSizeF(100, 100));
        const auto p = layoutBatch(sizes, QPointF(0, 0), 20);
        QCOMPARE(p, layoutBatch(sizes, QPointF(0, 0), 20));
        QCOMPARE(p.size(), 9);
        // Nine equal squares form a 3x3 grid (row width target ~ sqrt(area)).
        QCOMPARE(p[1] - p[0], QPointF(120, 0));
        QCOMPARE(p[3] - p[0], QPointF(0, 120));
        for (int i = 0; i < p.size(); ++i)
            for (int j = i + 1; j < p.size(); ++j)
                QVERIFY(!QRectF(p[i], sizes[i]).intersects(QRectF(p[j], sizes[j])));
        // Centred on the anchor.
        QCOMPARE(p.first() + QPointF(170, 170), QPointF(0, 0));
    }

    void staysInsideCoordinateRange()
    {
        const double limit = glacier::Limits::CoordinateLimit;
        const auto p = layoutBatch({QSizeF(1000, 1000)}, QPointF(limit, -limit));
        QVERIFY(QRectF(-limit, -limit, 2 * limit, 2 * limit).contains(QRectF(p.first(), QSizeF(1000, 1000))));
    }
};

GLACIER_TEST_CORE_MAIN(TestBatchLayout)
#include "tst_batchlayout.moc"
