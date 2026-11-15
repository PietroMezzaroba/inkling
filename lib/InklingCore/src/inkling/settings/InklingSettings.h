#pragma once

#include <cstdint>

#include "inkling/Orientation.h"
#include "inkling/Platform.h"

namespace inkling {

// Stored as a byte in settings.bin: never renumber, only append.
enum class RotationMode : uint8_t {
  Auto = 0,            // accelerometer, vertical only (0° <-> 180°)
  LockPortrait = 1,    // 0°, keys at the bottom
  LockInverted = 2,    // 180°, keys at the top
  LockLandscapeL = 3,  // 90°, keys on the left
  LockLandscapeR = 4,  // 270°, keys on the right
  AutoAll = 5,         // accelerometer, all four sides
  Count,
};

enum class AutoSleepOption : uint8_t {
  Min3 = 0,
  Min5 = 1,
  Min10 = 2,
  Disabled = 3,
  Count,
};

enum class WakeSensitivity : uint8_t {
  Fast = 0,    // 200 ms hold required
  Normal = 1,  // 350 ms hold required (default)
  Safe = 2,    // 500 ms hold required (pocket-safe)
  Count,
};

enum class RefreshCadence : uint8_t {
  Pages6 = 0,
  Pages12 = 1,
  Pages24 = 2,
  Manual = 3,
  Count,
};

// Same numbers as DeskSubWidget.
enum class DeskWidgetType : uint8_t {
  StoicQuote = 0,
  MoonPhase = 1,
  ObliqueCard = 2,
  Count,
};

struct InklingSettings {
  static constexpr uint32_t kMagic = 0x494E4B53u;  // "INKS"
  static constexpr uint8_t kVersion = 1;
  static constexpr const char* kPath = "/.inkling/settings.bin";

  uint32_t magic = kMagic;
  uint8_t version = kVersion;
  RotationMode rotationMode = RotationMode::Auto;
  bool invertKeysOn180 = true;  // keys follow the rotation (all non-0° orientations)
  AutoSleepOption autoSleep = AutoSleepOption::Min5;
  WakeSensitivity wakeSensitivity = WakeSensitivity::Normal;
  RefreshCadence refreshCadence = RefreshCadence::Pages12;
  DeskWidgetType deskWidget = DeskWidgetType::StoicQuote;
  bool nightMode = false;
  uint8_t reserved[7] = {};

  bool autoRotation() const { return rotationMode == RotationMode::Auto || rotationMode == RotationMode::AutoAll; }
  bool autoAllowsLandscape() const { return rotationMode == RotationMode::AutoAll; }
  // The fixed orientation of a Lock* mode; false for the automatic modes.
  bool lockedOrientation(Orientation& out) const;

  unsigned long autoSleepTimeoutMs() const;
  unsigned long wakeHoldMs() const;
  uint8_t fullRefreshPages() const;

  const char* rotationModeLabel() const;
  const char* autoSleepLabel() const;
  const char* wakeSensitivityLabel() const;
  const char* refreshCadenceLabel() const;
  const char* deskWidgetLabel() const;
  const char* invertKeysLabel() const;
  const char* nightModeLabel() const;

  void cycleRotationMode(int delta);
  void cycleInvertKeys();
  void cycleAutoSleep(int delta);
  void cycleWakeSensitivity(int delta);
  void cycleRefreshCadence(int delta);
  void cycleDeskWidget(int delta);
  void toggleNightMode();

  // Reads settings.bin; out-of-range fields (older or corrupt files) fall
  // back to their defaults one by one instead of discarding the whole file.
  bool load(Storage& storage);
  bool save(Storage& storage) const;
};

}  // namespace inkling
