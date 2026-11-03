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
//   GET  /coredump           raw core dump partition of the last crash
//   GET  /file?path=...      any file on the card, read-only
//   POST /update             raw reader .bin, header "X-Inkling-Token: <token>";
//                            written to the reader's slot, then restarts into it
namespace ota {

struct WifiCred {
  char ssid[64] = {};
  char password[96] = {};
};

struct Config {
  static constexpr int kMaxNetworks = 8;
  WifiCred networks[kMaxNetworks] = {};
  int networkCount = 0;
  char token[64] = {};

  const char* activeSsid() const {
    if (activeIdx >= 0 && activeIdx < networkCount) return networks[activeIdx].ssid;
    return (networkCount > 0) ? networks[0].ssid : "";
  }
  const char* activePassword() const {
    if (activeIdx >= 0 && activeIdx < networkCount) return networks[activeIdx].password;
    return (networkCount > 0) ? networks[0].password : "";
  }
  mutable int activeIdx = 0;
};

constexpr const char* kConfigPath = "/.inkling/wifi.txt";

bool loadConfig(Config& out);

// Platform hooks for the update screen and hardware buttons.
struct Hooks {
  void (*show)(const inkling::OtaView& view, void* user);
  bool (*backPressed)(void* user);
  bool (*confirmPressed)(void* user);
  void* user;
};

// Serves Cyber-Net Web Portal, Daily Ink Briefing, wireless book uploads, and
// OTA updates until the user presses Back, the service idles out, or an update
// lands; all end by booting the reader.
[[noreturn]] void run(const Config& config, const Hooks& hooks);

}  // namespace ota
