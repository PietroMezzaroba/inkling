#include "Marks.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace inkling {

namespace {
constexpr uint32_t kMarksMagic = 0x314B524Du;  // "MRK1"
}  // namespace

void BookMarks::load(Storage& storage, const char* cacheDir) {
  snprintf(path_, sizeof(path_), "%s/marks.bin", cacheDir);
  const int32_t n = storage.readFileAt(path_, 0, &file_, sizeof(file_));
  const uint32_t header = static_cast<uint32_t>(offsetof(File, marks));
  if (n < static_cast<int32_t>(header) || file_.magic != kMarksMagic || file_.count > static_cast<uint32_t>(kMax) ||
      static_cast<uint32_t>(n) < header + file_.count * sizeof(Mark)) {
    file_.count = 0;
  }
  file_.magic = kMarksMagic;
  for (uint32_t i = 0; i < file_.count; ++i) file_.marks[i].text[sizeof(file_.marks[i].text) - 1] = '\0';
}

void BookMarks::save(Storage& storage) const {
  if (!path_[0]) return;
  storage.writeFile(path_, &file_, static_cast<uint32_t>(offsetof(File, marks) + file_.count * sizeof(Mark)));
}

int BookMarks::bookmarkOn(uint16_t spine, uint32_t charStart, uint32_t charEnd) const {
  for (int i = 0; i < count(); ++i) {
    const Mark& m = file_.marks[i];
    if (m.type == Mark::Bookmark && m.spine == spine && m.charStart >= charStart && m.charStart < charEnd) return i;
  }
  return -1;
}

int BookMarks::highlightCount() const {
  int n = 0;
  for (int i = 0; i < count(); ++i) n += file_.marks[i].type == Mark::Highlight ? 1 : 0;
  return n;
}

bool BookMarks::add(Storage& storage, const Mark& mark) {
  const int n = count();
  if (n >= kMax) return false;
  int at = n;
  for (int i = 0; i < n; ++i) {
    const Mark& m = file_.marks[i];
    if (m.spine > mark.spine || (m.spine == mark.spine && m.charStart > mark.charStart)) {
      at = i;
      break;
    }
  }
  for (int i = n; i > at; --i) file_.marks[i] = file_.marks[i - 1];
  file_.marks[at] = mark;
  ++file_.count;
  save(storage);
  return true;
}

void BookMarks::remove(Storage& storage, int index) {
  const int n = count();
  if (index < 0 || index >= n) return;
  for (int i = index; i + 1 < n; ++i) file_.marks[i] = file_.marks[i + 1];
  --file_.count;
  save(storage);
}

}  // namespace inkling
