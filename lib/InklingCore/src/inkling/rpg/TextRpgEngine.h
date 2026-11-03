#pragma once

#include <FreeInkUIDisplayTarget.h>
#include <cstdint>

#include "inkling/DeviceStatus.h"
#include "inkling/Platform.h"
#include "inkling/ui/CyberCanvas.h"

namespace inkling {

enum class RpgRoomType : uint8_t {
  Encounter,
  Treasure,
  Shrine,
  Trap,
  Boss
};

struct RpgEnemy {
  const char* name;
  int hp;
  int maxHp;
  int attackPower;
};

class TextRpgEngine {
 public:
  TextRpgEngine();

  void newGame();
  void load(Storage& storage);
  void save(Storage& storage);

  // Input actions
  void moveCursor(int delta);
  void confirmChoice();

  // Reading synergy: reading pages outside grants torches & health
  void grantReadingBonus(int pages);

  void render(freeink::ui::DrawTarget& t, const DeviceStatus& status);

  bool isGameOver() const { return playerHp_ <= 0; }
  int playerHp() const { return playerHp_; }
  int depth() const { return depth_; }

 private:
  void generateRoom();
  void resolveChoice(int choice);

  int playerHp_ = 100;
  int playerMaxHp_ = 100;
  int torches_ = 4;
  int lockpicks_ = 3;
  int gold_ = 15;
  int xp_ = 0;
  int level_ = 1;
  const char* weaponName_ = "Pugnale di Ferro (+4)";
  int weaponBonus_ = 4;

  int depth_ = 1;
  int roomStep_ = 0;
  RpgRoomType roomType_ = RpgRoomType::Encounter;
  RpgEnemy enemy_{"Spettro d'Inchiostro", 35, 35, 10};

  char roomTitle_[64] = "Cripta dei Tomi Dimenticati";
  char roomDesc_[256] = "L'aria odora di pergamena antica e polvere. Uno Spettro d'Inchiostro emerge dal pavimento!";
  char choice0_[80] = "Attacca lo Spettro con il Pugnale";
  char choice1_[80] = "Usa una Torcia per accecare il nemico";
  char choice2_[80] = "Tenta di aggirarlo furtivamente";
  char lastLog_[128] = "Sei sceso nelle profondita' della Cripta. La tua avventura comincia.";

  int selectedChoice_ = 0;
  int choiceCount_ = 3;
  uint32_t rngSeed_ = 42;
};

}  // namespace inkling
