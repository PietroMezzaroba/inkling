// Inkling firmware entry point (Xteink X3).
//
// Phase 0: bring up every piece of hardware and show it on a diagnostic screen.
// All drawing goes through InklingCore so the simulator renders the same pixels.

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

#include "FirmwareSlots.h"
#include "inkling/Screens.h"

#if INKLING_SERIAL_LOG
#define LOG(fmt, ...) Serial.printf("[%lu] " fmt "\n", millis(), ##__VA_ARGS__)
#else
#define LOG(...) \
  do {           \
  } while (0)
#endif

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

freeink::ui::DisplayTarget* target = nullptr;
inkling::UiApp* app = nullptr;
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
  return changed;
}

void renderIfNeeded() {
  if (!app->invalidated()) return;
  app->render(freeink::ui::InputSnapshot{});
  freeink::ui::present(*display, app->lastRenderRefreshHint());
}

[[noreturn]] void powerOff() {
  LOG("power off");
  app->setScreen(inkling::sleepScreen, nullptr, freeink::ui::RefreshHint::Full);
  renderIfNeeded();
  display->deepSleep();
  imu.sleep();

  // On the X3 this also drives GPIO13 low, which cuts the SD rail and the
  // battery switch: on battery the device is truly off and the next Power press
  // cold-boots it. On USB power the chip deep-sleeps and Power wakes it.
  freeink::PowerManager::powerDownRailsForSleep();
  freeink::PowerManager::deepSleepUntilPowerButton();
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

void showMessage(const char* message) {
  app->setScreen(inkling::messageScreen, const_cast<char*>(message), freeink::ui::RefreshHint::Full);
  renderIfNeeded();
}

void bootOtherSlotOrComplain() {
  LOG("switching to other slot: %s", status.otherFirmware);
  showMessage("Riavvio nell'altro firmware...");
  firmware::bootOtherSlot();  // returns only on failure
  status.notice = "Nessun firmware nell'altro slot";
  app->setScreen(inkling::diagnosticScreen, &status, freeink::ui::RefreshHint::Full);
}

void handleButtons() {
  for (uint8_t b = InputManager::BTN_BACK; b <= InputManager::BTN_DOWN; ++b) {
    if (!input.wasPressed(b)) continue;
    status.lastButton = InputManager::getButtonName(b);
    status.buttonPresses++;
    pollStatus();  // fresh IMU values with every press
    app->invalidate();
    LOG("button %s", status.lastButton);
  }

  if (input.isPressed(InputManager::BTN_BACK)) {
    if (backPressedAt == 0) backPressedAt = millis();
    if (!backHoldHandled && millis() - backPressedAt >= kOtherSlotHoldMs) {
      backHoldHandled = true;
      bootOtherSlotOrComplain();
    }
  } else {
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

  // Board detection must come first: it picks the X3 profile (UC8253 or the
  // newer UC8279 batch) and latches the power rails before any pin is used.
  const bool x3Confirmed = freeink::selectXteinkDevice();

  // Escape hatch, checked before anything that could crash: Back held while
  // powering on restarts into the other slot (e.g. the previous CrossPet).
  input.begin();
  status.otherFirmware = firmware::otherSlotName();
  if (backHeldAtBoot()) {
    backHoldHandled = true;  // don't fire the long-press again for this same hold
    firmware::bootOtherSlot();  // returns only on failure
    status.notice = "Nessun firmware nell'altro slot";
  }

  const auto& pins = BoardConfig::ACTIVE.display;
  // The SD card shares the display's SPI bus on the X3 and only adds MISO.
  SPI.begin(pins.sclk, BoardConfig::ACTIVE.sd.miso, pins.mosi, pins.cs);

  status.sdReady = SdMan.begin();

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
  target = &displayTarget;
  app = &uiApp;
  app->setClearColor(freeink::ui::Color::White);

  // SD self-update: works for any ESP32-C3 app, so CrossPet can be put back
  // the same way by saving its .bin as /inkling.bin.
  if (status.sdReady && SdMan.exists(firmware::kSdUpdatePath)) {
    LOG("SD update found: %s", firmware::kSdUpdatePath);
    showMessage("Aggiornamento dalla scheda SD...");
    const char* error = "";
    firmware::installFromSd(firmware::kSdUpdatePath, &error);  // restarts on success
    LOG("SD update failed: %s", error);
    static char notice[64];
    snprintf(notice, sizeof(notice), "Aggiornamento fallito: %s", error);
    status.notice = notice;
  }

  app->setScreen(inkling::diagnosticScreen, &status, freeink::ui::RefreshHint::Full);
  renderIfNeeded();
}

void loop() {
  input.update();
  handleButtons();

  if (millis() - lastPollMs >= kStatusPollMs) {
    lastPollMs = millis();
    if (pollStatus()) app->invalidate();
  }

  renderIfNeeded();
  delay(10);
}
