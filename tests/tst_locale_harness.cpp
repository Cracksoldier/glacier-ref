// Verifies that the GLACIER_TEST_LOCALE harness really switches locales, and
// that the Qt APIs intended for serialization stay locale-independent.

#include "GlacierTestMain.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

#include <cstdio>

using namespace Qt::StringLiterals;

class TestLocaleHarness : public QObject
{
    Q_OBJECT

private slots:
    void requestedLocaleIsActive()
    {
        if (qEnvironmentVariableIsEmpty("GLACIER_TEST_LOCALE"))
            QSKIP("default-locale run");

        QCOMPARE(QLocale().decimalPoint(), u","_s);

        char buffer[16] = {};
        std::snprintf(buffer, sizeof buffer, "%.1f", 1.5);
        QCOMPARE(QByteArray(buffer), QByteArray("1,5"));
    }

    void serializationApisIgnoreLocale()
    {
        QCOMPARE(QString::number(1.5), u"1.5"_s);
        QCOMPARE(QByteArray::number(0.25), QByteArray("0.25"));

        bool ok = false;
        QCOMPARE(u"2.75"_s.toDouble(&ok), 2.75);
        QVERIFY(ok);

        const QJsonObject object{{u"zoom"_s, 1.5}};
        const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QCOMPARE(json, QByteArray(R"({"zoom":1.5})"));
        QCOMPARE(QJsonDocument::fromJson(json).object().value(u"zoom"_s).toDouble(), 1.5);
    }
};

GLACIER_TEST_CORE_MAIN(TestLocaleHarness)
#include "tst_locale_harness.moc"
