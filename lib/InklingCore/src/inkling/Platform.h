#pragma once

#include <BookStorage.h>

#include <cstdint>

// Optional diagnostics from the portable core. Platforms define INKLING_LOG
// (printf-style) to route it; by default it compiles away.
#ifndef INKLING_LOG
#define INKLING_LOG(...) \
  do {                   \
  } while (0)
#endif

namespace inkling {

// Everything the portable core needs from the outside world. The firmware backs
// it with the SD card and the RTC; the simulator with a host folder and fixed
// values. Paths are absolute from the card root ("/books/x.epub").
class Storage {
 public:
  virtual ~Storage() = default;

  // Calls `fn` for every entry of `dir` (not recursive). Returns false if the
  // directory cannot be opened.
  using DirFn = void (*)(const char* name, bool isDir, void* user);
  virtual bool listDir(const char* dir, DirFn fn, void* user) = 0;

  // Whole-file helpers for small files (progress, stats). readFile returns the
  // byte count or -1; writeFile replaces the file atomically where possible.
  virtual int32_t readFile(const char* path, void* dst, uint32_t cap) = 0;
  virtual bool writeFile(const char* path, const void* data, uint32_t len) = 0;
  virtual bool mkdirs(const char* path) = 0;

  // One book is open at a time: openBook() re-points a single source object.
  virtual freeink::book::BookSource* openBook(const char* path) = 0;
  virtual void closeBook() = 0;

  // Flat cache store rooted at `dir` (created if missing). Same single-instance
  // contract as openBook().
  virtual freeink::book::CacheStorage* cacheAt(const char* dir) = 0;
};

// Wall-clock time as read from the RTC: local time, no time zone math.
struct LocalTime {
  bool valid = false;
  uint16_t year = 0;
  uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

// Days since 1970-01-01 for a civil date (proleptic Gregorian).
int32_t dayNumber(uint16_t year, uint8_t month, uint8_t day);

// Seconds since 1970-01-01 00:00 local time; 0 for an invalid clock.
uint32_t localEpoch(const LocalTime& t);

}  // namespace inkling
