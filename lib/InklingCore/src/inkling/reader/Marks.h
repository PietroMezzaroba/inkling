#pragma once

#include <cstdint>

#include "inkling/Platform.h"

namespace inkling {

// A place the reader marked in a book: a bookmark (a page) or a highlight (a
// passage, also saved as a clipping). Both are found again after any relayout:
// the page by its chapter character offset, the passage by its words.
struct Mark {
  enum Type : uint8_t { Bookmark = 0, Highlight = 1 };
  uint8_t type = Bookmark;
  uint8_t pct = 0;           // book percentage, for the list
  uint16_t spine = 0;
  uint32_t charStart = 0;    // start of the page it was made on
  uint8_t wordCount = 0;     // highlight: number of words
  uint8_t reserved[3] = {};
  uint32_t firstHash[3] = {};  // highlight: its first words (see BookReader::wordHash)
  uint32_t lastHash = 0;
  char text[44] = {};        // first words, for the list
};

// The marks of the open book, kept in its cache folder.
class BookMarks {
 public:
  static constexpr int kMax = 40;

  void load(Storage& storage, const char* cacheDir);
  void clear() {
    file_.count = 0;
    path_[0] = '\0';
  }
  int count() const { return static_cast<int>(file_.count); }
  const Mark& at(int i) const { return file_.marks[i]; }
  const Mark* data() const { return file_.marks; }

  // Index of the bookmark on the page [charStart, charEnd) of `spine`, or -1.
  int bookmarkOn(uint16_t spine, uint32_t charStart, uint32_t charEnd) const;
  int highlightCount() const;

  // Adds in reading order. False when the list is full.
  bool add(Storage& storage, const Mark& mark);
  void remove(Storage& storage, int index);

 private:
  void save(Storage& storage) const;

  // Laid out as the file is, so saving is one write and needs no copy.
  struct File {
    uint32_t magic = 0x314B524Du;  // "MRK1"
    uint32_t count = 0;
    Mark marks[kMax];
  } file_;
  char path_[72] = {};
};

}  // namespace inkling
