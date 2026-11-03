#include "inkling/desk/DeskDashboard.h"

#include <cstdio>
#include <cstring>

namespace inkling {
namespace {

using freeink::ui::Color;
using freeink::ui::Paint;
using freeink::ui::Point;
using freeink::ui::Rect;
using freeink::ui::TextAlign;
using namespace ui::canvas;

int dayOfWeekMondayFirst(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  int w = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
  return (w + 6) % 7;  // 0 = Monday, ..., 6 = Sunday
}

int daysInMonth(int y, int m) {
  if (m == 2) {
    const bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    return leap ? 29 : 28;
  }
  if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
  return 31;
}

const char* monthNameItalian(int m) {
  static const char* const kMonths[] = {
      "GENNAIO", "FEBBRAIO", "MARZO",    "APRILE",   "MAGGIO",   "GIUGNO",
      "LUGLIO",  "AGOSTO",   "SETTEMBRE", "OTTOBRE",  "NOVEMBRE", "DICEMBRE",
  };
  if (m >= 1 && m <= 12) return kMonths[m - 1];
  return "MESE";
}

const char* weekdayNameItalian(int wd) {
  static const char* const kDays[] = {
      "LUNEDÌ", "MARTEDÌ", "MERCOLEDÌ", "GIOVEDÌ", "VENERDÌ", "SABATO", "DOMENICA",
  };
  if (wd >= 0 && wd < 7) return kDays[wd];
  return "GIORNO";
}

}  // namespace

DeskDashboard::DeskDashboard() = default;

void DeskDashboard::drawSplitFlapCard(freeink::ui::DrawTarget& t, int16_t x, int16_t y,
                                     int16_t w, int16_t h, char digit) {
  // 1. Solid black rounded card
  const Rect cardRect{x, y, w, h};
  fastFill(t, cardRect, Paint::solid(Color::Black), 8);
  fastStroke(t, cardRect, Paint::solid(Color::White), 1, 8);

  // 2. Corner rivet details
  fillCircle(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 8), 1, Color::White);
  fillCircle(t, static_cast<int16_t>(x + w - 9), static_cast<int16_t>(y + 8), 1, Color::White);
  fillCircle(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + h - 9), 1, Color::White);
  fillCircle(t, static_cast<int16_t>(x + w - 9), static_cast<int16_t>(y + h - 9), 1, Color::White);

  // 3. Thick geometric vector digit (white strokes on black flap)
  const int16_t padX = w >= 80 ? 20 : 8;
  const int16_t padY = h >= 100 ? 18 : 8;
  const int16_t dx = x + padX;
  const int16_t dy = y + padY;
  const int16_t dw = w - (padX * 2);
  const int16_t dh = h - (padY * 2);
  const int16_t sw = w >= 80 ? 14 : 6;  // stroke width
  const int16_t midY = dy + (dh / 2) - (sw / 2);

  auto drawHBar = [&](int16_t by) {
    fastFill(t, Rect{dx, by, dw, sw}, Paint::solid(Color::White), 2);
  };
  auto drawVLeft = [&](int16_t by, int16_t bh) {
    fastFill(t, Rect{dx, by, sw, bh}, Paint::solid(Color::White), 2);
  };
  auto drawVRight = [&](int16_t by, int16_t bh) {
    fastFill(t, Rect{static_cast<int16_t>(dx + dw - sw), by, sw, bh}, Paint::solid(Color::White), 2);
  };

  const int16_t halfH = dh / 2;

  switch (digit) {
    case '0':
      drawHBar(dy);
      drawHBar(static_cast<int16_t>(dy + dh - sw));
      drawVLeft(dy, dh);
      drawVRight(dy, dh);
      break;
    case '1': {
      drawVRight(dy, dh);
      const int16_t serifW = w >= 80 ? 14 : 6;
      // Top slant serif
      fastFill(t, Rect{static_cast<int16_t>(dx + dw - sw - serifW), dy, serifW, sw}, Paint::solid(Color::White), 2);
      // Bottom foot
      fastFill(t, Rect{static_cast<int16_t>(dx + dw - sw - serifW), static_cast<int16_t>(dy + dh - sw), static_cast<int16_t>(sw + serifW), sw},
               Paint::solid(Color::White), 2);
      break;
    }
    case '2':
      drawHBar(dy);
      drawVRight(dy, halfH);
      drawHBar(midY);
      drawVLeft(midY, halfH);
      drawHBar(static_cast<int16_t>(dy + dh - sw));
      break;
    case '3':
      drawHBar(dy);
      drawHBar(midY);
      drawHBar(static_cast<int16_t>(dy + dh - sw));
      drawVRight(dy, dh);
      break;
    case '4':
      drawVLeft(dy, static_cast<int16_t>(halfH + (sw / 2)));
      drawHBar(midY);
      drawVRight(dy, dh);
      break;
    case '5':
      drawHBar(dy);
      drawVLeft(dy, halfH);
      drawHBar(midY);
      drawVRight(midY, halfH);
      drawHBar(static_cast<int16_t>(dy + dh - sw));
      break;
    case '6':
      drawHBar(dy);
      drawVLeft(dy, dh);
      drawHBar(midY);
      drawVRight(midY, halfH);
      drawHBar(static_cast<int16_t>(dy + dh - sw));
      break;
    case '7':
      drawHBar(dy);
      drawVRight(dy, dh);
      break;
    case '8':
      drawHBar(dy);
      drawHBar(midY);
      drawHBar(static_cast<int16_t>(dy + dh - sw));
      drawVLeft(dy, dh);
      drawVRight(dy, dh);
      break;
    case '9':
      drawHBar(dy);
      drawVLeft(dy, halfH);
      drawHBar(midY);
      drawVRight(dy, dh);
      drawHBar(static_cast<int16_t>(dy + dh - sw));
      break;
    default:
      drawHBar(midY);
      break;
  }

  // 4. Physical horizontal split slit across the card (2px gap with flap bevel)
  const int16_t slitY = static_cast<int16_t>(y + (h / 2));
  fastFill(t, Rect{x, static_cast<int16_t>(slitY - 1), w, 3}, Paint::solid(Color::Black), 0);

  // 5. Flap hinge cutout notches on left & right
  fillCircle(t, static_cast<int16_t>(x + 1), slitY, 3, Color::Black);
  fillCircle(t, static_cast<int16_t>(x + w - 2), slitY, 3, Color::Black);
  strokeCircle(t, static_cast<int16_t>(x + 1), slitY, 4, 1, Color::White);
  strokeCircle(t, static_cast<int16_t>(x + w - 2), slitY, 4, 1, Color::White);
}

