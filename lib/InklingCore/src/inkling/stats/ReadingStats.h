#pragma once

#include <cstdint>

#include "inkling/Platform.h"

namespace inkling {

// What the reader has read: pages and time, today, over the last week and in
// total. Kept in /.inkling/reading.bin.
class ReadingStats {
 public:
  struct Day {
    uint16_t pages = 0;
    uint16_t minutes = 0;
  };

  void load(Storage& storage);
  void save(Storage& storage) const;

  // One page read; `seconds` is the time spent on it (0 when unknown).
  void onPage(const LocalTime& now, uint32_t seconds);

  uint32_t totalPages() const { return totalPages_; }
  uint32_t totalMinutes() const { return totalSeconds_ / 60u; }
  // Figures as of `now`: a day without reading yet counts as zero, and a
  // streak that was not continued yesterday or today is over.
  uint16_t todayPages(const LocalTime& now) const;
  uint16_t todayMinutes(const LocalTime& now) const;
  uint16_t streakDays(const LocalTime& now) const;
  // out[6] is today, out[0] six days ago.
  void lastWeek(const LocalTime& now, Day out[7]) const;

 private:
  int32_t dayOf(const LocalTime& now) const;
  void rollTo(int32_t today);

  uint32_t totalPages_ = 0;
  uint32_t totalSeconds_ = 0;
  int32_t lastDay_ = 0;       // day number of the last page read
  uint16_t dayPages_ = 0;     // on lastDay_
  uint16_t streak_ = 0;
  uint32_t daySeconds_ = 0;
  Day earlier_[6] = {};       // earlier_[0] = the day before lastDay_, ...
};

}  // namespace inkling
