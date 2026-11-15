#include "inkling/desk/DeskDashboard.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "inkling/Platform.h"

namespace inkling {
namespace {

using freeink::ui::Color;
using freeink::ui::Paint;
using freeink::ui::Point;
using freeink::ui::Rect;
using freeink::ui::TextAlign;
using namespace ui::canvas;

struct DailyQuote {
  const char* author;
  const char* quote;
};

static const DailyQuote kStoicQuotes[] = {
    {"MARCO AURELIO", "Hai potere sulla tua mente, non sugli eventi esterni. Comprendi questo con chiarezza, e troverai la forza."},
    {"SENECA", "Non è perché le cose sono difficili che non osiamo, è perché non osiamo che sono difficili."},
    {"EPITTETO", "Ciò che turba gli uomini non sono le cose in sé, ma i giudizi che essi formulano sulle cose."},
    {"ITALO CALVINO", "Prendete la vita con leggerezza, che leggerezza non è superficialità, ma planare sulle cose dall'alto."},
    {"JORGE LUIS BORGES", "Ho sempre immaginato che il Paradiso fosse una specie di biblioteca infinita."},
    {"NIETZSCHE", "Chi ha un perché abbastanza forte per vivere, può sopportare quasi qualsiasi come."},
    {"SENECA", "Mentre perdiamo il nostro tempo tra indugi, dubbi e rinvii, la vita se ne va inesorabile."},
    {"SCHOPENHAUER", "La solitudine è la sorte di tutti gli spiriti eccellenti."},
};

static const DailyQuote kObliqueQuotes[] = {
    {"BRIAN ENO", "Onora il tuo errore come un'intenzione nascosta."},
    {"BRIAN ENO", "Usa una vecchia idea con una forma nuova."},
    {"BRIAN ENO", "Cosa farebbe il tuo amico più caro al tuo posto?"},
    {"BRIAN ENO", "Togli gli elementi ambigui e convertili in specifici."},
    {"BRIAN ENO", "Fai una pausa improvvisa, osserva il silenzio e riparti."},
    {"BRIAN ENO", "Non costruire un muro; costruisci un mattone alla volta."},
    {"BRIAN ENO", "Guarda con estrema attenzione il dettaglio più imbarazzante e amplificalo."},
    {"BRIAN ENO", "Cambia strumento e ripeti dall'inizio con mano leggera."},
};

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

void DeskDashboard::renderQuote(freeink::ui::DrawTarget& t, freeink::ui::Rect r, bool oblique, int32_t day) {
  fastStroke(t, r, Paint::solid(Color::Black), 1, 6);
  fastFill(t, Rect{r.x, r.y, r.width, 22}, Paint::solid(Color::Black), 5);
  drawText(t, Rect{static_cast<int16_t>(r.x + 8), static_cast<int16_t>(r.y + 3), static_cast<int16_t>(r.width - 16), 16},
           oblique ? "STRATEGIA OBLIQUA DEL GIORNO" : "PENSIERO DEL GIORNO", ui::kFontMicro, Color::White,
           TextAlign::Left);
  const auto* list = oblique ? kObliqueQuotes : kStoicQuotes;
  const int count = static_cast<int>(oblique ? (sizeof(kObliqueQuotes) / sizeof(kObliqueQuotes[0]))
                                             : (sizeof(kStoicQuotes) / sizeof(kStoicQuotes[0])));
  const int idx = ((day % count) + count) % count;
  const auto& card = list[idx];
  freeink::ui::TextStyle qs;
  qs.font = static_cast<freeink::ui::FontId>(ui::kFontBody);
  qs.color = Color::Black;
  qs.align = TextAlign::Left;
  qs.maxLines = 4;
  t.text(Rect{static_cast<int16_t>(r.x + 14), static_cast<int16_t>(r.y + 30), static_cast<int16_t>(r.width - 28),
              static_cast<int16_t>(r.height - 60)},
         card.quote, qs);
  drawText(t, Rect{static_cast<int16_t>(r.x + 14), static_cast<int16_t>(r.bottom() - 26),
                   static_cast<int16_t>(r.width - 28), 20},
           card.author, ui::kFontSmall, Color::Black, TextAlign::Right);
}

void DeskDashboard::renderMoonPhase(freeink::ui::DrawTarget& t, freeink::ui::Rect r, int year, int month, int day) {
  fastStroke(t, r, Paint::solid(Color::Black), 1, 6);
  fastFill(t, Rect{r.x, r.y, r.width, 22}, Paint::solid(Color::Black), 5);
  drawText(t, Rect{static_cast<int16_t>(r.x + 8), static_cast<int16_t>(r.y + 3), static_cast<int16_t>(r.width - 16), 16},
           "FASE LUNARE", ui::kFontMicro, Color::White, TextAlign::Left);

  // Moon age from a reference new moon (2000-01-06 18:14 UTC), mean synodic
  // month: good to well under a day, plenty for a desk clock.
  constexpr double kSynodic = 29.530588853;
  const double days = static_cast<double>(dayNumber(static_cast<uint16_t>(year), static_cast<uint8_t>(month),
                                                    static_cast<uint8_t>(day))) +
                      0.5 - (10957.0 + 5.0 + 18.23 / 24.0);
  double age = std::fmod(days, kSynodic);
  if (age < 0) age += kSynodic;
  const double theta = 2.0 * 3.14159265358979 * age / kSynodic;
  const int illum = static_cast<int>((1.0 - std::cos(theta)) * 50.0 + 0.5);
  const bool waxing = age < kSynodic / 2.0;

  // Disc: dark outline, lit part filled row by row up to the terminator.
  const int16_t mRad = 30;
  const int16_t mcx = static_cast<int16_t>(r.x + 20 + mRad);
  const int16_t mcy = static_cast<int16_t>(r.y + 22 + (r.height - 22) / 2);
  fillCircle(t, mcx, mcy, mRad, Color::Black);
  const double c = std::cos(theta);
  for (int16_t dy = static_cast<int16_t>(-mRad + 2); dy <= mRad - 2; ++dy) {
    const double hw = std::sqrt(static_cast<double>((mRad - 2) * (mRad - 2) - dy * dy));
    double x0, x1;
    if (waxing) {
      x0 = hw * c;
      x1 = hw;
    } else {
      x0 = -hw;
      x1 = -hw * c;
    }
    const int16_t a = static_cast<int16_t>(mcx + x0);
    const int16_t b = static_cast<int16_t>(mcx + x1);
    if (b > a) fastFill(t, Rect{a, static_cast<int16_t>(mcy + dy), static_cast<int16_t>(b - a), 1},
                        Paint::solid(Color::White));
  }
  strokeCircle(t, mcx, mcy, mRad, 2, Color::Black);

  const char* phaseName;
  if (age < 1.85) phaseName = "Luna nuova";
  else if (age < 5.54) phaseName = "Falce crescente";
  else if (age < 9.23) phaseName = "Primo quarto";
  else if (age < 12.92) phaseName = "Gibbosa crescente";
  else if (age < 16.61) phaseName = "Luna piena";
  else if (age < 20.30) phaseName = "Gibbosa calante";
  else if (age < 23.99) phaseName = "Ultimo quarto";
  else if (age < 27.68) phaseName = "Falce calante";
  else phaseName = "Luna nuova";

  const double toFull = age < kSynodic / 2.0 ? kSynodic / 2.0 - age : kSynodic * 1.5 - age;
  const int16_t tx = static_cast<int16_t>(mcx + mRad + 20);
  const int16_t tw = static_cast<int16_t>(r.right() - tx - 12);
  char illumTxt[32], nextTxt[40];
  snprintf(illumTxt, sizeof(illumTxt), "Illuminata al %d%%", illum);
  const int nf = static_cast<int>(toFull + 0.5);
  if (nf == 0) {
    snprintf(nextTxt, sizeof(nextTxt), "Piena oggi");
  } else {
    snprintf(nextTxt, sizeof(nextTxt), "Prossima piena tra %d %s", nf, nf == 1 ? "giorno" : "giorni");
  }
  drawText(t, Rect{tx, static_cast<int16_t>(r.y + 40), tw, 26}, phaseName, ui::kFontTitle, Color::Black);
  drawText(t, Rect{tx, static_cast<int16_t>(r.y + 72), tw, 22}, illumTxt, ui::kFontSmall, Color::Black);
  drawText(t, Rect{tx, static_cast<int16_t>(r.y + 96), tw, 22}, nextTxt, ui::kFontSmall, Color::Black);
}

void DeskDashboard::render(freeink::ui::DrawTarget& t, const DeviceStatus& status, const DeskInfo& info) {
  clearScreen();
  const int16_t w = screenW();
  const int16_t h = screenH();
  const bool clock = status.clockValid;
  const int year = clock ? status.year : 2026;
  const int month = clock ? status.month : 1;
  const int day = clock ? status.day : 1;
  const int wd = dayOfWeekMondayFirst(year, month, day);
  const int32_t dayIdx = dayNumber(static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day));

