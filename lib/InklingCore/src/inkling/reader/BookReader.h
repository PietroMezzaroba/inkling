#pragma once

#include <BookArena.h>
#include <BookCatalog.h>
#include <FreeInkUICore.h>
#include <cache/PageCache.h>
#include <layout/ChapterLayout.h>
#include <render/PageRenderer.h>
#include <render/TtfFont.h>
#include <text/Hyphenator.h>

#include <cstddef>
#include <cstdint>

#include "inkling/Platform.h"

namespace inkling {

class Library;

enum class FontFamily : uint8_t {
  NotoSerif = 0,
  NotoSans = 1,
  Alegreya = 2,
  OpenDyslexic = 3,
  Count = 4,
};

constexpr uint8_t kFontFamilyCount = static_cast<uint8_t>(FontFamily::Count);
const char* fontFamilyName(uint8_t family);

// Fixed memory for one open book. Firmware and simulator use the same sizes, so
// a book that opens in the simulator fits on the device too.
struct ReaderMemory {
  static constexpr size_t kCatalog = 18 * 1024;  // resident catalog tables
  static constexpr size_t kScratch = 52 * 1024;  // layout state, catalog tables, page decode

  alignas(16) uint8_t catalog[kCatalog];
  alignas(16) uint8_t scratch[kScratch];
};

// The chapter page index (8 B/page) is a separate block, set with
// BookReader::setIndexBuffer(), so the platform can take it from what is left
// after the glyph caches instead of growing the big contiguous ReaderMemory
// block (on the X3 that block is the one that decides the glyph cache size).
// 8 KB (~1,000 pages per chapter) was too small: Hacklog's chapter 2 has
// 1,100+ pages at 42 px and failed with IoError.
struct IndexSizes {
  static constexpr size_t kMin = 8 * 1024;     // ~1,000 pages per chapter
  static constexpr size_t kIdeal = 16 * 1024;  // ~2,000 pages per chapter
};

// Glyph caches live outside ReaderMemory so the platform can size them from
// whatever heap is left: a bigger cache only means fewer re-rasterizations.
struct GlyphCacheSizes {
  static constexpr size_t kRegularMin = 8 * 1024;     // ~5 KB of it is slot tables
  static constexpr size_t kRegularIdeal = 24 * 1024;  // device has ~88 KB free with 16 KB
  static constexpr size_t kItalic = 8 * 1024;         // optional: without it italic falls back to regular
};

// Reading typography; changing any field relayouts into a new cache generation.
struct ReaderLayout {
  int16_t pageWidth = 528;   // logical portrait page
  int16_t pageHeight = 754;  // panel height minus the futuristic status HUD
  uint8_t fontFamily = 0;    // 0=NotoSerif, 1=NotoSans, 2=Alegreya, 3=OpenDyslexic
  uint16_t fontSizePx = 42;  // CrossPet-calibrated em-box height (30..66 px)
  uint16_t lineSpacingPct = 100;
  uint16_t paragraphSpacingPct = 100;
  int16_t marginH = 22;
  int16_t marginTop = 20;
  int16_t marginBottom = 14;
  bool justify = true;
  bool focusReading = false;  // Bionic / Focus reading (bolds initial letters of words)
  bool hyphenation = true;    // Syllabic hyphenation (IT / EN)
  bool publisherCss = false;  // Respect embedded EPUB CSS vs enforce user typography
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

  // Required before open(): the chapter page index arena (see IndexSizes).
  void setIndexBuffer(uint8_t* buffer, size_t len);

  bool initFonts(const uint8_t* regular, uint32_t regularLen, const uint8_t* italic, uint32_t italicLen,
                 uint8_t* regularCache, size_t regularCacheLen, uint8_t* italicCache, size_t italicCacheLen);
  void registerFontFamily(uint8_t family, const uint8_t* regular, uint32_t regularLen, const uint8_t* italic,
                          uint32_t italicLen);
  bool selectFontFamily(uint8_t family);
  uint8_t activeFontFamily() const { return activeFamily_; }
  freeink::book::TtfFont& regularFont() { return regular_; }

  BookStatus open(const char* path, const ReaderLayout& layout);
  BookStatus relayout(const ReaderLayout& layout);
  void close();
  bool isOpen() const { return open_; }

  void setParseBuffer(uint8_t* buffer, size_t len) {
    parseBuffer_ = buffer;
    parse_.init(buffer, len);
  }

  BookStatus prepare() { return ensureChapter(); }
  // Draws the current page. The position is NOT written to the card here:
  // call saveProgress() when the panel is idle (it only writes what changed).
  BookStatus render(const freeink::book::FrameTarget& frame);
  void saveProgress(bool forceSummary = false);

