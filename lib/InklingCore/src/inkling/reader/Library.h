#pragma once

#include <cstdint>

#include "inkling/Platform.h"

namespace inkling {

// Persistent summary written by BookReader whenever a book is opened or paged.
struct BookSummaryRecord {
  uint32_t magic = 0x4D534B49;  // "IKSM"
  uint8_t progressPct = 0;
  uint8_t started = 0;
  uint16_t spine = 0;
  uint16_t spineCount = 0;
  char title[56] = {};
  char author[36] = {};
};

// Books found on the card: the root and every visible top-level folder.
class Library {
 public:
  static constexpr int kMaxBooks = 32;

  struct Entry {
    char path[128];   // full path from card root
    char name[56];    // clean display title (kept as `name` for simulator compatibility)
    char author[36];  // parsed or metadata author
    uint8_t progressPct = 0;
    uint8_t started = 0;
  };

  void scan(Storage& storage);
  void refreshSummaries(Storage& storage);
  int count() const { return count_; }
  const Entry& at(int i) const { return entries_[i]; }

  static void summaryPathFor(const char* bookPath, char* out, uint32_t cap);
  static void parseFilenameTitleAuthor(const char* rawStem, char* titleOut, uint32_t titleCap, char* authorOut,
                                       uint32_t authorCap);

 private:
  void addFrom(Storage& storage, const char* dir);
  static void onEntry(const char* name, bool isDir, void* user);

  Entry entries_[kMaxBooks];
  int count_ = 0;

  // Scan state shared with the listDir callback.
  const char* scanDir_ = nullptr;
  char subdirs_[16][40];
  int subdirCount_ = 0;
  bool collectDirs_ = false;
};

}  // namespace inkling
