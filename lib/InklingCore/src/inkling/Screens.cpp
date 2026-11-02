#include "Screens.h"

#include <cstdio>

namespace inkling {

namespace {

constexpr uint8_t kDiagRows = 8;

}  // namespace

void diagnosticScreen(UiApp::ScreenType& screen, void* user) {
  const auto& s = *static_cast<const DeviceStatus*>(user);

  // Cell text lives in statics: the table borrows the pointers until the frame
  // is pushed, and the screen function is the only writer.
  static char device[40], clock[32], battery[32], sd[16], imu[48], button[40], other[52], memory[40];

  snprintf(device, sizeof(device), "%s (%s)", s.deviceName, s.panelName);
  if (s.clockValid) {
    snprintf(clock, sizeof(clock), "%02u:%02u  %02u/%02u/%04u", s.hour, s.minute, s.day, s.month, s.year);
  } else {
    snprintf(clock, sizeof(clock), "non impostata");
  }
  if (s.batteryPercent >= 0) {
    snprintf(battery, sizeof(battery), "%d%%%s", s.batteryPercent, s.charging ? " (in carica)" : "");
  } else {
    snprintf(battery, sizeof(battery), "sconosciuta");
  }
  snprintf(sd, sizeof(sd), "%s", s.sdReady ? "OK" : "assente");
  if (s.imuReady) {
    snprintf(imu, sizeof(imu), "x %.2f  y %.2f  z %.2f", s.ax, s.ay, s.az);
  } else {
    snprintf(imu, sizeof(imu), "assente");
  }
  if (s.lastButton) {
    snprintf(button, sizeof(button), "%s (%lu)", s.lastButton, static_cast<unsigned long>(s.buttonPresses));
  } else {
    snprintf(button, sizeof(button), "premi un tasto");
  }
  snprintf(other, sizeof(other), "%s", (s.otherFirmware && s.otherFirmware[0]) ? s.otherFirmware : "nessuno");
  if (s.freeHeap) {
    snprintf(memory, sizeof(memory), "%lu KB (max %lu KB)", static_cast<unsigned long>(s.freeHeap / 1024),
             static_cast<unsigned long>(s.largestBlock / 1024));
  } else {
    snprintf(memory, sizeof(memory), "n/d");
  }

  const char* cells[kDiagRows * 2] = {
      "Dispositivo", device,  //
      "Ora",         clock,   //
      "Batteria",    battery, //
      "Scheda SD",   sd,      //
      "Movimento",   imu,     //
      "Ultimo tasto", button, //
      "Memoria",     memory,  //
      "Altro slot",  other,
  };

  screen.header("Inkling", nullptr, "Diagnostica");
  screen.spacer(16);

  freeink::ui::TableProps table;
  table.cells = cells;
  table.rows = kDiagRows;
  table.cols = 2;
  table.rowHeight = 52;
  table.padding = 8;
  screen.table(table);

  const freeink::ui::FooterAction hints[] = {
      {.label = "Back: libreria", .enabled = false},
      {.label = "Back 3 s: altro slot", .enabled = false},
  };
  screen.footer(hints, 2, freeink::ui::LayoutAnchor::Bottom);

  if (s.notice) screen.centeredText(s.notice);
}

void sleepScreen(UiApp::ScreenType& screen, void* /*user*/) {
  screen.header("Inkling");
  screen.centeredText("Zzz... premi Power per svegliarmi");
}

void messageScreen(UiApp::ScreenType& screen, void* user) {
  screen.header("Inkling");
  screen.centeredText(static_cast<const char*>(user));
}

void otaScreen(UiApp::ScreenType& screen, void* user) {
  const auto& v = *static_cast<const OtaView*>(user);
  static char state[48];
  if (v.progress >= 0) {
    snprintf(state, sizeof(state), "%s %d%%", v.state, v.progress);
  } else {
    snprintf(state, sizeof(state), "%s", v.state);
  }
  const char* cells[] = {
      "Rete",      v.network,  //
      "Indirizzo", v.address,  //
      "Stato",     state,      //
      "Versione",  v.version,
  };
  screen.header("Aggiornamento Wi-Fi");
  screen.spacer(16);
  freeink::ui::TableProps table;
  table.cells = cells;
  table.rows = 4;
  table.cols = 2;
  table.rowHeight = 52;
  table.padding = 8;
  screen.table(table);

  const freeink::ui::FooterAction hints[] = {
      {.label = "Back: esci", .enabled = false},
  };
  screen.footer(hints, 1, freeink::ui::LayoutAnchor::Bottom);
}

}  // namespace inkling
