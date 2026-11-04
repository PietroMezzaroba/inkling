#include "inkling/settings/InklingSettings.h"

namespace inkling {

unsigned long InklingSettings::autoSleepTimeoutMs() const {
  switch (autoSleep) {
    case AutoSleepOption::Min3:     return 3UL * 60UL * 1000UL;
    case AutoSleepOption::Min5:     return 5UL * 60UL * 1000UL;
    case AutoSleepOption::Min10:    return 10UL * 60UL * 1000UL;
    case AutoSleepOption::Disabled: return 0xFFFFFFFFUL;
    default:                        return 5UL * 60UL * 1000UL;
  }
}

unsigned long InklingSettings::wakeHoldMs() const {
  switch (wakeSensitivity) {
    case WakeSensitivity::Fast:   return 200;
    case WakeSensitivity::Normal: return 350;
    case WakeSensitivity::Safe:   return 500;
    default:                      return 350;
  }
}

uint8_t InklingSettings::fullRefreshPages() const {
  switch (refreshCadence) {
    case RefreshCadence::Pages6:  return 6;
    case RefreshCadence::Pages12: return 12;
    case RefreshCadence::Pages24: return 24;
    case RefreshCadence::Manual:  return 255;
    default:                      return 12;
  }
}

const char* InklingSettings::rotationModeLabel() const {
  switch (rotationMode) {
    case RotationMode::Auto:         return "Auto (Sensore IMU)";
    case RotationMode::LockPortrait: return "Blocco 0° (Standard)";
    case RotationMode::LockInverted: return "Blocco 180° (Sottosopra)";
    default:                         return "Auto (Sensore IMU)";
  }
}

const char* InklingSettings::autoSleepLabel() const {
  switch (autoSleep) {
    case AutoSleepOption::Min3:     return "3 Minuti (Eco)";
    case AutoSleepOption::Min5:     return "5 Minuti (Normale)";
    case AutoSleepOption::Min10:    return "10 Minuti (Esteso)";
    case AutoSleepOption::Disabled: return "Disattivato (Sempre On)";
    default:                        return "5 Minuti (Normale)";
  }
}

const char* InklingSettings::wakeSensitivityLabel() const {
  switch (wakeSensitivity) {
    case WakeSensitivity::Fast:   return "Veloce (200 ms)";
    case WakeSensitivity::Normal: return "Normale (350 ms)";
    case WakeSensitivity::Safe:   return "Sicuro (500 ms)";
    default:                      return "Normale (350 ms)";
  }
}

const char* InklingSettings::refreshCadenceLabel() const {
  switch (refreshCadence) {
    case RefreshCadence::Pages6:  return "Ogni 6 pag (Qualita)";
    case RefreshCadence::Pages12: return "Ogni 12 pag (Bilanciato)";
    case RefreshCadence::Pages24: return "Ogni 24 pag (Fluido)";
    case RefreshCadence::Manual:  return "Solo Manuale";
    default:                      return "Ogni 12 pag (Bilanciato)";
  }
}

const char* InklingSettings::deskWidgetLabel() const {
  switch (deskWidget) {
    case DeskWidgetType::GitHubHeatmap: return "GitHub Heatmap";
    case DeskWidgetType::MoonPhase:     return "Fase Lunare & Cielo";
    case DeskWidgetType::StoicQuote:    return "Daily Stoic & Eno";
    default:                            return "GitHub Heatmap";
  }
}

const char* InklingSettings::invertKeysLabel() const {
  return invertKeysOn180 ? "Attiva (Spaziale)" : "Disattivata (Fisso)";
}

const char* InklingSettings::nightModeLabel() const {
  return nightMode ? "Dark Invertito" : "Chiaro Carta E-Ink";
}

void InklingSettings::cycleRotationMode(int delta) {
  const int count = static_cast<int>(RotationMode::Count);
  int cur = static_cast<int>(rotationMode);
  cur = (cur + delta + count) % count;
  rotationMode = static_cast<RotationMode>(cur);
}

void InklingSettings::cycleInvertKeys() {
  invertKeysOn180 = !invertKeysOn180;
}

void InklingSettings::cycleAutoSleep(int delta) {
  const int count = static_cast<int>(AutoSleepOption::Count);
  int cur = static_cast<int>(autoSleep);
  cur = (cur + delta + count) % count;
  autoSleep = static_cast<AutoSleepOption>(cur);
}

void InklingSettings::cycleWakeSensitivity(int delta) {
  const int count = static_cast<int>(WakeSensitivity::Count);
  int cur = static_cast<int>(wakeSensitivity);
  cur = (cur + delta + count) % count;
  wakeSensitivity = static_cast<WakeSensitivity>(cur);
}

void InklingSettings::cycleRefreshCadence(int delta) {
  const int count = static_cast<int>(RefreshCadence::Count);
  int cur = static_cast<int>(refreshCadence);
  cur = (cur + delta + count) % count;
  refreshCadence = static_cast<RefreshCadence>(cur);
}

void InklingSettings::cycleDeskWidget(int delta) {
  const int count = static_cast<int>(DeskWidgetType::Count);
  int cur = static_cast<int>(deskWidget);
  cur = (cur + delta + count) % count;
  deskWidget = static_cast<DeskWidgetType>(cur);
}

void InklingSettings::toggleNightMode() {
  nightMode = !nightMode;
}

bool InklingSettings::load(Storage& storage) {
  InklingSettings s{};
  const int32_t n = storage.readFile(kPath, &s, sizeof(s));
  if (n == static_cast<int32_t>(sizeof(s)) && s.magic == kMagic && s.version == kVersion) {
    *this = s;
    return true;
  }
  return false;
}

bool InklingSettings::save(Storage& storage) const {
  storage.mkdirs("/.inkling");
  return storage.writeFile(kPath, this, sizeof(*this));
}

}  // namespace inkling
