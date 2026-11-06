#include "GlacierTestMain.h"

#include "glacier/platform/PlatformInfo.h"

#include <QGuiApplication>
#include <QObject>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

class TestPlatformInfo : public QObject
{
    Q_OBJECT

private slots:
    void reportsActivePlatformPlugin()
    {
        const auto info = glacier::platform::collectPlatformInfo();
        QCOMPARE(info.qpaPlatform, QGuiApplication::platformName());
        QVERIFY(!info.qpaPlatform.isEmpty());
        QCOMPARE(info.qtRuntimeVersion, QString::fromLatin1(qVersion()));
        QVERIFY(!info.screens.isEmpty());
    }

    void textReportContainsKeyFacts()
    {
        const QString text = glacier::platform::collectPlatformInfo().toText();
        QVERIFY(text.contains(u"Qt platform plugin: "_s + QGuiApplication::platformName()));
        QVERIFY(text.contains(QString::fromLatin1(qVersion())));
        // Numbers are formatted locale-independently, also under
        // GLACIER_TEST_LOCALE=de_AT (no decimal commas such as "60,0").
        static const QRegularExpression decimalComma(u"\\d,\\d"_s);
        QVERIFY2(!decimalComma.match(text).hasMatch(), qPrintable(text));
    }
};

GLACIER_TEST_GUI_MAIN(TestPlatformInfo)
#include "tst_platforminfo.moc"
