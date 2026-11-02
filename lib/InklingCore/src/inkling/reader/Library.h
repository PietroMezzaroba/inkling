#pragma once

#include <cstdint>

#include "inkling/Platform.h"

namespace inkling {

// Books found on the card: the root and every visible top-level folder.
class Library {
 public:
  static constexpr int kMaxBooks = 40;

  struct Entry {
    char path[128];  // long "Author - Title (year, publisher)" names are common
    char name[56];  // file name without extension, cut to fit
  };

  void scan(Storage& storage);
  int count() const { return count_; }
  const Entry& at(int i) const { return entries_[i]; }

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