void DeskDashboard::drawWeatherIcon(freeink::ui::DrawTarget& t, int16_t x, int16_t y,
                                    WeatherCondition cond, int16_t size) {
  const int16_t cx = static_cast<int16_t>(x + (size / 2));
  const int16_t cy = static_cast<int16_t>(y + (size / 2));

  switch (cond) {
    case WeatherCondition::Sunny: {
      fillCircle(t, cx, cy, 11, Color::Black);
      for (int i = 0; i < 8; ++i) {
        int dx = 0, dy = 0;
        if (i == 0) dy = -18;
        else if (i == 1) { dx = 13; dy = -13; }
        else if (i == 2) dx = 18;
        else if (i == 3) { dx = 13; dy = 13; }
        else if (i == 4) dy = 18;
        else if (i == 5) { dx = -13; dy = 13; }
        else if (i == 6) dx = -18;
        else if (i == 7) { dx = -13; dy = -13; }
        fastFill(t, Rect{static_cast<int16_t>(cx + dx - 1), static_cast<int16_t>(cy + dy - 1), 3, 3},
                 Paint::solid(Color::Black), 0);
      }
      break;
    }
    case WeatherCondition::PartlyCloudy: {
      fillCircle(t, static_cast<int16_t>(cx - 6), static_cast<int16_t>(cy - 8), 8, Color::Black);
      fillCircle(t, static_cast<int16_t>(cx - 6), static_cast<int16_t>(cy + 4), 10, Color::White);
      fillCircle(t, static_cast<int16_t>(cx + 6), static_cast<int16_t>(cy + 2), 12, Color::White);
      fillCircle(t, static_cast<int16_t>(cx + 16), static_cast<int16_t>(cy + 6), 8, Color::White);
      strokeCircle(t, static_cast<int16_t>(cx - 6), static_cast<int16_t>(cy + 4), 10, 2, Color::Black);
      strokeCircle(t, static_cast<int16_t>(cx + 6), static_cast<int16_t>(cy + 2), 12, 2, Color::Black);
      strokeCircle(t, static_cast<int16_t>(cx + 16), static_cast<int16_t>(cy + 6), 8, 2, Color::Black);
      fastFill(t, Rect{static_cast<int16_t>(cx - 8), static_cast<int16_t>(cy + 8), 26, 6},
               Paint::solid(Color::Black), 2);
      break;
    }
    case WeatherCondition::Rain:
    case WeatherCondition::Storm: {
      fillCircle(t, static_cast<int16_t>(cx - 8), static_cast<int16_t>(cy - 2), 9, Color::Black);
      fillCircle(t, static_cast<int16_t>(cx + 4), static_cast<int16_t>(cy - 5), 12, Color::Black);
      fillCircle(t, static_cast<int16_t>(cx + 14), static_cast<int16_t>(cy - 1), 8, Color::Black);
      fastFill(t, Rect{static_cast<int16_t>(cx - 10), static_cast<int16_t>(cy + 2), 26, 6},
               Paint::solid(Color::Black), 0);
      if (cond == WeatherCondition::Storm) {
        fastFill(t, Rect{static_cast<int16_t>(cx - 2), static_cast<int16_t>(cy + 10), 3, 8}, Paint::solid(Color::Black), 0);
        fastFill(t, Rect{static_cast<int16_t>(cx - 5), static_cast<int16_t>(cy + 15), 8, 2}, Paint::solid(Color::Black), 0);
        fastFill(t, Rect{cx, static_cast<int16_t>(cy + 17), 3, 7}, Paint::solid(Color::Black), 0);
      } else {
        for (int i = 0; i < 3; ++i) {
          int16_t rx = static_cast<int16_t>(cx - 8 + (i * 8));
          fastFill(t, Rect{rx, static_cast<int16_t>(cy + 10), 2, 5}, Paint::solid(Color::Black), 1);
          fastFill(t, Rect{static_cast<int16_t>(rx + 3), static_cast<int16_t>(cy + 17), 2, 5},
                   Paint::solid(Color::Black), 1);
        }
      }
      break;
    }
    case WeatherCondition::Snow: {
      fillCircle(t, static_cast<int16_t>(cx - 8), static_cast<int16_t>(cy - 4), 8, Color::Black);
      fillCircle(t, static_cast<int16_t>(cx + 4), static_cast<int16_t>(cy - 6), 11, Color::Black);
      fillCircle(t, static_cast<int16_t>(cx + 14), static_cast<int16_t>(cy - 3), 7, Color::Black);
      fillCircle(t, static_cast<int16_t>(cx - 8), static_cast<int16_t>(cy + 12), 2, Color::Black);
      fillCircle(t, cx, static_cast<int16_t>(cy + 17), 2, Color::Black);
      fillCircle(t, static_cast<int16_t>(cx + 8), static_cast<int16_t>(cy + 12), 2, Color::Black);
      break;
    }
    default: {  // Cloudy / Fog
      fastFill(t, Rect{static_cast<int16_t>(cx - 16), static_cast<int16_t>(cy - 6), 32, 4},
               Paint::solid(Color::Black), 2);
      fastFill(t, Rect{static_cast<int16_t>(cx - 12), cy, 24, 4}, Paint::solid(Color::Black), 2);
      fastFill(t, Rect{static_cast<int16_t>(cx - 18), static_cast<int16_t>(cy + 6), 36, 4},
               Paint::solid(Color::Black), 2);
      break;
    }
  }
}