  // Paginates the NEXT chapter ahead of time, a page or so per call, so that
  // crossing a chapter boundary is an ordinary page turn. It borrows the same
  // arenas as rendering (and the framebuffer as parse buffer), so the page on
  // the panel must not be needed from the framebuffer afterwards, and any
  // other call on the reader drops unfinished work.
  bool prebuildWanted();
  bool prebuildStep();  // true while more work remains
  void prebuildCancel();
  bool prebuildActive() const { return preSpine_ >= 0; }

  // Page turns. `chapterFlipped` (optional) is set true when advancing across a chapter boundary.
  bool nextPage(bool* chapterFlipped = nullptr);
  bool prevPage();

  const char* title() const { return title_; }
  const char* author() const { return author_; }
  const char* path() const { return path_; }
  void currentChapterTitle(char* out, size_t cap) const;

  size_t tocCount() const;
  int tocEntry(size_t index, char* title, size_t titleCap, uint8_t* depth) const;
  int currentTocIndex() const;
  void jumpToChapter(uint16_t spine);
  void jumpRelativePercent(int deltaPct);

  uint16_t currentSpine() const { return pos_.spine; }
  uint16_t spineCount() const { return spineCount_; }
  uint32_t pageInChapter() const { return page_ + 1; }
  uint32_t pagesInChapter() const { return chapter_.pageCount(); }
  uint8_t percent() const;
  bool atEnd() const;
  ReadingPosition position() const { return pos_; }

  size_t scratchHighWater() const { return scratch_.highWater(); }
  size_t catalogHighWater() const { return catalogArena_.highWater(); }
  size_t indexHighWater() const { return index_.highWater(); }
  size_t parseHighWater() const { return parse_.highWater(); }

  // Ensures every EPUB in `library` has its 64x92 thumbnail (cov_<hash>.bin),
  // lock-screen cover (slp_<hash>.bin), and metadata summary (sum_<hash>.bin).
  void precacheLibrary(Library& library);

  // Renders the book's cover centered inside `box` (with frame & shadow) using
  // the cached 1-bit Atkinson bitmap (or live 1:1 decode when `box` is smaller).
  // Returns true if a real cover was drawn.
  bool drawCoverInBox(freeink::ui::DrawTarget& t, const char* bookPath, freeink::ui::Rect box);

 private:
  struct FontPair {
    const uint8_t* regular = nullptr;
    uint32_t regularLen = 0;
    const uint8_t* italic = nullptr;
    uint32_t italicLen = 0;
  };

  void applyLayout(const ReaderLayout& layout);
  BookStatus ensureChapter();
  BookStatus layoutChapter(const char* cacheName);
  void loadProgress();
  void prebuildEnd(bool keep);
  bool resolveCoverEntry(freeink::book::ZipEntry* out);
  void ensureCoverCache();

  Storage& storage_;
  ReaderMemory& mem_;
  freeink::book::Arena catalogArena_, scratch_, index_, glyphsRegular_, glyphsItalic_;
  freeink::book::Arena parse_;
  uint8_t* parseBuffer_ = nullptr;
  bool parseUsed_ = false;
  uint8_t* regularCacheBuf_ = nullptr;
  size_t regularCacheCap_ = 0;
  uint8_t* italicCacheBuf_ = nullptr;
  size_t italicCacheCap_ = 0;
  FontPair fontFamilies_[kFontFamilyCount] = {};
  uint8_t activeFamily_ = 0xFF;
  freeink::book::TtfFont regular_, italic_;
  freeink::book::FontChain fonts_;
  freeink::book::Hyphenator hyphIt_;
  freeink::book::Hyphenator hyphEn_;

  freeink::book::BookSource* source_ = nullptr;
  freeink::book::CacheStorage* cache_ = nullptr;
  freeink::book::BookCatalog catalog_;
  freeink::book::PageCacheReader chapter_;
  freeink::book::LayoutParams params_;

  bool open_ = false;
  bool isText_ = false;
  char cacheDir_[48] = {};
  char title_[96] = {};
  char author_[48] = {};
  char path_[128] = {};
  uint32_t generation_ = 0;
  uint16_t spineCount_ = 0;
  uint64_t bookBytes_ = 0;

  ReadingPosition pos_;
  int32_t loadedSpine_ = -1;
  uint32_t page_ = 0;
  bool wantLastPage_ = false;
  int8_t travel_ = 1;
  // Background pagination of the next chapter.
  freeink::book::ChapterLayoutSession preSession_;
  freeink::book::PageCacheWriter preWriter_;
  char preName_[48] = {};
  int32_t preSpine_ = -1;        // chapter being built, -1 = none
  int32_t preSettledSpine_ = -1; // chapter that needs no (more) attempts
  int32_t preAbortSpine_ = -1;
  uint8_t preAborts_ = 0;
  size_t preIndexMark_ = 0;
  int32_t lastSavedSpine_ = -1;
  uint32_t lastSavedCharStart_ = 0xFFFFFFFFu;
  uint8_t lastSavedPct_ = 0xFFu;
};

}  // namespace inkling
