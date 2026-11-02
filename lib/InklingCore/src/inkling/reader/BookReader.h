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
  // Peaks measured on a 5-book corpus (4,279 pages) with the framebuffer lent
  // as parse arena (setParseBuffer): catalog 16.5 KB, scratch 49.6 KB, index
  // 5.6 KB, parse 49.1 KB of the 52 KB framebuffer. On the ESP32-C3 every KB
  // here is heap the framebuffer, the loop stack and Wi-Fi also need.
  static constexpr size_t kCatalog = 18 * 1024;  // resident catalog tables
  static constexpr size_t kScratch = 52 * 1024;  // layout state, catalog tables, page decode
  static constexpr size_t kIndex = 8 * 1024;     // chapter page index (8 B/page): chapters up to ~1,000 pages

  alignas(16) uint8_t catalog[kCatalog];
  alignas(16) uint8_t scratch[kScratch];
  alignas(16) uint8_t index[kIndex];
};

// Glyph caches live outside ReaderMemory so the platform can size them from
// whatever heap is left: a bigger cache only means fewer re-rasterizations.
struct GlyphCacheSizes {
  static constexpr size_t kRegularMin = 8 * 1024;    // ~5 KB of it is slot tables
  static constexpr size_t kRegularIdeal = 16 * 1024;
  static constexpr size_t kItalic = 6 * 1024;        // optional: without it italic falls back to regular
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
  // Glyph caches are borrowed too (see GlyphCacheSizes); pass a null italic
  // cache to render italic with the regular face.
  bool initFonts(const uint8_t* regular, uint32_t regularLen, const uint8_t* italic, uint32_t italicLen,
                 uint8_t* regularCache, size_t regularCacheLen, uint8_t* italicCache, size_t italicCacheLen);
  freeink::book::TtfFont& regularFont() { return regular_; }

  // Opens a book (EPUB or TXT), building its catalog on first open, and
  // restores the saved position.
  BookStatus open(const char* path, const ReaderLayout& layout);
  void close();
  bool isOpen() const { return open_; }

  // Lends the reader a buffer for streaming parse state (inflate window, XML
  // buffers) during catalog builds and chapter layout. The framebuffer works:
  // its content is not needed while a chapter is being laid out, and render()
  // clears it again before drawing. Without it, parse state comes from scratch.
  void setParseBuffer(uint8_t* buffer, size_t len) {
    parseBuffer_ = buffer;
    parse_.init(buffer, len);
  }

  // Lays out the current chapter if it is not cached yet. render() does this
  // too; calling it first keeps slow work out of the UI frame.
  BookStatus prepare() { return ensureChapter(); }

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
  size_t parseHighWater() const { return parse_.highWater(); }

 private:
  BookStatus ensureChapter();
  BookStatus layoutChapter(const char* cacheName);
  void saveProgress();
  void loadProgress();

  Storage& storage_;
  ReaderMemory& mem_;
  freeink::book::Arena catalogArena_, scratch_, index_, glyphsRegular_, glyphsItalic_;
  freeink::book::Arena parse_;  // borrowed (see setParseBuffer); empty if none
  uint8_t* parseBuffer_ = nullptr;
  bool parseUsed_ = false;      // the parse buffer holds garbage since the last render
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
