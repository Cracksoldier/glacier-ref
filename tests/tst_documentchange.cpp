#include "GlacierTestMain.h"
#include "TestBoards.h"

#include "glacier/core/BoardDocument.h"

#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>

using namespace Qt::StringLiterals;
using namespace glacier;
using glacier::test::fakeImage;

Q_DECLARE_METATYPE(glacier::DocumentDelta)

class TestDocumentChange : public QObject
{
    Q_OBJECT

    static std::unique_ptr<BoardDocument> board(QList<Id> *ids)
    {
        auto doc = std::make_unique<BoardDocument>();
        const auto added = doc->addImages({fakeImage(1, {0, 0}), fakeImage(2, {200, 0}), fakeImage(3, {0, 200})});
        *ids = added->itemIds;
        return doc;
    }

private slots:
    void inverseRestoresExactState()
    {
        QList<Id> ids;
        auto doc = board(&ids);
        const Snapshot before = doc->snapshotAll();
        const ViewSettings viewBefore = doc->view();

        DocumentChange change;
        ImageItemData moved = *doc->image(ids[0]);
        moved.transform = Affine2D{0.5, 0.5, -0.5, 0.5, 30, 40};
        moved.crop = QRectF(0.1, 0.2, 0.5, 0.5);
        change.update.images.append(moved);
        change.remove.append(ids[2]);
        NoteData note;
        note.id = Id::generate();
        note.text = u"hello"_s;
        change.insert.notes.append(note);
        change.view = ViewSettings{0xFF112233u, true};

        QSignalSpy changed(doc.get(), &BoardDocument::changed);
        const auto inverse = doc->apply(change);
        QVERIFY(inverse);
        QCOMPARE(doc->validate(), QString());
        QCOMPARE(changed.size(), 1);
        const auto delta = changed.first().first().value<DocumentDelta>();
        QCOMPARE(delta.removed, QList<Id>{ids[2]});
        QCOMPARE(delta.inserted, QList<Id>{note.id});
        QCOMPARE(delta.updated, QList<Id>{ids[0]});
        QVERIFY(delta.viewChanged);
        QCOMPARE(doc->view().grayscale, true);

        const Snapshot after = doc->snapshotAll();
        const auto forward = doc->apply(*inverse);
        QVERIFY(forward);
        QCOMPARE(doc->snapshotAll(), before);
        QCOMPARE(doc->view(), viewBefore);
        QVERIFY(doc->apply(*forward)); // redo
        QCOMPARE(doc->snapshotAll(), after);
    }

    void invalidChangeIsAtomic_data()
    {
        QTest::addColumn<int>("defect");
        QTest::newRow("update missing") << 0;
        QTest::newRow("remove missing") << 1;
        QTest::newRow("insert duplicate id") << 2;
        QTest::newRow("update asset") << 3;
        QTest::newRow("empty group") << 4;
        QTest::newRow("nested group") << 5;
        QTest::newRow("membership mismatch") << 6;
        QTest::newRow("note too long") << 7;
        QTest::newRow("removing referenced asset") << 8;
        QTest::newRow("non-invertible transform") << 9;
    }

    void invalidChangeIsAtomic()
    {
        QFETCH(int, defect);
        QList<Id> ids;
        auto doc = board(&ids);
        const Snapshot before = doc->snapshotAll();
        const quint64 revision = doc->revision();

        DocumentChange change;
        ImageItemData ok = *doc->image(ids[1]);
        ok.transform = Affine2D::translation(999, 999);
        change.update.images.append(ok); // a valid part that must not survive
        switch (defect) {
        case 0: {
            ImageItemData ghost = *doc->image(ids[0]);
            ghost.id = Id::generate();
            change.update.images.append(ghost);
            break;
        }
        case 1: change.remove.append(Id::generate()); break;
        case 2: {
            ImageItemData dup = *doc->image(ids[0]);
            change.insert.images.append(dup);
            break;
        }
        case 3: change.update.assets.append(*doc->asset(doc->image(ids[0])->assetId)); break;
        case 4: {
            GroupData g;
            g.id = Id::generate();
            change.insert.groups.append(g);
            break;
        }
        case 5: {
            GroupData inner{Id::generate(), u"inner"_s, {}, {ids[2]}, 1, false};
            GroupData outer{Id::generate(), u"outer"_s, {}, {inner.id}, 2, false};
            ImageItemData child = *doc->image(ids[2]);
            child.groupId = inner.id;
            change.update.images.append(child);
            change.insert.groups.append(inner);
            change.insert.groups.append(outer);
            break;
        }
        case 6: {
            GroupData g{Id::generate(), u"g"_s, {}, {ids[0]}, 1, false}; // child not marked as member
            change.insert.groups.append(g);
            break;
        }
        case 7: {
            NoteData n;
            n.id = Id::generate();
            n.text = QString(doc->limits().maxNoteCharacters + 1, u'x');
            change.insert.notes.append(n);
            break;
        }
        case 8: change.remove.append(doc->image(ids[0])->assetId); break;
        case 9: {
            ImageItemData bad = *doc->image(ids[0]);
            bad.transform = Affine2D::scaling(0, 1);
            change.update.images.append(bad);
            break;
        }
        }
        QString error;
        QVERIFY(!doc->apply(change, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(doc->snapshotAll(), before);
        QCOMPARE(doc->revision(), revision);
    }

    void groupsAndQueries()
    {
        QList<Id> ids;
        auto doc = board(&ids);
        GroupData g{Id::generate(), u"refs"_s, Affine2D::translation(100, 100), {ids[0], ids[1]}, 10, false};
        DocumentChange change;
        change.insert.groups.append(g);
        for (int i : {0, 1}) {
            ImageItemData child = *doc->image(ids[i]);
            child.groupId = g.id;
            child.transform = Affine2D::translation(i * 200 - 100, -100); // world unchanged
            change.update.images.append(child);
        }
        QVERIFY(doc->apply(change));
        QCOMPARE(doc->validate(), QString());
        QCOMPARE(doc->groupOf(ids[0]), g.id);
        QCOMPARE(doc->worldTransform(ids[1]).translationPart(), QPointF(200, 0));
        QCOMPARE(doc->worldBounds(g.id), QRectF(0, 0, 300, 100));
        QCOMPARE(doc->topLevelIdsByZ(), (QList<Id>{ids[2], g.id}));
        QCOMPARE(doc->itemIdsByZ(), (QList<Id>{ids[2], ids[0], ids[1]}));
        const Snapshot snap = doc->snapshot({g.id});
        QCOMPARE(snap.groups.size(), 1);
        QCOMPARE(snap.images.size(), 2);
        QCOMPARE(snap.assets.size(), 2);
        QCOMPARE(doc->kind(g.id), EntityKind::Group);
    }
};

GLACIER_TEST_CORE_MAIN(TestDocumentChange)
#include "tst_documentchange.moc"
