#include "GlacierTestMain.h"

#include "glacier/core/BoardDocument.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QObject>
#include <QSignalSpy>

#include <limits>

using namespace Qt::StringLiterals;
using namespace glacier;

namespace {

QByteArray digestOf(const QByteArray &content)
{
    return QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex();
}

NewImage image(const QByteArray &content, QPointF pos = {}, QSize size = {100, 50})
{
    NewImage n;
    n.sha256 = digestOf(content);
    n.mediaType = u"image/png"_s;
    n.orientedSize = size;
    n.byteLength = content.size() + 1;
    n.position = pos;
    return n;
}

} // namespace

class TestBoardDocument : public QObject
{
    Q_OBJECT

private slots:
    void startsEmptyAndClean()
    {
        BoardDocument doc;
        QCOMPARE(doc.itemCount(), 0);
        QCOMPARE(doc.revision(), 0u);
        QVERIFY(!doc.isDirty());
        QVERIFY(!doc.documentToken().isNull());
        QVERIFY(doc.boundingRect().isNull());
        QCOMPARE(doc.validate(), QString());
    }

    void addImagesIsOneRevisionAndSignal()
    {
        BoardDocument doc;
        QSignalSpy added(&doc, &BoardDocument::itemsAdded);
        QString error;
        const auto result = doc.addImages({image("a", {0, 0}), image("b", {200, 0})}, &error);
        QVERIFY2(result.has_value(), qPrintable(error));
        QCOMPARE(result->itemIds.size(), 2);
        QCOMPARE(result->newAssetIds.size(), 2);
        QCOMPARE(doc.revision(), 1u);
        QVERIFY(doc.isDirty());
        QCOMPARE(added.size(), 1);
        QCOMPARE(added.first().first().value<QList<Id>>(), result->itemIds);
        QCOMPARE(doc.boundingRect(), QRectF(0, 0, 300, 50));
        QCOMPARE(doc.validate(), QString());
    }

    void displaySizeDefaultsToOrientedSize()
    {
        BoardDocument doc;
        const auto result = doc.addImages({image("a", {5, 6}, {640, 480})});
        QVERIFY(result);
        const ImageItemData *item = doc.item(result->itemIds.first());
        QVERIFY(item);
        QCOMPARE(item->displaySize, QSizeF(640, 480));
        QCOMPARE(item->transform.translationPart(), QPointF(5, 6));
        QCOMPARE(item->crop, QRectF(0, 0, 1, 1));
        QVERIFY(!item->locked);
    }

    void assetsAreDeduplicatedByDigest()
    {
        BoardDocument doc;
        const auto first = doc.addImages({image("same"), image("same", {300, 0})});
        QVERIFY(first);
        QCOMPARE(first->itemIds.size(), 2);
        QCOMPARE(first->newAssetIds.size(), 1);
        const auto second = doc.addImages({image("same", {600, 0})});
        QVERIFY(second);
        QVERIFY(second->newAssetIds.isEmpty());
        QCOMPARE(doc.assetCount(), 1);
        QCOMPARE(doc.item(second->itemIds.first())->assetId, first->newAssetIds.first());
    }

    void zOrderFollowsInsertion()
    {
        BoardDocument doc;
        const auto a = doc.addImages({image("a"), image("b")});
        const auto b = doc.addImages({image("c")});
        QVERIFY(a && b);
        const QList<Id> expected{a->itemIds[0], a->itemIds[1], b->itemIds[0]};
        QCOMPARE(doc.itemIdsByZ(), expected);
    }

    void rejectsInvalidBatchAtomically_data()
    {
        QTest::addColumn<int>("defect");
        QTest::newRow("bad digest") << 0;
        QTest::newRow("unsupported media type") << 1;
        QTest::newRow("zero size") << 2;
        QTest::newRow("too many pixels") << 3;
        QTest::newRow("non-finite position") << 4;
        QTest::newRow("outside coordinate range") << 5;
        QTest::newRow("asset too large") << 6;
    }

    void rejectsInvalidBatchAtomically()
    {
        QFETCH(int, defect);
        Limits limits;
        limits.maxDecodedPixels = 1000 * 1000;
        BoardDocument doc(limits);
        NewImage bad = image("bad");
        switch (defect) {
        case 0: bad.sha256 = "not-a-digest"; break;
        case 1: bad.mediaType = u"image/x-photoshop"_s; break;
        case 2: bad.orientedSize = QSize(0, 10); break;
        case 3: bad.orientedSize = QSize(2000, 1000); break;
        case 4: bad.position = QPointF(std::numeric_limits<double>::quiet_NaN(), 0); break;
        case 5: bad.position = QPointF(Limits::CoordinateLimit - 10, 0); break;
        case 6: bad.byteLength = limits.maxAssetBytes + 1; break;
        }
        QSignalSpy added(&doc, &BoardDocument::itemsAdded);
        QString error;
        QVERIFY(!doc.addImages({image("good"), bad}, &error).has_value());
        QVERIFY(!error.isEmpty());
        QCOMPARE(doc.itemCount(), 0);
        QCOMPARE(doc.assetCount(), 0);
        QCOMPARE(doc.revision(), 0u);
        QCOMPARE(added.size(), 0);
        QCOMPARE(doc.validate(), QString());
    }

    void enforcesItemLimit()
    {
        Limits limits;
        limits.maxItems = 3;
        BoardDocument doc(limits);
        QVERIFY(doc.addImages({image("a"), image("b")}));
        QString error;
        QVERIFY(!doc.addImages({image("c"), image("d")}, &error));
        QVERIFY(error.contains(u"3"_s));
        QCOMPARE(doc.itemCount(), 2);
        QVERIFY(doc.addImages({image("c")}));
    }

    void removeItemsKeepsAssetsUntilRemoved()
    {
        BoardDocument doc;
        const auto result = doc.addImages({image("a"), image("b")});
        QVERIFY(result);
        QSignalSpy removed(&doc, &BoardDocument::itemsRemoved);

        QString error;
        QVERIFY(!doc.removeAssets({result->newAssetIds.first()}, &error)); // still referenced
        QVERIFY(doc.removeItems({result->itemIds.first()}));
        QCOMPARE(removed.size(), 1);
        QCOMPARE(doc.itemCount(), 1);
        QCOMPARE(doc.assetCount(), 2);
        QVERIFY(doc.removeAssets({result->newAssetIds.first()}));
        QCOMPARE(doc.assetCount(), 1);
        QVERIFY(!doc.assetByDigest(digestOf("a")));
        QCOMPARE(doc.validate(), QString());

        QVERIFY(!doc.removeItems({Id::generate()}, &error)); // unknown item: nothing removed
        QCOMPARE(doc.itemCount(), 1);
    }

    void markCleanTracksRevision()
    {
        BoardDocument doc;
        QVERIFY(doc.addImages({image("a")}));
        doc.markClean();
        QVERIFY(!doc.isDirty());
        QVERIFY(doc.addImages({image("b")}));
        QVERIFY(doc.isDirty());
    }
};

GLACIER_TEST_CORE_MAIN(TestBoardDocument)
#include "tst_boarddocument.moc"
