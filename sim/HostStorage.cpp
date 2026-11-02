#include "HostStorage.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>

namespace sim {

bool HostStorage::listDir(const char* dir, DirFn fn, void* user) {
  DIR* d = opendir(hostPath(dir).c_str());
  if (!d) return false;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0'))) continue;
    fn(e->d_name, e->d_type == DT_DIR, user);
  }
  closedir(d);
  return true;
}

int32_t HostStorage::readFile(const char* path, void* dst, uint32_t cap) {
  FILE* f = fopen(hostPath(path).c_str(), "rb");
  if (!f) return -1;
  const size_t n = fread(dst, 1, cap, f);
  fclose(f);
  return static_cast<int32_t>(n);
}

bool HostStorage::writeFile(const char* path, const void* data, uint32_t len) {
  const std::string final = hostPath(path);
  const std::string tmp = final + ".tmp";
  FILE* f = fopen(tmp.c_str(), "wb");
  if (!f) return false;
  const bool ok = fwrite(data, 1, len, f) == len;
  if (fclose(f) != 0 || !ok) return false;
  return rename(tmp.c_str(), final.c_str()) == 0;
}

bool HostStorage::mkdirs(const char* path) {
  std::string p = hostPath(path);
  for (size_t i = root_.size() + 1; i <= p.size(); ++i) {
    if (i == p.size() || p[i] == '/') {
      const std::string part = p.substr(0, i);
      if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
  }
  return true;
}

freeink::book::BookSource* HostStorage::openBook(const char* path) {
  closeBook();
  source_.file = fopen(hostPath(path).c_str(), "rb");
  if (!source_.file) return nullptr;
  fseek(source_.file, 0, SEEK_END);
  source_.bytes = static_cast<uint64_t>(ftell(source_.file));
  return &source_;
}

void HostStorage::closeBook() {
  if (source_.file) fclose(source_.file);
  source_.file = nullptr;
  source_.bytes = 0;
}

freeink::book::CacheStorage* HostStorage::cacheAt(const char* dir) {
  if (!mkdirs(dir)) return nullptr;
  cache_.dir = hostPath(dir);
  return &cache_;
}

int32_t HostStorage::Source::readAt(uint64_t offset, void* dst, uint32_t len) {
  if (fseek(file, static_cast<long>(offset), SEEK_SET) != 0) return -1;
  return static_cast<int32_t>(fread(dst, 1, len, file));
}

bool HostStorage::Cache::exists(const char* name) {
  struct stat st;
  return stat(path(name).c_str(), &st) == 0;
}

bool HostStorage::Cache::remove(const char* name) { return ::remove(path(name).c_str()) == 0; }

int64_t HostStorage::Cache::fileSize(const char* name) {
  struct stat st;
  if (stat(path(name).c_str(), &st) != 0) return -1;
  return st.st_size;
}

int32_t HostStorage::Cache::readAt(const char* name, uint32_t offset, void* dst, uint32_t len) {
  FILE* f = fopen(path(name).c_str(), "rb");
  if (!f) return -1;
  fseek(f, static_cast<long>(offset), SEEK_SET);
  const size_t n = fread(dst, 1, len, f);
  fclose(f);
  return static_cast<int32_t>(n);
}

bool HostStorage::Cache::beginWrite(const char* name) {
  writeName = name;
  writing = fopen((path(name) + ".tmp").c_str(), "w+b");
  return writing != nullptr;
}

bool HostStorage::Cache::write(const void* data, uint32_t len) {
  return writing && fwrite(data, 1, len, writing) == len;
}

bool HostStorage::Cache::endWrite() {
  if (!writing) return false;
  const bool ok = fclose(writing) == 0;
  writing = nullptr;
  const std::string final = path(writeName.c_str());
  return ok && rename((final + ".tmp").c_str(), final.c_str()) == 0;
}

int32_t HostStorage::Cache::readBackAt(uint32_t offset, void* dst, uint32_t len) {
  if (!writing) return -1;
  const long resume = ftell(writing);
  fseek(writing, static_cast<long>(offset), SEEK_SET);
  const size_t n = fread(dst, 1, len, writing);
  fseek(writing, resume, SEEK_SET);
  return static_cast<int32_t>(n);
}

}  // namespace sim