void DeskDashboard::renderMiniCalendar(freeink::ui::DrawTarget& t, freeink::ui::Rect r, int year, int month, int today) {
  fastStroke(t, r, Paint::solid(Color::Black), 1, 6);
  fastFill(t, Rect{r.x, r.y, r.width, 22}, Paint::solid(Color::Black), 5);
  drawText(t, Rect{static_cast<int16_t>(r.x + 6), static_cast<int16_t>(r.y + 3), static_cast<int16_t>(r.width - 12), 16},
           "CALENDARIO MESE", ui::kFontMicro, Color::White, TextAlign::Left);

  static const char* const kDayHeaders[] = {"L", "M", "M", "G", "V", "S", "D"};
  const int16_t colW = (r.width - 12) / 7;
  const int16_t startX = r.x + 6;
  const int16_t headY = r.y + 26;

  for (int i = 0; i < 7; ++i) {
    drawText(t, Rect{static_cast<int16_t>(startX + i * colW), headY, colW, 14},
             kDayHeaders[i], ui::kFontMicro, Color::Black, TextAlign::Center);
  }

  const int firstWd = dayOfWeekMondayFirst(year, month, 1);
  const int numDays = daysInMonth(year, month);
  int curRow = 0;
  int curCol = firstWd;
  const int16_t rowStartY = headY + 16;
  const int16_t rowH = 15;

  for (int d = 1; d <= numDays; ++d) {
    const int16_t dx = startX + curCol * colW;
    const int16_t dy = rowStartY + curRow * rowH;
    char numStr[4];
    snprintf(numStr, sizeof(numStr), "%d", d);

    if (d == today) {
      fillCircle(t, static_cast<int16_t>(dx + (colW / 2)), static_cast<int16_t>(dy + 7), 7, Color::Black);
      drawText(t, Rect{dx, dy, colW, 14}, numStr, ui::kFontMicro, Color::White, TextAlign::Center);
    } else {
      drawText(t, Rect{dx, dy, colW, 14}, numStr, ui::kFontMicro, Color::Black, TextAlign::Center);
    }

    curCol++;
    if (curCol == 7) {
      curCol = 0;
      curRow++;
    }
  }
}

