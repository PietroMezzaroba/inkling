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
  const int16_t w = screenW();
  const bool land = isLandscape();

  clearScreen();
  drawTopHud(t, s, "SISTEMA");

  // Four cards: stacked in portrait, two columns in landscape.
  const int16_t m = 16;
  const int16_t gap = 12;
  const int16_t colW = land ? static_cast<int16_t>((w - 2 * m - gap) / 2) : static_cast<int16_t>(w - 2 * m);
  const int16_t col2 = land ? static_cast<int16_t>(m + colW + gap) : m;
  const int16_t top = 50;
  const Rect hwCard{m, top, colW, 150};
  const Rect imuCard{col2, land ? top : static_cast<int16_t>(hwCard.bottom() + gap), colW, 176};
  const Rect pwrCard{m, static_cast<int16_t>((land ? hwCard.bottom() : imuCard.bottom()) + gap), colW, 132};
  const Rect tipCard{col2, static_cast<int16_t>((land ? imuCard.bottom() : pwrCard.bottom()) + gap), colW, 86};

  // 1. Hardware & memory.
  drawCard(t, hwCard, 8, 4, false, true);
  const int16_t hx = static_cast<int16_t>(hwCard.x + 14);
  const int16_t hw = static_cast<int16_t>(hwCard.width - 28);
  drawPill(t, hx, static_cast<int16_t>(hwCard.y + 12), 24, "HARDWARE E MEMORIA", kFontMicro, true, 10);
  char panelBuf[48];
  snprintf(panelBuf, sizeof(panelBuf), "%s · %s", s.deviceName, s.panelName);
  drawText(t, Rect{hx, static_cast<int16_t>(hwCard.y + 42), hw, 28}, panelBuf, kFontTitle, Color::Black);
  char slotBuf[64];
  snprintf(slotBuf, sizeof(slotBuf), "Altro slot: %s", (s.otherFirmware && s.otherFirmware[0]) ? s.otherFirmware : "vuoto");
  drawText(t, Rect{hx, static_cast<int16_t>(hwCard.y + 72), hw, 22}, slotBuf, kFontSmall, Color::Black);
  char heapBuf[64];
  uint8_t heapPct = 0;
  if (s.freeHeap > 0) {
    const uint32_t freeKb = s.freeHeap / 1024;
    heapPct = static_cast<uint8_t>(freeKb > 140 ? 100 : (freeKb * 100) / 140);
    snprintf(heapBuf, sizeof(heapBuf), "RAM libera %lu KB · blocco max %lu KB", static_cast<unsigned long>(freeKb),
             static_cast<unsigned long>(s.largestBlock / 1024));
  } else {
    snprintf(heapBuf, sizeof(heapBuf), "RAM: non misurata (simulatore)");
  }
  drawText(t, Rect{hx, static_cast<int16_t>(hwCard.y + 96), hw, 22}, heapBuf, kFontSmall, Color::Black);
  drawSegmentedBar(t, Rect{hx, static_cast<int16_t>(hwCard.y + 122), hw, 16}, heapPct, 12, false);

  // 2. Sensors: accelerometer bubble + readouts.
  drawCard(t, imuCard, 8, 4, false, true);
  drawPill(t, static_cast<int16_t>(imuCard.x + 14), static_cast<int16_t>(imuCard.y + 12), 24, "SENSORI", kFontMicro,
           true, 10);
  const int16_t rcx = static_cast<int16_t>(imuCard.x + 70);
  const int16_t rcy = static_cast<int16_t>(imuCard.y + 104);
  const int16_t rr = 50;
  strokeCircle(t, rcx, rcy, rr, 2, Color::Black);
  strokeCircle(t, rcx, rcy, 28, 1, Color::Black);
  fastFill(t, Rect{static_cast<int16_t>(rcx - rr), rcy, static_cast<int16_t>(2 * rr), 1}, Paint::solid(Color::Black));
  fastFill(t, Rect{rcx, static_cast<int16_t>(rcy - rr), 1, static_cast<int16_t>(2 * rr)}, Paint::solid(Color::Black));
  int16_t bx = rcx, by = rcy;
  if (s.imuReady) {
    int dx = static_cast<int>(s.ax * 40.0f);
    int dy = static_cast<int>(-s.ay * 40.0f);
    dx = dx < -40 ? -40 : (dx > 40 ? 40 : dx);
    dy = dy < -40 ? -40 : (dy > 40 ? 40 : dy);
    bx = static_cast<int16_t>(rcx + dx);
    by = static_cast<int16_t>(rcy + dy);
  }
  fillCircle(t, bx, by, 8, Color::Black);
  fillCircle(t, bx, by, 3, Color::White);

  const int16_t sx = static_cast<int16_t>(imuCard.x + 136);
  const int16_t sw = static_cast<int16_t>(imuCard.right() - sx - 12);
  char imuTxt[56], rtcTxt[56], sdTxt[48], btnTxt[56];
  if (s.imuReady) {
    snprintf(imuTxt, sizeof(imuTxt), "X %+.2f  Y %+.2f  Z %+.2f", s.ax, s.ay, s.az);
  } else {
    snprintf(imuTxt, sizeof(imuTxt), "Accelerometro assente");
  }
  if (s.clockValid) {
    snprintf(rtcTxt, sizeof(rtcTxt), "Orologio %02u:%02u · %02u/%02u/%04u", s.hour, s.minute, s.day, s.month, s.year);
  } else {
    snprintf(rtcTxt, sizeof(rtcTxt), "Orologio non impostato");
  }
  snprintf(sdTxt, sizeof(sdTxt), "microSD %s", s.sdReady ? "pronta" : "ASSENTE");
  if (s.lastButton) {
    snprintf(btnTxt, sizeof(btnTxt), "Tasto %s (#%lu)", s.lastButton, static_cast<unsigned long>(s.buttonPresses));
  } else {
    snprintf(btnTxt, sizeof(btnTxt), "Premi un tasto per provarlo");
  }
  drawText(t, Rect{sx, static_cast<int16_t>(imuCard.y + 46), sw, 24}, imuTxt, fitFont(t, imuTxt, sw, kFontBody), Color::Black);
  drawText(t, Rect{sx, static_cast<int16_t>(imuCard.y + 76), sw, 22}, rtcTxt, kFontSmall, Color::Black);
  drawText(t, Rect{sx, static_cast<int16_t>(imuCard.y + 102), sw, 22}, sdTxt, kFontSmall, Color::Black);
  drawText(t, Rect{sx, static_cast<int16_t>(imuCard.y + 128), sw, 22}, btnTxt, kFontSmall, Color::Black);

  // 3. Battery.
  drawCard(t, pwrCard, 8, 4, false, false);
  const int16_t px = static_cast<int16_t>(pwrCard.x + 14);
  const int16_t pw = static_cast<int16_t>(pwrCard.width - 28);
  drawPill(t, px, static_cast<int16_t>(pwrCard.y + 12), 24, "ALIMENTAZIONE", kFontMicro, true, 10);
  char battLine[64];
  if (s.batteryPercent >= 0) {
    snprintf(battLine, sizeof(battLine), "Batteria %d%%%s", s.batteryPercent, s.charging ? " · in carica" : "");
  } else {
    snprintf(battLine, sizeof(battLine), "Batteria: lettura non disponibile");
  }
  drawText(t, Rect{px, static_cast<int16_t>(pwrCard.y + 44), pw, 26}, battLine, kFontBody, Color::Black);
  drawProgressBar(t, Rect{px, static_cast<int16_t>(pwrCard.y + 76), pw, 16},
                  static_cast<uint8_t>(s.batteryPercent >= 0 ? s.batteryPercent : 0), false);
  if (s.notice && s.notice[0]) {
    drawPill(t, px, static_cast<int16_t>(pwrCard.y + 100), 24, s.notice, kFontSmall, true, 12);
  } else {
    drawText(t, Rect{px, static_cast<int16_t>(pwrCard.y + 100), pw, 22}, "Power: riposo · tieni 1 s: spegni",
             kFontSmall, Color::Black);
  }

  // 4. Escape hatches.
  drawCard(t, tipCard, 8, 0, true, false);
  drawText(t, Rect{static_cast<int16_t>(tipCard.x + 12), static_cast<int16_t>(tipCard.y + 10),
                   static_cast<int16_t>(tipCard.width - 24), 30},
           "OK: Wi-Fi e portale web", kFontBody, Color::White, TextAlign::Center, false);
  drawText(t, Rect{static_cast<int16_t>(tipCard.x + 12), static_cast<int16_t>(tipCard.y + 46),
                   static_cast<int16_t>(tipCard.width - 24), 26},
           "BACK tenuto 3 s: avvia l'altro firmware", kFontSmall, Color::White, TextAlign::Center, false);

  drawBottomKeyBar(t, "OK", "Wi-Fi", "BACK", "Home", "BACK 3s", "Altro slot");
}

