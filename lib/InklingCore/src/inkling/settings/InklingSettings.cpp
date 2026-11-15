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

bool InklingSettings::lockedOrientation(Orientation& out) const {
  switch (rotationMode) {
    case RotationMode::LockPortrait:   out = Orientation::Portrait; return true;
    case RotationMode::LockInverted:   out = Orientation::PortraitInverted; return true;
    case RotationMode::LockLandscapeL: out = Orientation::LandscapeClockwise; return true;
    case RotationMode::LockLandscapeR: out = Orientation::LandscapeCounterClockwise; return true;
    default:                           return false;
  }
}

const char* InklingSettings::rotationModeLabel() const {
  switch (rotationMode) {
    case RotationMode::Auto:           return "Auto verticale";
    case RotationMode::AutoAll:        return "Auto 4 lati";
    case RotationMode::LockPortrait:   return "Verticale 0°";
    case RotationMode::LockLandscapeL: return "Orizz. 90° tasti SX";
    case RotationMode::LockInverted:   return "Capovolto 180°";
    case RotationMode::LockLandscapeR: return "Orizz. 270° tasti DX";
    default:                           return "Auto verticale";
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
    case DeskWidgetType::StoicQuote:  return "Pensiero del giorno";
    case DeskWidgetType::MoonPhase:   return "Fase lunare";
    case DeskWidgetType::ObliqueCard: return "Strategia obliqua";
    default:                          return "Pensiero del giorno";
  }
}

const char* InklingSettings::invertKeysLabel() const {
  return invertKeysOn180 ? "Seguono lo schermo" : "Fissi (come a 0°)";
}

const char* InklingSettings::nightModeLabel() const {
  return nightMode ? "Dark Invertito" : "Chiaro Carta E-Ink";
}

void InklingSettings::cycleRotationMode(int delta) {
  // UI order: the two automatic modes, then the four fixed sides clockwise.
  static constexpr RotationMode kOrder[] = {
      RotationMode::Auto,           RotationMode::AutoAll,      RotationMode::LockPortrait,
      RotationMode::LockLandscapeL, RotationMode::LockInverted, RotationMode::LockLandscapeR,
  };
  constexpr int count = static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0]));
  int cur = 0;
  for (int i = 0; i < count; ++i) {
    if (kOrder[i] == rotationMode) cur = i;
  }
  cur = ((cur + delta) % count + count) % count;
  rotationMode = kOrder[cur];
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
  if (n != static_cast<int32_t>(sizeof(s)) || s.magic != kMagic || s.version != kVersion) return false;
  const InklingSettings def{};
  // Raw bytes from the card: clamp every enum, bools included (anything but 0/1).
  auto bytes = [](const void* p) { return *static_cast<const uint8_t*>(p); };
  if (bytes(&s.rotationMode) >= static_cast<uint8_t>(RotationMode::Count)) s.rotationMode = def.rotationMode;
  if (bytes(&s.autoSleep) >= static_cast<uint8_t>(AutoSleepOption::Count)) s.autoSleep = def.autoSleep;
  if (bytes(&s.wakeSensitivity) >= static_cast<uint8_t>(WakeSensitivity::Count)) {
    s.wakeSensitivity = def.wakeSensitivity;
  }
  if (bytes(&s.refreshCadence) >= static_cast<uint8_t>(RefreshCadence::Count)) s.refreshCadence = def.refreshCadence;
  if (bytes(&s.deskWidget) >= static_cast<uint8_t>(DeskWidgetType::Count)) s.deskWidget = def.deskWidget;
  if (bytes(&s.invertKeysOn180) > 1) s.invertKeysOn180 = def.invertKeysOn180;
  if (bytes(&s.nightMode) > 1) s.nightMode = def.nightMode;
  *this = s;
  return true;
}

bool InklingSettings::save(Storage& storage) const {
  storage.mkdirs("/.inkling");
  return storage.writeFile(kPath, this, sizeof(*this));
}

}  // namespace inkling