  // Top strip.
  drawPill(t, 14, 8, 22, "OROLOGIO · XTEINK X3", ui::kFontMicro, true, 10);
  char battPill[32];
  if (status.batteryPercent >= 0) {
    snprintf(battPill, sizeof(battPill), "%d%%%s", static_cast<int>(status.batteryPercent),
             status.charging ? " · IN CARICA" : "");
  } else {
    snprintf(battPill, sizeof(battPill), "%s", status.charging ? "IN CARICA" : "BATTERIA --");
  }
  const int16_t battW = static_cast<int16_t>(textWidth(t, battPill, ui::kFontMicro) + 16);
  drawPill(t, static_cast<int16_t>(w - 14 - battW), 8, 22, battPill, ui::kFontMicro, false, 8);

  char hStr[4] = "--", mStr[4] = "--";
  if (clock) {
    snprintf(hStr, sizeof(hStr), "%02d", status.hour % 100);
    snprintf(mStr, sizeof(mStr), "%02d", status.minute % 100);
  }
  char fullDate[80];
  if (clock) {
    snprintf(fullDate, sizeof(fullDate), "%s %d %s %d", weekdayNameItalian(wd), day, monthNameItalian(month), year);
  } else {
    snprintf(fullDate, sizeof(fullDate), "ORA NON IMPOSTATA · COLLEGATI AL WI-FI");
  }

