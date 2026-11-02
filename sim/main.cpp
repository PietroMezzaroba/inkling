// Inkling host simulator: renders the same screens the firmware draws into an
// X3-sized 1-bit framebuffer and saves each one as a PNG.
//
//   cmake -S sim -B build/sim && cmake --build build/sim && build/sim/inkling-sim sim/out

#include <FreeInkUIDisplayTarget.h>

#include <cstdio>
#include <string>
#include <vector>

#include "PngWriter.h"
#include "inkling/Screens.h"

namespace {

// Native X3 panel: 792x528, 1 bpp, MSB first, 0 = ink.
constexpr int16_t kPanelW = 792;
constexpr int16_t kPanelH = 528;
constexpr int16_t kPanelWBytes = kPanelW / 8;

struct Canvas {
  std::vector<uint8_t> fb = std::vector<uint8_t>(static_cast<size_t>(kPanelWBytes) * kPanelH, 0xFF);
  freeink::ui::DisplayTarget target{fb.data(), kPanelW, kPanelH, kPanelWBytes};

  // Saves the panel as the reader sees it: the UI draws portrait (528x792) by
  // rotating into the landscape-native buffer, so rotate back for the PNG.
  bool save(const std::string& path) const {
    const int outW = target.logicalWidth();
    const int outH = target.logicalHeight();
    std::vector<uint8_t> gray(static_cast<size_t>(outW) * outH, 0xFF);
    for (int y = 0; y < outH; ++y) {
      for (int x = 0; x < outW; ++x) {
        int px = x, py = y;
        if (outW != kPanelW) {  // portrait: logical (x, y) -> panel (y, W-1-x)
          px = y;
          py = kPanelH - 1 - x;
        }
        const uint8_t byte = fb[static_cast<size_t>(py) * kPanelWBytes + (px >> 3)];
        const bool ink = (byte & (0x80 >> (px & 7))) == 0;
        gray[static_cast<size_t>(y) * outW + x] = ink ? 0x10 : 0xF4;  // e-paper-ish contrast
      }
    }
    return sim::writeGrayPng(path.c_str(), outW, outH, gray);
  }
};

bool renderTo(const std::string& dir, const char* name, inkling::UiApp::ScreenFn screen, void* user) {
  Canvas canvas;
  inkling::UiApp app(canvas.target, canvas.target.deviceContext());
  app.setClearColor(freeink::ui::Color::White);
  app.setScreen(screen, user);
  app.render();
  const std::string path = dir + "/" + name + ".png";
  if (!canvas.save(path)) {
    std::fprintf(stderr, "failed to write %s\n", path.c_str());
    return false;
  }
  std::printf("%s\n", path.c_str());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string outDir = argc > 1 ? argv[1] : "sim/out";

  inkling::DeviceStatus status;
  status.panelName = "UC8279";
  status.clockValid = true;
  status.year = 2026;
  status.month = 11;
  status.day = 2;
  status.hour = 14;
  status.minute = 5;
  status.batteryPercent = 87;
  status.charging = true;
  status.sdReady = true;
  status.imuReady = true;
  status.ax = 0.01f;
  status.ay = -0.98f;
  status.az = 0.05f;
  status.lastButton = "Confirm";
  status.buttonPresses = 12;

  inkling::DeviceStatus fresh;  // first boot: nothing detected yet

  bool ok = true;
  ok &= renderTo(outDir, "diagnostic", inkling::diagnosticScreen, &status);
  ok &= renderTo(outDir, "diagnostic-empty", inkling::diagnosticScreen, &fresh);
  ok &= renderTo(outDir, "sleep", inkling::sleepScreen, nullptr);
  return ok ? 0 : 1;
}
