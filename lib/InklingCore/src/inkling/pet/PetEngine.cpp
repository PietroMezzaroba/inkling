#include "PetEngine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace inkling {

namespace {

constexpr uint32_t kPetMagic = 0x50544E49;  // "INTP"
constexpr uint16_t kPetVersion = 2;

uint8_t clamp100(int v) {
  if (v < 0) return 0;
  if (v > 100) return 100;
  return static_cast<uint8_t>(v);
}

// Extracts an integer field from a flat JSON string (for migrating CrossPet SD data).
int32_t extractJsonInt(const char* json, const char* key, int32_t fallback) {
  char pattern[40];
  snprintf(pattern, sizeof(pattern), "\"%s\":", key);
  const char* p = strstr(json, pattern);
  if (!p) return fallback;
  p += strlen(pattern);
  while (*p == ' ' || *p == '\t') ++p;
  if (*p < '0' || *p > '9') return fallback;
  return static_cast<int32_t>(strtol(p, nullptr, 10));
}

constexpr uint16_t kStageThresholds[] = {
    0,    // Spore
    15,   // Drop (Gocciolino)
    60,   // Glyph (Glifo)
    180,  // Guardian (Custode del Codice)
    450,  // Archon (Arconte Astrale)
};

}  // namespace

void PetEngine::load(Storage& storage, const LocalTime& now) {
  PetSaveData loaded{};
  const int32_t n = storage.readFile(kSavePath, &loaded, sizeof(loaded));
  if (n == static_cast<int32_t>(sizeof(loaded)) && loaded.magic == kPetMagic && loaded.version == kPetVersion) {
    data_ = loaded;
    syncTime(now);
    recomputeStageAndArchetype(false);
    return;
  }

  // Fresh start: initialize defaults and check if CrossPet left a pet state on SD to migrate!
  data_ = PetSaveData{};
  if (now.valid) {
    const int32_t d = dayNumber(now.year, now.month, now.day);
    data_.birthDay = d;
    data_.lastReadDay = d;
    data_.lastEpoch = localEpoch(now);
  }

  char legacyJson[512] = {};
  if (storage.readFile("/.crosspoint/pet/pet.json", legacyJson, sizeof(legacyJson) - 1) > 0 ||
      storage.readFile("/.crosspoint/pet/state.json", legacyJson, sizeof(legacyJson) - 1) > 0 ||
      storage.readFile("/.crosspet/pet.json", legacyJson, sizeof(legacyJson) - 1) > 0) {
    int32_t pages = extractJsonInt(legacyJson, "totalPagesRead", 0);
    if (pages <= 0) pages = extractJsonInt(legacyJson, "pagesRead", 0);
    int32_t streak = extractJsonInt(legacyJson, "currentStreak", 0);
    if (streak <= 0) streak = extractJsonInt(legacyJson, "streak", 1);
    if (pages > 0) {
      data_.totalPagesRead = static_cast<uint32_t>(pages);
      data_.xp = static_cast<uint32_t>(pages * 6);
      data_.level = static_cast<uint8_t>(1 + data_.xp / 80);
      data_.inkCrystals = static_cast<uint16_t>(3 + pages / 8);
    }
    if (streak > 0) {
      data_.streakDays = static_cast<uint16_t>(streak);
      data_.bestStreak = static_cast<uint16_t>(streak);
    }
  }

  recomputeStageAndArchetype(false);
  save(storage);
}

void PetEngine::save(Storage& storage) const {
  storage.writeFile(kSavePath, &data_, sizeof(data_));
}

void PetEngine::syncTime(const LocalTime& now) {
  if (!now.valid) return;

  const int32_t today = dayNumber(now.year, now.month, now.day);
  if (data_.birthDay == 0) data_.birthDay = today;

  // Daily rollover check.
  if (data_.lastReadDay > 0 && today > data_.lastReadDay) {
    const int32_t gap = today - data_.lastReadDay;
    if (gap > 1 && data_.dailyPages == 0) {
      // Missed a full calendar day without reading: streak resets to 1 on next read,
      // but never kills the pet!
      if (gap >= 3) {
        data_.harmony = clamp100(static_cast<int>(data_.harmony) - 8);
      }
    }
    if (today != data_.lastReadDay) {
      data_.dailyPages = 0;
    }
  }

  const uint32_t epoch = localEpoch(now);
  if (data_.lastEpoch > 0 && epoch > data_.lastEpoch) {
    const uint32_t elapsedSec = epoch - data_.lastEpoch;
    const uint32_t elapsedHours = elapsedSec / 3600u;
    if (elapsedHours > 0) {
      // Gentle decay capped at 45 points even after weeks off, so the experience stays welcoming.
      const int decay = static_cast<int>(elapsedHours > 30 ? 30 : elapsedHours);
      data_.satiety = clamp100(static_cast<int>(data_.satiety) - (decay * 3) / 2);
      data_.mood = clamp100(static_cast<int>(data_.mood) - decay);
      data_.lastEpoch = epoch;
    }
  } else if (data_.lastEpoch == 0) {
    data_.lastEpoch = epoch;
  }
}

