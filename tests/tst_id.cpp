#include "GlacierTestMain.h"

#include "glacier/core/Id.h"

#include <QCoreApplication>
#include <QObject>
#include <QSet>

using namespace Qt::StringLiterals;
using glacier::Id;

class TestId : public QObject
{
    Q_OBJECT

private slots:
    void generatedIdsAreUniqueAndRandom()
    {
        QSet<Id> seen;
        for (int i = 0; i < 1000; ++i) {
            const Id id = Id::generate();
            QCOMPARE(id.toUuid().version(), QUuid::Random);
            QVERIFY(!seen.contains(id));
            seen.insert(id);
        }
    }

    void canonicalStringForm()
    {
        const QString text = Id::generate().toString();
        QCOMPARE(text.size(), 36);
        QCOMPARE(text, text.toLower());
        QVERIFY(!text.contains(u'{'));
        QCOMPARE(text.count(u'-'), 4);
    }

    void roundTrip()
    {
        const Id id = Id::generate();
        const std::optional<Id> parsed = Id::fromString(id.toString());
        QVERIFY(parsed.has_value());
        QCOMPARE(*parsed, id);
    }

    void rejectsNonCanonical_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("empty") << QString();
        QTest::newRow("null uuid") << u"00000000-0000-0000-0000-000000000000"_s;
        QTest::newRow("braced") << u"{3f2b8c1e-7a4d-4e0f-9b6a-2c5d8e1f0a7b}"_s;
        QTest::newRow("uppercase") << u"3F2B8C1E-7A4D-4E0F-9B6A-2C5D8E1F0A7B"_s;
        QTest::newRow("no dashes") << u"3f2b8c1e7a4d4e0f9b6a2c5d8e1f0a7b"_s;
        QTest::newRow("too short") << u"3f2b8c1e-7a4d-4e0f-9b6a-2c5d8e1f0a7"_s;
        QTest::newRow("padded") << u" 3f2b8c1e-7a4d-4e0f-9b6a-2c5d8e1f0a7b"_s;
        QTest::newRow("not hex") << u"zf2b8c1e-7a4d-4e0f-9b6a-2c5d8e1f0a7b"_s;
    }

    void rejectsNonCanonical()
    {
        QFETCH(QString, text);
        QVERIFY(!Id::fromString(text).has_value());
    }

    void acceptsCanonical()
    {
        const auto parsed = Id::fromString(u"3f2b8c1e-7a4d-4e0f-9b6a-2c5d8e1f0a7b");
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->toString(), u"3f2b8c1e-7a4d-4e0f-9b6a-2c5d8e1f0a7b"_s);
    }
};

GLACIER_TEST_CORE_MAIN(TestId)
#include "tst_id.moc"