  if (w > h) {
    // Widescreen Landscape layout (792x528):
    // Left column (x=14..394, width=380): Split-flap clock + Date + Current Book + Sub-widget
    // Right column (x=408..778, width=370): Calendar + Reading Streak
    const int16_t leftX = 14;
    const int16_t leftW = 380;
    const int16_t cardW = 82;
    const int16_t cardH = 114;
    const int16_t flapY = 36;
    const int16_t total = static_cast<int16_t>(cardW * 4 + 6 * 2 + 28);
    const int16_t startX = static_cast<int16_t>(leftX + (leftW - total) / 2);

    drawSplitFlapCard(t, startX, flapY, cardW, cardH, hStr[0]);
    drawSplitFlapCard(t, static_cast<int16_t>(startX + cardW + 6), flapY, cardW, cardH, hStr[1]);
    const int16_t colonX = static_cast<int16_t>(startX + cardW * 2 + 6 + 14);
    fillCircle(t, colonX, static_cast<int16_t>(flapY + 38), 4, Color::Black);
    fillCircle(t, colonX, static_cast<int16_t>(flapY + 76), 4, Color::Black);
    const int16_t minStartX = static_cast<int16_t>(colonX + 14);
    drawSplitFlapCard(t, minStartX, flapY, cardW, cardH, mStr[0]);
    drawSplitFlapCard(t, static_cast<int16_t>(minStartX + cardW + 6), flapY, cardW, cardH, mStr[1]);

    drawText(t, Rect{leftX, 156, leftW, 24}, fullDate, ui::kFontBody, Color::Black, TextAlign::Center);

    const Rect bookCard{leftX, 184, leftW, 132};
    drawCard(t, bookCard, 10, 3, false, true);
    if (info.bookTitle && info.bookTitle[0]) {
      drawBookCover(t, 26, 204, info.bookTitle, info.bookAuthor, info.bookCoverBits);
      const int16_t bx = static_cast<int16_t>(26 + kCoverW + 14);
      const int16_t bw = static_cast<int16_t>(bookCard.right() - bx - 12);
      drawPill(t, bx, 198, 22, "IN LETTURA", ui::kFontMicro, true, 8);
      char pct[8];
      snprintf(pct, sizeof(pct), "%u%%", static_cast<unsigned>(info.bookPct));
      drawText(t, Rect{static_cast<int16_t>(bx + bw - 56), 196, 56, 24}, pct, ui::kFontBody, Color::Black,
               TextAlign::Right, false);
      drawText(t, Rect{bx, 226, bw, 28}, info.bookTitle, ui::kFontBody, Color::Black);
      drawText(t, Rect{bx, 254, bw, 22}, info.bookAuthor ? info.bookAuthor : "", ui::kFontSmall, Color::Black);
      drawProgressBar(t, Rect{bx, 286, bw, 14}, info.bookPct, false);
    } else {
      drawIcon16(t, 30, 204, Icon::Book, Color::Black);
      drawPill(t, 54, 200, 22, "IN LETTURA", ui::kFontMicro, true, 8);
      drawText(t, Rect{30, 236, static_cast<int16_t>(leftW - 32), 28}, "Nessun libro sulla microSD", ui::kFontBody,
               Color::Black);
    }

    const Rect subRect{leftX, 326, leftW, 154};
    switch (subWidget_) {
      case DeskSubWidget::MoonPhase:
        renderMoonPhase(t, subRect, year, month, day);
        break;
      case DeskSubWidget::ObliqueCard:
        renderQuote(t, subRect, true, dayIdx);
        break;
      case DeskSubWidget::StoicQuote:
      default:
        renderQuote(t, subRect, false, dayIdx);
        break;
    }

    const int16_t rightX = 408;
    const int16_t rightW = static_cast<int16_t>(w - rightX - 14);
    const Rect calRect{rightX, 36, rightW, 166};
    renderMiniCalendar(t, calRect, year, month, clock ? day : 0);

    const Rect streakRect{rightX, 212, rightW, 268};
    fastStroke(t, streakRect, Paint::solid(Color::Black), 1, 6);
    fastFill(t, Rect{streakRect.x, streakRect.y, streakRect.width, 22}, Paint::solid(Color::Black), 5);
    const int16_t sx = static_cast<int16_t>(streakRect.x + 16);
    const int16_t sw = static_cast<int16_t>(streakRect.width - 32);
    drawText(t, Rect{static_cast<int16_t>(streakRect.x + 10), static_cast<int16_t>(streakRect.y + 3),
                     static_cast<int16_t>(streakRect.width - 20), 16},
             "SERIE E OBIETTIVO DI LETTURA", ui::kFontMicro, Color::White, TextAlign::Left);
    char streakBig[32];
    snprintf(streakBig, sizeof(streakBig), "%d %s", info.streakDays, info.streakDays == 1 ? "GIORNO" : "GIORNI");
    drawText(t, Rect{sx, static_cast<int16_t>(streakRect.y + 36), sw, 34}, streakBig, ui::kFontHero, Color::Black);
    char pagOggi[48];
    snprintf(pagOggi, sizeof(pagOggi), "Oggi: %d / %d pagine lette", info.todayPages, info.goalPages);
    drawText(t, Rect{sx, static_cast<int16_t>(streakRect.y + 82), sw, 24}, pagOggi, ui::kFontBody, Color::Black);
    const int goalPct = info.goalPages > 0 ? (info.todayPages * 100 / info.goalPages) : 0;
    drawProgressBar(t, Rect{sx, static_cast<int16_t>(streakRect.y + 116), sw, 16},
                    static_cast<uint8_t>(goalPct > 100 ? 100 : goalPct), false);
    char pagTot[48];
    snprintf(pagTot, sizeof(pagTot), "Totale complessivo: %lu pagine", static_cast<unsigned long>(info.totalPages));
    drawText(t, Rect{sx, static_cast<int16_t>(streakRect.y + 150), sw, 22}, pagTot, ui::kFontSmall, Color::Black);

    drawBottomKeyBar(t, "SX/DX", "Widget", "OK", "Aggiorna", "BACK", "Esci");
    return;
  }