void DeskDashboard::renderGitHubHeatmap(freeink::ui::DrawTarget& t, freeink::ui::Rect r) {
  fastStroke(t, r, Paint::solid(Color::Black), 1, 6);
  fastFill(t, Rect{r.x, r.y, r.width, 22}, Paint::solid(Color::Black), 5);
  drawText(t, Rect{static_cast<int16_t>(r.x + 8), static_cast<int16_t>(r.y + 3), static_cast<int16_t>(r.width - 16), 16},
           "GITHUB COMMITS (12 WEEKS)", ui::kFontMicro, Color::White, TextAlign::Left);

  const int16_t cellSize = 10;
  const int16_t gap = 4;
  const int16_t startX = r.x + 14;
  const int16_t startY = r.y + 30;

  for (int col = 0; col < 12; ++col) {
    for (int row = 0; row < 7; ++row) {
      const int16_t cx = static_cast<int16_t>(startX + col * (cellSize + gap));
      const int16_t cy = static_cast<int16_t>(startY + row * (cellSize + gap));
      const uint8_t hash = static_cast<uint8_t>((col * 17 + row * 29 + 11) % 100);
      const Rect cell{cx, cy, cellSize, cellSize};

      if (hash < 30) {
        fastStroke(t, cell, Paint::solid(Color::Black), 1, 1);
      } else if (hash < 65) {
        fastStroke(t, cell, Paint::solid(Color::Black), 1, 1);
        fillCircle(t, static_cast<int16_t>(cx + 5), static_cast<int16_t>(cy + 5), 1, Color::Black);
      } else if (hash < 88) {
        fastFill(t, cell, Paint::solid(Color::Black), 1);
        fastFill(t, Rect{static_cast<int16_t>(cx + 2), static_cast<int16_t>(cy + 2), 2, 2}, Paint::solid(Color::White), 0);
        fastFill(t, Rect{static_cast<int16_t>(cx + 6), static_cast<int16_t>(cy + 2), 2, 2}, Paint::solid(Color::White), 0);
        fastFill(t, Rect{static_cast<int16_t>(cx + 4), static_cast<int16_t>(cy + 5), 2, 2}, Paint::solid(Color::White), 0);
      } else {
        fastFill(t, cell, Paint::solid(Color::Black), 1);
      }
    }
  }

  drawText(t, Rect{static_cast<int16_t>(r.x + 14), static_cast<int16_t>(r.y + r.height - 22), static_cast<int16_t>(r.width - 28), 16},
           "184 contributi · Streak attiva: 14gg", ui::kFontMicro, Color::Black, TextAlign::Right);
}

