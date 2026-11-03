#pragma once

#include <FreeInkUIDisplayTarget.h>
#include <cstdint>

#include "inkling/DeviceStatus.h"
#include "inkling/ui/CyberCanvas.h"

namespace inkling {

enum class OracleDeckType : uint8_t {
  DailyStoic,
  ObliqueStrategies
};

struct OracleCard {
  const char* author;
  const char* source;
  const char* quote;
  const char* takeaway;
};

class OracleDeck {
 public:
  OracleDeck();

  OracleDeckType activeDeck() const { return deckType_; }
  void toggleDeck();
  void nextCard();
  void prevCard();
  void shuffle();

  // Triggered on physical accelerometer shake (IMU)
  void onShake();

  const OracleCard& currentCard() const;
  int currentIndex() const { return currentIdx_; }
  int totalCards() const;

  void render(freeink::ui::DrawTarget& t, const DeviceStatus& status);

 private:
  OracleDeckType deckType_ = OracleDeckType::DailyStoic;
  int currentIdx_ = 0;
  bool justShuffled_ = false;
  uint32_t seed_ = 1337;
};

}  // namespace inkling