  // 1. Split-flap clock (Portrait 528x792).
  const int16_t cardW = 104;
  const int16_t cardH = 144;
  const int16_t flapY = 38;
  const int16_t total = static_cast<int16_t>(cardW * 4 + 8 * 2 + 40);
  const int16_t startX = static_cast<int16_t>((w - total) / 2);
  drawSplitFlapCard(t, startX, flapY, cardW, cardH, hStr[0]);
  drawSplitFlapCard(t, static_cast<int16_t>(startX + cardW + 8), flapY, cardW, cardH, hStr[1]);
  const int16_t colonX = static_cast<int16_t>(startX + cardW * 2 + 8 + 20);
  fillCircle(t, colonX, static_cast<int16_t>(flapY + 48), 5, Color::Black);
  fillCircle(t, colonX, static_cast<int16_t>(flapY + 96), 5, Color::Black);
  const int16_t minStartX = static_cast<int16_t>(colonX + 20);
  drawSplitFlapCard(t, minStartX, flapY, cardW, cardH, mStr[0]);
  drawSplitFlapCard(t, static_cast<int16_t>(minStartX + cardW + 8), flapY, cardW, cardH, mStr[1]);

  // 2. Date.
  drawText(t, Rect{14, 192, static_cast<int16_t>(w - 28), 26}, fullDate, ui::kFontTitle, Color::Black,
           TextAlign::Center);

