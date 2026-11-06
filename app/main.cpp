#include "AppServices.h"
#include "MainWindow.h"

#include "glacier/core/AppInfo.h"
#include "glacier/platform/PlatformInfo.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QMessageBox>
#include <QStringList>
#include <QTextStream>

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    QApplication::setOrganizationName(glacier::app_info::organizationName());
    QApplication::setOrganizationDomain(glacier::app_info::organizationDomain());
    QApplication::setApplicationName(glacier::app_info::applicationName());
    QApplication::setApplicationVersion(glacier::app_info::version());

    QApplication app(argc, argv);
    QApplication::setApplicationDisplayName(glacier::app_info::displayName());
    // Sets the Wayland app_id and links windows to the desktop entry.
    QGuiApplication::setDesktopFileName(glacier::app_info::applicationId());
    QApplication::setWindowIcon(QIcon(u":/icons/glacier-ref.svg"_s));

    QCommandLineParser parser;
    parser.setApplicationDescription(QApplication::translate("main", "Offline reference board"));
    parser.addHelpOption();
    const QCommandLineOption versionOption(
        QStringList{u"v"_s, u"version"_s},
        QApplication::translate("main", "Print the version and exit."));
    parser.addOption(versionOption);
    const QCommandLineOption platformInfoOption(
        u"platform-info"_s,
        QApplication::translate("main", "Print platform information (Qt platform plugin, session, screens) and exit."));
    parser.addOption(platformInfoOption);
    parser.process(app);

    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << glacier::app_info::displayName() << u' ' << glacier::app_info::version() << u'\n';
        return 0;
    }
    if (parser.isSet(platformInfoOption)) {
        QTextStream(stdout) << glacier::platform::collectPlatformInfo().toText();
        return 0;
    }

    QString error;
    const std::unique_ptr<glacier::AppServices> services = glacier::AppServices::create({}, &error);
    if (!services) {
        QMessageBox::critical(nullptr, glacier::app_info::displayName(),
                              QApplication::translate("main", "Glacier Ref could not start: %1").arg(error));
        return 1;
    }

    int status = 0;
    {
        glacier::MainWindow window(*services); // destroyed before the services
        window.show();
        status = QApplication::exec();
    }
    return status;
}
