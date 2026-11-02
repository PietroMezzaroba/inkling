#include "SdStorage.h"

#include <cstdio>
#include <cstring>

bool SdStorage::listDir(const char* dir, DirFn fn, void* user) {
  FsFile root = SdMan.open(dir, O_RDONLY);
  if (!root || !root.isDirectory()) return false;
  char name[128];
  for (FsFile f = root.openNextFile(); f; f = root.openNextFile()) {
    f.getName(name, sizeof(name));
    fn(name, f.isDirectory(), user);
  }
  return true;
}

int32_t SdStorage::readFile(const char* path, void* dst, uint32_t cap) {
  FsFile f = SdMan.open(path, O_RDONLY);
  if (!f) return -1;
  return static_cast<int32_t>(f.read(dst, cap));
}

bool SdStorage::writeFile(const char* path, const void* data, uint32_t len) {
  // Write a temp file, then swap it in: a power cut mid-write leaves the old
  // file (or the temp), never a half-written one under the real name.
  char tmp[128];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  FsFile f = SdMan.open(tmp, O_RDWR | O_CREAT | O_TRUNC);
  if (!f) return false;
  const bool ok = f.write(static_cast<const uint8_t*>(data), len) == len;
  f.close();
  if (!ok) return false;
  SdMan.remove(path);
  return SdMan.rename(tmp, path);
}

bool SdStorage::mkdirs(const char* path) { return SdMan.exists(path) || SdMan.mkdir(path, true); }

freeink::book::BookSource* SdStorage::openBook(const char* path) {
  closeBook();
  source_.file = SdMan.open(path, O_RDONLY);
  if (!source_.file) return nullptr;
  source_.bytes = source_.file.fileSize();
  return &source_;
}

void SdStorage::closeBook() {
  if (source_.file) source_.file.close();
  source_.bytes = 0;
  cache_.closeAll();
}

freeink::book::CacheStorage* SdStorage::cacheAt(const char* dir) {
  if (!mkdirs(dir)) return nullptr;
  cache_.closeAll();
  snprintf(cache_.dir, sizeof(cache_.dir), "%s", dir);
  return &cache_;
}

int32_t SdStorage::Source::readAt(uint64_t offset, void* dst, uint32_t len) {
  if (!file.seekSet(offset)) return -1;
  return static_cast<int32_t>(file.read(dst, len));
}

void SdStorage::Cache::path(const char* name, char* out, size_t cap) const { snprintf(out, cap, "%s/%s", dir, name); }

void SdStorage::Cache::closeAll() {
  if (reading) reading.close();
  if (writing) writing.close();
  readingName[0] = writingName[0] = '\0';
}

bool SdStorage::Cache::exists(const char* name) {
  char p[112];
  path(name, p, sizeof(p));
  return SdMan.exists(p);
}

bool SdStorage::Cache::remove(const char* name) {
  if (strcmp(name, readingName) == 0) {
    reading.close();
    readingName[0] = '\0';
  }
  char p[112];
  path(name, p, sizeof(p));
  return SdMan.remove(p);
}

int64_t SdStorage::Cache::fileSize(const char* name) {
  char p[112];
  path(name, p, sizeof(p));
  FsFile f = SdMan.open(p, O_RDONLY);
  if (!f) return -1;
  return static_cast<int64_t>(f.fileSize());
}

int32_t SdStorage::Cache::readAt(const char* name, uint32_t offset, void* dst, uint32_t len) {
  if (!reading || strcmp(name, readingName) != 0) {
    if (reading) reading.close();
    char p[112];
    path(name, p, sizeof(p));
    reading = SdMan.open(p, O_RDONLY);
    if (!reading) {
      readingName[0] = '\0';
      return -1;
    }
    snprintf(readingName, sizeof(readingName), "%s", name);
  }
  if (!reading.seekSet(offset)) return -1;
  return static_cast<int32_t>(reading.read(dst, len));
}

bool SdStorage::Cache::beginWrite(const char* name) {
  // A stale read handle on the file being replaced would see old bytes.
  if (strcmp(name, readingName) == 0) {
    reading.close();
    readingName[0] = '\0';
  }
  char p[112], tmp[120];
  path(name, p, sizeof(p));
  snprintf(tmp, sizeof(tmp), "%s.tmp", p);
  writing = SdMan.open(tmp, O_RDWR | O_CREAT | O_TRUNC);
  if (!writing) return false;
  snprintf(writingName, sizeof(writingName), "%s", name);
  return true;
}

bool SdStorage::Cache::write(const void* data, uint32_t len) {
  return writing && writing.write(static_cast<const uint8_t*>(data), len) == len;
}

bool SdStorage::Cache::endWrite() {
  if (!writing) return false;
  writing.close();
  char p[112], tmp[120];
  path(writingName, p, sizeof(p));
  snprintf(tmp, sizeof(tmp), "%s.tmp", p);
  writingName[0] = '\0';
  SdMan.remove(p);
  return SdMan.rename(tmp, p);
}

int32_t SdStorage::Cache::readBackAt(uint32_t offset, void* dst, uint32_t len) {
  if (!writing) return -1;
  const uint64_t resume = writing.curPosition();
  if (!writing.seekSet(offset)) return -1;
  const int n = writing.read(dst, len);
  writing.seekSet(resume);
  return n;
}
