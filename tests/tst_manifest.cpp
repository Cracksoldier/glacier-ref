#include "GlacierTestMain.h"
#include "TestBoardFiles.h"
#include "TestBoards.h"

#include "glacier/persistence/Manifest.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

#include <functional>

using namespace Qt::StringLiterals;
using namespace glacier;
using namespace glacier::persistence;
using namespace glacier::test;

namespace {

using Edit = std::function<void(QJsonObject &)>;

QJsonObject toObject(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

QByteArray toJson(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Edits the first element of array `key` with `edit`.
Edit inFirst(const QString &key, std::function<void(QJsonObject &)> edit)
{
    return [key, edit](QJsonObject &root) {
        QJsonArray a = root.value(key).toArray();
        QJsonObject first = a.at(0).toObject();
        edit(first);
        a.replace(0, first);
        root.insert(key, a);
    };
}

// Edits the item with the given ID.
Edit inItem(const Id &id, std::function<void(QJsonObject &)> edit)
{
    return [id, edit](QJsonObject &root) {
        QJsonArray a = root.value(u"items"_s).toArray();
        for (qsizetype i = 0; i < a.size(); ++i) {
            QJsonObject o = a.at(i).toObject();
            if (o.value(u"id"_s).toString() == id.toString()) {
                edit(o);
                a.replace(i, o);
            }
        }
        root.insert(u"items"_s, a);
    };
}

} // namespace

Q_DECLARE_METATYPE(Edit)

class TestManifest : public QObject
{
    Q_OBJECT

    StoreFixture m_fixture;

private slots:
    void initTestCase()
    {
        QVERIFY(m_fixture.store);
    }

    void roundTripPreservesEverything()
    {
        const BoardContent board = richBoard(*m_fixture.store);
        ManifestWriteOptions options;
        options.generator = u"Glacier Ref test"_s;
        const QByteArray json = writeManifest(board, options);
        QString error;
        const auto read = readManifest(json, QString(), Limits{}, &error);
        QVERIFY2(read, qPrintable(error));
        QCOMPARE(sorted(read->content), sorted(board));
        QCOMPARE(read->root.value(u"generator"_s).toString(), u"Glacier Ref test"_s);

        // Writing again gives identical text (deterministic, locale-independent).
        QCOMPARE(writeManifest(read->content, options), json);
    }

    void writesTheDocumentedShape()
    {
        const BoardContent board = richBoard(*m_fixture.store);
        const QJsonObject root = toObject(writeManifest(board, {}));
        QCOMPARE(root.value(u"format"_s).toString(), u"glacier-ref"_s);
        QCOMPARE(root.value(u"schemaVersion"_s).toInt(), 1);
        QCOMPARE(root.value(u"documentId"_s).toString(), u"00000000-0000-4000-8000-000000000001"_s);
        QCOMPARE(root.value(u"created"_s).toString(), u"2026-11-07T10:00:00.250Z"_s);
        const QJsonObject view = root.value(u"view"_s).toObject();
        QCOMPARE(view.value(u"background"_s).toString(), u"#FF20242A"_s);
        QCOMPARE(view.value(u"zoom"_s).toDouble(), 0.75);
        // Decimal points, never commas, whatever the locale (de_AT run).
        const QByteArray json = writeManifest(board, {});
        QVERIFY(json.contains("\"zoom\": 0.75"));
        QVERIFY(json.contains("-45.25"));

        const QJsonObject asset = root.value(u"assets"_s).toArray().at(0).toObject();
        QCOMPARE(asset.value(u"kind"_s).toString(), u"embedded"_s);
        QCOMPARE(asset.value(u"entry"_s).toString(),
                 u"assets/"_s + asset.value(u"sha256"_s).toString() + u".png"_s);
        for (const QJsonValue &v : root.value(u"items"_s).toArray()) {
            const QJsonObject item = v.toObject();
            QCOMPARE(item.value(u"transform"_s).toArray().size(), 6);
        }
    }

    void dropsUnreferencedAssets()
    {
        BoardContent board = richBoard(*m_fixture.store);
        Asset orphan = storeAsset(*m_fixture.store, encode(markedImage({5, 5}, Qt::black), "png"));
        board.snapshot.assets.append(orphan);
        const auto read = readManifest(writeManifest(board, {}), QString(), Limits{}, nullptr);
        QVERIFY(read);
        QCOMPARE(read->content.snapshot.assets.size(), board.snapshot.assets.size() - 1);
    }

    void linkedPathsAreRelativeToTheBoard()
    {
        BoardContent board = richBoard(*m_fixture.store);
        Asset linked = board.snapshot.assets.first();
        linked.id = fixedId(99);
        linked.sha256 = fakeDigest(99);
        linked.linked = true;
        linked.linkPath = u"/home/a/pics/ref one.png"_s;
        board.snapshot.assets.append(linked);
        ImageItemData item = board.snapshot.images.first();
        item.id = fixedId(98);
        item.assetId = linked.id;
        board.snapshot.images.append(item);

        ManifestWriteOptions options;
        options.boardDir = u"/home/a/boards"_s;
        const QByteArray json = writeManifest(board, options);
        const QJsonArray assets = toObject(json).value(u"assets"_s).toArray();
        const QJsonObject stored = assets.last().toObject();
        QCOMPARE(stored.value(u"kind"_s).toString(), u"linked"_s);
        QCOMPARE(stored.value(u"path"_s).toString(), u"../pics/ref one.png"_s);
        QCOMPARE(stored.value(u"pathKind"_s).toString(), u"relative"_s);
        QVERIFY(!stored.contains(u"entry"_s));

        // Read back from another location: the link resolves against it.
        const auto read = readManifest(json, u"/mnt/copy/boards"_s, Limits{}, nullptr);
        QVERIFY(read);
        for (const Asset &a : read->content.snapshot.assets)
            if (a.linked)
                QCOMPARE(a.linkPath, u"/mnt/copy/pics/ref one.png"_s);
        // Unsaved boards (no directory) store absolute paths.
        const QJsonObject absolute = toObject(writeManifest(board, {})).value(u"assets"_s).toArray().last().toObject();
        QCOMPARE(absolute.value(u"pathKind"_s).toString(), u"absolute"_s);
        QCOMPARE(absolute.value(u"path"_s).toString(), u"/home/a/pics/ref one.png"_s);
    }

    void ignoresUnknownMembers()
    {
        QJsonObject root = toObject(writeManifest(richBoard(*m_fixture.store), {}));
        root.insert(u"futureFeature"_s, QJsonObject{{u"x"_s, 1}});
        inFirst(u"items"_s, [](QJsonObject &o) { o.insert(u"sparkle"_s, true); })(root);
        QVERIFY(readManifest(toJson(root), QString(), Limits{}, nullptr));
    }

    void rejectsInvalidManifests_data()
    {
        QTest::addColumn<Edit>("edit");
        QTest::addColumn<QString>("message");
        const Id imageA = fixedId(20);
        const Id member = fixedId(22);

        QTest::newRow("format") << Edit([](QJsonObject &r) { r.insert(u"format"_s, u"pureref"_s); })
                                << u"not a Glacier Ref board"_s;
        QTest::newRow("newer schema") << Edit([](QJsonObject &r) { r.insert(u"schemaVersion"_s, 2); })
                                      << u"newer version of Glacier Ref"_s;
        QTest::newRow("older schema") << Edit([](QJsonObject &r) { r.insert(u"schemaVersion"_s, 0); })
                                      << u"cannot read"_s;
        QTest::newRow("schema not a number") << Edit([](QJsonObject &r) { r.insert(u"schemaVersion"_s, u"1"_s); })
                                             << u"schema version"_s;
        QTest::newRow("missing document id") << Edit([](QJsonObject &r) { r.remove(u"documentId"_s); })
                                             << u"documentId"_s;
        QTest::newRow("braced id") << Edit([](QJsonObject &r) {
            r.insert(u"documentId"_s, u"{00000000-0000-4000-8000-000000000001}"_s);
        }) << u"not a valid ID"_s;
        QTest::newRow("uppercase id") << Edit([](QJsonObject &r) {
            r.insert(u"documentId"_s, u"00000000-0000-4000-8000-00000000000A"_s);
        }) << u"not a valid ID"_s;
        QTest::newRow("local timestamp") << Edit([](QJsonObject &r) {
            r.insert(u"created"_s, u"2026-11-07T10:00:00.000+01:00"_s);
        }) << u"UTC timestamp"_s;
        QTest::newRow("title control char") << Edit([](QJsonObject &r) { r.insert(u"title"_s, u"a\u0007b"_s); })
                                            << u"control characters"_s;
        QTest::newRow("duplicate item id") << Edit([](QJsonObject &r) {
            QJsonArray items = r.value(u"items"_s).toArray();
            items.append(items.at(0));
            r.insert(u"items"_s, items);
        }) << u"more than once"_s;
        QTest::newRow("id shared by item and group") << inItem(imageA, [](QJsonObject &o) {
            o.insert(u"id"_s, fixedId(40).toString());
        }) << u"more than once"_s;
        QTest::newRow("non-finite transform") << inItem(imageA, [](QJsonObject &o) {
            o.insert(u"transform"_s, QJsonArray{1, 0, 0, 1, u"NaN"_s, 0});
        }) << u"finite number"_s;
        QTest::newRow("singular transform") << inItem(imageA, [](QJsonObject &o) {
            o.insert(u"transform"_s, QJsonArray{1, 2, 2, 4, 0, 0});
        }) << u"not invertible"_s;
        QTest::newRow("short transform") << inItem(imageA, [](QJsonObject &o) {
            o.insert(u"transform"_s, QJsonArray{1, 0, 0, 1, 0});
        }) << u"array of 6 numbers"_s;
        QTest::newRow("outside coordinate range") << inItem(imageA, [](QJsonObject &o) {
            o.insert(u"transform"_s, QJsonArray{1, 0, 0, 1, 2.0e7, 0});
        }) << u"coordinate range"_s;
        QTest::newRow("bad crop") << inItem(imageA, [](QJsonObject &o) {
            o.insert(u"crop"_s, QJsonArray{0.5, 0, 0.8, 1});
        }) << u"crop"_s;
        QTest::newRow("missing asset") << inItem(imageA, [](QJsonObject &o) {
            o.insert(u"asset"_s, fixedId(77).toString());
        }) << u"missing asset"_s;
        QTest::newRow("unknown item type") << inItem(imageA, [](QJsonObject &o) {
            o.insert(u"type"_s, u"video"_s);
        }) << u"unknown type"_s;
        QTest::newRow("member without group") << inItem(member, [](QJsonObject &o) { o.remove(u"group"_s); })
                                              << u"inconsistent"_s;
        QTest::newRow("member of unknown group") << inItem(member, [](QJsonObject &o) {
            o.insert(u"group"_s, fixedId(78).toString());
        }) << u"inconsistent"_s;
        QTest::newRow("nested group") << inFirst(u"groups"_s, [](QJsonObject &o) {
            QJsonArray children = o.value(u"children"_s).toArray();
            children.append(o.value(u"id"_s));
            o.insert(u"children"_s, children);
        }) << u"cannot be nested"_s;
        QTest::newRow("empty group") << inFirst(u"groups"_s, [](QJsonObject &o) {
            o.insert(u"children"_s, QJsonArray());
        }) << u"at least one member"_s;
        QTest::newRow("bad digest") << inFirst(u"assets"_s, [](QJsonObject &o) {
            o.insert(u"sha256"_s, u"ABC"_s);
        }) << u"SHA-256"_s;
        QTest::newRow("unsupported media type") << inFirst(u"assets"_s, [](QJsonObject &o) {
            o.insert(u"mediaType"_s, u"image/vnd.adobe.photoshop"_s);
        }) << u"unsupported media type"_s;
        QTest::newRow("traversal entry") << inFirst(u"assets"_s, [](QJsonObject &o) {
            o.insert(u"entry"_s, u"../../etc/passwd"_s);
        }) << u"unexpected archive entry"_s;
        QTest::newRow("unknown asset kind") << inFirst(u"assets"_s, [](QJsonObject &o) {
            o.insert(u"kind"_s, u"remote"_s);
        }) << u"unknown kind"_s;
        QTest::newRow("oversized asset") << inFirst(u"assets"_s, [](QJsonObject &o) {
            o.insert(u"byteLength"_s, 1.0e12);
        }) << u"whole number"_s;
        QTest::newRow("oversized dimensions") << inFirst(u"assets"_s, [](QJsonObject &o) {
            o.insert(u"width"_s, 100000);
            o.insert(u"height"_s, 100000);
        }) << u"exceed the limit"_s;
        QTest::newRow("linked relative without board") << inFirst(u"assets"_s, [](QJsonObject &o) {
            o.insert(u"kind"_s, u"linked"_s);
            o.insert(u"path"_s, u"pics/a.png"_s);
            o.insert(u"pathKind"_s, u"relative"_s);
        }) << u"invalid path"_s;
        QTest::newRow("bad colour") << inItem(fixedId(30), [](QJsonObject &o) {
            o.insert(u"textColor"_s, u"red"_s);
        }) << u"colour"_s;
        QTest::newRow("translucent background") << Edit([](QJsonObject &r) {
            QJsonObject view = r.value(u"view"_s).toObject();
            view.insert(u"background"_s, u"#80FFFFFF"_s);
            r.insert(u"view"_s, view);
        }) << u"opaque"_s;
        QTest::newRow("zoom out of range") << Edit([](QJsonObject &r) {
            QJsonObject view = r.value(u"view"_s).toObject();
            view.insert(u"zoom"_s, 1000);
            r.insert(u"view"_s, view);
        }) << u"zoom"_s;
        QTest::newRow("note too long") << inItem(fixedId(30), [](QJsonObject &o) {
            o.insert(u"text"_s, QString(100001, u'x'));
        }) << u"too long"_s;
        QTest::newRow("items not an array") << Edit([](QJsonObject &r) { r.insert(u"items"_s, 5); })
                                            << u"must be an array"_s;
    }

    void rejectsInvalidManifests()
    {
        QFETCH(Edit, edit);
        QFETCH(QString, message);
        QJsonObject root = toObject(writeManifest(richBoard(*m_fixture.store), {}));
        edit(root);
        QString error;
        QVERIFY(!readManifest(toJson(root), QString(), Limits{}, &error));
        QVERIFY2(error.contains(message, Qt::CaseInsensitive), qPrintable(error));
    }

    void rejectsMalformedJsonAndLimits()
    {
        QString error;
        QVERIFY(!readManifest("{\"format\": \"glacier-ref\",", QString(), Limits{}, &error));
        QVERIFY(error.contains(u"not valid JSON"_s));
        QVERIFY(!readManifest("[1, 2]", QString(), Limits{}, &error));

        // Depth is checked before parsing.
        const QByteArray deep = QByteArray(40, '[') + QByteArray(40, ']');
        QCOMPARE(jsonNestingDepth(deep), 40);
        QVERIFY(!readManifest(deep, QString(), Limits{}, &error));
        QVERIFY(error.contains(u"nested too deeply"_s));
        // Brackets inside strings don't count.
        QCOMPARE(jsonNestingDepth("{\"a\": \"[[[[\\\"]]]]\", \"b\": [1]}"), 2);

        Limits small;
        small.maxManifestBytes = 100;
        QVERIFY(!readManifest(writeManifest(richBoard(*m_fixture.store), {}), QString(), small, &error));
        QVERIFY(error.contains(u"too large"_s));

        Limits fewItems;
        fewItems.maxItems = 3;
        QVERIFY(!readManifest(writeManifest(richBoard(*m_fixture.store), {}), QString(), fewItems, &error));
    }

    void migrationsUpgradeOlderSchemas()
    {
        // A hypothetical schema 0 stored the title as "name".
        QJsonObject root = toObject(writeManifest(richBoard(*m_fixture.store), {}));
        root.insert(u"schemaVersion"_s, 0);
        root.insert(u"name"_s, root.take(u"title"_s));
        QString error;
        QVERIFY(!readManifest(toJson(root), QString(), Limits{}, &error)); // no migration registered
        QVERIFY(error.contains(u"format version 0"_s));

        MigrationTable table;
        table.insert(0, [](QJsonObject &m, QString *) {
            m.insert(u"title"_s, m.take(u"name"_s));
            return true;
        });
        const auto read = readManifest(toJson(root), QString(), Limits{}, &error, table);
        QVERIFY2(read, qPrintable(error));
        QCOMPARE(read->content.metadata.title, richBoard(*m_fixture.store).metadata.title);
        QCOMPARE(read->root.value(u"schemaVersion"_s).toInt(), 1);

        MigrationTable failing;
        failing.insert(0, [](QJsonObject &, QString *e) {
            *e = u"broken"_s;
            return false;
        });
        QVERIFY(!readManifest(toJson(root), QString(), Limits{}, &error, failing));
        QVERIFY(error.contains(u"broken"_s));

        // Version 2 boards are rejected even with migrations to version 1.
        root.insert(u"schemaVersion"_s, 2);
        QVERIFY(!readManifest(toJson(root), QString(), Limits{}, &error, table));
        QVERIFY(error.contains(u"newer version"_s));
        QVERIFY(builtInMigrations().isEmpty());
    }
};

GLACIER_TEST_GUI_MAIN(TestManifest)
#include "tst_manifest.moc"
