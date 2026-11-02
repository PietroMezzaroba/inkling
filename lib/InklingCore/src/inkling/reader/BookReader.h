#pragma once

#include <BookArena.h>
#include <BookCatalog.h>
#include <cache/PageCache.h>
#include <layout/ChapterLayout.h>
#include <render/PageRenderer.h>
#include <render/TtfFont.h>

#include <cstddef>
#include <cstdint>

#include "inkling/Platform.h"

namespace inkling {

// Fixed memory for one open book. Firmware and simulator use the same sizes, so
// a book that opens in the simulator fits on the device too.
struct ReaderMemory {
  // Peaks measured on a 5-book corpus (4,279 pages): catalog 16.5 KB,
  // scratch 96.4 KB, index 5.6 KB. The ESP32-C3 heap still has to hold the
  // 52 KB framebuffer and the 16 KB loop stack, so there is no slack to waste.
  static constexpr size_t kCatalog = 24 * 1024;        // resident catalog tables
  static constexpr size_t kScratch = 104 * 1024;       // layout, catalog build, page decode
  static constexpr size_t kIndex = 12 * 1024;          // chapter page index (8 B/page) while writing and reading
  static constexpr size_t kGlyphsRegular = 20 * 1024;  // glyph cache of the body face
  static constexpr size_t kGlyphsItalic = 10 * 1024;   // italic is rarer: a smaller cache that refills more often

  alignas(16) uint8_t catalog[kCatalog];
  alignas(16) uint8_t scratch[kScratch];
  alignas(16) uint8_t index[kIndex];
  alignas(16) uint8_t glyphsRegular[kGlyphsRegular];
  alignas(16) uint8_t glyphsItalic[kGlyphsItalic];
};

// Reading typography; changing any field relayouts into a new cache generation.
struct ReaderLayout {
  int16_t pageWidth = 528;   // logical portrait page
  int16_t pageHeight = 762;  // panel height minus the status line
  uint16_t fontSizePx = 30;
  int16_t marginH = 26;
  int16_t marginTop = 22;
  int16_t marginBottom = 14;
};

// Where the reader is: a chapter and a character offset inside it. The offset
// survives relayouts (font size changes land on the same sentence).
struct ReadingPosition {
  uint16_t spine = 0;
  uint32_t charStart = 0;
};

class BookReader {
 public:
  using BookStatus = freeink::book::BookStatus;

  BookReader(Storage& storage, ReaderMemory& memory);

  // Loads the two reading faces. `data` must stay addressable for the reader's
  // lifetime (flash-mapped on the device).
  bool initFonts(const uint8_t* regular, uint32_t regularLen, const uint8_t* italic, uint32_t italicLen);
  freeink::book::TtfFont& regularFont() { return regular_; }

  // Opens a book (EPUB or TXT), building its catalog on first open, and
  // restores the saved position.
  BookStatus open(const char* path, const ReaderLayout& layout);
  void close();
  bool isOpen() const { return open_; }

  // Lays out the current chapter if needed and draws the current page.
  BookStatus render(const freeink::book::FrameTarget& frame);

  // Page turns. They only move the position; the next render() does the work.
  // Return false at the very start/end of the book.
  bool nextPage();
  bool prevPage();

  const char* title() const { return title_; }
  uint32_t pageInChapter() const { return page_ + 1; }
  uint32_t pagesInChapter() const { return chapter_.pageCount(); }
  uint8_t percent() const;
  bool atEnd() const;
  ReadingPosition position() const { return pos_; }

  // Peak arena use since boot, to size ReaderMemory against real books.
  size_t scratchHighWater() const { return scratch_.highWater(); }
  size_t catalogHighWater() const { return catalogArena_.highWater(); }
  size_t indexHighWater() const { return index_.highWater(); }

 private:
  BookStatus ensureChapter();
  BookStatus layoutChapter(const char* cacheName);
  void saveProgress();
  void loadProgress();

  Storage& storage_;
  ReaderMemory& mem_;
  freeink::book::Arena catalogArena_, scratch_, index_, glyphsRegular_, glyphsItalic_;
  freeink::book::TtfFont regular_, italic_;
  freeink::book::FontChain fonts_;

  freeink::book::BookSource* source_ = nullptr;
  freeink::book::CacheStorage* cache_ = nullptr;
  freeink::book::BookCatalog catalog_;
  freeink::book::PageCacheReader chapter_;
  freeink::book::LayoutParams params_;

  bool open_ = false;
  bool isText_ = false;
  char cacheDir_[48] = {};
  char title_[96] = {};
  uint32_t generation_ = 0;
  uint16_t spineCount_ = 0;
  uint64_t bookBytes_ = 0;  // sum of spine sizes, for the percentage

  ReadingPosition pos_;
  int32_t loadedSpine_ = -1;  // chapter currently in chapter_
  uint32_t page_ = 0;
  // Set when a backwards turn crosses into the previous chapter: land on its
  // last page once it is laid out. `travel_` is the direction for skipping
  // empty chapters.
  bool wantLastPage_ = false;
  int8_t travel_ = 1;
};

}  // namespace inkling
