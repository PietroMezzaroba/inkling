#pragma once

#include <cstdint>

#include "inkling/Platform.h"

namespace inkling {

enum class RotationMode : uint8_t {
  Auto = 0,       // Accelerometer automatic transposition (0° <-> 180°)
  LockPortrait,   // Locked to 0° (Standard Portrait)
  LockInverted,   // Locked to 180° (Upside-down Portrait, buttons at top)
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

enum class DeskWidgetType : uint8_t {
  GitHubHeatmap = 0,
  MoonPhase = 1,
  StoicQuote = 2,
  Count,
};

struct InklingSettings {
  static constexpr uint32_t kMagic = 0x494E4B53u;  // "INKS"
  static constexpr uint8_t kVersion = 1;
  static constexpr const char* kPath = "/.inkling/settings.bin";

  uint32_t magic = kMagic;
  uint8_t version = kVersion;
  RotationMode rotationMode = RotationMode::Auto;
  bool invertKeysOn180 = true;
  AutoSleepOption autoSleep = AutoSleepOption::Min5;
  WakeSensitivity wakeSensitivity = WakeSensitivity::Normal;
  RefreshCadence refreshCadence = RefreshCadence::Pages12;
  DeskWidgetType deskWidget = DeskWidgetType::GitHubHeatmap;
  bool nightMode = false;
  uint8_t reserved[7] = {};

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

  bool load(Storage& storage);
  bool save(Storage& storage) const;
};

}  // namespace inkling