bool PetEngine::onPageRead(Storage& storage, const LocalTime& now, bool chapterDone, bool bookDone) {
  consumeBannerTick();
  ++sessionPages_;
  ++data_.totalPagesRead;
  ++data_.dailyPages;
  ++data_.pageCrystalAccum;
  lastActionKind_ = 1;
  ++animFrame_;

  if (now.valid) {
    const int32_t today = dayNumber(now.year, now.month, now.day);
    if (data_.lastReadDay == 0) {
      data_.lastReadDay = today;
      data_.streakDays = 1;
    } else if (today == data_.lastReadDay + 1) {
      ++data_.streakDays;
      data_.lastReadDay = today;
    } else if (today > data_.lastReadDay + 1) {
      data_.streakDays = 1;
      data_.lastReadDay = today;
    }
    if (data_.streakDays > data_.bestStreak) data_.bestStreak = data_.streakDays;
    if (now.hour >= 20 || now.hour < 6) ++data_.nightPages;
    data_.lastEpoch = localEpoch(now);
  }

  // Feed ink energy & resonance.
  data_.satiety = clamp100(static_cast<int>(data_.satiety) + (chapterDone ? 10 : 3));
  data_.mood = clamp100(static_cast<int>(data_.mood) + (chapterDone ? 8 : 2));
  data_.harmony = clamp100(static_cast<int>(data_.harmony) + 1);

  bool milestone = false;
  uint32_t gainedXp = 6;
  if (chapterDone) {
    ++data_.chaptersFinished;
    gainedXp += 30;
  }
  if (bookDone) {
    ++data_.booksFinished;
    gainedXp += 120;
    data_.inkCrystals = static_cast<uint16_t>(data_.inkCrystals + 5);
  }

  // Daily goal completion reward.
  if (data_.dailyPages == data_.dailyGoal) {
    gainedXp += 45;
    data_.inkCrystals = static_cast<uint16_t>(data_.inkCrystals + 2);
    data_.harmony = clamp100(static_cast<int>(data_.harmony) + 10);
    lastActionKind_ = 6;
    char msg[56];
    snprintf(msg, sizeof(msg), "OBIETTIVO %u PAG! +2 CRISTALLI", static_cast<unsigned>(data_.dailyGoal));
    setBanner(msg, 5);
    milestone = true;
  }

  // Crystal earned every 8 pages.
  if (data_.pageCrystalAccum >= 8) {
    data_.pageCrystalAccum = 0;
    ++data_.inkCrystals;
    if (!milestone) {
      setBanner("+1 CRISTALLO D'INCHIOSTRO", 2);
      milestone = true;
    }
  }

  // XP & Level-up check.
  data_.xp += gainedXp;
  while (data_.level < 99 && data_.xp >= xpForNextLevel()) {
    ++data_.level;
    char msg[56];
    snprintf(msg, sizeof(msg), "LIVELLO %u RAGGIUNTO!", static_cast<unsigned>(data_.level));
    setBanner(msg, 5);
    milestone = true;
  }

  const uint8_t prevStage = data_.stage;
  recomputeStageAndArchetype(true);
  if (data_.stage != prevStage) {
    milestone = true;
  }

  if (milestone || chapterDone || bookDone || (sessionPages_ % 4) == 0) {
    save(storage);
  }
  return milestone;
}

bool PetEngine::feedInkCrystal(Storage& storage) {
  if (data_.inkCrystals == 0) {
    setBanner("Leggi 8 pagine per ottenere Cristalli!", 3);
    return false;
  }
  --data_.inkCrystals;
  data_.satiety = clamp100(static_cast<int>(data_.satiety) + 28);
  data_.mood = clamp100(static_cast<int>(data_.mood) + 16);
  data_.harmony = clamp100(static_cast<int>(data_.harmony) + 6);
  data_.xp += 15;
  while (data_.level < 99 && data_.xp >= xpForNextLevel()) {
    ++data_.level;
  }
  lastActionKind_ = 2;
  ++animFrame_;
  setBanner("Cristallo assorbito! +28 Inchiostro", 3);
  save(storage);
  return true;
}

