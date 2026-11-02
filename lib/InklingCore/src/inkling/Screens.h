#pragma once

#include <FreeInkApp.h>

#include "DeviceStatus.h"

namespace inkling {

// One app type for firmware and simulator: interaction and handler capacities
// are fixed at compile time (FreeInkUI never allocates).
using UiApp = freeink::ui::FreeInkApp<48, 16>;

// Futuristic hardware & sensor telemetry deck. `user` must point to a DeviceStatus.
void diagnosticScreen(UiApp::ScreenType& screen, void* user);

// Shown right before the device powers off, and left on the panel while it is off.
// `user` may point to an `InklingApp` (for live Pet + Book stats) or be nullptr.
void sleepScreen(UiApp::ScreenType& screen, void* user);

// Full-screen system modal message; `user` must point to a NUL-terminated string.
void messageScreen(UiApp::ScreenType& screen, void* user);

// What the Wi-Fi update mode shows; `user` must point to an OtaView.
struct OtaView {
  const char* network = "";
  char address[48] = {};
  const char* state = "";
  const char* version = "";
  int8_t progress = -1;  // percent while receiving firmware, -1 otherwise
};
void otaScreen(UiApp::ScreenType& screen, void* user);

}  // namespace inkling