void DeskDashboard::renderMoonPhase(freeink::ui::DrawTarget& t, freeink::ui::Rect r, int year, int month, int day) {
  fastStroke(t, r, Paint::solid(Color::Black), 1, 6);
  fastFill(t, Rect{r.x, r.y, r.width, 22}, Paint::solid(Color::Black), 5);
  drawText(t, Rect{static_cast<int16_t>(r.x + 8), static_cast<int16_t>(r.y + 3), static_cast<int16_t>(r.width - 16), 16},
           "FASE LUNARE ASTRONOMICA", ui::kFontMicro, Color::White, TextAlign::Left);

  int rYr = year % 100;
  rYr %= 19;
  if (rYr > 9) rYr -= 19;
  int k = rYr * 11 % 30;
  int mOffset = (month < 3) ? (month + 2) : month;
  int age = (k + mOffset + day) % 30;

  const int16_t mcx = r.x + 50;
  const int16_t mcy = r.y + 70;
  const int16_t mRad = 26;

  strokeCircle(t, mcx, mcy, mRad, 2, Color::Black);
  fillCircle(t, mcx, mcy, mRad - 1, Color::Black);
  fillCircle(t, static_cast<int16_t>(mcx - 16), mcy, mRad, Color::White);
  strokeCircle(t, mcx, mcy, mRad, 2, Color::Black);

  const char* phaseName = "Gibbosa Crescente";
  if (age < 3) phaseName = "Luna Nuova";
  else if (age < 8) phaseName = "Falce Crescente";
  else if (age < 11) phaseName = "Primo Quarto";
  else if (age < 16) phaseName = "Luna Piena";
  else if (age < 20) phaseName = "Gibbosa Calante";
  else if (age < 24) phaseName = "Ultimo Quarto";
  else phaseName = "Falce Calante";

  const int16_t tx = r.x + 92;
  const int16_t tw = r.width - 100;
  drawText(t, Rect{tx, static_cast<int16_t>(r.y + 44), tw, 22}, phaseName, ui::kFontBody, Color::Black);
  drawText(t, Rect{tx, static_cast<int16_t>(r.y + 70), tw, 18}, "Illuminazione: 84%", ui::kFontSmall, Color::Black);
  drawText(t, Rect{tx, static_cast<int16_t>(r.y + 92), tw, 16}, "Prossima Piena: tra 4 gg", ui::kFontMicro, Color::Black);
}