  // 3. Current book.
  const Rect bookCard{14, 226, static_cast<int16_t>(w - 28), 126};
  drawCard(t, bookCard, 10, 3, false, true);
  if (info.bookTitle && info.bookTitle[0]) {
    drawBookCover(t, 28, 243, info.bookTitle, info.bookAuthor, info.bookCoverBits);
    const int16_t bx = static_cast<int16_t>(28 + kCoverW + 16);
    const int16_t bw = static_cast<int16_t>(bookCard.right() - bx - 14);
    drawPill(t, bx, 240, 22, "IN LETTURA", ui::kFontMicro, true, 8);
    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", static_cast<unsigned>(info.bookPct));
    drawText(t, Rect{static_cast<int16_t>(bx + bw - 60), 238, 60, 26}, pct, ui::kFontTitle, Color::Black, TextAlign::Right,
             false);
    drawText(t, Rect{bx, 270, bw, 30}, info.bookTitle, ui::kFontTitle, Color::Black);
    drawText(t, Rect{bx, 300, bw, 22}, info.bookAuthor ? info.bookAuthor : "", ui::kFontSmall,
             Color::Black);
    drawProgressBar(t, Rect{bx, 326, bw, 14}, info.bookPct, false);
  } else {
    drawIcon16(t, 30, 244, Icon::Book, Color::Black);
    drawPill(t, 54, 240, 22, "IN LETTURA", ui::kFontMicro, true, 8);
    drawText(t, Rect{30, 276, static_cast<int16_t>(w - 60), 30}, "Nessun libro sulla microSD", ui::kFontTitle,
             Color::Black);
  }

  // 4. Calendar + reading streak.
  const int16_t halfW = static_cast<int16_t>((w - 40) / 2);
  const Rect calRect{14, 362, halfW, 160};
  renderMiniCalendar(t, calRect, year, month, clock ? day : 0);

  const Rect streakRect{static_cast<int16_t>(26 + halfW), 362, halfW, 160};
  fastStroke(t, streakRect, Paint::solid(Color::Black), 1, 6);
  fastFill(t, Rect{streakRect.x, streakRect.y, streakRect.width, 22}, Paint::solid(Color::Black), 5);
  const int16_t sx = static_cast<int16_t>(streakRect.x + 12);
  const int16_t sw = static_cast<int16_t>(streakRect.width - 24);
  drawText(t, Rect{static_cast<int16_t>(streakRect.x + 8), static_cast<int16_t>(streakRect.y + 3),
                   static_cast<int16_t>(streakRect.width - 16), 16},
           "LETTURA", ui::kFontMicro, Color::White, TextAlign::Left);
  char streakBig[32];
  snprintf(streakBig, sizeof(streakBig), "%d %s", info.streakDays, info.streakDays == 1 ? "GIORNO" : "GIORNI");
  drawText(t, Rect{sx, static_cast<int16_t>(streakRect.y + 30), sw, 30}, streakBig, ui::kFontHero, Color::Black);
  char pagOggi[48];
  snprintf(pagOggi, sizeof(pagOggi), "Oggi %d / %d pagine", info.todayPages, info.goalPages);
  drawText(t, Rect{sx, static_cast<int16_t>(streakRect.y + 64), sw, 22}, pagOggi, ui::kFontBody, Color::Black);
  const int goalPct = info.goalPages > 0 ? (info.todayPages * 100 / info.goalPages) : 0;
  drawProgressBar(t, Rect{sx, static_cast<int16_t>(streakRect.y + 92), sw, 14},
                  static_cast<uint8_t>(goalPct > 100 ? 100 : goalPct), false);
  char pagTot[48];
  snprintf(pagTot, sizeof(pagTot), "Totale: %lu pagine", static_cast<unsigned long>(info.totalPages));
  drawText(t, Rect{sx, static_cast<int16_t>(streakRect.y + 120), sw, 22}, pagTot, ui::kFontSmall, Color::Black);

  // 5. Configurable widget.
  const Rect subRect{14, 532, static_cast<int16_t>(w - 28), 152};
  switch (subWidget_) {
    case DeskSubWidget::MoonPhase:
      renderMoonPhase(t, subRect, year, month, day);
      break;
    case DeskSubWidget::ObliqueCard:
      renderQuote(t, subRect, true, dayIdx);
      break;
    case DeskSubWidget::StoicQuote:
    default:
      renderQuote(t, subRect, false, dayIdx);
      break;
  }

  drawBottomKeyBar(t, "SX/DX", "Widget", "OK", "Aggiorna", "BACK", "Esci");
}

}  // namespace inkling
