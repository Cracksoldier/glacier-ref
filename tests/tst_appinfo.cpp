#include "GlacierTestMain.h"

#include "glacier/core/AppInfo.h"

#include <QCoreApplication>
#include <QObject>
#include <QRegularExpression>

using namespace Qt::StringLiterals;
namespace app_info = glacier::app_info;

class TestAppInfo : public QObject
{
    Q_OBJECT

private slots:
    void identity()
    {
        QCOMPARE(app_info::applicationId(), u"io.github.cracksoldier.GlacierRef"_s);
        QCOMPARE(app_info::boardMimeType(), u"application/x-glacierref"_s);
        QCOMPARE(app_info::boardFileSuffix(), u"glacierref"_s);
        QCOMPARE(app_info::displayName(), u"Glacier Ref"_s);
    }

    void applicationIdIsReverseDns()
    {
        // Wayland app_id / Flatpak ID rules: dot-separated, at least three
        // components, each starting with a letter.
        static const QRegularExpression pattern(u"^[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*){2,}$"_s);
        QVERIFY(pattern.match(app_info::applicationId()).hasMatch());
    }

    void versionIsSemantic()
    {
        static const QRegularExpression pattern(u"^\\d+\\.\\d+\\.\\d+$"_s);
        QVERIFY2(pattern.match(app_info::version()).hasMatch(), qPrintable(app_info::version()));
    }

    void homepageIsProjectRepository()
    {
        QCOMPARE(app_info::homepageUrl(), u"https://github.com/Cracksoldier/glacier-ref"_s);
    }
};

GLACIER_TEST_CORE_MAIN(TestAppInfo)
#include "tst_appinfo.moc"