void DeskDashboard::render(freeink::ui::DrawTarget& t, const DeviceStatus& status, int readingStreak,
                           int todayPages, int goalPages, int totalPages) {
  clearScreen();
  const int16_t w = kScreenW;

  const int hour = status.clockValid ? status.hour : 18;
  const int minute = status.clockValid ? status.minute : 25;
  const int year = status.clockValid ? status.year : 2026;
  const int month = status.clockValid ? status.month : 11;
  const int day = status.clockValid ? status.day : 3;
  const int wd = dayOfWeekMondayFirst(year, month, day);

  // Top Minimal HUD Bar
  drawPill(t, 14, 8, 22, "DESK AMBIENT · XTEINK X3", ui::kFontMicro, true, 10);
  char battPill[32];
  snprintf(battPill, sizeof(battPill), "%u%% %s", static_cast<unsigned>(status.batteryPercent),
           status.charging ? "⚡ CHG" : "BAT");
  drawPill(t, static_cast<int16_t>(w - 88), 8, 22, battPill, ui::kFontMicro, false, 8);

  // 1. GIGANTIC SPLIT-FLAP CLOCK CARDS (y=38..190)
  const int16_t cardW = 104;
  const int16_t cardH = 144;
  const int16_t startX = 14;
  const int16_t flapY = 38;

  char hStr[4], mStr[4];
  snprintf(hStr, sizeof(hStr), "%02d", hour);
  snprintf(mStr, sizeof(mStr), "%02d", minute);

  drawSplitFlapCard(t, startX, flapY, cardW, cardH, hStr[0]);
  drawSplitFlapCard(t, static_cast<int16_t>(startX + cardW + 8), flapY, cardW, cardH, hStr[1]);

  const int16_t colonX = static_cast<int16_t>(startX + (cardW * 2) + 20);
  fillCircle(t, colonX, static_cast<int16_t>(flapY + 48), 5, Color::Black);
  fillCircle(t, colonX, static_cast<int16_t>(flapY + 96), 5, Color::Black);

  const int16_t minStartX = static_cast<int16_t>(colonX + 20);
  drawSplitFlapCard(t, minStartX, flapY, cardW, cardH, mStr[0]);
  drawSplitFlapCard(t, static_cast<int16_t>(minStartX + cardW + 8), flapY, cardW, cardH, mStr[1]);

  // 2. DATE STRIP
  char fullDate[80];
  snprintf(fullDate, sizeof(fullDate), "%s %d %s %d", weekdayNameItalian(wd), day, monthNameItalian(month), year);
  drawText(t, Rect{14, 192, static_cast<int16_t>(w - 28), 24}, fullDate, ui::kFontTitle, Color::Black,
           TextAlign::Center);

  // 3. WEATHER BENTO HERO CARD
  const Rect weatherCard{14, 226, static_cast<int16_t>(w - 28), 126};
  drawCard(t, weatherCard, 10, 3, false, true);

  drawWeatherIcon(t, 28, 252, weather_.condition, 56);

  const int16_t wx = 100;
  const int16_t ww = static_cast<int16_t>(w - wx - 28);
  drawPill(t, wx, 238, 22, "METEO LOCALE (WI-FI ONCE/H)", ui::kFontMicro, true, 8);

  char tempBig[32];
  snprintf(tempBig, sizeof(tempBig), "%d°C", weather_.tempC);
  drawText(t, Rect{wx, 264, 80, 34}, tempBig, ui::kFontHero, Color::Black);

  char minMax[48];
  snprintf(minMax, sizeof(minMax), "Min %d° · Max %d° · Umidità %u%%", weather_.tempMin, weather_.tempMax,
           weather_.humidity);
  drawText(t, Rect{static_cast<int16_t>(wx + 84), 272, static_cast<int16_t>(ww - 84), 22}, minMax,
           ui::kFontSmall, Color::Black);

  drawText(t, Rect{wx, 304, ww, 22}, weather_.conditionText, ui::kFontBody, Color::Black);
  drawText(t, Rect{wx, 326, ww, 18}, weather_.syncTime, ui::kFontMicro, Color::Black);

  // 4. BENTO ROW 1: MINI-CALENDAR & READING STREAK
  const int16_t halfW = static_cast<int16_t>((w - 40) / 2);

  const Rect calRect{14, 362, halfW, 160};
  renderMiniCalendar(t, calRect, year, month, day);

  const Rect streakRect{static_cast<int16_t>(26 + halfW), 362, halfW, 160};
  fastStroke(t, streakRect, Paint::solid(Color::Black), 1, 6);
  fastFill(t, Rect{streakRect.x, streakRect.y, streakRect.width, 22}, Paint::solid(Color::Black), 5);
  drawText(t, Rect{static_cast<int16_t>(streakRect.x + 8), static_cast<int16_t>(streakRect.y + 3),
                   static_cast<int16_t>(streakRect.width - 16), 16},
           "LETTURA & STREAK", ui::kFontMicro, Color::White, TextAlign::Left);

  char streakBig[32];
  snprintf(streakBig, sizeof(streakBig), "%d GIORNI", readingStreak);
  drawText(t, Rect{static_cast<int16_t>(streakRect.x + 12), static_cast<int16_t>(streakRect.y + 30),
                   static_cast<int16_t>(streakRect.width - 24), 30},
           streakBig, ui::kFontHero, Color::Black);

  char pagOggi[48];
  snprintf(pagOggi, sizeof(pagOggi), "Oggi: %d / %d pag", todayPages, goalPages);
  drawText(t, Rect{static_cast<int16_t>(streakRect.x + 12), static_cast<int16_t>(streakRect.y + 64),
                   static_cast<int16_t>(streakRect.width - 24), 22},
           pagOggi, ui::kFontBody, Color::Black);

  const uint8_t goalPct = goalPages > 0 ? (todayPages * 100 / goalPages) : 0;
  drawProgressBar(t, Rect{static_cast<int16_t>(streakRect.x + 12), static_cast<int16_t>(streakRect.y + 92),
                          static_cast<int16_t>(streakRect.width - 24), 14},
                  goalPct > 100 ? 100 : goalPct, false);

  char pagTot[48];
  snprintf(pagTot, sizeof(pagTot), "Totale letto: %d pagine", totalPages);
  drawText(t, Rect{static_cast<int16_t>(streakRect.x + 12), static_cast<int16_t>(streakRect.y + 116),
                   static_cast<int16_t>(streakRect.width - 24), 18},
           pagTot, ui::kFontSmall, Color::Black);

  drawText(t, Rect{static_cast<int16_t>(streakRect.x + 12), static_cast<int16_t>(streakRect.y + 136),
                   static_cast<int16_t>(streakRect.width - 24), 16},
           "Ritmo: Ottimo (+18% mese)", ui::kFontMicro, Color::Black);

  // 5. BENTO ROW 2: CONFIGURABLE SUB-WIDGET
  const Rect subRect{14, 532, static_cast<int16_t>(w - 28), 152};
  if (subWidget_ == DeskSubWidget::GitHubHeatmap) {
    renderGitHubHeatmap(t, subRect);
  } else if (subWidget_ == DeskSubWidget::MoonPhase) {
    renderMoonPhase(t, subRect, year, month, day);
  } else {
    renderGitHubHeatmap(t, subRect);
  }

  // 6. BOTTOM CONTROL STRIP
  drawBottomKeyBar(t, "DESTRA", "Cambia Widget", "OK", "Aggiorna Meteo", "BACK", "Esci");
}

}  // namespace inkling
