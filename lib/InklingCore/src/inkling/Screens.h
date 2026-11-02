#pragma once

#include <FreeInkApp.h>

#include "DeviceStatus.h"

namespace inkling {

// One app type for firmware and simulator: interaction and handler capacities
// are fixed at compile time (FreeInkUI never allocates).
using UiApp = freeink::ui::FreeInkApp<48, 16>;

// Phase 0 diagnostic screen: proves display, buttons, clock, battery, SD and
// IMU all work. `user` must point to a DeviceStatus.
void diagnosticScreen(UiApp::ScreenType& screen, void* user);

// Shown right before the device powers off, and left on the panel while it is off.
void sleepScreen(UiApp::ScreenType& screen, void* user);

}  // namespace inkling
