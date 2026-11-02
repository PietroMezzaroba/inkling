#pragma once

#include <cstdint>

#include "inkling/Platform.h"

namespace inkling {

enum class PetStage : uint8_t {
  Spore = 0,     // Spora d'Inchiostro (0..14 pag)
  Drop = 1,      // Gocciolino (15..59 pag)
  Glyph = 2,     // Glifo (60..179 pag)
  Guardian = 3,  // Custode del Codice (180..449 pag)
  Archon = 4,    // Arconte Astrale (450+ pag)
};

enum class PetArchetype : uint8_t {
  Scholar = 0,   // Studioso — lettura costante
  Explorer = 1,  // Esploratore — dinamico e curioso (IMU + ritmo rapido)
  Dreamer = 2,   // Sognatore — lettore serale/notturno
};

enum class PetMood : uint8_t {
  Radiant = 0,   // Risonanza massima / felice
  Focused = 1,   // Concentrato / attento
  Hungry = 2,    // Affamato d'inchiostro
  Sleepy = 3,    // Assonnato / modalità notte
  Dormant = 4,   // Torpore d'inchiostro (inattivo da giorni, mai morto!)
};

// Persistent binary record saved to /.inkling/pet.bin (64 bytes, aligned).
struct PetSaveData {
  uint32_t magic = 0x50544E49;  // "INTP"
  uint16_t version = 2;
  uint8_t stage = 0;
  uint8_t archetype = 0;

  uint8_t satiety = 82;   // 0..100 (Inchiostro)
  uint8_t mood = 86;      // 0..100 (Risonanza)
  uint8_t harmony = 75;   // 0..100 (Sincronia)
  uint8_t level = 1;      // 1..99

  uint32_t xp = 0;
  uint32_t totalPagesRead = 0;
  uint16_t chaptersFinished = 0;
  uint16_t booksFinished = 0;

  uint16_t streakDays = 1;
  uint16_t bestStreak = 1;
  uint16_t dailyPages = 0;
  uint16_t dailyGoal = 15;

  uint16_t inkCrystals = 3;  // Cristalli d'Inchiostro ◈ (spendibili nel Santuario)
  uint16_t pageCrystalAccum = 0;

  int32_t lastReadDay = 0;     // civil dayNumber()
  int32_t birthDay = 0;        // civil dayNumber()
  uint32_t lastEpoch = 0;      // localEpoch() of last update

  uint16_t nightPages = 0;     // pages read between 20:00 and 05:59
  uint16_t imuInteractions = 0;
  uint8_t nightLamp = 0;       // 1 = lampada del santuario accesa
  uint8_t reserved[5] = {};
};

class PetEngine {
 public:
  static constexpr const char* kSavePath = "/.inkling/pet.bin";

  void load(Storage& storage, const LocalTime& now);
  void save(Storage& storage) const;

  // Updates elapsed time decay/rest from the RTC and checks daily rollover.
  void syncTime(const LocalTime& now);

  // Called on every forward page turn while reading a book.
  // Returns true if a milestone banner (level up, crystal, daily goal, evolution) fired.
  bool onPageRead(Storage& storage, const LocalTime& now, bool chapterDone, bool bookDone);

  // User actions in the Sanctuary or via IMU shake.
  bool feedInkCrystal(Storage& storage);
  void interact(Storage& storage, bool fromImuShake);
  void toggleNightLamp(Storage& storage);

  const PetSaveData& state() const { return data_; }
  PetStage stage() const { return static_cast<PetStage>(data_.stage); }
  PetArchetype archetype() const { return static_cast<PetArchetype>(data_.archetype); }
  PetMood moodState(const LocalTime& now) const;

  uint32_t xpForNextLevel() const;
  uint8_t levelProgressPct() const;
  uint16_t pagesForNextStage() const;
  uint8_t stageProgressPct() const;
  uint8_t dailyGoalPct() const;
  uint16_t ageDays(const LocalTime& now) const;

  const char* petName() const;
  const char* stageName() const;
  const char* archetypeName() const;
  const char* moodLabel(const LocalTime& now) const;
  const char* currentDialogue(const LocalTime& now) const;

  // Short toast/event banner (e.g. "★ LIVELLO 3!" or "◈ +1 CRISTALLO"), empty if none active.
  const char* activeBanner() const { return bannerTicks_ > 0 ? banner_ : nullptr; }
  void consumeBannerTick() {
    if (bannerTicks_ > 0) --bannerTicks_;
  }
  uint16_t sessionPages() const { return sessionPages_; }
  void resetSessionPages() { sessionPages_ = 0; }
  uint8_t animFrame() const { return animFrame_; }
  void stepAnim() { ++animFrame_; }

 private:
  void recomputeStageAndArchetype(bool allowBanner);
  void setBanner(const char* text, uint8_t ticks = 4);

  PetSaveData data_{};
  char banner_[56] = {};
  uint8_t bannerTicks_ = 0;
  uint8_t animFrame_ = 0;
  uint8_t lastActionKind_ = 0;  // 0=none, 1=read, 2=fed, 3=petted, 4=imu, 5=evolved, 6=goal
  uint16_t sessionPages_ = 0;
};

}  // namespace inkling
