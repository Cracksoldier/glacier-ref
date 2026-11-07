#include "glacier/platform/PlatformWindowService.h"

#include <QGuiApplication>
#include <QWindow>

using namespace Qt::StringLiterals;

namespace glacier::platform {

WindowCapabilities capabilitiesFor(const QString &platformName)
{
    WindowCapabilities c;
    const QString name = platformName.toLower();
    if (name.startsWith(u"wayland"_s)) { // "wayland", "wayland-egl"
        c.opacity = false;
        c.alwaysOnTopConfirmed = false;
        c.positioning = false;
    } else if (name == u"offscreen"_s || name == u"minimal"_s) {
        c.systemMoveResize = false; // no window system to move anything
    }
    return c;
}

WindowCapabilities currentCapabilities()
{
    return capabilitiesFor(QGuiApplication::platformName());
}

StayOnTop stayOnTopStatus(bool requested, const WindowCapabilities &capabilities)
{
    if (!requested)
        return StayOnTop::Off;
    return capabilities.alwaysOnTopConfirmed ? StayOnTop::On : StayOnTop::Requested;
}

bool startSystemMove(QWindow *window)
{
    return window && window->startSystemMove();
}

bool startSystemResize(QWindow *window, Qt::Edges edges)
{
    return window && edges != Qt::Edges() && window->startSystemResize(edges);
}

} // namespace glacier::platform
