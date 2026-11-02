#include "BreachGame.h"

#include <cstdio>
#include <cstring>

namespace inkling {

namespace {

int iabs(int v) { return v < 0 ? -v : v; }

}  // namespace

uint32_t BreachGame::nextRand() {
  uint32_t x = rng_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rng_ = x ? x : 0x4E455552u;
  return rng_;
}

void BreachGame::startNewRun() {
  score_ = 0;
  empCharges_ = 1;
  startLevel(1);
}

void BreachGame::startLevel(uint16_t lv) {
  level_ = lv < 1 ? 1 : lv;
  turn_ = 0;
  iceTrace_ = 0;
  levelCleared_ = false;
  gameOver_ = false;
  if (empCharges_ == 0) empCharges_ = 1;

  for (int y = 0; y < kGridH; ++y) {
    for (int x = 0; x < kGridW; ++x) {
      grid_[y][x] = Node::Empty;
    }
  }
  for (int i = 0; i < kMaxSentinels; ++i) {
    sentinels_[i] = Sentinel{};
  }

  // Player starts at bottom-left (0, 5), Uplink Portal at top-right (5, 0)
  px_ = 0;
  py_ = kGridH - 1;
  grid_[0][kGridW - 1] = Node::Uplink;

  auto pickEmpty = [&](int minDistFromPlayer, int& outX, int& outY) {
    for (int tries = 0; tries < 80; ++tries) {
      const int rx = static_cast<int>(nextRand() % kGridW);
      const int ry = static_cast<int>(nextRand() % kGridH);
      if ((rx == px_ && ry == py_) || grid_[ry][rx] != Node::Empty) continue;
      if (sentinelAt(rx, ry) != nullptr) continue;
      if (iabs(rx - px_) + iabs(ry - py_) < minDistFromPlayer) continue;
      outX = rx;
      outY = ry;
      return true;
    }
    return false;
  };

  // Place Data-Cores (3 on lv 1, up to 5 on higher levels)
  totalCores_ = static_cast<uint8_t>(3 + (level_ >= 3 ? 1 : 0) + (level_ >= 5 ? 1 : 0));
  coresLeft_ = 0;
  for (uint8_t i = 0; i < totalCores_; ++i) {
    int cx = 0, cy = 0;
    if (pickEmpty(2, cx, cy)) {
      grid_[cy][cx] = Node::DataCore;
      ++coresLeft_;
    }
  }
  totalCores_ = coresLeft_;

  // Place 1 Firewall Switch and 3..6 Firewall nodes (never blocking player's immediate neighbors)
  int swX = 0, swY = 0;
  if (pickEmpty(2, swX, swY)) {
    grid_[swY][swX] = Node::Switch;
  }

  const int fwCount = 3 + static_cast<int>(level_ % 4);
  for (int i = 0; i < fwCount; ++i) {
    int fx = 0, fy = 0;
    if (pickEmpty(2, fx, fy)) {
      // Keep Uplink neighbors open so portal is never walled in
      if (fx >= kGridW - 2 && fy <= 1) continue;
      grid_[fy][fx] = Node::Firewall;
    }
  }

  // Place 1..2 EMP / Spoof nodes
  const int empCount = (level_ % 2 == 0) ? 2 : 1;
  for (int i = 0; i < empCount; ++i) {
    int ex = 0, ey = 0;
    if (pickEmpty(2, ex, ey)) {
      grid_[ey][ex] = Node::Emp;
    }
  }

  // Spawn ICE Sentinels (1 on lv 1, 2 on lv 2-3, 3 on lv 4-5, 4 on lv 6+)
  int numSentinels = 1 + static_cast<int>(level_ / 2);
  if (numSentinels > kMaxSentinels) numSentinels = kMaxSentinels;
  for (int i = 0; i < numSentinels; ++i) {
    int sx = 0, sy = 0;
    if (pickEmpty(4, sx, sy)) {
      sentinels_[i].x = static_cast<int8_t>(sx);
      sentinels_[i].y = static_cast<int8_t>(sy);
      sentinels_[i].stunTurns = 0;
      sentinels_[i].active = true;
    }
  }

  snprintf(statusLine_, sizeof(statusLine_), "Ruba i %u DATA-CORE ($) e raggiungi UPLINK!",
           static_cast<unsigned>(coresLeft_));
}

const BreachGame::Sentinel* BreachGame::sentinelAt(int x, int y) const {
  for (int i = 0; i < kMaxSentinels; ++i) {
    if (sentinels_[i].active && sentinels_[i].x == x && sentinels_[i].y == y) {
      return &sentinels_[i];
    }
  }
  return nullptr;
}

bool BreachGame::triggerEmpBlast() {
  if (levelCleared_ || gameOver_) {
    if (levelCleared_) {
      startLevel(static_cast<uint16_t>(level_ + 1));
    } else {
      startNewRun();
    }
    return false;
  }
  if (empCharges_ == 0) {
    snprintf(statusLine_, sizeof(statusLine_), "Nessuna carica EMP! Raccogli un nodo [EMP].");
    return false;
  }

  --empCharges_;
  if (iceTrace_ > 25) {
    iceTrace_ = static_cast<uint8_t>(iceTrace_ - 25);
  } else {
    iceTrace_ = 0;
  }

  int stunned = 0;
  for (int i = 0; i < kMaxSentinels; ++i) {
    if (!sentinels_[i].active) continue;
    const int dist = iabs(sentinels_[i].x - px_) + iabs(sentinels_[i].y - py_);
    if (dist <= 2) {
      sentinels_[i].active = false;
      score_ += 150;
      ++stunned;
    } else {
      sentinels_[i].stunTurns = 2;
    }
  }

  // Also clear any adjacent Firewalls
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      const int nx = px_ + dx;
      const int ny = py_ + dy;
      if (nx >= 0 && nx < kGridW && ny >= 0 && ny < kGridH && grid_[ny][nx] == Node::Firewall) {
        grid_[ny][nx] = Node::Empty;
      }
    }
  }

  if (stunned > 0) {
    snprintf(statusLine_, sizeof(statusLine_), "ONDA EMP! %d Sentinelle ICE neutralizzate!", stunned);
  } else {
    snprintf(statusLine_, sizeof(statusLine_), "ONDA EMP! Sentinelle stordite e Trace -25%%!");
  }
  return false;
}