void PetEngine::interact(Storage& storage, bool fromImuShake) {
  data_.mood = clamp100(static_cast<int>(data_.mood) + (fromImuShake ? 14 : 10));
  data_.harmony = clamp100(static_cast<int>(data_.harmony) + 4);
  if (fromImuShake) ++data_.imuInteractions;
  lastActionKind_ = fromImuShake ? 4 : 3;
  ++animFrame_;
  recomputeStageAndArchetype(false);
  setBanner(fromImuShake ? "Gesto IMU rilevato! Risonanza +14%" : "Coccola ricevuta! Risonanza +10%", 3);
  save(storage);
}

void PetEngine::toggleNightLamp(Storage& storage) {
  data_.nightLamp = data_.nightLamp ? 0 : 1;
  ++animFrame_;
  setBanner(data_.nightLamp ? "Lampada astrale accesa" : "Lampada astrale spenta", 2);
  save(storage);
}

PetMood PetEngine::moodState(const LocalTime& now) const {
  if (data_.nightLamp || (now.valid && (now.hour >= 23 || now.hour < 6))) {
    return PetMood::Sleepy;
  }
  if (data_.satiety < 20 && data_.mood < 25) return PetMood::Dormant;
  if (data_.satiety < 38) return PetMood::Hungry;
  if (data_.mood >= 78 && data_.satiety >= 65) return PetMood::Radiant;
  return PetMood::Focused;
}

uint32_t PetEngine::xpForNextLevel() const {
  // Smooth RPG curve: Lv1->60, Lv2->140, Lv3->240, Lv4->360...
  const uint32_t lv = data_.level;
  return lv * 50u + lv * lv * 10u;
}

uint8_t PetEngine::levelProgressPct() const {
  const uint32_t prevLv = data_.level > 1 ? (data_.level - 1u) : 0u;
  const uint32_t floorXp = prevLv * 50u + prevLv * prevLv * 10u;
  const uint32_t ceilXp = xpForNextLevel();
  if (ceilXp <= floorXp || data_.xp <= floorXp) return 0;
  const uint32_t span = ceilXp - floorXp;
  const uint32_t cur = data_.xp - floorXp;
  const uint32_t pct = (cur * 100u) / span;
  return static_cast<uint8_t>(pct > 100u ? 100u : pct);
}

uint16_t PetEngine::pagesForNextStage() const {
  if (data_.stage >= static_cast<uint8_t>(PetStage::Archon)) return kStageThresholds[4];
  return kStageThresholds[data_.stage + 1];
}

uint8_t PetEngine::stageProgressPct() const {
  if (data_.stage >= static_cast<uint8_t>(PetStage::Archon)) return 100;
  const uint16_t lo = kStageThresholds[data_.stage];
  const uint16_t hi = kStageThresholds[data_.stage + 1];
  if (data_.totalPagesRead <= lo || hi <= lo) return 0;
  const uint32_t pct = ((data_.totalPagesRead - lo) * 100u) / (hi - lo);
  return static_cast<uint8_t>(pct > 100u ? 100u : pct);
}

uint8_t PetEngine::dailyGoalPct() const {
  if (data_.dailyGoal == 0) return 100;
  const uint32_t pct = (static_cast<uint32_t>(data_.dailyPages) * 100u) / data_.dailyGoal;
  return static_cast<uint8_t>(pct > 100u ? 100u : pct);
}

uint16_t PetEngine::ageDays(const LocalTime& now) const {
  if (!now.valid || data_.birthDay <= 0) return data_.streakDays > 0 ? data_.streakDays : 1;
  const int32_t diff = dayNumber(now.year, now.month, now.day) - data_.birthDay;
  return static_cast<uint16_t>(diff >= 0 ? diff + 1 : 1);
}

const char* PetEngine::petName() const {
  return "Kuro-Ink";
}

const char* PetEngine::stageName() const {
  switch (stage()) {
    case PetStage::Spore:    return "Spora d'Inchiostro";
    case PetStage::Drop:     return "Gocciolino";
    case PetStage::Glyph:    return "Glifo Vivente";
    case PetStage::Guardian: return "Custode del Codice";
    case PetStage::Archon:   return "Arconte Astrale";
  }
  return "Spirito";
}

