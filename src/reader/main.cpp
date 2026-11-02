// Inkling firmware entry point (Xteink X3).
//
// Hardware bring-up and the main loop. Everything the user sees is InklingApp
// (lib/InklingCore), which the host simulator runs unchanged.

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <FreeInkUIDisplayTarget.h>
#include <Imu.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <Rtc.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <XteinkDetect.h>

#include <new>

#include "DeviceLog.h"
#include "FirmwareSlots.h"
#include "SdStorage.h"
#include "inkling/App.h"
#include "inkling/Screens.h"

// Reading faces, embedded in flash by board_build.embed_files and read in place
// through the flash cache: no RAM copy.
extern const uint8_t kFontRegularStart[] asm("_binary_assets_fonts_Alegreya_Regular_ttf_start");
extern const uint8_t kFontRegularEnd[] asm("_binary_assets_fonts_Alegreya_Regular_ttf_end");
extern const uint8_t kFontItalicStart[] asm("_binary_assets_fonts_Alegreya_Italic_ttf_start");
extern const uint8_t kFontItalicEnd[] asm("_binary_assets_fonts_Alegreya_Italic_ttf_end");

namespace {

// Holding Power this long turns the device off.
constexpr unsigned long kPowerOffHoldMs = 1000;
// Holding Back this long on the diagnostic screen restarts into the other slot.
constexpr unsigned long kOtherSlotHoldMs = 3000;
// How long Back is sampled at boot for the "hold Back while powering on" escape.
constexpr unsigned long kBootEscapeSampleMs = 300;
// How often the clock, battery and IMU are re-read while idle.
constexpr unsigned long kStatusPollMs = 30000;

EInkDisplay* display = nullptr;
InputManager input;
freeink::Rtc rtc;
freeink::Imu imu;
BatteryMonitor* battery = nullptr;

SdStorage storage;
// ~112 KB of reader arenas (see BookReader.h), taken from the heap at boot
// right after the framebuffer, while the heap is still one unfragmented block.
// Heap rather than .bss so a future boot mode without the reader (Wi-Fi
// updates) gets this RAM back.
inkling::ReaderMemory* readerMemory = nullptr;
inkling::BookReader* reader = nullptr;
inkling::UiApp* ui = nullptr;
inkling::InklingApp* app = nullptr;
inkling::DeviceStatus status;

// The press that powered the device on is still held at boot; Power only arms
// the power-off gesture after it has been released once.
bool powerArmed = false;
unsigned long powerPressedAt = 0;
unsigned long backPressedAt = 0;
bool backHoldHandled = false;
unsigned long lastPollMs = 0;

const char* panelName() {
  return BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279 ? "UC8279" : "UC8253";
}

// Re-reads clock, battery and IMU. Returns true when something the screen shows changed.
bool pollStatus() {
  bool changed = false;

  freeink::Rtc::DateTime now;
  const bool clockValid = rtc.present() && rtc.now(now) && now.year >= 2025;
  if (clockValid != status.clockValid || (clockValid && (now.minute != status.minute || now.hour != status.hour ||
                                                         now.day != status.day))) {
    changed = true;
  }
  status.clockValid = clockValid;
  if (clockValid) {
    status.year = now.year;
    status.month = now.month;
    status.day = now.day;
    status.hour = now.hour;
    status.minute = now.minute;
  }

  uint16_t percent = 0;
  const int16_t battPct = battery->readPercentageChecked(percent) ? static_cast<int16_t>(percent) : -1;
  const bool charging = battery->isCharging();
  if (battPct != status.batteryPercent || charging != status.charging) changed = true;
  status.batteryPercent = battPct;
  status.charging = charging;

  freeink::Imu::Sample sample;
  if (status.imuReady && imu.read(sample)) {
    status.ax = sample.ax;
    status.ay = sample.ay;
    status.az = sample.az;
  }

  status.freeHeap = ESP.getFreeHeap();
  status.largestBlock = ESP.getMaxAllocHeap();
  return changed;
}

void presentFrame(freeink::ui::RefreshHint hint, void*) { freeink::ui::present(*display, hint); }

void renderNow() {
  if (!ui->invalidated()) return;
  ui->render(freeink::ui::InputSnapshot{});
  presentFrame(ui->lastRenderRefreshHint(), nullptr);
}

[[noreturn]] void powerOff() {
  LOG("power off");
  app->shutdown();  // reading position is already saved per page; this closes files
  ui->setScreen(inkling::sleepScreen, nullptr, freeink::ui::RefreshHint::Full);
  renderNow();
  display->deepSleep();
  imu.sleep();

  // On the X3 this also drives GPIO13 low, which cuts the SD rail and the
  // battery switch: on battery the device is truly off and the next Power press
  // cold-boots it. On USB power the chip deep-sleeps and Power wakes it.
  freeink::PowerManager::powerDownRailsForSleep();
  freeink::PowerManager::deepSleepUntilPowerButton();
}

void onAppRequest(inkling::InklingApp::Request request, void*) {
  if (request == inkling::InklingApp::Request::WifiUpdate) {
    // The updater lives in the other app slot.
    LOG("switching to the updater: %s", firmware::otherSlotName());
    app->shutdown();
    firmware::bootOtherSlot();  // returns only on failure
    app->showMessage("Nessun updater nell'altro slot");
  }
}

// Samples the buttons briefly so the ADC ladder debounce settles.
bool backHeldAtBoot() {
  const unsigned long start = millis();
  while (millis() - start < kBootEscapeSampleMs) {
    input.update();
    delay(5);
  }
  return input.isPressed(InputManager::BTN_BACK);
}

void handleButtons() {
  static constexpr struct {
    uint8_t hw;
    inkling::Button button;
  } kMap[] = {
      {InputManager::BTN_BACK, inkling::Button::Back},   {InputManager::BTN_CONFIRM, inkling::Button::Confirm},
      {InputManager::BTN_LEFT, inkling::Button::Left},   {InputManager::BTN_RIGHT, inkling::Button::Right},
      {InputManager::BTN_UP, inkling::Button::Up},       {InputManager::BTN_DOWN, inkling::Button::Down},
  };
  for (const auto& m : kMap) {
    if (!input.wasPressed(m.hw)) continue;
    status.lastButton = InputManager::getButtonName(m.hw);
    status.buttonPresses++;
    if (app->mode() == inkling::InklingApp::Mode::Diagnostic) pollStatus();  // fresh IMU values per press
    app->onButton(m.button);
  }

  // Back held 3 s on the diagnostic screen: restart into the other slot. Only
  // there, so closing a book with Back can never turn into a firmware switch.
  if (input.isPressed(InputManager::BTN_BACK) && app->mode() == inkling::InklingApp::Mode::Diagnostic) {
    if (backPressedAt == 0) backPressedAt = millis();
    if (!backHoldHandled && millis() - backPressedAt >= kOtherSlotHoldMs) {
      backHoldHandled = true;
      LOG("switching to other slot: %s", status.otherFirmware);
      app->showMessage("Riavvio nell'altro firmware...");
      app->update();
      firmware::bootOtherSlot();  // returns only on failure
      app->showMessage("Nessun firmware nell'altro slot");
    }
  } else if (!input.isPressed(InputManager::BTN_BACK)) {
    backPressedAt = 0;
    backHoldHandled = false;
  }

  const bool powerDown = input.isPressed(InputManager::BTN_POWER);
  if (!powerDown) {
    powerArmed = true;
    powerPressedAt = 0;
    return;
  }
  if (!powerArmed) return;
  if (powerPressedAt == 0) powerPressedAt = millis();
  if (millis() - powerPressedAt >= kPowerOffHoldMs) powerOff();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  devlog::begin("reader");

  // Board detection must come first: it picks the X3 profile (UC8253 or the
  // newer UC8279 batch) and latches the power rails before any pin is used.
  const bool x3Confirmed = freeink::selectXteinkDevice();

  // Escape hatch, checked before anything that could crash: Back held while
  // powering on restarts into the other slot (e.g. the previous CrossPet).
  input.begin();
  status.otherFirmware = firmware::otherSlotName();
  const bool backHeld = backHeldAtBoot();
  if (backHeld) {
    backHoldHandled = true;     // don't fire the long-press again for this same hold
    firmware::bootOtherSlot();  // returns only on failure
  }

  const auto& pins = BoardConfig::ACTIVE.display;
  // The SD card shares the display's SPI bus on the X3 and only adds MISO.
  SPI.begin(pins.sclk, BoardConfig::ACTIVE.sd.miso, pins.mosi, pins.cs);

  status.sdReady = SdMan.begin();
  // Boot steps go to the card too, synced line by line: if this build crashes
  // before markValid(), the bootloader rolls back to the updater, which can
  // still serve this file.
  if (status.sdReady) devlog::attachFile("/.inkling/boot.log");

  static EInkDisplay einkDisplay(pins.sclk, pins.mosi, pins.cs, pins.dc, pins.rst, pins.busy);
  display = &einkDisplay;
  display->setDisplayX3();
  display->begin();

  static BatteryMonitor batteryMonitor;
  battery = &batteryMonitor;
  rtc.begin();
  status.imuReady = imu.begin();

  status.panelName = panelName();
  pollStatus();
  lastPollMs = millis();

  LOG("Inkling boot: x3=%d panel=%s sd=%d rtc=%d imu=%d", x3Confirmed, status.panelName, status.sdReady,
      rtc.present(), status.imuReady);

  static freeink::ui::DisplayTarget displayTarget(display->getFrameBuffer(), display->getDisplayWidth(),
                                                  display->getDisplayHeight(), display->getDisplayWidthBytes());
  static inkling::UiApp uiApp(displayTarget, displayTarget.deviceContext());
  ui = &uiApp;
  ui->setClearColor(freeink::ui::Color::White);


  freeink::book::FrameTarget frame{display->getFrameBuffer(), static_cast<int16_t>(display->getDisplayWidth()),
                                   static_cast<int16_t>(display->getDisplayHeight()),
                                   static_cast<int16_t>(display->getDisplayWidthBytes())};
  frame.format = freeink::book::FrameFormat::Mono1Dithered;
  frame.rotation = freeink::book::FrameRotation::Portrait;

  // Reader memory, right after the framebuffer while the heap is one block:
  // the fixed arenas first, then glyph caches sized from what is left. If even
  // the minimum does not fit, the device still boots into library and
  // diagnostics. A floor of free heap stays for SD, UI and system tasks:
  // without it a later small allocation would fail somewhere far less obvious.
  constexpr size_t kHeapFloor = 16 * 1024;
  using Glyphs = inkling::GlyphCacheSizes;
  auto spare = [] {
    const size_t free = ESP.getFreeHeap();
    return free > kHeapFloor ? free - kHeapFloor : 0;
  };
  LOG("heap before reader: free %u, max block %u", static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(ESP.getMaxAllocHeap()));
  if (ESP.getMaxAllocHeap() >= sizeof(inkling::ReaderMemory) &&
      spare() >= sizeof(inkling::ReaderMemory) + Glyphs::kRegularMin) {
    readerMemory = new (std::nothrow) inkling::ReaderMemory;
  }
  bool fontsOk = false;
  if (readerMemory) {
    size_t regularLen = spare() >= Glyphs::kRegularIdeal ? Glyphs::kRegularIdeal : Glyphs::kRegularMin;
    uint8_t* regularCache = static_cast<uint8_t*>(malloc(regularLen));
    uint8_t* italicCache = nullptr;
    if (regularCache && spare() >= Glyphs::kItalic) italicCache = static_cast<uint8_t*>(malloc(Glyphs::kItalic));

    static inkling::BookReader bookReader(storage, *readerMemory);
    reader = &bookReader;
    // Parsing borrows the framebuffer while a chapter is laid out.
    reader->setParseBuffer(display->getFrameBuffer(),
                           static_cast<size_t>(display->getDisplayWidthBytes()) * display->getDisplayHeight());
    if (regularCache) {
      fontsOk = reader->initFonts(kFontRegularStart, static_cast<uint32_t>(kFontRegularEnd - kFontRegularStart),
                                  kFontItalicStart, static_cast<uint32_t>(kFontItalicEnd - kFontItalicStart),
                                  regularCache, regularLen, italicCache, italicCache ? Glyphs::kItalic : 0);
    }
    LOG("glyph caches: regular %u, italic %u", static_cast<unsigned>(regularCache ? regularLen : 0),
        static_cast<unsigned>(italicCache ? Glyphs::kItalic : 0));
  } else {
    status.notice = "Memoria insufficiente per il lettore";
  }
  LOG("reader memory: %s (%u bytes), fonts: %d, free heap %u", readerMemory ? "ok" : "FAILED",
      static_cast<unsigned>(sizeof(inkling::ReaderMemory)), fontsOk, static_cast<unsigned>(ESP.getFreeHeap()));

  static inkling::InklingApp inklingApp(storage, reader, status, uiApp, frame);
  app = &inklingApp;
  app->setPresenter(presentFrame, nullptr);
  app->setRequestHandler(onAppRequest, nullptr);

  // SD self-update: works for any ESP32-C3 app, so CrossPet can be put back
  // the same way by saving its .bin as /inkling.bin.
  if (status.sdReady && SdMan.exists(firmware::kSdUpdatePath)) {
    LOG("SD update found: %s", firmware::kSdUpdatePath);
    app->showMessage("Aggiornamento dalla scheda SD...");
    app->update();
    const char* error = "";
    firmware::installFromSd(firmware::kSdUpdatePath, &error);  // restarts on success
    LOG("SD update failed: %s", error);
    static char notice[64];
    snprintf(notice, sizeof(notice), "Aggiornamento fallito: %s", error);
    status.notice = notice;
  }

  if (backHeld) status.notice = "Nessun firmware nell'altro slot";
  if (!status.sdReady || !fontsOk || status.notice) {
    app->showDiagnostic();  // something to tell: start on the hardware screen
  } else {
    app->begin();  // library
  }
  app->update();

  // First screen is up: this build works, keep it (cancels the rollback), and
  // stop writing the log to the card.
  firmware::markValid();
  LOG("boot done, free heap %u (max block %u)", static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(ESP.getMaxAllocHeap()));
  devlog::detachFile();
}

void loop() {
  input.update();
  handleButtons();

  if (millis() - lastPollMs >= kStatusPollMs) {
    lastPollMs = millis();
    if (pollStatus()) app->refreshDiagnostic();
  }

  app->update();
  delay(10);
}