namespace {

// Draws a bold Swiss / Bauhaus geometric astrolabe & horizon composition when
// no EPUB cover image is available for the lock screen.
void drawArchitecturalWallpaper(DrawTarget& t, Rect box, const DeviceStatus* status) {
  fastFill(t, box, Paint::solid(Color::Black), 10);

  const int16_t cx = static_cast<int16_t>(box.x + box.width / 2);
  const int16_t cy = static_cast<int16_t>(box.y + box.height / 2 - 24);
  const int16_t maxR = static_cast<int16_t>((box.width < box.height ? box.width : box.height) / 2 - 28);

  // Lower horizon band with crisp diagonal / horizontal scanlines
  const int16_t horizonY = static_cast<int16_t>(cy + maxR / 2);
  const Rect lowerBand{static_cast<int16_t>(box.x + 12), horizonY, static_cast<int16_t>(box.width - 24),
                       static_cast<int16_t>(box.bottom() - horizonY - 12)};
  if (lowerBand.height > 20) {
    fastFill(t, lowerBand, Paint::solid(Color::White), 6);
    fillScanlines(t, lowerBand, 4, Color::Black);
    fastStroke(t, lowerBand, Paint::solid(Color::White), 2, 6);
  }

  // Concentric astrolabe rings
  strokeCircle(t, cx, cy, maxR, 2, Color::White);
  if (maxR > 40) strokeCircle(t, cx, cy, static_cast<int16_t>(maxR - 18), 1, Color::White);
  if (maxR > 80) strokeCircle(t, cx, cy, static_cast<int16_t>(maxR - 42), 2, Color::White);

  // Precision crosshairs
  fastFill(t, Rect{static_cast<int16_t>(box.x + 20), cy, static_cast<int16_t>(box.width - 40), 1},
           Paint::solid(Color::White));
  fastFill(t, Rect{cx, static_cast<int16_t>(box.y + 20), 1, static_cast<int16_t>(box.height - 40)},
           Paint::solid(Color::White));

  // Inner geometric eclipse disc
  const int16_t innerR = static_cast<int16_t>(maxR > 90 ? 52 : 34);
  fillCircle(t, cx, cy, innerR, Color::White);
  fillCircle(t, static_cast<int16_t>(cx + innerR / 3), static_cast<int16_t>(cy - innerR / 4),
             static_cast<int16_t>(innerR - 8), Color::Black);

  // Corner degree ticks
  drawPill(t, static_cast<int16_t>(box.x + 18), static_cast<int16_t>(box.y + 16), 22, "528×792 E-INK", kFontMicro,
           false, 8);
  if (status && status->clockValid) {
    char clk[16];
    snprintf(clk, sizeof(clk), "%02u:%02u", static_cast<unsigned>(status->hour), static_cast<unsigned>(status->minute));
    const int16_t cw = static_cast<int16_t>(textWidth(t, clk, kFontMicro) + 18);
    drawPill(t, static_cast<int16_t>(box.right() - 18 - cw), static_cast<int16_t>(box.y + 16), 22, clk, kFontMicro,
             false, 8);
  }

  // Center badge at bottom of artBox
  const int16_t badgeW = static_cast<int16_t>(box.width - 64);
  const Rect plate{static_cast<int16_t>(box.x + 32), static_cast<int16_t>(box.bottom() - 56), badgeW, 38};
  fastFill(t, plate, Paint::solid(Color::Black), 8);
  fastStroke(t, plate, Paint::solid(Color::White), 2, 8);
  drawText(t, plate, "INKLING OS · E-READER", kFontBody, Color::White, TextAlign::Center, false);
}

}  // namespace

