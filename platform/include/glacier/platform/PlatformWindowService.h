#pragma once

#include <QString>
#include <Qt>

class QWindow;

namespace glacier::platform {

// Window capabilities and native integration (spec §6, §7, M4.03). The
// platform plugin decides what can be promised: on native Wayland the
// compositor controls stacking and positions, so always-on-top can only be
// requested and window opacity is not offered.
struct WindowCapabilities
{
    bool opacity = true;               // setWindowOpacity() has an effect
    bool alwaysOnTopConfirmed = true;  // the stay-on-top hint is honoured by the platform itself
    bool systemMoveResize = true;      // startSystemMove/Resize may work (each call still reports)
    bool positioning = true;           // top-level positions can be restored
};

// For a Qt platform plugin name ("wayland", "xcb", "windows", "cocoa", ...).
WindowCapabilities capabilitiesFor(const QString &platformName);
// For the running application.
WindowCapabilities currentCapabilities();

enum class StayOnTop {
    Off,
    On,        // the platform honours the hint
    Requested, // the hint is set; the compositor decides (never shown as guaranteed)
};
StayOnTop stayOnTopStatus(bool requested, const WindowCapabilities &capabilities);

// Starts a compositor-driven move or resize from the current input event
// (spec §7: never by polling global coordinates). False when unsupported.
bool startSystemMove(QWindow *window);
bool startSystemResize(QWindow *window, Qt::Edges edges);

} // namespace glacier::platform
