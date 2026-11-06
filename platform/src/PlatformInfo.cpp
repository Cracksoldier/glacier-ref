#include "glacier/platform/PlatformInfo.h"

#include "glacier/core/AppInfo.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSysInfo>
#include <QtGlobal>

using namespace Qt::StringLiterals;

namespace glacier::platform {

namespace {

QString envOrEmpty(const char *name)
{
    return qEnvironmentVariable(name);
}

QString orNotSet(const QString &value)
{
    return value.isEmpty() ? u"(not set)"_s : value;
}

} // namespace

PlatformInfo collectPlatformInfo()
{
    PlatformInfo info;
    info.applicationVersion = app_info::version();
    info.qtRuntimeVersion = QString::fromLatin1(qVersion());
    info.qtBuildVersion = QString::fromLatin1(QT_VERSION_STR);
    info.qpaPlatform = QGuiApplication::platformName();
    info.qpaOverride = envOrEmpty("QT_QPA_PLATFORM");
    info.sessionType = envOrEmpty("XDG_SESSION_TYPE");
    info.currentDesktop = envOrEmpty("XDG_CURRENT_DESKTOP");
    info.waylandDisplaySet = qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
    info.operatingSystem = QSysInfo::prettyProductName();
    info.kernel = QSysInfo::kernelType() + u' ' + QSysInfo::kernelVersion();
    info.cpuArchitecture = QSysInfo::currentCpuArchitecture();

    const auto screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        info.screens.append(ScreenInfo{
            screen->name(),
            screen->size(),
            screen->devicePixelRatio(),
            screen->refreshRate(),
        });
    }
    return info;
}

QString PlatformInfo::toText() const
{
    // Formatting uses QString::number (locale-independent) on purpose, so
    // reports compare cleanly across machines and locales.
    QString text;
    text += u"Glacier Ref "_s + applicationVersion + u'\n';
    text += u"Qt runtime/build: "_s + qtRuntimeVersion + u" / "_s + qtBuildVersion + u'\n';
    text += u"Qt platform plugin: "_s + qpaPlatform + u'\n';
    text += u"QT_QPA_PLATFORM: "_s + orNotSet(qpaOverride) + u'\n';
    text += u"XDG_SESSION_TYPE: "_s + orNotSet(sessionType) + u'\n';
    text += u"XDG_CURRENT_DESKTOP: "_s + orNotSet(currentDesktop) + u'\n';
    text += u"WAYLAND_DISPLAY set: "_s + (waylandDisplaySet ? u"yes"_s : u"no"_s) + u'\n';
    text += u"OS: "_s + operatingSystem + u'\n';
    text += u"Kernel: "_s + kernel + u'\n';
    text += u"CPU architecture: "_s + cpuArchitecture + u'\n';
    text += u"Screens: "_s + QString::number(screens.size()) + u'\n';
    for (const ScreenInfo &screen : screens) {
        text += u"  - "_s + screen.name + u": "_s
              + QString::number(screen.logicalSize.width()) + u'x'
              + QString::number(screen.logicalSize.height())
              + u" logical, DPR "_s + QString::number(screen.devicePixelRatio)
              + u", "_s + QString::number(screen.refreshRate, 'f', 1) + u" Hz\n"_s;
    }
    return text;
}

} // namespace glacier::platform
