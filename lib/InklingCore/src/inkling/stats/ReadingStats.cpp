#include "ReadingStats.h"

#include <cstring>

namespace inkling {

namespace {

constexpr const char* kPath = "/.inkling/reading.bin";
constexpr uint32_t kMagicV1 = 0x54534452u;  // "RDST": pages only
constexpr uint32_t kMagicV2 = 0x32534452u;  // "RDS2": pages, time, last week

struct RecordV1 {
  uint32_t magic;
  uint32_t totalPages;
  int32_t lastReadDay;
  uint16_t dailyPages;
  uint16_t streakDays;
};

struct RecordV2 {
  uint32_t magic;
  uint32_t totalPages;
  uint32_t totalSeconds;
  int32_t lastDay;
  uint16_t dayPages;
  uint16_t streak;
  uint32_t daySeconds;
  uint16_t earlierPages[6];
  uint16_t earlierMinutes[6];
};

}  // namespace

void ReadingStats::load(Storage& storage) {
  RecordV2 r{};
  const int32_t n = storage.readFile(kPath, &r, sizeof(r));
  if (n == static_cast<int32_t>(sizeof(RecordV2)) && r.magic == kMagicV2) {
    totalPages_ = r.totalPages;
    totalSeconds_ = r.totalSeconds;
    lastDay_ = r.lastDay;
    dayPages_ = r.dayPages;
    streak_ = r.streak;
    daySeconds_ = r.daySeconds;
    for (int i = 0; i < 6; ++i) earlier_[i] = Day{r.earlierPages[i], r.earlierMinutes[i]};
  } else if (n >= static_cast<int32_t>(sizeof(RecordV1)) && r.magic == kMagicV1) {
    RecordV1 old;
    memcpy(&old, &r, sizeof(old));
    totalPages_ = old.totalPages;
    lastDay_ = old.lastReadDay;
    dayPages_ = old.dailyPages;
    streak_ = old.streakDays;
  }
}

void ReadingStats::save(Storage& storage) const {
  RecordV2 r{};
  r.magic = kMagicV2;
  r.totalPages = totalPages_;
  r.totalSeconds = totalSeconds_;
  r.lastDay = lastDay_;
  r.dayPages = dayPages_;
  r.streak = streak_;
  r.daySeconds = daySeconds_;
  for (int i = 0; i < 6; ++i) {
    r.earlierPages[i] = earlier_[i].pages;
    r.earlierMinutes[i] = earlier_[i].minutes;
  }
  storage.mkdirs("/.inkling");
  storage.writeFile(kPath, &r, sizeof(r));
}

int32_t ReadingStats::dayOf(const LocalTime& now) const {
  return now.valid ? dayNumber(now.year, now.month, now.day) : lastDay_;
}

void ReadingStats::rollTo(int32_t today) {
  if (today == lastDay_) return;
  const int32_t gap = today - lastDay_;
  if (lastDay_ == 0 || gap < 0 || gap > 6) {
    for (auto& d : earlier_) d = Day{};
  } else {
    // lastDay_ becomes "gap days ago".
    Day shifted[6] = {};
    const Day last{dayPages_, static_cast<uint16_t>(daySeconds_ / 60u)};
    for (int i = 0; i < 6; ++i) {
      const int from = i - gap;  // index in the old "earlier" list, -1 = lastDay_
      if (from == -1) shifted[i] = last;
      if (from >= 0) shifted[i] = earlier_[from];
    }
    memcpy(earlier_, shifted, sizeof(shifted));
  }
  streak_ = (lastDay_ != 0 && gap == 1) ? static_cast<uint16_t>(streak_ + 1) : 1;
  lastDay_ = today;
  dayPages_ = 0;
  daySeconds_ = 0;
}

void ReadingStats::onPage(const LocalTime& now, uint32_t seconds) {
  if (now.valid) {
    rollTo(dayNumber(now.year, now.month, now.day));
  } else if (streak_ == 0) {
    streak_ = 1;
  }
  ++totalPages_;
  if (dayPages_ < 0xFFFF) ++dayPages_;
  daySeconds_ += seconds;
  totalSeconds_ += seconds;
}

uint16_t ReadingStats::todayPages(const LocalTime& now) const { return dayOf(now) == lastDay_ ? dayPages_ : 0; }

uint16_t ReadingStats::todayMinutes(const LocalTime& now) const {
  return dayOf(now) == lastDay_ ? static_cast<uint16_t>(daySeconds_ / 60u) : 0;
}

uint16_t ReadingStats::streakDays(const LocalTime& now) const {
  const int32_t gap = dayOf(now) - lastDay_;
  return (gap == 0 || gap == 1) ? streak_ : 0;
}

void ReadingStats::lastWeek(const LocalTime& now, Day out[7]) const {
  for (int i = 0; i < 7; ++i) out[i] = Day{};
  const int32_t gap = dayOf(now) - lastDay_;
  if (lastDay_ == 0 || gap < 0 || gap > 6) return;
  // out[6 - k] is k days ago; lastDay_ is `gap` days ago.
  out[6 - gap] = Day{dayPages_, static_cast<uint16_t>(daySeconds_ / 60u)};
  for (int i = 0; i < 6; ++i) {
    const int ago = gap + 1 + i;
    if (ago <= 6) out[6 - ago] = earlier_[i];
  }
}

}  // namespace inkling
