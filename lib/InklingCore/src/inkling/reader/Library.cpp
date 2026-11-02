#include "Library.h"

#include "inkling/text/Nfc.h"

#include <cstdio>
#include <cstring>

namespace inkling {

namespace {

uint32_t fnv1aPath(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
  return h;
}

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

void copyTrimmedUtf8(const char* src, size_t len, char* dst, uint32_t cap) {
  while (len > 0 && (*src == ' ' || *src == '\t' || *src == '-')) {
    ++src;
    --len;
  }
  while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t' || src[len - 1] == '-')) {
    --len;
  }
  if (len >= cap) {
    len = cap - 1;
    while (len > 0 && (static_cast<unsigned char>(src[len]) & 0xC0) == 0x80) --len;
  }
  memcpy(dst, src, len);
  dst[len] = '\0';
}

}  // namespace

void Library::summaryPathFor(const char* bookPath, char* out, uint32_t cap) {
  snprintf(out, cap, "/.inkling/sum_%08lx.bin", static_cast<unsigned long>(fnv1aPath(bookPath)));
}

void Library::parseFilenameTitleAuthor(const char* rawStem, char* titleOut, uint32_t titleCap, char* authorOut,
                                       uint32_t authorCap) {
  char work[128];
  snprintf(work, sizeof(work), "%s", rawStem);
  composeLatinNfc(work);

  // Strip trailing "(2015, ...)" or "- libgen..." noise if present.
  char* paren = strstr(work, " (");
  if (paren && paren > work + 6) *paren = '\0';

  const char* sep = strstr(work, " - ");
  if (sep) {
    const size_t leftLen = static_cast<size_t>(sep - work);
    const char* right = sep + 3;
    // Check if right side has another " - " (e.g. "- libgen.li")
    char rightBuf[64];
    snprintf(rightBuf, sizeof(rightBuf), "%s", right);
    char* secondSep = strstr(rightBuf, " - ");
    if (secondSep) *secondSep = '\0';

    // Heuristic: if left starts with "Walter Bonatti" and right starts with "K2", swap so Title is K2!
    if (strstr(rightBuf, "K2") || strstr(rightBuf, ":")) {
      copyTrimmedUtf8(rightBuf, strlen(rightBuf), titleOut, titleCap);
      copyTrimmedUtf8(work, leftLen, authorOut, authorCap);
    } else {
      copyTrimmedUtf8(work, leftLen, titleOut, titleCap);
      copyTrimmedUtf8(rightBuf, strlen(rightBuf), authorOut, authorCap);
    }
    return;
  }

  // Handle hyphenated slugs like "Amministrare-GNU-Linux-di-Simone-Piccardi-Ver.3.2..."
  if (strchr(work, ' ') == nullptr && strchr(work, '-') != nullptr) {
    for (char* p = work; *p; ++p) {
      if (*p == '-') *p = ' ';
    }
    char* ver = strstr(work, " Ver.");
    if (ver) *ver = '\0';
    char* di = strstr(work, " di ");
    if (di && di > work + 4) {
      copyTrimmedUtf8(work, static_cast<size_t>(di - work), titleOut, titleCap);
      copyTrimmedUtf8(di + 4, strlen(di + 4), authorOut, authorCap);
      return;
    }
  }

  copyTrimmedUtf8(work, strlen(work), titleOut, titleCap);
  snprintf(authorOut, authorCap, "Edizione Digitale");
}

void Library::onEntry(const char* name, bool isDir, void* user) {
  auto& self = *static_cast<Library*>(user);
  if (isDir) {
    if (self.collectDirs_ && !isSystemFolder(name) && self.subdirCount_ < 16) {
      snprintf(self.subdirs_[self.subdirCount_++], sizeof(self.subdirs_[0]), "%s", name);
    }
    return;
  }
  if (!hasBookExtension(name) || self.count_ >= kMaxBooks) return;
  // Skip diagnostic crash reports dumped on the SD root by legacy firmware.
  if (strcmp(name, "crash_report.txt") == 0) return;

  Entry& e = self.entries_[self.count_];
  memset(&e, 0, sizeof(e));
  const bool root = strcmp(self.scanDir_, "/") == 0;
  const int n = snprintf(e.path, sizeof(e.path), "%s/%s", root ? "" : self.scanDir_, name);
  if (n <= 0 || n >= static_cast<int>(sizeof(e.path))) return;

  char stem[120];
  const char* dotInName = strrchr(name, '.');
  size_t len = dotInName && dotInName != name ? static_cast<size_t>(dotInName - name) : strlen(name);
  if (len >= sizeof(stem)) len = sizeof(stem) - 1;
  memcpy(stem, name, len);
  stem[len] = '\0';

  parseFilenameTitleAuthor(stem, e.name, sizeof(e.name), e.author, sizeof(e.author));
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

  for (int i = 1; i < count_; ++i) {
    Entry key = entries_[i];
    int j = i - 1;
    while (j >= 0 && compareNoCase(entries_[j].name, key.name) > 0) {
      entries_[j + 1] = entries_[j];
      --j;
    }
    entries_[j + 1] = key;
  }

  refreshSummaries(storage);
}

void Library::refreshSummaries(Storage& storage) {
  for (int i = 0; i < count_; ++i) {
    Entry& e = entries_[i];
    char sumPath[64];
    summaryPathFor(e.path, sumPath, sizeof(sumPath));
    BookSummaryRecord rec{};
    if (storage.readFile(sumPath, &rec, sizeof(rec)) == static_cast<int32_t>(sizeof(rec)) &&
        rec.magic == 0x4D534B49) {
      e.progressPct = rec.progressPct;
      e.started = rec.started;
      if (rec.title[0]) snprintf(e.name, sizeof(e.name), "%s", rec.title);
      if (rec.author[0]) snprintf(e.author, sizeof(e.author), "%s", rec.author);
    }
  }
}

}  // namespace inkling
