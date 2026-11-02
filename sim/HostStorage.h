#pragma once

#include <cstdio>
#include <string>

#include "inkling/Platform.h"

namespace sim {

// inkling::Storage over a host folder that plays the SD card.
class HostStorage final : public inkling::Storage {
 public:
  explicit HostStorage(std::string root) : root_(std::move(root)) {}

  bool listDir(const char* dir, DirFn fn, void* user) override;
  int32_t readFile(const char* path, void* dst, uint32_t cap) override;
  bool writeFile(const char* path, const void* data, uint32_t len) override;
  bool mkdirs(const char* path) override;
  freeink::book::BookSource* openBook(const char* path) override;
  void closeBook() override;
  freeink::book::CacheStorage* cacheAt(const char* dir) override;

  std::string hostPath(const char* path) const { return root_ + path; }

 private:
  class Source final : public freeink::book::BookSource {
   public:
    FILE* file = nullptr;
    uint64_t bytes = 0;
    int32_t readAt(uint64_t offset, void* dst, uint32_t len) override;
    uint64_t size() const override { return bytes; }
  };

  class Cache final : public freeink::book::CacheStorage {
   public:
    std::string dir;
    std::string writeName;
    FILE* writing = nullptr;
    std::string path(const char* name) const { return dir + "/" + name; }
    bool exists(const char* name) override;
    bool remove(const char* name) override;
    int64_t fileSize(const char* name) override;
    int32_t readAt(const char* name, uint32_t offset, void* dst, uint32_t len) override;
    bool beginWrite(const char* name) override;
    bool write(const void* data, uint32_t len) override;
    bool endWrite() override;
    int32_t readBackAt(uint32_t offset, void* dst, uint32_t len) override;
  };

  std::string root_;
  Source source_;
  Cache cache_;
};

}  // namespace sim
