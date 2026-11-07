#pragma once

#include "SettingsService.h"

#include <QPalette>

namespace glacier::theme {

// The restrained dark Glacier appearance (spec §3): Fusion with a dark
// palette, or the platform's own style. Applied app-wide and live.

// Remembers the platform style; call once before the first apply().
void rememberSystemStyle();
void apply(SettingsService::Theme theme);
QPalette glacierPalette();

} // namespace glacier::theme
