#include <cstdio>
#include "Platform.h"

namespace inkling {

int32_t dayNumber(uint16_t year, uint8_t month, uint8_t day) {
  // Howard Hinnant's days_from_civil.
  const int32_t y = static_cast<int32_t>(year) - (month <= 2 ? 1 : 0);
  const int32_t era = (y >= 0 ? y : y - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
  const uint32_t mp = (month + 9) % 12;
  const uint32_t doy = (153 * mp + 2) / 5 + day - 1;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

uint32_t localEpoch(const LocalTime& t) {
  if (!t.valid) return 0;
  const int32_t days = dayNumber(t.year, t.month, t.day);
  return static_cast<uint32_t>(days) * 86400u + t.hour * 3600u + t.minute * 60u + t.second;
}

int weekdayMondayFirst(uint16_t year, uint8_t month, uint8_t day) {
  // 1970-01-01 was a Thursday (index 3 with Monday = 0).
  const int32_t n = dayNumber(year, month, day);
  return static_cast<int>(((n % 7) + 7 + 3) % 7);
}

void formatItalianDate(const LocalTime& t, char* out, uint32_t cap, bool withYear) {
  if (!out || cap == 0) return;
  out[0] = '\0';
  if (!t.valid || t.month < 1 || t.month > 12 || t.day < 1) return;
  static const char* const kDays[] = {"Lunedì", "Martedì", "Mercoledì", "Giovedì", "Venerdì", "Sabato", "Domenica"};
  static const char* const kMonths[] = {"gennaio", "febbraio", "marzo",     "aprile",  "maggio",   "giugno",
                                        "luglio",  "agosto",   "settembre", "ottobre", "novembre", "dicembre"};
  const char* wd = kDays[weekdayMondayFirst(t.year, t.month, t.day)];
  if (withYear) {
    snprintf(out, cap, "%s %u %s %u", wd, static_cast<unsigned>(t.day), kMonths[t.month - 1],
             static_cast<unsigned>(t.year));
  } else {
    snprintf(out, cap, "%s %u %s", wd, static_cast<unsigned>(t.day), kMonths[t.month - 1]);
  }
}

}  // namespace inkling
