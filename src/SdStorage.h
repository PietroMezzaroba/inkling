#pragma once

#include <SDCardManager.h>

#include "inkling/Platform.h"

// inkling::Storage over the SD card (SdFat via the FreeInk SD manager).
class SdStorage final : public inkling::Storage {
 public:
  bool listDir(const char* dir, DirFn fn, void* user) override;
  int32_t readFile(const char* path, void* dst, uint32_t cap) override;
  bool writeFile(const char* path, const void* data, uint32_t len) override;
  bool mkdirs(const char* path) override;
  freeink::book::BookSource* openBook(const char* path) override;
  void closeBook() override;
  freeink::book::CacheStorage* cacheAt(const char* dir) override;

 private:
  class Source final : public freeink::book::BookSource {
   public:
    FsFile file;
    uint64_t bytes = 0;
    int32_t readAt(uint64_t offset, void* dst, uint32_t len) override;
    uint64_t size() const override { return bytes; }
  };

  class Cache final : public freeink::book::CacheStorage {
   public:
    char dir[64] = {};
    bool exists(const char* name) override;
    bool remove(const char* name) override;
    int64_t fileSize(const char* name) override;
    int32_t readAt(const char* name, uint32_t offset, void* dst, uint32_t len) override;
    bool beginWrite(const char* name) override;
    bool write(const void* data, uint32_t len) override;
    bool endWrite() override;
    int32_t readBackAt(uint32_t offset, void* dst, uint32_t len) override;

    void closeAll();

   private:
    void path(const char* name, char* out, size_t cap) const;
    // The page index is read 8 bytes at a time: keep the last file open.
    FsFile reading;
    char readingName[40] = {};
    FsFile writing;
    char writingName[40] = {};
  };

  Source source_;
  Cache cache_;
};
