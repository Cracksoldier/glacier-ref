#include "GlacierTestMain.h"
#include "TestBoards.h"

#include "glacier/core/SessionAssetStore.h"
#include "glacier/editing/ClipboardFragment.h"
#include "glacier/editing/Operations.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QGuiApplication>
#include <QObject>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::editing;

namespace {

// A fragment with real asset bytes (digest must match).
ClipboardFragment sampleFragment(BoardDocument &doc)
{
    const QByteArray bytesA("first image bytes"), bytesB("second image bytes");
    NewImage a = test::fakeImage(0, {0, 0});
    a.sha256 = SessionAssetStore::sha256Hex(bytesA);
    a.byteLength = bytesA.size();
    NewImage b = test::fakeImage(0, {200, 0});
    b.sha256 = SessionAssetStore::sha256Hex(bytesB);
    b.byteLength = bytesB.size();
    const auto ids = doc.addImages({a, b})->itemIds;
    const OpResult grouped = ops::group(doc, ids, u"pair"_s);
    doc.apply(*grouped.change);
    doc.apply(*ops::createNote(doc, {50, 300}, u"Größe ✓"_s).change);

    ClipboardFragment fragment;
    fragment.snapshot = doc.snapshot(doc.topLevelIdsByZ());
    fragment.assetBytes.insert(a.sha256, bytesA);
    fragment.assetBytes.insert(b.sha256, bytesB);
    return fragment;
}

} // namespace

class TestClipboardFragment : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip()
    {
        BoardDocument doc;
        const ClipboardFragment fragment = sampleFragment(doc);
        const QByteArray encoded = encodeFragment(fragment);
        QString error;
        const auto decoded = decodeFragment(encoded, {}, &error);
        QVERIFY2(decoded, qPrintable(error));
        QCOMPARE(decoded->snapshot, fragment.snapshot);
        QCOMPARE(decoded->assetBytes, fragment.assetBytes);

        // And it pastes into another board.
        BoardDocument other;
        const OpResult pasted = ops::paste(other, decoded->snapshot, QPointF(0, 0));
        QVERIFY(pasted.change && other.apply(*pasted.change));
        QCOMPARE(other.itemCount(), 3);
        QCOMPARE(other.validate(), QString());
    }

    void rejectsMalformedInput_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty") << QByteArray() << u"empty"_s;
        QTest::newRow("garbage") << QByteArray("\xff\x00\x13 not cbor", 13) << u"not a Glacier Ref"_s;
        QTest::newRow("wrong format") << QCborValue(QCborMap{{u"format"_s, u"other"_s}}).toCbor() << u"not a Glacier Ref"_s;
        QTest::newRow("future version")
            << QCborValue(QCborMap{{u"format"_s, u"glacier-ref-items"_s}, {u"version"_s, 99}}).toCbor()
            << u"version"_s;
    }

    void rejectsMalformedInput()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, expected);
        QString error;
        QVERIFY(!decodeFragment(data, {}, &error));
        QVERIFY2(error.contains(expected), qPrintable(error));
    }

    void rejectsTamperedContent_data()
    {
        QTest::addColumn<int>("defect");
        QTest::newRow("digest mismatch") << 0;
        QTest::newRow("bad id") << 1;
        QTest::newRow("non-finite transform") << 2;
        QTest::newRow("oversized note") << 3;
        QTest::newRow("asset over limit") << 4;
    }

    void rejectsTamperedContent()
    {
        QFETCH(int, defect);
        BoardDocument doc;
        ClipboardFragment fragment = sampleFragment(doc);
        Limits limits;
        switch (defect) {
        case 0: fragment.assetBytes[fragment.snapshot.assets.first().sha256] = "tampered"; break;
        case 1: {
            QCborMap root = QCborValue::fromCbor(encodeFragment(fragment)).toMap();
            QCborArray images = root.value(u"images"_s).toArray();
            QCborMap first = images.at(0).toMap();
            first.insert(u"id"_s, u"../../etc"_s);
            images[0] = first;
            root.insert(u"images"_s, images);
            QVERIFY(!decodeFragment(QCborValue(root).toCbor(), limits));
            return;
        }
        case 2: fragment.snapshot.images.first().transform.dx = std::numeric_limits<double>::infinity(); break;
        case 3: limits.maxNoteCharacters = 3; break;
        case 4: limits.maxAssetBytes = 4; break;
        }
        QString error;
        QVERIFY(!decodeFragment(encodeFragment(fragment), limits, &error));
        QVERIFY(!error.isEmpty());
    }
};

GLACIER_TEST_GUI_MAIN(TestClipboardFragment)
#include "tst_clipboardfragment.moc"