const char* PetEngine::archetypeName() const {
  switch (archetype()) {
    case PetArchetype::Scholar:  return "Studioso";
    case PetArchetype::Explorer: return "Esploratore";
    case PetArchetype::Dreamer:  return "Sognatore";
  }
  return "Studioso";
}

const char* PetEngine::moodLabel(const LocalTime& now) const {
  switch (moodState(now)) {
    case PetMood::Radiant: return "Risonante";
    case PetMood::Focused: return "Concentrato";
    case PetMood::Hungry:  return "Affamato";
    case PetMood::Sleepy:  return "Assonnato";
    case PetMood::Dormant: return "In Torpore";
  }
  return "Attivo";
}

const char* PetEngine::currentDialogue(const LocalTime& now) const {
  if (bannerTicks_ > 0 && banner_[0]) return banner_;

  switch (lastActionKind_) {
    case 2: return "Quel Cristallo d'Inchiostro era delizioso! Mi sento brillare.";
    case 3: return "Adoro quando passi a trovarmi nel Santuario!";
    case 4: return "Wooo! Ho sentito il movimento del giroscopio, che capriola!";
    case 5: return "Sento le parole trasformarmi... mi sono evoluto!";
    case 6: return "Obiettivo del giorno completato! Siamo una squadra perfetta.";
    default: break;
  }

  const PetMood m = moodState(now);
  if (m == PetMood::Sleepy) {
    return "Le stelle d'inchiostro brillano... leggiamo ancora un po' prima di dormire?";
  }
  if (m == PetMood::Hungry || m == PetMood::Dormant) {
    return "Ho un languorino di storie... apriamo un capitolo per nutrirmi?";
  }

  if (data_.dailyPages == 0) {
    if (now.valid && now.hour < 12) {
      return "Buongiorno! Quale avventura sfogliamo oggi insieme?";
    }
    return "Pronto a sintonizzarmi! Ogni pagina letta mi dona nuova energia.";
  }

  if (data_.dailyPages < data_.dailyGoal) {
    static char buf[88];
    const unsigned rem = data_.dailyGoal - data_.dailyPages;
    snprintf(buf, sizeof(buf), "Ottimo ritmo! Ancora %u %s per il traguardo di oggi.", rem,
             rem == 1 ? "pagina" : "pagine");
    return buf;
  }

  switch (stage()) {
    case PetStage::Spore:
      return "Sento il calore delle prime pagine... presto mi schiuderò!";
    case PetStage::Drop:
      return "Ogni parola che leggi fa crescere le mie piccole antenne d'inchiostro!";
    case PetStage::Glyph:
      return "I glifi danzano attorno a me: la tua costanza mi rende fortissimo.";
    case PetStage::Guardian:
      return "Custodisco ogni libro che hai letto nella mia memoria astrale.";
    case PetStage::Archon:
      return "Abbiamo trasceso la carta elettronica: siamo pura conoscenza!";
  }
  return "Continuiamo a esplorare tra le righe!";
}

void PetEngine::recomputeStageAndArchetype(bool allowBanner) {
  uint8_t newStage = 0;
  for (uint8_t i = 0; i < 5; ++i) {
    if (data_.totalPagesRead >= kStageThresholds[i]) newStage = i;
  }
  if (newStage > data_.stage) {
    data_.stage = newStage;
    if (allowBanner) {
      lastActionKind_ = 5;
      char msg[56];
      snprintf(msg, sizeof(msg), "EVOLUZIONE: %s!", stageName());
      setBanner(msg, 6);
    }
  } else {
    data_.stage = newStage;
  }

  if (data_.imuInteractions >= 3 && data_.imuInteractions * 8u >= data_.totalPagesRead) {
    data_.archetype = static_cast<uint8_t>(PetArchetype::Explorer);
  } else if (data_.nightPages * 2u > data_.totalPagesRead && data_.nightPages >= 5) {
    data_.archetype = static_cast<uint8_t>(PetArchetype::Dreamer);
  } else {
    data_.archetype = static_cast<uint8_t>(PetArchetype::Scholar);
  }
}

void PetEngine::setBanner(const char* text, uint8_t ticks) {
  snprintf(banner_, sizeof(banner_), "%s", text);
  bannerTicks_ = ticks;
}

}  // namespace inkling
