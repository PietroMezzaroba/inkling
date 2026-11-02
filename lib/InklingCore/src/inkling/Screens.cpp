#include "Screens.h"

#include <cstdio>
#include <cstring>

#include "inkling/App.h"
#include "inkling/ui/CyberCanvas.h"

namespace inkling {

using namespace inkling::ui;
using namespace inkling::ui::canvas;

void diagnosticScreen(UiApp::ScreenType& screen, void* user) {
  const auto& s = *static_cast<const DeviceStatus*>(user);
  auto& t = screen.target();
  const int16_t w = kScreenW;

  clearScreen();
  PetEngine dummyPet{};
  drawTopHud(t, s, dummyPet, "SISTEMA");

  // 1. Hardware & Memory Deck Card
  const Rect hwCard{16, 52, static_cast<int16_t>(w - 32), 186};
  drawCard(t, hwCard, 8, 4, false, true);
  drawPill(t, 30, 64, 24, "TELEMETRIA HARDWARE", kFontMicro, true, 10);

  char panelBuf[48];
  snprintf(panelBuf, sizeof(panelBuf), "%s · Display %s", s.deviceName, s.panelName);
  drawText(t, Rect{30, 96, static_cast<int16_t>(w - 60), 28}, panelBuf, kFontTitle, Color::Black);

  char slotBuf[64];
  snprintf(slotBuf, sizeof(slotBuf), "Altro slot OTA: %s",
           (s.otherFirmware && s.otherFirmware[0]) ? s.otherFirmware : "nessuno");
  drawText(t, Rect{30, 126, static_cast<int16_t>(w - 60), 22}, slotBuf, kFontSmall, Color::Black);

  // Heap gauge
  uint8_t heapPct = 68;
  char heapBuf[64];
  if (s.freeHeap > 0) {
    const uint32_t freeKb = s.freeHeap / 1024;
    const uint32_t maxKb = s.largestBlock / 1024;
    heapPct = static_cast<uint8_t>(freeKb > 140 ? 100 : (freeKb * 100) / 140);
    snprintf(heapBuf, sizeof(heapBuf), "SRAM Libera: %lu KB  ( Blocco max: %lu KB )",
             static_cast<unsigned long>(freeKb), static_cast<unsigned long>(maxKb));
  } else {
    snprintf(heapBuf, sizeof(heapBuf), "SRAM Simulatore: Budget X3 attivo (78 KB arene)");
  }
  drawText(t, Rect{30, 156, static_cast<int16_t>(w - 60), 22}, heapBuf, kFontSmall, Color::Black);
  drawSegmentedBar(t, Rect{30, 184, static_cast<int16_t>(w - 60), 18}, heapPct, 12, false);

  // 2. IMU Gyro / Accelerometer Radar Card + Sensor Grid
  const Rect imuCard{16, 252, static_cast<int16_t>(w - 32), 210};
  drawCard(t, imuCard, 8, 4, false, true);
  drawPill(t, 30, 264, 24, "GIROSCOPIO IMU & SENSORI", kFontMicro, true, 10);

  // Radar Crosshair on the left
  const int16_t rcx = 104;
  const int16_t rcy = 368;
  strokeCircle(t, rcx, rcy, 56, 2, Color::Black);
  strokeCircle(t, rcx, rcy, 32, 1, Color::Black);
  t.line(Point{static_cast<int16_t>(rcx - 58), rcy}, Point{static_cast<int16_t>(rcx + 58), rcy}, 1,
         Paint::solid(Color::Black));
  t.line(Point{rcx, static_cast<int16_t>(rcy - 58)}, Point{rcx, static_cast<int16_t>(rcy + 58)}, 1,
         Paint::solid(Color::Black));

  int16_t bx = rcx, by = rcy;
  if (s.imuReady) {
    int dx = static_cast<int>(s.ax * 40.0f);
    int dy = static_cast<int>(-s.ay * 40.0f);
    if (dx < -44) dx = -44;
    if (dx > 44) dx = 44;
    if (dy < -44) dy = -44;
    if (dy > 44) dy = 44;
    bx = static_cast<int16_t>(rcx + dx);
    by = static_cast<int16_t>(rcy + dy);
  }
  fillCircle(t, bx, by, 8, Color::Black);
  fillCircle(t, bx, by, 3, Color::White);

  // Right-side sensor readouts
  const int16_t sx = 186;
  const int16_t sw = static_cast<int16_t>(w - sx - 30);
  char imuTxt[56], rtcTxt[56], sdTxt[48], btnTxt[56];
  if (s.imuReady) {
    snprintf(imuTxt, sizeof(imuTxt), "X: %+.2fg  Y: %+.2fg  Z: %+.2fg", s.ax, s.ay, s.az);
  } else {
    snprintf(imuTxt, sizeof(imuTxt), "Sensore QMI8658: non rilevato");
  }
  if (s.clockValid) {
    snprintf(rtcTxt, sizeof(rtcTxt), "RTC DS3231: %02u:%02u · %02u/%02u/%04u", s.hour, s.minute, s.day, s.month,
             s.year);
  } else {
    snprintf(rtcTxt, sizeof(rtcTxt), "RTC DS3231: ora non impostata");
  }
  snprintf(sdTxt, sizeof(sdTxt), "MicroSD SPI: %s", s.sdReady ? "PRONTA (FAT/exFAT)" : "ASSENTE");
  if (s.lastButton) {
    snprintf(btnTxt, sizeof(btnTxt), "Input ADC: %s (#%lu)", s.lastButton,
             static_cast<unsigned long>(s.buttonPresses));
  } else {
    snprintf(btnTxt, sizeof(btnTxt), "Input ADC: in attesa di pressione");
  }

  drawText(t, Rect{sx, 300, sw, 26}, imuTxt, kFontBody, Color::Black);
  drawText(t, Rect{sx, 336, sw, 24}, rtcTxt, kFontSmall, Color::Black);
  drawText(t, Rect{sx, 368, sw, 24}, sdTxt, kFontSmall, Color::Black);
  drawText(t, Rect{sx, 400, sw, 24}, btnTxt, kFontSmall, Color::Black);

  // 3. Battery & Power Management Card
  const Rect pwrCard{16, 476, static_cast<int16_t>(w - 32), 160};
  drawCard(t, pwrCard, 8, 4, false, false);
  drawPill(t, 30, 488, 24, "ALIMENTAZIONE & STATO", kFontMicro, true, 10);

  char battLine[64];
  if (s.batteryPercent >= 0) {
    snprintf(battLine, sizeof(battLine), "Batteria BQ27220: %d%% %s", s.batteryPercent,
             s.charging ? "(In carica via USB)" : "(Scarica attiva)");
  } else {
    snprintf(battLine, sizeof(battLine), "Batteria BQ27220: telemetria simulata");
  }
  drawText(t, Rect{30, 522, static_cast<int16_t>(w - 60), 26}, battLine, kFontBody, Color::Black);
  drawProgressBar(t, Rect{30, 556, static_cast<int16_t>(w - 60), 18},
                  static_cast<uint8_t>(s.batteryPercent >= 0 ? s.batteryPercent : 85), false);

  if (s.notice && s.notice[0]) {
    drawPill(t, 30, 590, 28, s.notice, kFontSmall, true, 14);
  } else {
    drawText(t, Rect{30, 588, static_cast<int16_t>(w - 60), 26},
             "Tieni premuto BACK per 3 secondi per avviare l'altro slot firmware.", kFontSmall, Color::Black);
  }

  // 4. Bottom escape banner
  const Rect tipCard{16, 650, static_cast<int16_t>(w - 32), 86};
  drawCard(t, tipCard, 8, 0, true, false);
  drawText(t, Rect{30, 660, static_cast<int16_t>(w - 60), 28}, "PREMI OK: AVVIA CYBER-NET WI-FI & DAILY INK",
           kFontBody, Color::White, TextAlign::Center, false);
  drawText(t, Rect{30, 692, static_cast<int16_t>(w - 60), 24},
           "Power 1s: Spegni X3   ·   Back 3s: Cambia Slot OTA", kFontSmall, Color::White, TextAlign::Center, false);

  drawBottomKeyBar(t, "OK", "Cyber-Net Wi-Fi", "BACK", "Torna a Home", "BACK 3s", "Altro Slot");
}

void sleepScreen(UiApp::ScreenType& screen, void* user) {
  auto& t = screen.target();
  const int16_t w = kScreenW;
  const int16_t h = kScreenH;
  const auto* app = static_cast<const InklingApp*>(user);

  clearScreen();

  // Outer poster frame
  const Rect outer{14, 14, static_cast<int16_t>(w - 28), static_cast<int16_t>(h - 28)};
  drawCard(t, outer, 12, 5, false, true);

  // Top Header Strip
  const Rect hdr{28, 28, static_cast<int16_t>(w - 56), 44};
  fastFill(t, hdr, Paint::solid(Color::Black), 8);
  drawText(t, Rect{44, 32, 240, 36}, "INKLING X3 // RIPOSO", kFontBody, Color::White, TextAlign::Left, false);
  drawPill(t, static_cast<int16_t>(w - 168), 38, 24, "ZERO ENERGIA", kFontMicro, false, 10);

  // Starry Night Observatory Card with Sleeping Companion
  const Rect sky{28, 84, static_cast<int16_t>(w - 56), 370};
  fastFill(t, sky, Paint::solid(Color::Black), 10);

  // Crescent Moon & Stars on dark background
  fillCircle(t, static_cast<int16_t>(w - 92), 144, 28, Color::White);
  fillCircle(t, static_cast<int16_t>(w - 80), 134, 24, Color::Black);

  static constexpr int16_t kStars[][2] = {
      {64, 118},  {118, 152}, {176, 110}, {244, 136}, {310, 114},
      {72, 210},  {140, 245}, {360, 210}, {430, 238}, {96, 310},
  };
  for (const auto& st : kStars) {
    fastFill(t, Rect{st[0], st[1], 3, 3}, Paint::solid(Color::White));
  }
  // Constellation lines
  t.line(Point{64, 118}, Point{118, 152}, 1, Paint::solid(Color::White));
  t.line(Point{118, 152}, Point{176, 110}, 1, Paint::solid(Color::White));
  t.line(Point{176, 110}, Point{244, 136}, 1, Paint::solid(Color::White));

  const PetStage stg = app ? app->pet().stage() : PetStage::Glyph;
  drawInklingSprite(t, static_cast<int16_t>(w / 2), 286, 4, stg, PetMood::Sleepy, 0, true);

  drawPill(t, static_cast<int16_t>(w / 2 - 90), 398, 28,
           app ? app->pet().stageName() : "Custode dei Sogni", kFontSmall, false, 16);

  // Reading & Companion Summary Card at the bottom of the poster
  const Rect info{28, 470, static_cast<int16_t>(w - 56), 216};
  drawCard(t, info, 10, 4, false, true);

  if (app) {
    char statsLine[64];
    snprintf(statsLine, sizeof(statsLine), "LV.%u  ·  STREAK %u GIORNI  ·  %lu PAGINE LETTE",
             static_cast<unsigned>(app->pet().state().level),
             static_cast<unsigned>(app->pet().state().streakDays),
             static_cast<unsigned long>(app->pet().state().totalPagesRead));
    drawPill(t, 44, 486, 26, statsLine, kFontMicro, true, 12);

    const int idx = app->activeBookIndex();
    if (idx >= 0 && idx < app->library().count()) {
      const auto& b = app->library().at(idx);
      drawBookCover(t, 44, 526, b.name, b.author, nullptr);
      drawText(t, Rect{124, 526, static_cast<int16_t>(w - 168), 30}, b.name, kFontTitle, Color::Black);
      drawText(t, Rect{124, 558, static_cast<int16_t>(w - 168), 24}, b.author, kFontSmall, Color::Black);
      drawProgressBar(t, Rect{124, 592, static_cast<int16_t>(w - 228), 18}, b.progressPct, false);
      char pctTxt[16];
      snprintf(pctTxt, sizeof(pctTxt), "%u%%", static_cast<unsigned>(b.progressPct));
      drawText(t, Rect{static_cast<int16_t>(w - 98), 588, 54, 24}, pctTxt, kFontBody, Color::Black, TextAlign::Right,
               false);
      drawText(t, Rect{124, 620, static_cast<int16_t>(w - 168), 22},
               "Segnalibro e stato del Pet salvati su microSD.", kFontSmall, Color::Black);
    } else {
      drawText(t, Rect{44, 536, static_cast<int16_t>(w - 88), 32},
               "Sogni d'inchiostro in corso...", kFontTitle, Color::Black, TextAlign::Center, false);
      drawText(t, Rect{44, 576, static_cast<int16_t>(w - 88), 26},
               "Al risveglio riprenderemo da dove avevi lasciato.", kFontSmall, Color::Black, TextAlign::Center,
               false);
    }
  } else {
    drawPill(t, 44, 490, 26, "STATO SALVATO SU MICROSD", kFontMicro, true, 12);
    drawText(t, Rect{44, 538, static_cast<int16_t>(w - 88), 34}, "Zzz... Sogni d'inchiostro", kFontTitle, Color::Black,
             TextAlign::Center, false);
    drawText(t, Rect{44, 580, static_cast<int16_t>(w - 88), 26},
             "Il tuo compagno riposa senza consumare batteria.", kFontSmall, Color::Black, TextAlign::Center, false);
  }

  // Bottom wake prompt
  const Rect wake{28, 704, static_cast<int16_t>(w - 56), 54};
  fastFill(t, wake, Paint::solid(Color::Black), 8);
  drawText(t, wake, "PREMI POWER PER RISVEGLIARE INKLING X3", kFontBody, Color::White, TextAlign::Center, false);
}

void messageScreen(UiApp::ScreenType& screen, void* user) {
  auto& t = screen.target();
  const int16_t w = kScreenW;
  const int16_t h = kScreenH;
  const char* msg = static_cast<const char*>(user);

  clearScreen();
  fillDotGrid(t, Rect{0, 0, w, h}, 8, Color::Black);

  const Rect card{36, static_cast<int16_t>(h / 2 - 140), static_cast<int16_t>(w - 72), 280};
  drawCard(t, card, 12, 6, false, true);

  const Rect hdr{static_cast<int16_t>(card.x + 12), static_cast<int16_t>(card.y + 12),
                 static_cast<int16_t>(card.width - 24), 40};
  fastFill(t, hdr, Paint::solid(Color::Black), 6);
  drawText(t, hdr, "INKLING OS // SISTEMA", kFontBody, Color::White, TextAlign::Center, false);

  drawInklingSprite(t, static_cast<int16_t>(w / 2), static_cast<int16_t>(card.y + 118), 2, PetStage::Drop,
                    PetMood::Focused, 0, false);

  TextStyle st;
  st.font = static_cast<freeink::ui::FontId>(kFontTitle);
  st.color = Color::Black;
  st.align = TextAlign::Center;
  st.maxLines = 3;
  t.text(Rect{static_cast<int16_t>(card.x + 24), static_cast<int16_t>(card.y + 168),
              static_cast<int16_t>(card.width - 48), 92},
         msg ? msg : "...", st);
}

void otaScreen(UiApp::ScreenType& screen, void* user) {
  const auto& v = *static_cast<const OtaView*>(user);
  auto& t = screen.target();
  const int16_t w = kScreenW;
  const int16_t h = kScreenH;

  clearScreen();
  fillDotGrid(t, Rect{0, 0, w, h}, 8, Color::Black);

  const Rect hdr{0, 0, w, 46};
  fastFill(t, hdr, Paint::solid(Color::Black));
  drawIcon16(t, 18, 15, Icon::Wifi, Color::White);
  drawText(t, Rect{44, 6, 440, 34}, "INKLING // CYBER-NET & PORTALE WEB", kFontBody, Color::White);

  const Rect card{20, 66, static_cast<int16_t>(w - 40), 638};
  drawCard(t, card, 12, 6, false, true);

  drawPill(t, 40, 86, 26, "PORTALE WEB + DAILY INK ATTIVO", kFontMicro, true, 12);

  drawText(t, Rect{40, 126, 200, 24}, "Rete Wi-Fi Connessa", kFontSmall, Color::Black);
  drawText(t, Rect{40, 150, static_cast<int16_t>(w - 80), 30}, v.network && v.network[0] ? v.network : "---",
           kFontTitle, Color::Black);

  drawText(t, Rect{40, 194, static_cast<int16_t>(w - 80), 24}, "Apri nel Browser (Telefono / Mac)", kFontSmall,
           Color::Black);
  const Rect ipBox{40, 220, static_cast<int16_t>(w - 80), 50};
  fastFill(t, ipBox, Paint::solid(Color::Black), 8);
  char urlBuf[64];
  if (v.address[0] && v.address[0] != '-') {
    snprintf(urlBuf, sizeof(urlBuf), "http://%s", v.address);
  } else {
    snprintf(urlBuf, sizeof(urlBuf), "Connessione Wi-Fi in corso...");
  }
  drawText(t, ipBox, urlBuf, kFontTitle, Color::White, TextAlign::Center, false);

  drawText(t, Rect{40, 286, 240, 24}, "Stato Cyber-Net", kFontSmall, Color::Black);
  char stateLine[64];
  if (v.progress >= 0) {
    snprintf(stateLine, sizeof(stateLine), "%s (%d%%)", v.state, v.progress);
  } else {
    snprintf(stateLine, sizeof(stateLine), "%s", v.state);
  }
  drawText(t, Rect{40, 310, static_cast<int16_t>(w - 80), 30}, stateLine, kFontTitle, Color::Black);

  if (v.progress >= 0) {
    drawSegmentedBar(t, Rect{40, 348, static_cast<int16_t>(w - 80), 26}, static_cast<uint8_t>(v.progress), 16, false);
  } else {
    drawProgressBar(t, Rect{40, 350, static_cast<int16_t>(w - 80), 18}, 100, false);
  }

  // Feature guide box inside the card
  const Rect guide{40, 388, static_cast<int16_t>(w - 80), 244};
  fastStroke(t, guide, Paint::solid(Color::Black), 2, 8);
  const Rect guideHdr{40, 388, static_cast<int16_t>(w - 80), 34};
  fastFill(t, guideHdr, Paint::solid(Color::Black), 6);
  drawText(t, Rect{54, 391, static_cast<int16_t>(w - 108), 28}, "FUNZIONI WI-FI INTEGRATE", kFontSmall, Color::White);

  drawIcon16(t, 54, 438, Icon::Book, Color::Black);
  drawText(t, Rect{78, 432, static_cast<int16_t>(w - 130), 26}, "1. DAILY INK BRIEFING (Tasto OK)", kFontBody,
           Color::Black);
  drawText(t, Rect{78, 458, static_cast<int16_t>(w - 130), 24},
           "Scarica ANSA Top News + Tech + HackerNews come libro", kFontSmall, Color::Black);

  drawIcon16(t, 54, 502, Icon::Spark, Color::Black);
  drawText(t, Rect{78, 496, static_cast<int16_t>(w - 130), 26}, "2. CYBER-DECK WEB PORTAL (Browser)", kFontBody,
           Color::Black);
  drawText(t, Rect{78, 522, static_cast<int16_t>(w - 130), 24},
           "Carica EPUB/TXT/MD senza fili e invia Note Second Brain", kFontSmall, Color::Black);

  drawIcon16(t, 54, 566, Icon::Compass, Color::Black);
  drawText(t, Rect{78, 560, static_cast<int16_t>(w - 130), 26}, "3. CLOCK SYNC & OTA FIRMWARE", kFontBody,
           Color::Black);
  drawText(t, Rect{78, 586, static_cast<int16_t>(w - 130), 24},
           "Orologio RTC sincronizzato via NTP · Mac: scripts/ota.sh", kFontSmall, Color::Black);

  char verLine[64];
  snprintf(verLine, sizeof(verLine), "Build: %s · http://inkling.local",
           v.version && v.version[0] ? v.version : "dev");
  drawText(t, Rect{40, 646, static_cast<int16_t>(w - 80), 24}, verLine, kFontMicro, Color::Black, TextAlign::Center,
           false);

  drawBottomKeyBar(t, "OK", "Scarica Daily Ink", "BACK", "Torna al Lettore");
}

}  // namespace inkling
