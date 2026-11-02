#pragma once

#include <cstdint>

#include "inkling/Screens.h"

// The updater's Wi-Fi service. It runs in the app slot opposite the reader,
// so the Wi-Fi stack has the heap the reader would otherwise need.
//
// On the card, /.inkling/wifi.txt holds one key=value per line:
//   ssid=...      network name (2.4 GHz: the ESP32-C3 has no 5 GHz radio)
//   password=...  network password
//   token=...     shared secret the Mac sends with every update
//
// HTTP API (port 80, also reachable as http://inkling.local):
//   GET  /info               version, slots, free heap
//   GET  /log                updater log ring
//   GET  /sdlog[?name=...]   log files on the card: boot (reader, default), updater
//   POST /update             raw reader .bin, header "X-Inkling-Token: <token>";
//                            written to the reader's slot, then restarts into it
namespace ota {

struct Config {
  char ssid[64] = {};
  char password[96] = {};
  char token[64] = {};
};

constexpr const char* kConfigPath = "/.inkling/wifi.txt";

bool loadConfig(Config& out);

// Platform hooks for the update screen and the exit key.
struct Hooks {
  void (*show)(const inkling::OtaView& view, void* user);
  bool (*backPressed)(void* user);
  void* user;
};

// Serves updates until the user presses Back, the service idles out, or an
// update lands; all three end by booting the reader.
[[noreturn]] void run(const Config& config, const Hooks& hooks);

}  // namespace ota
