// Inkling firmware entry point (Xteink X3).
//
// Hardware bring-up and the main loop. Everything the user sees is InklingApp
// (lib/InklingCore), which the host simulator runs unchanged.

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <FreeInkUIDisplayTarget.h>
#include <driver/Uc8253X3Driver.h>
#include <lut/Uc8253X3Luts.h>
#include <Imu.h>
#include <InputManager.h>
#include <PowerManager.h>
#include <Rtc.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <XteinkDetect.h>

#include <cmath>
#include <new>

#include "DeviceLog.h"
#include "FirmwareSlots.h"
#include "SdStorage.h"
#include "inkling/App.h"
#include "inkling/Orientation.h"
#include "inkling/Screens.h"
#include "inkling/UiGlyphFallback.h"
#include "inkling/ui/InklingFonts.h"

// Reading faces, embedded in flash by board_build.embed_files and read in place
// through the flash cache: no RAM copy.
extern const uint8_t kNotoSerifRegStart[] asm("_binary_assets_fonts_NotoSerif_Regular_ttf_start");
extern const uint8_t kNotoSerifRegEnd[] asm("_binary_assets_fonts_NotoSerif_Regular_ttf_end");
extern const uint8_t kNotoSerifItStart[] asm("_binary_assets_fonts_NotoSerif_Italic_ttf_start");
extern const uint8_t kNotoSerifItEnd[] asm("_binary_assets_fonts_NotoSerif_Italic_ttf_end");

extern const uint8_t kNotoSansRegStart[] asm("_binary_assets_fonts_NotoSans_Regular_ttf_start");
extern const uint8_t kNotoSansRegEnd[] asm("_binary_assets_fonts_NotoSans_Regular_ttf_end");
extern const uint8_t kNotoSansItStart[] asm("_binary_assets_fonts_NotoSans_Italic_ttf_start");
extern const uint8_t kNotoSansItEnd[] asm("_binary_assets_fonts_NotoSans_Italic_ttf_end");

extern const uint8_t kAlegreyaRegStart[] asm("_binary_assets_fonts_Alegreya_Regular_ttf_start");
extern const uint8_t kAlegreyaRegEnd[] asm("_binary_assets_fonts_Alegreya_Regular_ttf_end");
extern const uint8_t kAlegreyaItStart[] asm("_binary_assets_fonts_Alegreya_Italic_ttf_start");
extern const uint8_t kAlegreyaItEnd[] asm("_binary_assets_fonts_Alegreya_Italic_ttf_end");

extern const uint8_t kDyslexicRegStart[] asm("_binary_assets_fonts_OpenDyslexic_Regular_otf_start");
extern const uint8_t kDyslexicRegEnd[] asm("_binary_assets_fonts_OpenDyslexic_Regular_otf_end");
extern const uint8_t kDyslexicItStart[] asm("_binary_assets_fonts_OpenDyslexic_Italic_otf_start");
extern const uint8_t kDyslexicItEnd[] asm("_binary_assets_fonts_OpenDyslexic_Italic_otf_end");

extern const uint8_t kUpdaterBinStart[] asm("_binary_assets_updater_bin_start");
extern const uint8_t kUpdaterBinEnd[] asm("_binary_assets_updater_bin_end");