void sleepScreen(UiApp::ScreenType& screen, void* user) {
  auto& t = screen.target();
  const int16_t w = screenW();
  const int16_t h = screenH();
  const bool land = isLandscape();
  auto* app = static_cast<InklingApp*>(user);

  clearScreen();
  // "Immagini": a picture from the card fills the panel, nothing over it.
  if (app && app->reader() && app->settings().sleepScreen == SleepScreenMode::Images) {
    char image[128];
    if (app->nextSleepImage(image, sizeof(image)) && app->reader()->drawImageFile(t, image, Rect{0, 0, w, h})) {
      return;
    }
    clearScreen();  // no picture, or one that does not decode: the usual screen
  }
  const bool showCover = !app || app->settings().sleepScreen != SleepScreenMode::Poster;
  const Rect outer{10, 10, static_cast<int16_t>(w - 20), static_cast<int16_t>(h - 20)};
  drawCard(t, outer, 12, 4, false, true);

  // Top status strip inside the lock frame
  const int16_t ix = 24;
  const int16_t iw = static_cast<int16_t>(w - 48);
  drawPill(t, ix, 22, 24, "XTEINK X3 // IN RIPOSO", kFontMicro, true, 10);

  char statusRight[40] = "STANDBY";
  if (app) {
    const auto& st = app->status();
    if (st.clockValid && st.batteryPercent >= 0) {
      snprintf(statusRight, sizeof(statusRight), "%02u:%02u · %d%%", static_cast<unsigned>(st.hour),
               static_cast<unsigned>(st.minute), st.batteryPercent);
    } else if (st.batteryPercent >= 0) {
      snprintf(statusRight, sizeof(statusRight), "BATTERIA %d%%", st.batteryPercent);
    }
  }
  const int16_t srW = static_cast<int16_t>(textWidth(t, statusRight, kFontMicro) + 20);
  drawPill(t, static_cast<int16_t>(ix + iw - srW), 22, 24, statusRight, kFontMicro, false, 10);

  const int idx = app ? app->activeBookIndex() : -1;
  const bool hasBook = (app && idx >= 0 && idx < app->library().count());

  if (!land) {
    // Portrait (528x792): Large Book Cover Poster (480x556) + Sleek Editorial Footer (480x146)
    const Rect artBox{ix, 54, iw, 556};
    fillDotGrid(t, artBox, 6, Color::Black);
    fastStroke(t, artBox, Paint::solid(Color::Black), 2, 10);

    bool drewCover = false;
    if (showCover && hasBook && app->reader()) {
      drewCover = app->reader()->drawCoverInBox(t, app->library().at(idx).path, artBox);
    }
    if (!drewCover) {
      drawArchitecturalWallpaper(t, artBox, app ? &app->status() : nullptr);
    }

    const Rect info{ix, 620, iw, 148};
    drawCard(t, info, 10, 3, false, true);
    const int16_t tx = static_cast<int16_t>(info.x + 16);
    const int16_t tw = static_cast<int16_t>(info.width - 32);

    if (hasBook) {
      const auto& b = app->library().at(idx);
      drawText(t, Rect{tx, static_cast<int16_t>(info.y + 10), tw, 28}, b.name, kFontTitle, Color::Black);
      const char* author = (b.author[0] != '\0') ? b.author : "Volume EPUB su microSD";
      drawText(t, Rect{tx, static_cast<int16_t>(info.y + 40), static_cast<int16_t>(tw - 70), 22}, author, kFontSmall,
               Color::Black);
      char pctTxt[16];
      snprintf(pctTxt, sizeof(pctTxt), "%u%%", static_cast<unsigned>(b.progressPct));
      drawText(t, Rect{static_cast<int16_t>(tx + tw - 64), static_cast<int16_t>(info.y + 38), 64, 24}, pctTxt,
               kFontBody, Color::Black, TextAlign::Right, false);
      drawProgressBar(t, Rect{tx, static_cast<int16_t>(info.y + 68), tw, 14}, b.progressPct, false);
    } else {
      drawText(t, Rect{tx, static_cast<int16_t>(info.y + 18), tw, 28}, "Nessun volume attivo", kFontTitle, Color::Black,
               TextAlign::Center, false);
      drawText(t, Rect{tx, static_cast<int16_t>(info.y + 50), tw, 22}, "Al risveglio tornerai alla schermata Home",
               kFontSmall, Color::Black, TextAlign::Center, false);
    }

    const Rect wake{static_cast<int16_t>(info.x + 12), static_cast<int16_t>(info.bottom() - 48),
                    static_cast<int16_t>(info.width - 24), 36};
    fastFill(t, wake, Paint::solid(Color::Black), 8);
    drawText(t, wake, "PREMI POWER PER RIPRENDERE", kFontSmall, Color::White, TextAlign::Center, false);
  } else {
    // Landscape (792x528): Left Book Cover Showcase (356x448) + Right Editorial Card (376x448)
    const Rect artBox{ix, 54, 356, 448};
    fillDotGrid(t, artBox, 6, Color::Black);
    fastStroke(t, artBox, Paint::solid(Color::Black), 2, 10);

    bool drewCover = false;
    if (showCover && hasBook && app->reader()) {
      drewCover = app->reader()->drawCoverInBox(t, app->library().at(idx).path, artBox);
    }
    if (!drewCover) {
      drawArchitecturalWallpaper(t, artBox, app ? &app->status() : nullptr);
    }

    const int16_t rx = static_cast<int16_t>(artBox.right() + 12);
    const Rect info{rx, 54, static_cast<int16_t>(ix + iw - rx), 448};
    drawCard(t, info, 10, 3, false, true);
    const int16_t tx = static_cast<int16_t>(info.x + 18);
    const int16_t tw = static_cast<int16_t>(info.width - 36);

    if (hasBook) {
      const auto& b = app->library().at(idx);
      drawPill(t, tx, static_cast<int16_t>(info.y + 18), 24, b.started ? "IN LETTURA" : "IN LIBRERIA", kFontMicro,
               true, 10);
      TextStyle ts;
      ts.font = static_cast<freeink::ui::FontId>(kFontTitle);
      ts.color = Color::Black;
      ts.maxLines = 3;
      t.text(Rect{tx, static_cast<int16_t>(info.y + 56), tw, 92}, b.name, ts);
      drawText(t, Rect{tx, static_cast<int16_t>(info.y + 154), tw, 24}, b.author[0] ? b.author : " ", kFontBody,
               Color::Black);

      fastFill(t, Rect{tx, static_cast<int16_t>(info.y + 194), tw, 2}, Paint::solid(Color::Black));

      char progLbl[40];
      snprintf(progLbl, sizeof(progLbl), "AVANZAMENTO LETTURA · %u%%", static_cast<unsigned>(b.progressPct));
      drawText(t, Rect{tx, static_cast<int16_t>(info.y + 212), tw, 22}, progLbl, kFontMicro, Color::Black);
      drawProgressBar(t, Rect{tx, static_cast<int16_t>(info.y + 240), tw, 18}, b.progressPct, false);
      drawText(t, Rect{tx, static_cast<int16_t>(info.y + 276), tw, 22}, "Segnalibro sincronizzato su microSD",
               kFontSmall, Color::Black);
    } else {
      drawText(t, Rect{tx, static_cast<int16_t>(info.y + 120), tw, 32}, "INKLING X3", kFontTitle, Color::Black,
               TextAlign::Center, false);
      drawText(t, Rect{tx, static_cast<int16_t>(info.y + 164), tw, 24}, "Dispositivo in modalità riposo", kFontSmall,
               Color::Black, TextAlign::Center, false);
    }

    const Rect wake{static_cast<int16_t>(info.x + 14), static_cast<int16_t>(info.bottom() - 54),
                    static_cast<int16_t>(info.width - 28), 40};
    fastFill(t, wake, Paint::solid(Color::Black), 8);
    drawText(t, wake, "PREMI POWER PER RIPRENDERE", kFontSmall, Color::White, TextAlign::Center, false);
  }
}