void BreachGame::checkSentinelCollisions() {
  for (int i = 0; i < kMaxSentinels; ++i) {
    if (!sentinels_[i].active) continue;
    if (sentinels_[i].x == px_ && sentinels_[i].y == py_) {
      if (empCharges_ > 0) {
        --empCharges_;
        sentinels_[i].active = false;
        score_ += 120;
        snprintf(statusLine_, sizeof(statusLine_), "Sentinella ICE distrutta col tuo scudo EMP!");
      } else {
        sentinels_[i].active = false;
        const uint8_t nextTrace = static_cast<uint8_t>(iceTrace_ + 40);
        iceTrace_ = nextTrace > 100 ? 100 : nextTrace;
        snprintf(statusLine_, sizeof(statusLine_), "ALLARME! Impatto ICE (+40%% Trace)!");
      }
    }
  }
}

void BreachGame::stepSentinels() {
  for (int i = 0; i < kMaxSentinels; ++i) {
    auto& s = sentinels_[i];
    if (!s.active) continue;
    if (s.stunTurns > 0) {
      --s.stunTurns;
      continue;
    }
    // On Level 1, sentinels advance every 2nd turn so new players can outmaneuver them easily
    if (level_ == 1 && (turn_ & 1) == 1) continue;

    const int dx = px_ - s.x;
    const int dy = py_ - s.y;
    int stepX = 0, stepY = 0;
    if (iabs(dx) >= iabs(dy)) {
      stepX = (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
    } else {
      stepY = (dy > 0) ? 1 : (dy < 0 ? -1 : 0);
    }

    auto canEnter = [&](int tx, int ty) {
      if (tx < 0 || tx >= kGridW || ty < 0 || ty >= kGridH) return false;
      if (grid_[ty][tx] == Node::Firewall || grid_[ty][tx] == Node::Uplink) return false;
      if (sentinelAt(tx, ty) != nullptr) return false;
      return true;
    };

    if (canEnter(s.x + stepX, s.y + stepY)) {
      s.x = static_cast<int8_t>(s.x + stepX);
      s.y = static_cast<int8_t>(s.y + stepY);
    } else {
      // Try alternate axis
      stepX = (stepX == 0 && dx != 0) ? (dx > 0 ? 1 : -1) : 0;
      stepY = (stepY == 0 && dy != 0) ? (dy > 0 ? 1 : -1) : 0;
      if (canEnter(s.x + stepX, s.y + stepY)) {
        s.x = static_cast<int8_t>(s.x + stepX);
        s.y = static_cast<int8_t>(s.y + stepY);
      }
    }
  }
}

bool BreachGame::movePlayer(int dx, int dy) {
  if (levelCleared_ || gameOver_) return false;
  const int nx = px_ + dx;
  const int ny = py_ + dy;
  if (nx < 0 || nx >= kGridW || ny < 0 || ny >= kGridH) return false;

  if (grid_[ny][nx] == Node::Firewall) {
    snprintf(statusLine_, sizeof(statusLine_), "FIREWALL ATTIVO! Usa [SW] o premi OK (EMP)!");
    return false;
  }

  px_ = static_cast<int8_t>(nx);
  py_ = static_cast<int8_t>(ny);
  ++turn_;

  // Passive trace increment per turn (+5% base)
  const uint8_t traceStep = static_cast<uint8_t>(4 + (level_ >= 4 ? 2 : 1));
  iceTrace_ = static_cast<uint8_t>(iceTrace_ + traceStep > 100 ? 100 : iceTrace_ + traceStep);

  const Node cell = grid_[py_][px_];
  if (cell == Node::DataCore) {
    grid_[py_][px_] = Node::Empty;
    if (coresLeft_ > 0) --coresLeft_;
    score_ += 100;
    if (coresLeft_ == 0) {
      snprintf(statusLine_, sizeof(statusLine_), "TUTTI I CORE RUBATI! Corri al nodo [EXIT]!");
    } else {
      snprintf(statusLine_, sizeof(statusLine_), "DATA-CORE acquisito! Mancano %u Core.",
               static_cast<unsigned>(coresLeft_));
    }
  } else if (cell == Node::Switch) {
    grid_[py_][px_] = Node::Empty;
    score_ += 60;
    for (int y = 0; y < kGridH; ++y) {
      for (int x = 0; x < kGridW; ++x) {
        if (grid_[y][x] == Node::Firewall) grid_[y][x] = Node::Empty;
      }
    }
    snprintf(statusLine_, sizeof(statusLine_), "RELAY [SW] HACKERATO! Tutti i Firewall disattivati!");
  } else if (cell == Node::Emp) {
    grid_[py_][px_] = Node::Empty;
    if (empCharges_ < 5) ++empCharges_;
    iceTrace_ = iceTrace_ > 35 ? static_cast<uint8_t>(iceTrace_ - 35) : 0;
    score_ += 50;
    snprintf(statusLine_, sizeof(statusLine_), "SPOOF [EMP] RACCOLTO! Trace -35%% e +1 Carica EMP!");
  } else if (cell == Node::Uplink) {
    if (coresLeft_ == 0) {
      levelCleared_ = true;
      score_ += 250 + static_cast<uint32_t>(100 - iceTrace_) * 2;
      snprintf(statusLine_, sizeof(statusLine_), "BREACH COMPLETATO! +1 Cristallo Pet! Premi OK.");
      return true;
    } else {
      snprintf(statusLine_, sizeof(statusLine_), "UPLINK BLOCCATO! Ruba prima i %u DATA-CORE ($)!",
               static_cast<unsigned>(coresLeft_));
    }
  } else {
    snprintf(statusLine_, sizeof(statusLine_), "Nodo (%d,%d) sicuro · OK/Scuoti: Onda EMP (%u)",
             static_cast<int>(px_ + 1), static_cast<int>(py_ + 1), static_cast<unsigned>(empCharges_));
  }

  checkSentinelCollisions();
  if (!levelCleared_) {
    stepSentinels();
    checkSentinelCollisions();
  }

  if (iceTrace_ >= 100 && !levelCleared_) {
    iceTrace_ = 100;
    gameOver_ = true;
    snprintf(statusLine_, sizeof(statusLine_), "TRACCIAMENTO 100%%! Connessione interrotta. Premi OK.");
  }
  return false;
}

}  // namespace inkling
