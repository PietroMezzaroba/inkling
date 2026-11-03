#include "inkling/rpg/TextRpgEngine.h"

#include <cstdio>
#include <cstring>

namespace inkling {
namespace {

using freeink::ui::Color;
using freeink::ui::Paint;
using freeink::ui::Point;
using freeink::ui::Rect;
using freeink::ui::TextAlign;
using freeink::ui::TextStyle;
using namespace ui::canvas;

static const char* const kRoomTitles[] = {
    "Vestibolo delle Candele Spente",
    "Biblioteca degli Scribi Proibiti",
    "Santuario dell'Inchiostro Nero",
    "Cella dell'Alchimista Cieco",
    "Cripta dei Custodi di Pietra",
    "Galleria delle Rivelazioni",
    "La Fossa dei Manoscritti Perduti",
};

static const char* const kEncounterDescs[] = {
    "Un denso fumo d'inchiostro si condensa in una creatura spettrale armata di artigli taglienti.",
    "Un Guardiano Meccanico a molla si risveglia con un cigolio sinistro e ti sbarra la strada.",
    "Un Golem fatto di tomi rilegati in piombo si erge minaccioso tra gli scaffali millenari.",
    "Un Tessitore d'Ombre scende dal soffitto, gli occhi luminescenti fissi sulla tua borsa.",
};

static const char* const kTreasureDescs[] = {
    "Tra due statue decapitate noti un pesante forziere cerchiato di ferro antico e un leggio d'oro.",
    "Una nicchia murata e' crollata, rivelando urne alchemiche, tomi rari e una borsa di monete.",
    "Un altare di marmo nero ospita una cassa chiusa da una serratura runica complessa.",
};

static const char* const kShrineDescs[] = {
    "Una sorgente d'acqua cristallina scorre attraverso una vasca di granito. L'atmosfera e' serena.",
    "Un cerchio di rune curative emette un calore rassicurante. E' possibile riposare e rifocillarsi.",
    "La statua della Dea dei Tomi ti offre protezione in cambio di un'offerta votiva.",
};

}  // namespace

TextRpgEngine::TextRpgEngine() {
  generateRoom();
}

void TextRpgEngine::newGame() {
  playerHp_ = 100;
  playerMaxHp_ = 100;
  torches_ = 4;
  lockpicks_ = 3;
  gold_ = 15;
  xp_ = 0;
  level_ = 1;
  depth_ = 1;
  roomStep_ = 0;
  weaponName_ = "Pugnale di Ferro (+4)";
  weaponBonus_ = 4;
  snprintf(lastLog_, sizeof(lastLog_), "Sei sceso al Piano %d. L'avventura comincia.", depth_);
  generateRoom();
}

void TextRpgEngine::load(Storage& storage) {
  uint8_t buf[32];
  if (storage.readFile("/.inkling/rpg_save.bin", buf, sizeof(buf)) >= 24) {
    playerHp_ = buf[0];
    playerMaxHp_ = buf[1];
    torches_ = buf[2];
    lockpicks_ = buf[3];
    gold_ = buf[4] | (buf[5] << 8);
    xp_ = buf[6] | (buf[7] << 8);
    level_ = buf[8];
    depth_ = buf[9];
    weaponBonus_ = buf[10];
    if (weaponBonus_ > 10) weaponName_ = "Lama del Silenzio (+14)";
    else if (weaponBonus_ > 5) weaponName_ = "Spada Runica (+8)";
    else weaponName_ = "Pugnale di Ferro (+4)";
    generateRoom();
  }
}

void TextRpgEngine::save(Storage& storage) {
  uint8_t buf[32] = {};
  buf[0] = static_cast<uint8_t>(playerHp_ > 0 ? playerHp_ : 0);
  buf[1] = static_cast<uint8_t>(playerMaxHp_);
  buf[2] = static_cast<uint8_t>(torches_);
  buf[3] = static_cast<uint8_t>(lockpicks_);
  buf[4] = static_cast<uint8_t>(gold_ & 0xFF);
  buf[5] = static_cast<uint8_t>((gold_ >> 8) & 0xFF);
  buf[6] = static_cast<uint8_t>(xp_ & 0xFF);
  buf[7] = static_cast<uint8_t>((xp_ >> 8) & 0xFF);
  buf[8] = static_cast<uint8_t>(level_);
  buf[9] = static_cast<uint8_t>(depth_);
  buf[10] = static_cast<uint8_t>(weaponBonus_);
  storage.writeFile("/.inkling/rpg_save.bin", buf, 24);
}

void TextRpgEngine::grantReadingBonus(int pages) {
  if (pages <= 0) return;
  const int torchesAdd = pages / 15;
  const int healAdd = pages * 2;
  torches_ += torchesAdd;
  if (torches_ > 10) torches_ = 10;
  playerHp_ += healAdd;
  if (playerHp_ > playerMaxHp_) playerHp_ = playerMaxHp_;
}

void TextRpgEngine::moveCursor(int delta) {
  if (choiceCount_ <= 0) return;
  selectedChoice_ = (selectedChoice_ + delta + choiceCount_) % choiceCount_;
}

void TextRpgEngine::generateRoom() {
  rngSeed_ = rngSeed_ * 1664525u + 1013904223u;
  const int titleIdx = (rngSeed_ >> 8) % (sizeof(kRoomTitles) / sizeof(kRoomTitles[0]));
  snprintf(roomTitle_, sizeof(roomTitle_), "Piano %d · %s", depth_, kRoomTitles[titleIdx]);

  selectedChoice_ = 0;
  choiceCount_ = 3;

  const int typeRoll = (rngSeed_ >> 16) % 100;
  if (typeRoll < 55) {
    // Encounter
    roomType_ = RpgRoomType::Encounter;
    const int descIdx = (rngSeed_ >> 4) % (sizeof(kEncounterDescs) / sizeof(kEncounterDescs[0]));
    snprintf(roomDesc_, sizeof(roomDesc_), "%s", kEncounterDescs[descIdx]);

    static const char* const kNames[] = {"Spettro d'Inchiostro", "Guardiano Meccanico", "Golem di Tomi", "Tessitore d'Ombre"};
    enemy_.name = kNames[descIdx];
    enemy_.maxHp = 25 + depth_ * 10;
    enemy_.hp = enemy_.maxHp;
    enemy_.attackPower = 8 + depth_ * 3;

    snprintf(choice0_, sizeof(choice0_), "Attacca con %s", weaponName_);
    snprintf(choice1_, sizeof(choice1_), torches_ > 0 ? "Usa una Torcia per accecare il nemico (-1)" : "(Senza torce)");
    snprintf(choice2_, sizeof(choice2_), "Fuga rapida verso le scale (-8 HP)");
  } else if (typeRoll < 85) {
    // Treasure
    roomType_ = RpgRoomType::Treasure;
    const int descIdx = (rngSeed_ >> 4) % (sizeof(kTreasureDescs) / sizeof(kTreasureDescs[0]));
    snprintf(roomDesc_, sizeof(roomDesc_), "%s", kTreasureDescs[descIdx]);

    snprintf(choice0_, sizeof(choice0_), lockpicks_ > 0 ? "Scassina il forziere antico (-1 Grimaldello)" : "Esamina il forziere a mani nude");
    snprintf(choice1_, sizeof(choice1_), "Leggi i tomi sul leggio (+XP)");
    snprintf(choice2_, sizeof(choice2_), "Prosegui verso la porta successiva");
  } else {
    // Shrine
    roomType_ = RpgRoomType::Shrine;
    const int descIdx = (rngSeed_ >> 4) % (sizeof(kShrineDescs) / sizeof(kShrineDescs[0]));
    snprintf(roomDesc_, sizeof(roomDesc_), "%s", kShrineDescs[descIdx]);

    snprintf(choice0_, sizeof(choice0_), "Bevi dalla vasca benedetta (+30 HP)");
    snprintf(choice1_, sizeof(choice1_), gold_ >= 20 ? "Offri 20 Monete per potenziare l'arma" : "Medita in silenzio (+1 Torcia)");
    snprintf(choice2_, sizeof(choice2_), "Riprendi la discesa nei sotterranei");
  }
}

void TextRpgEngine::confirmChoice() {
  if (playerHp_ <= 0) {
    newGame();
    return;
  }
  resolveChoice(selectedChoice_);
}

void TextRpgEngine::resolveChoice(int choice) {
  rngSeed_ = rngSeed_ * 1664525u + 1013904223u;
  const int roll = (rngSeed_ >> 12) % 20 + 1;

  if (roomType_ == RpgRoomType::Encounter) {
    if (choice == 0) {
      // Attack
      const int dmg = 12 + weaponBonus_ + (roll / 3);
      enemy_.hp -= dmg;
      if (enemy_.hp <= 0) {
        // Monster slain!
        const int goldFound = 8 + (roll % 10) + depth_ * 2;
        gold_ += goldFound;
        xp_ += 25;
        snprintf(lastLog_, sizeof(lastLog_), "Colpo vincente! Hai inflitto %d danni. Nemico sconfitto (+%d Monete).", dmg, goldFound);
        roomStep_++;
        if (roomStep_ >= 3) {
          depth_++;
          roomStep_ = 0;
          snprintf(lastLog_, sizeof(lastLog_), "Vittoria! Hai trovato le scale per il Piano %d.", depth_);
        }
        generateRoom();
      } else {
        // Enemy retaliates
        const int enemyDmg = enemy_.attackPower - 2 + (roll % 5);
        playerHp_ -= enemyDmg;
        snprintf(lastLog_, sizeof(lastLog_), "Colpisci (%d dmg). Il nemico risponde con ferocia: perdi %d HP!", dmg, enemyDmg);
      }
    } else if (choice == 1) {
      // Torch flash
      if (torches_ > 0) {
        torches_--;
        enemy_.hp -= (18 + weaponBonus_);
        snprintf(lastLog_, sizeof(lastLog_), "La torcia acceca il nemico! Gli infliggi 18 danni senza subire colpi.");
        if (enemy_.hp <= 0) {
          gold_ += 10;
          xp_ += 25;
          generateRoom();
        }
      } else {
        snprintf(lastLog_, sizeof(lastLog_), "Non hai torce! Il nemico ne approfitta e ti colpisce (-10 HP).");
        playerHp_ -= 10;
      }
    } else {
      // Escape
      playerHp_ -= 8;
      snprintf(lastLog_, sizeof(lastLog_), "Fuggi trafelato nel corridoio buio subendo 8 danni da graffio.");
      generateRoom();
    }
  } else if (roomType_ == RpgRoomType::Treasure) {
    if (choice == 0) {
      if (lockpicks_ > 0) {
        lockpicks_--;
        const int loot = 25 + depth_ * 5;
        gold_ += loot;
        if (roll > 12) {
          weaponBonus_ += 2;
          weaponName_ = "Spada Runica (+8)";
          snprintf(lastLog_, sizeof(lastLog_), "Scassinato! Trovi %d Monete e una nuova Spada Runica (+8)!", loot);
        } else {
          torches_ += 2;
          snprintf(lastLog_, sizeof(lastLog_), "Scassinato! All'interno trovi %d Monete d'oro e 2 Torce.", loot);
        }
      } else {
        playerHp_ -= 12;
        snprintf(lastLog_, sizeof(lastLog_), "Trappola ad aghi! Il forziere scatta e subisci 12 danni.");
      }
    } else if (choice == 1) {
      xp_ += 35;
      snprintf(lastLog_, sizeof(lastLog_), "Decifri i manoscritti antichi (+35 XP). La tua saggezza aumenta.");
    } else {
      snprintf(lastLog_, sizeof(lastLog_), "Avanzi con passo cauto verso il prossimo corridoio.");
    }
    roomStep_++;
    if (roomStep_ >= 3) {
      depth_++;
      roomStep_ = 0;
    }
    generateRoom();
  } else {
    // Shrine
    if (choice == 0) {
      playerHp_ += 30;
      if (playerHp_ > playerMaxHp_) playerHp_ = playerMaxHp_;
      snprintf(lastLog_, sizeof(lastLog_), "L'acqua risana le tue ferite. Recuperi 30 HP!");
    } else if (choice == 1) {
      if (gold_ >= 20) {
        gold_ -= 20;
        weaponBonus_ += 4;
        weaponName_ = "Lama del Silenzio (+14)";
        snprintf(lastLog_, sizeof(lastLog_), "Offerta accettata! La tua lama e' benedetta: Lama del Silenzio (+14)!");
      } else {
        torches_ += 1;
        snprintf(lastLog_, sizeof(lastLog_), "La meditazione nel tempio ti dona pace e trovi 1 Torcia.");
      }
    } else {
      snprintf(lastLog_, sizeof(lastLog_), "Saluti l'altare e prosegui la discesa nella cripta.");
    }
    roomStep_++;
    if (roomStep_ >= 3) {
      depth_++;
      roomStep_ = 0;
    }
    generateRoom();
  }

  // Level up check
  if (xp_ >= level_ * 60) {
    level_++;
    playerMaxHp_ += 20;
    playerHp_ = playerMaxHp_;
    snprintf(lastLog_, sizeof(lastLog_), "LIVELLO SUPERATO! Sei al Livello %d. HP massimi aumentati!", level_);
  }
}

void TextRpgEngine::render(freeink::ui::DrawTarget& t, const DeviceStatus& status) {
  (void)status;
  clearScreen();
  const int16_t w = kScreenW;

  // 1. Top HUD Bar (y=8..34)
  drawPill(t, 14, 8, 22, "LA CRIPTA DEI TOMI // ROGUE", ui::kFontMicro, true, 10);
  char depthBadge[32];
  snprintf(depthBadge, sizeof(depthBadge), "PIANO %d · LV.%d", depth_, level_);
  drawPill(t, static_cast<int16_t>(w - 116), 8, 22, depthBadge, ui::kFontMicro, false, 8);

  // 2. Character Stats Strip (y=38..104)
  const Rect statsCard{14, 38, static_cast<int16_t>(w - 28), 66};
  drawCard(t, statsCard, 8, 3, false, true);

  // HP Gauge
  char hpStr[32];
  snprintf(hpStr, sizeof(hpStr), "HP %d/%d", playerHp_ > 0 ? playerHp_ : 0, playerMaxHp_);
  drawText(t, Rect{26, 46, 140, 18}, hpStr, ui::kFontBody, Color::Black);
  const uint8_t hpPct = playerMaxHp_ > 0 ? (playerHp_ * 100 / playerMaxHp_) : 0;
  drawProgressBar(t, Rect{26, 70, 140, 14}, hpPct > 100 ? 100 : hpPct, false);

  // Resources: Torches, Picks, Gold
  char r1[32], r2[32], r3[32];
  snprintf(r1, sizeof(r1), "Torce: %d", torches_);
  snprintf(r2, sizeof(r2), "Grimaldelli: %d", lockpicks_);
  snprintf(r3, sizeof(r3), "Monete: %d", gold_);

  drawText(t, Rect{186, 46, 120, 18}, r1, ui::kFontSmall, Color::Black);
  drawText(t, Rect{186, 70, 120, 18}, r2, ui::kFontSmall, Color::Black);
  drawText(t, Rect{320, 46, 160, 18}, r3, ui::kFontSmall, Color::Black);
  drawText(t, Rect{320, 70, 160, 18}, weaponName_, ui::kFontMicro, Color::Black);

  // 3. Room Title & Atmospheric Narrative Card (y=112..352)
  const Rect storyCard{14, 112, static_cast<int16_t>(w - 28), 238};
  drawCard(t, storyCard, 10, 4, false, false);

  // Room Title Banner
  fastFill(t, Rect{26, 124, static_cast<int16_t>(w - 52), 30}, Paint::solid(Color::Black), 6);
  drawText(t, Rect{36, 128, static_cast<int16_t>(w - 72), 22}, roomTitle_, ui::kFontBody, Color::White);

  // Narrative Description
  TextStyle descStyle;
  descStyle.font = static_cast<freeink::ui::FontId>(ui::kFontTitle);
  descStyle.color = Color::Black;
  descStyle.align = TextAlign::Left;
  descStyle.maxLines = 5;
  t.text(Rect{28, 170, static_cast<int16_t>(w - 56), 110}, roomDesc_, descStyle);

  // Enemy / Object Badge if encounter
  if (roomType_ == RpgRoomType::Encounter && enemy_.hp > 0) {
    const Rect enemyBox{28, 290, static_cast<int16_t>(w - 56), 46};
    fastStroke(t, enemyBox, Paint::solid(Color::Black), 1, 6);
    drawText(t, Rect{38, 296, 200, 20}, enemy_.name, ui::kFontBody, Color::Black);
    char ehp[32];
    snprintf(ehp, sizeof(ehp), "HP: %d/%d", enemy_.hp, enemy_.maxHp);
    drawText(t, Rect{static_cast<int16_t>(w - 150), 296, 80, 20}, ehp, ui::kFontSmall, Color::Black,
             TextAlign::Right);
    const uint8_t enemyPct = (enemy_.hp * 100) / enemy_.maxHp;
    drawProgressBar(t, Rect{38, 320, static_cast<int16_t>(w - 76), 10}, enemyPct, false);
  } else if (roomType_ == RpgRoomType::Treasure) {
    drawPill(t, 28, 304, 22, "CAMERA DEL TESORO · SCEGLI COME PROCEDERE", ui::kFontMicro, true, 8);
  } else {
    drawPill(t, 28, 304, 22, "SANTUARIO DI PACE · RIPOSO & BENEDIZIONI", ui::kFontMicro, true, 8);
  }

  // 4. Interactive Choices List (y=360..574)
  const char* const choices[] = {choice0_, choice1_, choice2_};
  const int16_t choiceStartY = 360;
  const int16_t choiceH = 66;

  for (int i = 0; i < choiceCount_; ++i) {
    const int16_t cy = choiceStartY + (i * (choiceH + 8));
    const bool sel = (selectedChoice_ == i);
    const Rect cr{14, cy, static_cast<int16_t>(w - 28), choiceH};

    drawCard(t, cr, 8, sel ? 4 : 2, sel, sel);
    const Color fg = sel ? Color::White : Color::Black;

    char prefix[8];
    snprintf(prefix, sizeof(prefix), "[%c]", 'A' + i);
    drawText(t, Rect{28, static_cast<int16_t>(cy + 18), 36, 24}, prefix, ui::kFontBody, fg);

    drawText(t, Rect{68, static_cast<int16_t>(cy + 18), static_cast<int16_t>(w - 110), 30},
             choices[i], ui::kFontBody, fg);

    if (sel) {
      drawPill(t, static_cast<int16_t>(w - 90), static_cast<int16_t>(cy + 20), 22, "CONFERMA", ui::kFontMicro,
               false, 6);
    }
  }

  // 5. Combat / Event Log Strip (y=588..736)
  const Rect logCard{14, 588, static_cast<int16_t>(w - 28), 146};
  drawCard(t, logCard, 10, 3, false, false);

  fastFill(t, Rect{24, 598, static_cast<int16_t>(w - 48), 24}, Paint::solid(Color::Black), 4);
  drawText(t, Rect{32, 602, static_cast<int16_t>(w - 64), 16}, "REGISTRO DEGLI EVENTI", ui::kFontMicro,
           Color::White);

  TextStyle logStyle;
  logStyle.font = static_cast<freeink::ui::FontId>(ui::kFontBody);
  logStyle.color = Color::Black;
  logStyle.align = TextAlign::Left;
  logStyle.maxLines = 3;
  t.text(Rect{26, 634, static_cast<int16_t>(w - 52), 66}, lastLog_, logStyle);

  if (playerHp_ <= 0) {
    drawPill(t, 26, 704, 22, "SEI CADUTO IN BATTAGLIA · PREMI OK PER RICOMINCIARE", ui::kFontMicro, true, 8);
  } else {
    drawText(t, Rect{26, 706, static_cast<int16_t>(w - 52), 16},
             "Consiglio: leggere libri in Inkling OS ricarica le tue Torce e cura i tuoi HP!", ui::kFontMicro,
             Color::Black);
  }

  // Bottom Key Bar
  drawBottomKeyBar(t, "SU/GIÙ", "Scegli", "OK", "Azione", "BACK", "Esci");
}

}  // namespace inkling
