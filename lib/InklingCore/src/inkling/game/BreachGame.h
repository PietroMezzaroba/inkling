#pragma once

#include <cstdint>

namespace inkling {

class BreachGame {
 public:
  static constexpr int kGridW = 6;
  static constexpr int kGridH = 6;
  static constexpr int kMaxSentinels = 4;

  enum class Node : uint8_t {
    Empty = 0,
    Firewall = 1,
    DataCore = 2,
    Switch = 3,
    Emp = 4,
    Uplink = 5,
  };

  struct Sentinel {
    int8_t x = 0;
    int8_t y = 0;
    uint8_t stunTurns = 0;
    bool active = false;
  };

  BreachGame() { startNewRun(); }

  void startNewRun();
  void startLevel(uint16_t lv);

  // Player actions: returns true if a level was just completed (so caller can reward Pet!).
  bool movePlayer(int dx, int dy);
  bool triggerEmpBlast();

  uint16_t level() const { return level_; }
  uint32_t score() const { return score_; }
  uint8_t iceTrace() const { return iceTrace_; }
  uint8_t empCharges() const { return empCharges_; }
  uint8_t coresLeft() const { return coresLeft_; }
  uint8_t totalCores() const { return totalCores_; }
  bool uplinkOpen() const { return coresLeft_ == 0; }
  bool isLevelCleared() const { return levelCleared_; }
  bool isGameOver() const { return gameOver_; }

  int8_t playerX() const { return px_; }
  int8_t playerY() const { return py_; }
  Node nodeAt(int x, int y) const { return grid_[y][x]; }
  const Sentinel* sentinelAt(int x, int y) const;
  const char* statusLine() const { return statusLine_; }

 private:
  uint32_t nextRand();
  void stepSentinels();
  void checkSentinelCollisions();

  uint32_t rng_ = 0x4E455552u;
  uint16_t level_ = 1;
  uint16_t turn_ = 0;
  uint32_t score_ = 0;
  uint8_t iceTrace_ = 0;
  uint8_t empCharges_ = 1;
  uint8_t coresLeft_ = 0;
  uint8_t totalCores_ = 0;
  int8_t px_ = 0;
  int8_t py_ = 0;
  bool levelCleared_ = false;
  bool gameOver_ = false;
  Node grid_[kGridH][kGridW] = {};
  Sentinel sentinels_[kMaxSentinels] = {};
  char statusLine_[64] = {};
};

}  // namespace inkling