namespace freeink {

// Calibrated overdrive differential LUT bank for UC8253:
// Uses the OEM mode-2 two-stage overdrive (0xAA -> 0xA0 for BW, 0x55 -> 0x50 for WB,
// 0x04 micro-settle for BB) with 5-frame primary pulses + 4-frame overdrive settle,
// eliminating gray ghosting ("aloni") while keeping page turns snappy (~290 ms).
inline const uint8_t lut_x3_vcom_turbo[] PROGMEM = {
    0x00, 0x05, 0x01, 0x05, 0x05, 0x01, 0x00, 0x04, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
inline const uint8_t lut_x3_ww_turbo[] PROGMEM = {
    0x20, 0x05, 0x01, 0x05, 0x05, 0x01, 0x00, 0x04, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
inline const uint8_t lut_x3_bw_turbo[] PROGMEM = {
    0xAA, 0x05, 0x01, 0x05, 0x05, 0x01, 0xA0, 0x04, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
inline const uint8_t lut_x3_wb_turbo[] PROGMEM = {
    0x55, 0x05, 0x01, 0x05, 0x05, 0x01, 0x50, 0x04, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
inline const uint8_t lut_x3_bb_turbo[] PROGMEM = {
    0x00, 0x05, 0x01, 0x05, 0x05, 0x01, 0x04, 0x04, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const Uc8253X3Config& inklingUc8253X3Config() {
  static const Uc8253X3Config kCfg = {
      {lut_x3_vcom_normal, lut_x3_ww_normal, lut_x3_bw_normal, lut_x3_wb_normal, lut_x3_bb_normal},
      {lut_x3_vcom_half, lut_x3_ww_half, lut_x3_bw_half, lut_x3_wb_half, lut_x3_bb_half},
      {lut_x3_vcom_turbo, lut_x3_ww_turbo, lut_x3_bw_turbo, lut_x3_wb_turbo, lut_x3_bb_turbo},
      {lut_x3_vcom_full, lut_x3_ww_full, lut_x3_bw_full, lut_x3_wb_full, lut_x3_bb_full},
      {lut_x3_vcom_gc, lut_x3_ww_gc, lut_x3_bw_gc, lut_x3_wb_gc, lut_x3_bb_gc},
      {lut_x3_vcom_aa_pre_bw_mid, lut_x3_ww_aa_pre_bw_mid, lut_x3_bw_aa_pre_bw_mid, lut_x3_wb_aa_pre_bw_mid,
       lut_x3_bb_aa_pre_bw_mid},
      X3_LUT_LEN,
  };
  return kCfg;
}

}  // namespace freeink

namespace {

// Holding Power this long turns the device off.
constexpr unsigned long kPowerOffHoldMs = 1000;
// Holding Back this long on the diagnostic screen restarts into the other slot.
constexpr unsigned long kOtherSlotHoldMs = 3000;
// How long Back is sampled at boot for the "hold Back while powering on" escape.
constexpr unsigned long kBootEscapeSampleMs = 300;
// How often the clock, battery and IMU are re-read while idle.
constexpr unsigned long kStatusPollMs = 30000;
// CPU clock while a page is being read (nothing to compute, keys polled).
constexpr uint32_t kIdleCpuMhz = 10;

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

freeink::ui::DisplayTarget* gDisplayTarget = nullptr;
freeink::book::FrameTarget* gFrameTarget = nullptr;
freeink::ui::Orientation currentOrientation = freeink::ui::Orientation::Portrait;

void renderNow();

// The app's orientation handler: rotates the UI target, then lets the app
// follow (page rotation, relayout, full repaint). Only InklingApp calls it, via
// setDesiredOrientation()/applyOrientation(), so the two never disagree.
void setAppOrientation(freeink::ui::Orientation orient) {
  if (currentOrientation == orient) return;
  LOG("orientation %s -> %s", inkling::orientationName(currentOrientation), inkling::orientationName(orient));
  currentOrientation = orient;
  inkling::ui::canvas::setCanvasOrientation(orient);
  if (gDisplayTarget) {
    gDisplayTarget->setOrientation(orient);
    if (ui) ui->setDevice(gDisplayTarget->deviceContext());
  }
  if (gFrameTarget) gFrameTarget->rotation = inkling::frameRotationFor(orient);
  if (app) app->onOrientationChanged(orient);
}

void onOrientationRequest(freeink::ui::Orientation orient, void*) { setAppOrientation(orient); }

inkling::OrientationTracker orientationTracker;

// The press that powered the device on is still held at boot; Power only arms
// the power-off gesture after it has been released once.
bool powerArmed = false;
unsigned long powerPressedAt = 0;
unsigned long backPressedAt = 0;
bool backHoldHandled = false;
unsigned long lastPollMs = 0;
unsigned long lastImuMs = 0;
unsigned long lastActivityMs = 0;
bool cpuTurboActive = true;

// Full speed for anything that computes or talks SPI (rendering, the SD card):
// below 80 MHz the bus clock follows the CPU clock.
void cpuFull() {
  if (!cpuTurboActive) {
    setCpuFrequencyMhz(160);
    cpuTurboActive = true;
  }
}

void ensureCpuTurbo() {
  lastActivityMs = millis();
  cpuFull();
}

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

void presentFrame(freeink::ui::RefreshHint hint, void*) {
  if (hint == freeink::ui::RefreshHint::Fast) {
    // Non-blocking start: fires the differential overdrive waveform and lets
    // the main loop keep sampling buttons at 500 Hz while the panel refreshes.
    display->triggerDisplay(EInkDisplay::FAST_REFRESH);
  } else if (hint == freeink::ui::RefreshHint::Clean) {
    // Non-blocking single-pass scrub (WW==BW, WB==BB): drives every pixel
    // unconditionally to clean white/black in ~350 ms without double-flashing.
    display->triggerDisplay(EInkDisplay::HALF_REFRESH);
  } else {
    display->displayBuffer(EInkDisplay::FULL_REFRESH);
  }
}

void renderNow() {
  if (!ui->invalidated()) return;
  display->completeDisplay();
  ui->render(freeink::ui::InputSnapshot{});
  presentFrame(ui->lastRenderRefreshHint(), nullptr);
}

RTC_DATA_ATTR bool rtcDeviceWasSleeping = false;

[[noreturn]] void powerOff() {
  cpuFull();
  LOG("power off");
  display->completeDisplay();
  app->shutdown();  // reading position is already saved per page; this closes files
  ui->setScreen(inkling::sleepScreen, app, freeink::ui::RefreshHint::Full);
  renderNow();
  display->completeDisplay();
  display->deepSleep();
  imu.sleep();

  // Remember that the device was placed into sleep with the lock screen displayed
  rtcDeviceWasSleeping = true;

  // On the X3 this also drives GPIO13 low, which cuts the SD rail and the
  // battery switch: on battery the device is truly off and the next Power press
  // cold-boots it. On USB power the chip deep-sleeps and Power wakes it.
  freeink::PowerManager::powerDownRailsForSleep();
  freeink::PowerManager::deepSleepUntilPowerButton();
}

void onAppRequest(inkling::InklingApp::Request request, void*) {
  if (request == inkling::InklingApp::Request::WifiUpdate) {
    app->shutdown();
    const size_t updaterLen = static_cast<size_t>(kUpdaterBinEnd - kUpdaterBinStart);
    if (firmware::updaterSlotNeedsUpgrade(kUpdaterBinStart, updaterLen)) {
      LOG("upgrading other slot with embedded Cyber-Net updater (%u bytes)", static_cast<unsigned>(updaterLen));
      app->showMessage("Installazione Cyber-Net Hub 2.0 in corso...");
      app->update();
      display->completeDisplay();
      firmware::installEmbeddedUpdaterAndBoot(kUpdaterBinStart, updaterLen);
    }
    LOG("switching to the updater: %s", firmware::otherSlotName());
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

constexpr uint8_t kButtonQueueCap = 16;
inkling::Button buttonQueue[kButtonQueueCap];
uint8_t buttonQueueHead = 0;
uint8_t buttonQueueTail = 0;

void enqueueButton(inkling::Button b) {
  const uint8_t next = (buttonQueueTail + 1) % kButtonQueueCap;
  if (next != buttonQueueHead) {
    buttonQueue[buttonQueueTail] = b;
    buttonQueueTail = next;
  }
}

bool dequeueButton(inkling::Button& out) {
  if (buttonQueueHead == buttonQueueTail) return false;
  out = buttonQueue[buttonQueueHead];
  buttonQueueHead = (buttonQueueHead + 1) % kButtonQueueCap;
  return true;
}

void latchButtons() {
  struct ButtonMap {
    uint8_t hw;
    inkling::Button button;
  };
  static constexpr ButtonMap kMap[] = {
      {InputManager::BTN_BACK, inkling::Button::Back}, {InputManager::BTN_CONFIRM, inkling::Button::Confirm},
      {InputManager::BTN_LEFT, inkling::Button::Left}, {InputManager::BTN_RIGHT, inkling::Button::Right},
      {InputManager::BTN_UP, inkling::Button::Up},     {InputManager::BTN_DOWN, inkling::Button::Down},
  };
  // Keys follow the screen in every orientation unless the user turned it off.
  const bool spatial = app && app->settings().invertKeysOn180;
  for (const auto& m : kMap) {
    if (!input.wasPressed(m.hw)) continue;
    ensureCpuTurbo();
    status.lastButton = InputManager::getButtonName(m.hw);
    status.buttonPresses++;
    enqueueButton(spatial ? inkling::spatialButton(m.button, currentOrientation) : m.button);
  }

  // Back held 3 s on the diagnostic screen: restart into the other slot. Only
  // there, so closing a book with Back can never turn into a firmware switch.
  if (input.isPressed(InputManager::BTN_BACK) && app->mode() == inkling::InklingApp::Mode::Diagnostic) {
    if (backPressedAt == 0) backPressedAt = millis();
    if (!backHoldHandled && millis() - backPressedAt >= kOtherSlotHoldMs) {
      backHoldHandled = true;
      display->completeDisplay();
      LOG("switching to other slot: %s", status.otherFirmware);
      app->showMessage("Riavvio nell'altro firmware...");
      app->update();
      display->completeDisplay();
      firmware::bootOtherSlot();  // returns only on failure
      app->showMessage("Nessun firmware nell'altro slot");
    }
  } else if (!input.isPressed(InputManager::BTN_BACK)) {
    backPressedAt = 0;
    backHoldHandled = false;
  }

  const bool powerDown = input.isPressed(InputManager::BTN_POWER);
  if (!powerDown) {
    if (powerArmed && powerPressedAt > 0) {
      const unsigned long duration = millis() - powerPressedAt;
      powerPressedAt = 0;
      // Single click on Power button (press and release): enter sleep immediately!
      if (duration >= 40 && duration < 900) {
        LOG("power button clicked (%lu ms): going to sleep", duration);
        powerOff();
      }
    }
    powerArmed = true;
    powerPressedAt = 0;
    return;
  }
  if (!powerArmed) return;
  if (powerPressedAt == 0) powerPressedAt = millis();
  if (millis() - powerPressedAt >= kPowerOffHoldMs) {
    LOG("power button held: going to sleep");
    powerOff();
  }
}

}  // namespace

void setup() {
  setCpuFrequencyMhz(160);
  Serial.begin(115200);
  devlog::begin("reader");

  // Board detection must come first: it picks the X3 profile (UC8253 or the
  // newer UC8279 batch) and latches the power rails before any pin is used.
  const bool x3Confirmed = freeink::selectXteinkDevice();
  BoardConfig::ACTIVE.displaySpiHz = 20000000u;

  // Escape hatch, checked before anything that could crash: Back held while
  // powering on restarts into the other slot (e.g. the previous CrossPet).
  input.begin();

  // Spurious / glitch wake-up filter while asleep:
  // When the device is asleep with the lock screen displayed, waking up occurs
  // when the user clicks or presses the Power button (ESP_SLEEP_WAKEUP_GPIO).
  // Non-GPIO events (such as power glitches or brownout from plugging / unplugging the USB cable)
  // are detected and ignored, keeping the lock screen intact and returning to sleep immediately.
  const esp_reset_reason_t resetReason = esp_reset_reason();
  const esp_sleep_wakeup_cause_t wakeupCause = esp_sleep_get_wakeup_cause();
  if (rtcDeviceWasSleeping) {
    if (resetReason != ESP_RST_DEEPSLEEP || wakeupCause != ESP_SLEEP_WAKEUP_GPIO) {
      LOG("ignoring non-gpio glitch wake while asleep (reason %d, cause %d), returning to sleep",
          static_cast<int>(resetReason), static_cast<int>(wakeupCause));
      freeink::PowerManager::powerDownRailsForSleep();
      freeink::PowerManager::deepSleepUntilPowerButton();
    }
    // Genuine power button click / press: wake up immediately!
    rtcDeviceWasSleeping = false;
  }

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
  BoardConfig::ACTIVE.displaySpiHz = 20000000u;  // 20 MHz SPI (2x default 10 MHz)
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
  gDisplayTarget = &displayTarget;
  inkling::ui::bindInklingFonts(displayTarget);
  static inkling::UiApp uiApp(displayTarget, displayTarget.deviceContext());
  ui = &uiApp;

  static freeink::book::FrameTarget frame{display->getFrameBuffer(), static_cast<int16_t>(display->getDisplayWidth()),
                                          static_cast<int16_t>(display->getDisplayHeight()),
                                          static_cast<int16_t>(display->getDisplayWidthBytes())};
  frame.format = freeink::book::FrameFormat::Mono1Dithered;
  frame.rotation = freeink::book::FrameRotation::Portrait;
  gFrameTarget = &frame;

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
    // stb_truetype mallocs while rasterizing each glyph (outline, edges,
    // active-edge chunks). Upstream asked for one ~56 KB block and crashed the
    // device; third_party/stb-patched keeps chunks at ~2 KB, so a modest
    // contiguous reserve is enough. Italic goes first, then the regular cache
    // shrinks to its minimum.
    constexpr size_t kRasterReserve = 8 * 1024;
    auto fits = [&](size_t len) {
      return ESP.getMaxAllocHeap() >= len + kRasterReserve && spare() >= len + kRasterReserve;
    };
    size_t regularLen = fits(Glyphs::kRegularIdeal) ? Glyphs::kRegularIdeal : Glyphs::kRegularMin;
    uint8_t* regularCache = static_cast<uint8_t*>(malloc(regularLen));
    uint8_t* italicCache = nullptr;
    if (regularCache && fits(Glyphs::kItalic)) italicCache = static_cast<uint8_t*>(malloc(Glyphs::kItalic));
    if (italicCache && ESP.getMaxAllocHeap() < kRasterReserve) {
      free(italicCache);  // the two caches split the big free block: give it back
      italicCache = nullptr;
    }

    static inkling::BookReader bookReader(storage, *readerMemory);
    reader = &bookReader;
    // Chapter page index: its own block, taken after the glyph caches so it
    // never shrinks them; the ideal size when the rasterizer reserve survives.
    using Index = inkling::IndexSizes;
    size_t indexLen = fits(Index::kIdeal) ? Index::kIdeal : Index::kMin;
    uint8_t* indexBuf = static_cast<uint8_t*>(malloc(indexLen));
    if (!indexBuf && indexLen != Index::kMin) {
      indexLen = Index::kMin;
      indexBuf = static_cast<uint8_t*>(malloc(indexLen));
    }
    if (indexBuf) {
      reader->setIndexBuffer(indexBuf, indexLen);
    } else {
      indexLen = 0;
      status.notice = "Memoria insufficiente per l'indice";
    }
    // Parsing borrows the framebuffer while a chapter is laid out.
    reader->setParseBuffer(display->getFrameBuffer(),
                           static_cast<size_t>(display->getDisplayWidthBytes()) * display->getDisplayHeight());
    if (regularCache) {
      fontsOk = reader->initFonts(kNotoSerifRegStart, static_cast<uint32_t>(kNotoSerifRegEnd - kNotoSerifRegStart),
                                  kNotoSerifItStart, static_cast<uint32_t>(kNotoSerifItEnd - kNotoSerifItStart),
                                  regularCache, regularLen, italicCache, italicCache ? Glyphs::kItalic : 0);
      reader->registerFontFamily(1, kNotoSansRegStart, static_cast<uint32_t>(kNotoSansRegEnd - kNotoSansRegStart),
                                 kNotoSansItStart, static_cast<uint32_t>(kNotoSansItEnd - kNotoSansItStart));
      reader->registerFontFamily(2, kAlegreyaRegStart, static_cast<uint32_t>(kAlegreyaRegEnd - kAlegreyaRegStart),
                                 kAlegreyaItStart, static_cast<uint32_t>(kAlegreyaItEnd - kAlegreyaItStart));
      reader->registerFontFamily(3, kDyslexicRegStart, static_cast<uint32_t>(kDyslexicRegEnd - kDyslexicRegStart),
                                 kDyslexicItStart, static_cast<uint32_t>(kDyslexicItEnd - kDyslexicItStart));
    }
    if (fontsOk) {
      // UI chrome (menus, titles) draws what its ASCII bitmap font lacks, like
      // accented letters, with the reading face.
      static inkling::UiGlyphFallback uiFallback;
      uiFallback.setFont(&reader->regularFont());
      displayTarget.setGlyphFallback(&uiFallback);
    }
    LOG("glyph caches: regular %u, italic %u, index %u, max block left %u",
        static_cast<unsigned>(regularCache ? regularLen : 0), static_cast<unsigned>(italicCache ? Glyphs::kItalic : 0),
        static_cast<unsigned>(indexLen), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  } else {
    status.notice = "Memoria insufficiente per il lettore";
  }
  LOG("reader memory: %s (%u bytes), fonts: %d, free heap %u", readerMemory ? "ok" : "FAILED",
      static_cast<unsigned>(sizeof(inkling::ReaderMemory)), fontsOk, static_cast<unsigned>(ESP.getFreeHeap()));

  static inkling::InklingApp inklingApp(storage, reader, status, uiApp, frame);
  app = &inklingApp;
  app->setPresenter(presentFrame, nullptr);
  app->setRequestHandler(onAppRequest, nullptr);
  app->setOrientationHandler(onOrientationRequest, nullptr);

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

  // Rotation locks were applied by begin(); automatic modes start from how
  // the device is held right now.
  if (app->settings().autoRotation() && status.imuReady) {
    freeink::ui::Orientation initial;
    if (inkling::OrientationTracker::classify(status.ax, status.ay, status.az, app->settings().autoAllowsLandscape(),
                                              initial)) {
      app->setDesiredOrientation(initial);
    }
  }
  app->update();
  display->completeDisplay();

  // First screen is up: this build works, keep it (cancels the rollback), and
  // stop writing the log to the card.
  firmware::markValid();
  lastActivityMs = millis();
  LOG("boot done, free heap %u (max block %u)", static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(ESP.getMaxAllocHeap()));
  devlog::detachFile();
}

void loop() {
  status.uptimeMs = millis();
  // Sample ADC button ladder at ~500 Hz while active or refreshing so rapid
  // clicks are never missed.
  input.update();
  latchButtons();

  if (display->refreshBusy()) {
    delay(2);
    return;
  }

  // Panel is idle: finish any post-waveform DTM1 sync before touching the
  // framebuffer or parsing chapters.
  display->completeDisplay();

  inkling::Button btn;
  while (dequeueButton(btn)) {
    ensureCpuTurbo();
    if (app->mode() == inkling::InklingApp::Mode::Diagnostic) pollStatus();
    app->onButton(btn);
  }

  if (status.imuReady && buttonQueueHead == buttonQueueTail && millis() - lastImuMs >= 200) {
    lastImuMs = millis();
    freeink::Imu::Sample sample;
    if (imu.read(sample)) {
      static float prevAx = 0, prevAy = 0, prevAz = 0;
      const float dAx = std::fabs(sample.ax - prevAx);
      const float dAy = std::fabs(sample.ay - prevAy);
      const float dAz = std::fabs(sample.az - prevAz);
      prevAx = sample.ax;
      prevAy = sample.ay;
      prevAz = sample.az;

      // Picking up or tilting the device counts as user activity
      if (dAx > 0.18f || dAy > 0.18f || dAz > 0.18f) {
        lastActivityMs = millis();
      }

      status.ax = sample.ax;
      status.ay = sample.ay;
      status.az = sample.az;
      app->onImuSample(sample.ax, sample.ay, sample.az);

      // Auto-rotation (Settings: "Auto verticale" / "Auto 4 lati").
      if (app->settings().autoRotation()) {
        freeink::ui::Orientation next;
        if (orientationTracker.update(sample.ax, sample.ay, sample.az, app->settings().autoAllowsLandscape(),
                                      app->desiredOrientation(), next)) {
          app->setDesiredOrientation(next);
        }
      } else {
        orientationTracker.reset();
      }
    }
  }

  if (millis() - lastPollMs >= kStatusPollMs) {
    lastPollMs = millis();
    if (pollStatus()) app->refreshDiagnostic();
  }

  if (ui->invalidated()) cpuFull();
  app->update();

  // Deferred work (position and statistics to the card, the next chapter's
  // pagination), one slice per pass and only while no key is down or bouncing,
  // so a press is never stuck behind it.
  bool idleWorked = false;
  if (!display->refreshBusy() && buttonQueueHead == buttonQueueTail && !input.isDebouncePending() &&
      !input.isPressed(InputManager::BTN_POWER)) {
    const uint32_t idleMs = static_cast<uint32_t>(millis() - lastActivityMs);
    if (app->hasIdleWork(idleMs)) {
      cpuFull();
      idleWorked = app->idleTick(idleMs);
    }
  }

  // Inactivity auto-sleep (configurable in Settings):
  const unsigned long autoSleepTimeoutMs = app->settings().autoSleepTimeoutMs();
  const bool isDeskClockOnUsb = (app->mode() == inkling::InklingApp::Mode::DeskAmbient) && status.charging;
  if (!isDeskClockOnUsb && autoSleepTimeoutMs != 0xFFFFFFFFUL && (millis() - lastActivityMs >= autoSleepTimeoutMs)) {
    cpuFull();
    LOG("inactivity timeout (%lu ms): entering auto-sleep", autoSleepTimeoutMs);
    powerOff();
  }

  // Power: 160 MHz with 2 ms polling for 3 s after a key, while the panel
  // refreshes and while there is deferred work. Then the CPU drops to 10 MHz
  // (the clock CrossPoint idles this board at) and the keys are polled every
  // 15 ms. A key change needs two matching samples to register, so the moment
  // one sample differs the next comes 2 ms later, at full speed.
  if (idleWorked) return;
  if (display->refreshBusy() || (millis() - lastActivityMs < 3000)) {
    delay(2);
  } else if (input.isDebouncePending()) {
    cpuFull();
    delay(2);
  } else {
    if (cpuTurboActive) {
      setCpuFrequencyMhz(kIdleCpuMhz);
      cpuTurboActive = false;
    }
    delay(15);
  }
}