void messageScreen(UiApp::ScreenType& screen, void* user) {
  auto& t = screen.target();
  const int16_t w = screenW();
  const int16_t h = screenH();
  const char* msg = static_cast<const char*>(user);

  clearScreen();
  fillDotGrid(t, Rect{0, 0, w, h}, 8, Color::Black);

  const Rect card{36, static_cast<int16_t>(h / 2 - 120), static_cast<int16_t>(w - 72), 240};
  drawCard(t, card, 12, 6, false, true);

  const Rect hdr{static_cast<int16_t>(card.x + 12), static_cast<int16_t>(card.y + 12),
                 static_cast<int16_t>(card.width - 24), 40};
  fastFill(t, hdr, Paint::solid(Color::Black), 6);
  drawText(t, hdr, "INKLING OS // SISTEMA", kFontBody, Color::White, TextAlign::Center, false);

  const int16_t cx = static_cast<int16_t>(w / 2);
  const int16_t cy = static_cast<int16_t>(card.y + 92);
  fillCircle(t, cx, cy, 20, Color::Black);
  drawIcon16(t, static_cast<int16_t>(cx - 8), static_cast<int16_t>(cy - 8), Icon::Book, Color::White);

  TextStyle st;
  st.font = static_cast<freeink::ui::FontId>(kFontTitle);
  st.color = Color::Black;
  st.align = TextAlign::Center;
  st.maxLines = 3;
  t.text(Rect{static_cast<int16_t>(card.x + 24), static_cast<int16_t>(card.y + 126),
              static_cast<int16_t>(card.width - 48), 92},
         msg ? msg : "...", st);
}

