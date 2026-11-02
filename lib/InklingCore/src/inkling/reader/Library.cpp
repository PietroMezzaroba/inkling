#include "Library.h"

#include <cstdio>
#include <cstring>

namespace inkling {

namespace {

bool hasBookExtension(const char* name) {
  const char* dot = strrchr(name, '.');
  if (!dot) return false;
  char ext[8] = {};
  for (int i = 0; i < 7 && dot[1 + i]; ++i) {
    const char c = dot[1 + i];
    ext[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }
  return strcmp(ext, "epub") == 0 || strcmp(ext, "txt") == 0 || strcmp(ext, "md") == 0;
}

// Phone and OS folders that never hold books but can hold thousands of files.
bool isSystemFolder(const char* name) {
  static const char* const kSkip[] = {"Android", "DCIM", "LOST.DIR", "System Volume Information", "Music",
                                      "Pictures", "Movies", "Ringtones", "Alarms", "Notifications", "Podcasts",
                                      "Recordings", "Audiobooks", "screenshots"};
  if (name[0] == '.') return true;
  for (const char* skip : kSkip) {
    if (strcmp(name, skip) == 0) return true;
  }
  return false;
}

int compareNoCase(const char* a, const char* b) {
  for (;; ++a, ++b) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y || x == '\0') return static_cast<unsigned char>(x) - static_cast<unsigned char>(y);
  }
}

}  // namespace

void Library::onEntry(const char* name, bool isDir, void* user) {
  auto& self = *static_cast<Library*>(user);
  if (isDir) {
    if (self.collectDirs_ && !isSystemFolder(name) && self.subdirCount_ < 16) {
      snprintf(self.subdirs_[self.subdirCount_++], sizeof(self.subdirs_[0]), "%s", name);
    }
    return;
  }
  if (!hasBookExtension(name) || self.count_ >= kMaxBooks) return;
  Entry& e = self.entries_[self.count_];
  const bool root = strcmp(self.scanDir_, "/") == 0;
  const int n = snprintf(e.path, sizeof(e.path), "%s/%s", root ? "" : self.scanDir_, name);
  if (n <= 0 || n >= static_cast<int>(sizeof(e.path))) return;  // path too long: skip it
  // Display name: drop the extension, then cut to fit without splitting a
  // UTF-8 sequence (accented titles are common).
  const char* dotInName = strrchr(name, '.');
  size_t len = dotInName && dotInName != name ? static_cast<size_t>(dotInName - name) : strlen(name);
  if (len > sizeof(e.name) - 1) {
    len = sizeof(e.name) - 1;
    while (len > 0 && (static_cast<unsigned char>(name[len]) & 0xC0) == 0x80) --len;
  }
  memcpy(e.name, name, len);
  e.name[len] = '\0';
  self.count_++;
}

void Library::addFrom(Storage& storage, const char* dir) {
  scanDir_ = dir;
  storage.listDir(dir, onEntry, this);
}

void Library::scan(Storage& storage) {
  count_ = 0;
  subdirCount_ = 0;
  collectDirs_ = true;
  addFrom(storage, "/");
  collectDirs_ = false;
  char dir[48];
  for (int i = 0; i < subdirCount_; ++i) {
    snprintf(dir, sizeof(dir), "/%s", subdirs_[i]);
    addFrom(storage, dir);
  }

  // Insertion sort by name: at most kMaxBooks entries.
  for (int i = 1; i < count_; ++i) {
    Entry key = entries_[i];
    int j = i - 1;
    while (j >= 0 && compareNoCase(entries_[j].name, key.name) > 0) {
      entries_[j + 1] = entries_[j];
      --j;
    }
    entries_[j + 1] = key;
  }
}

}  // namespace inkling
