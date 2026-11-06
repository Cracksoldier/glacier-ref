#pragma once

#include <QList>
#include <QSize>
#include <QString>

namespace glacier::platform {

struct ScreenInfo
{
    QString name;
    QSize logicalSize;
    qreal devicePixelRatio = 1.0;
    qreal refreshRate = 0.0;
};

// Runtime environment facts for diagnostics and validation records
// (e.g. confirming native Wayland). Contains no personal paths.
struct PlatformInfo
{
    QString applicationVersion;
    QString qtRuntimeVersion;
    QString qtBuildVersion;
    QString qpaPlatform;      // Qt platform plugin in use: "wayland", "xcb", "windows", "cocoa", ...
    QString qpaOverride;      // QT_QPA_PLATFORM, if set
    QString sessionType;      // XDG_SESSION_TYPE (Linux)
    QString currentDesktop;   // XDG_CURRENT_DESKTOP (Linux)
    bool waylandDisplaySet = false;
    QString operatingSystem;
    QString kernel;
    QString cpuArchitecture;
    QList<ScreenInfo> screens;

    // Multi-line, human-readable report.
    QString toText() const;
};

// Collects the current environment. Requires a constructed QGuiApplication.
PlatformInfo collectPlatformInfo();

} // namespace glacier::platform