void otaScreen(UiApp::ScreenType& screen, void* user) {
  const auto& v = *static_cast<const OtaView*>(user);
  auto& t = screen.target();
  const int16_t w = screenW();
  const int16_t h = screenH();

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
  drawText(t, Rect{54, 391, static_cast<int16_t>(w - 108), 28}, "GUIDA RAPIDA WI-FI", kFontSmall, Color::White);

  drawIcon16(t, 54, 438, Icon::Book, Color::Black);
  drawText(t, Rect{78, 432, static_cast<int16_t>(w - 130), 26}, "1. SCARICA GIORNALE (Premi OK sull'X3)", kFontBody,
           Color::Black);
  drawText(t, Rect{78, 458, static_cast<int16_t>(w - 130), 24},
           "Crea il libro quotidiano ANSA Italia + Mondo + Tecnologia", kFontSmall, Color::Black);

  drawIcon16(t, 54, 502, Icon::Spark, Color::Black);
  drawText(t, Rect{78, 496, static_cast<int16_t>(w - 130), 26}, "2. CARICA LIBRI SENZA FILI (Da Telefono/Mac)",
           kFontBody, Color::Black);
  drawText(t, Rect{78, 522, static_cast<int16_t>(w - 130), 24},
           "Apri l'indirizzo http:// qui sopra per inviare EPUB o Note", kFontSmall, Color::Black);

  drawIcon16(t, 54, 566, Icon::Compass, Color::Black);
  drawText(t, Rect{78, 560, static_cast<int16_t>(w - 130), 26}, "3. AGGIORNAMENTO OTA SENZA FILI", kFontBody,
           Color::Black);
  drawText(t, Rect{78, 586, static_cast<int16_t>(w - 130), 24},
           "Invia il firmware .bin dal Mac con scripts/ota.sh o dal browser", kFontSmall, Color::Black);

  char verLine[64];
  snprintf(verLine, sizeof(verLine), "Build: %s · http://inkling.local",
           v.version && v.version[0] ? v.version : "dev");
  drawText(t, Rect{40, 646, static_cast<int16_t>(w - 80), 24}, verLine, kFontMicro, Color::Black, TextAlign::Center,
           false);

  drawBottomKeyBar(t, "OK", "Scarica Daily Ink", "BACK", "Torna al Lettore");
}

}  // namespace inkling
