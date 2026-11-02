#pragma once

#include <cstdint>

#include "inkling/Screens.h"

// Wi-Fi update mode: a dedicated boot where the reader is not loaded, so the
// Wi-Fi stack gets the RAM. Entered from the library, left with Back (or after
// a successful update, which restarts into the new firmware).
//
// On the card, /.inkling/wifi.txt holds one key=value per line:
//   ssid=...      network name
//   password=...  network password
//   token=...     shared secret the Mac sends with every update
//
// HTTP API (port 80, also reachable as http://inkling.local):
//   GET  /info    version, slots, free heap
//   GET  /log     device log ring (survives restarts)
//   POST /update  raw firmware .bin, header "X-Inkling-Token: <token>"
namespace ota {

struct Config {
  char ssid[64] = {};
  char password[96] = {};
  char token[64] = {};
};

constexpr const char* kConfigPath = "/.inkling/wifi.txt";

bool loadConfig(Config& out);

// Restarts into update mode.
[[noreturn]] void restartIntoUpdateMode();
// True exactly once after restartIntoUpdateMode().
bool consumeUpdateModeRequest();

// Platform hooks for the update screen and the exit key.
struct Hooks {
  void (*show)(const inkling::OtaView& view, void* user);
  bool (*backPressed)(void* user);
  void* user;
};

// Runs update mode until the user leaves or an update completes; both end in
// a restart.
[[noreturn]] void run(const Config& config, const Hooks& hooks);

}  // namespace ota
