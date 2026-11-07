#include "AppServices.h"
#include "DiagnosticLog.h"
#include "Theme.h"
#include "MainWindow.h"
#include "RecoveryDialog.h"

#include "glacier/core/AppInfo.h"
#include "glacier/core/Diagnostics.h"
#include "glacier/io/DecoderSelfCheck.h"
#include "glacier/io/ImageDecoder.h"
#include "glacier/platform/PlatformInfo.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QIcon>
#include <QMessageBox>
#include <QStringList>
#include <QTextStream>

#include <cstring>

using namespace Qt::StringLiterals;

namespace {

// `--check-image-formats`: decodes a sample of every v1 format with the
// shipped plugins and exits non-zero if any fails. Release packaging runs it
// on each package (spec §4); it needs no display, so it runs before
// QApplication exists.
int checkImageFormats(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    glacier::io::configureImageAllocationLimit(glacier::Limits{});
    QTextStream out(stdout);
    bool allOk = true;
    for (const glacier::io::FormatCheck &check : glacier::io::checkDecoders()) {
        out << glacier::io::displayName(check.format) << u": "_s
            << (check.ok ? u"ok"_s : u"FAILED (%1)"_s.arg(check.detail)) << u'\n';
        allOk = allOk && check.ok;
    }
    out << u"Plugin paths: "_s << QCoreApplication::libraryPaths().join(u"; "_s) << u'\n';
    return allOk ? 0 : 1;
}

} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--check-image-formats") == 0)
            return checkImageFormats(argc, argv);
    }

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
    // Handled before QApplication is created; listed here for --help.
    parser.addOption(QCommandLineOption(
        u"check-image-formats"_s,
        QApplication::translate("main", "Decode a sample of every supported image format and exit (packaging check).")));
    parser.addPositionalArgument(u"boards"_s, QApplication::translate("main", "Boards (.glacierref) to open."),
                                 u"[boards...]"_s);
    parser.process(app);

    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << glacier::app_info::displayName() << u' ' << glacier::app_info::version() << u'\n';
        return 0;
    }
    if (parser.isSet(platformInfoOption)) {
        QTextStream(stdout) << glacier::platform::collectPlatformInfo().toText();
        return 0;
    }

    // Local diagnostics (M4.09): redacted, stderr plus a small per-process log.
    glacier::diagnostics::installLog({glacier::AppServices::logDirectory()});
    qCInfo(lcApp, "%s %s, Qt %s, platform %s", qPrintable(glacier::app_info::displayName()),
           qPrintable(glacier::app_info::version()), qVersion(), qPrintable(QGuiApplication::platformName()));

    QString error;
    const std::unique_ptr<glacier::AppServices> services = glacier::AppServices::create({}, &error);
    if (!services) {
        QMessageBox::critical(nullptr, glacier::app_info::displayName(),
                              QApplication::translate("main", "Glacier Ref could not start: %1").arg(error));
        return 1;
    }

    // The Glacier appearance (spec §3), switchable live in Preferences.
    glacier::theme::rememberSystemStyle();
    glacier::theme::apply(services->settings()->theme());
    QObject::connect(services->settings(), &glacier::SettingsService::appearanceChanged, &app,
                     [&services] { glacier::theme::apply(services->settings()->theme()); });

    auto *window = new glacier::MainWindow(*services);
    window->setAttribute(Qt::WA_DeleteOnClose); // more windows via File > New Window
    window->show();

    // Boards with unsaved changes from sessions that ended abnormally (spec §5).
    if (const auto recoverable = services->recoverable(); !recoverable.isEmpty()) {
        glacier::RecoveryDialog dialog(
            recoverable,
            [&](const glacier::RecoveryDialog::Entry &e) { glacier::MainWindow::recoverBoard(*services, e, window); },
            [&](const glacier::RecoveryDialog::Entry &e) { glacier::MainWindow::openBoard(*services, e.boardPath, window); },
            [](const glacier::RecoveryDialog::Entry &e) { glacier::persistence::RecoveryStore::discardEntry(e); },
            window);
        dialog.exec();
    }
    // The first board loads into the first window, others into new windows.
    for (const QString &board : parser.positionalArguments())
        glacier::MainWindow::openBoard(*services, board, window);
    const int status = QApplication::exec();

    // Windows must be gone before the services they use.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    bool allClosed = true;
    const auto remaining = QApplication::topLevelWidgets();
    for (QWidget *widget : remaining) {
        if (qobject_cast<glacier::MainWindow *>(widget)) {
            allClosed = false; // not closed normally: keep its recovery data
            delete widget;
        }
    }
    // Normal exit with every window closed: nothing left to recover.
    if (allClosed && services->recovery())
        services->recovery()->removeSession();
    return status;
}
