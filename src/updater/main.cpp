// Inkling updater (Xteink X3): a small app in the slot opposite the reader.
// It joins Wi-Fi and installs new reader builds sent from the Mac, and serves
// the logs the reader left on the SD card. Back returns to the reader.

#include <Arduino.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <FreeInkUIDisplayTarget.h>
#include <InputManager.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <XteinkDetect.h>

#include "DeviceLog.h"
#include "FirmwareSlots.h"
#include "OtaMode.h"
#include "inkling/Screens.h"
#include "inkling/ui/CyberCanvas.h"
#include "inkling/ui/InklingFonts.h"

namespace {

EInkDisplay* display = nullptr;
InputManager input;
inkling::UiApp* ui = nullptr;

void render() {
  if (!ui->invalidated()) return;
  ui->render(freeink::ui::InputSnapshot{});
  freeink::ui::present(*display, ui->lastRenderRefreshHint());
}

void showView(const inkling::OtaView& view, void*) {
  static inkling::OtaView shown;
  shown = view;
  ui->setScreen(inkling::otaScreen, &shown, freeink::ui::RefreshHint::Fast);
  render();
}

bool latchedBack = false;
bool latchedConfirm = false;

void serviceButtons() {
  input.update();
  if (input.wasPressed(InputManager::BTN_BACK)) latchedBack = true;
  if (input.wasPressed(InputManager::BTN_CONFIRM)) latchedConfirm = true;
}

bool backPressed(void*) {
  serviceButtons();
  if (!latchedBack) return false;
  latchedBack = false;
  return true;
}

bool confirmPressed(void*) {
  serviceButtons();
  if (!latchedConfirm) return false;
  latchedConfirm = false;
  return true;
}

[[noreturn]] void failAndWait(const char* message) {
  LOG("updater: %s", message);
  static char text[96];
  snprintf(text, sizeof(text), "%s", message);
  ui->setScreen(inkling::messageScreen, text, freeink::ui::RefreshHint::Full);
  render();
  while (!backPressed(nullptr)) delay(50);
  firmware::bootOtherSlot();
  esp_restart();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  devlog::begin("updater");

  freeink::selectXteinkDevice();
  input.begin();

  const auto& pins = BoardConfig::ACTIVE.display;
  SPI.begin(pins.sclk, BoardConfig::ACTIVE.sd.miso, pins.mosi, pins.cs);
  const bool sdReady = SdMan.begin();
  if (sdReady) devlog::attachFile("/.inkling/updater.log");

  static EInkDisplay einkDisplay(pins.sclk, pins.mosi, pins.cs, pins.dc, pins.rst, pins.busy);
  display = &einkDisplay;
  display->setDisplayX3();
  display->begin();

  static freeink::ui::DisplayTarget target(display->getFrameBuffer(), display->getDisplayWidth(),
                                           display->getDisplayHeight(), display->getDisplayWidthBytes());
  inkling::ui::bindInklingFonts(target);
  inkling::ui::canvas::bindFramebuffer(display->getFrameBuffer(), display->getDisplayWidth(),
                                       display->getDisplayHeight(), display->getDisplayWidthBytes());
  static inkling::UiApp app(target, target.deviceContext());
  ui = &app;

  LOG("updater: sd=%d free heap %u, reader slot: %s", sdReady, static_cast<unsigned>(ESP.getFreeHeap()),
      firmware::otherSlotName());

  // Up and drawing: confirm this image so the bootloader keeps it.
  ui->setScreen(inkling::messageScreen, const_cast<char*>("Avvio Cyber-Net Hub..."), freeink::ui::RefreshHint::Full);
  render();
  firmware::markValid();

  if (!sdReady) failAndWait("Scheda SD assente. Back: torna al lettore");
  static ota::Config config;
  if (!ota::loadConfig(config)) failAndWait("Manca /.inkling/wifi.txt. Back: torna al lettore");
  devlog::detachFile();  // the HTTP task logs from here; keep SD writes to the minimum

  ota::run(config, ota::Hooks{showView, backPressed, confirmPressed, nullptr});  // never returns
}

void loop() {}
