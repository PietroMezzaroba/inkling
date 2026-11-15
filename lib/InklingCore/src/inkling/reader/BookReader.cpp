#include "BookReader.h"

#include "Library.h"
#include "hyph_it.h"
#include "inkling/text/Nfc.h"
#include "inkling/ui/CyberCanvas.h"
#include <render/ImageRenderer.h>
#include <text/hyph_en_us.h>

#include <cstdio>
#include <cstring>

namespace inkling {

using freeink::book::BookCatalog;
using freeink::book::BookStatus;
using freeink::book::ChapterLayout;
using freeink::book::Page;
using freeink::book::PageCacheWriter;
using freeink::book::PageRenderer;
using freeink::book::ZipEntry;

const char* fontFamilyName(uint8_t family) {
  switch (static_cast<FontFamily>(family)) {
    case FontFamily::NotoSerif:    return "Noto Serif";
    case FontFamily::NotoSans:     return "Noto Sans";
    case FontFamily::Alegreya:     return "Alegreya";
    case FontFamily::OpenDyslexic: return "OpenDyslexic";
    default:                       return "Noto Serif";
  }
}

namespace {

constexpr uint32_t kFontFingerprints[kFontFamilyCount] = {
    0x4E535205u,  // "NSR" v5 (Noto Serif)
    0x4E534E05u,  // "NSN" v5 (Noto Sans)
    0x414C4705u,  // "ALG" v5 (Alegreya)
    0x4F445905u,  // "ODY" v5 (OpenDyslexic)
};
constexpr const char* kCacheRoot = "/.inkling/cache";

uint32_t fnv1a(const char* s, uint32_t h = 2166136261u) {
  for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
  return h;
}

bool endsWithNoCase(const char* s, const char* suffix) {
  const size_t n = strlen(s), m = strlen(suffix);
  if (n < m) return false;
  for (size_t i = 0; i < m; ++i) {
    char a = s[n - m + i], b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

void titleFromPath(const char* path, char* out, size_t cap) {
  const char* base = strrchr(path, '/');
  base = base ? base + 1 : path;
  snprintf(out, cap, "%s", base);
  char* dot = strrchr(out, '.');
  if (dot && dot != out) *dot = '\0';
  composeLatinNfc(out);
}

struct SavedPosition {
  uint32_t magic;
  uint32_t generation;
  uint16_t spine;
  uint16_t reserved;
  uint32_t charStart;
};
constexpr uint32_t kPositionMagic = 0x504B4E49;  // "INKP"

}  // namespace

BookReader::BookReader(Storage& storage, ReaderMemory& memory) : storage_(storage), mem_(memory) {
  catalogArena_.init(mem_.catalog, sizeof(mem_.catalog));
  scratch_.init(mem_.scratch, sizeof(mem_.scratch));
  hyphIt_.init(freeink::book::k_hyph_it, freeink::book::k_hyph_it_size);
  hyphEn_.init(freeink::book::k_hyph_en_us, freeink::book::k_hyph_en_us_size);
}

void BookReader::setIndexBuffer(uint8_t* buffer, size_t len) { index_.init(buffer, len); }

bool BookReader::initFonts(const uint8_t* regular, uint32_t regularLen, const uint8_t* italic, uint32_t italicLen,
                           uint8_t* regularCache, size_t regularCacheLen, uint8_t* italicCache,
                           size_t italicCacheLen) {
  regularCacheBuf_ = regularCache;
  regularCacheCap_ = regularCacheLen;
  italicCacheBuf_ = italicCache;
  italicCacheCap_ = italicCacheLen;
  registerFontFamily(0, regular, regularLen, italic, italicLen);
  return selectFontFamily(0);
}

void BookReader::registerFontFamily(uint8_t family, const uint8_t* regular, uint32_t regularLen,
                                    const uint8_t* italic, uint32_t italicLen) {
  if (family >= kFontFamilyCount) return;
  fontFamilies_[family] = FontPair{regular, regularLen, italic, italicLen};
}

bool BookReader::selectFontFamily(uint8_t family) {
  if (family >= kFontFamilyCount || !fontFamilies_[family].regular || fontFamilies_[family].regularLen == 0) {
    family = 0;
  }
  if (activeFamily_ == family && regular_.ready()) return true;
  const auto& pair = fontFamilies_[family];
  if (!pair.regular || pair.regularLen == 0 || !regularCacheBuf_ || regularCacheCap_ == 0) return false;

  fonts_ = freeink::book::FontChain{};
  glyphsRegular_.init(regularCacheBuf_, regularCacheCap_);
  if (!regular_.init(pair.regular, pair.regularLen, glyphsRegular_)) return false;
  fonts_.add(&regular_, freeink::book::StyleNone);
  if (pair.italic && pair.italicLen > 0 && italicCacheBuf_ && italicCacheCap_ > 0) {
    glyphsItalic_.init(italicCacheBuf_, italicCacheCap_);
    if (italic_.init(pair.italic, pair.italicLen, glyphsItalic_)) {
      fonts_.add(&italic_, freeink::book::StyleItalic);
    }
  }
  activeFamily_ = family;
  return true;
}

void BookReader::applyLayout(const ReaderLayout& layout) {
  const uint8_t fam = layout.fontFamily < kFontFamilyCount ? layout.fontFamily : 0;
  selectFontFamily(fam);

  params_.pageWidth = layout.pageWidth;
  params_.pageHeight = layout.pageHeight;
  params_.marginLeft = layout.marginH;
  params_.marginRight = layout.marginH;
  params_.marginTop = layout.marginTop;
  params_.marginBottom = layout.marginBottom;
  params_.baseSizePx = layout.fontSizePx;
  params_.lineSpacingPct = layout.lineSpacingPct;
  params_.paragraphSpacingPct = layout.paragraphSpacingPct;
  params_.defaultAlign = layout.justify ? freeink::book::TextAlign::Justify : freeink::book::TextAlign::Left;
  params_.focusReading = layout.focusReading;
  params_.embeddedStyles = layout.publisherCss;
  params_.stylesheet = (!isText_ && layout.publisherCss) ? catalog_.stylesheet() : nullptr;
  params_.font = &fonts_;

  if (layout.hyphenation) {
    const char* lang = params_.language ? params_.language : "it";
    const bool isEn = (lang[0] == 'e' || lang[0] == 'E') && (lang[1] == 'n' || lang[1] == 'N');
    params_.hyphenator = isEn ? &hyphEn_ : &hyphIt_;
  } else {
    params_.hyphenator = nullptr;
  }

  uint32_t fp = kFontFingerprints[activeFamily_ < kFontFamilyCount ? activeFamily_ : 0];
  if (layout.hyphenation) fp ^= 0x48595048u;    // "HYPH"
  if (layout.publisherCss) fp ^= 0x43535331u;   // "CSS1"
  generation_ = freeink::book::layoutGenerationHash(params_, fp);
}

BookStatus BookReader::open(const char* path, const ReaderLayout& layout) {
  close();
  preSettledSpine_ = -1;
  preAbortSpine_ = -1;
  preAborts_ = 0;

  source_ = storage_.openBook(path);
  if (!source_) return BookStatus::IoError;
  isText_ = endsWithNoCase(path, ".txt") || endsWithNoCase(path, ".md");

  char sizeTag[24];
  snprintf(sizeTag, sizeof(sizeTag), "#%llu", static_cast<unsigned long long>(source_->size()));
  snprintf(cacheDir_, sizeof(cacheDir_), "%s/%08lx", kCacheRoot,
            static_cast<unsigned long>(fnv1a(sizeTag, fnv1a(path))));
  storage_.mkdirs(cacheDir_);
  cache_ = storage_.cacheAt(cacheDir_);
  if (!cache_) return BookStatus::IoError;

  titleFromPath(path, title_, sizeof(title_));
  Library::parseFilenameTitleAuthor(title_, title_, sizeof(title_), author_, sizeof(author_));
  snprintf(path_, sizeof(path_), "%s", path);
  params_ = freeink::book::LayoutParams{};
  params_.language = "it";

  if (isText_) {
    spineCount_ = 1;
    bookBytes_ = source_->size();
  } else {
    catalogArena_.reset();
    scratch_.reset();
    BookStatus st = catalog_.open(*source_, *cache_, catalogArena_, scratch_);
    if (st != BookStatus::Ok && st != BookStatus::OutOfMemory) {
      INKLING_LOG("catalog: building (%s)", freeink::book::bookStatusName(st));
      scratch_.reset();
      parse_.reset();
      parseUsed_ = parse_.capacity() > 0;
      st = BookCatalog::build(*source_, *cache_, scratch_, parse_.capacity() ? &parse_ : nullptr);
      scratch_.reset();
      if (st != BookStatus::Ok) return st;
      catalogArena_.reset();
      st = catalog_.open(*source_, *cache_, catalogArena_, scratch_);
    }
    scratch_.reset();
    if (st != BookStatus::Ok) return st;

    spineCount_ = static_cast<uint16_t>(catalog_.spineCount());
    bookBytes_ = 0;
    for (uint16_t i = 0; i < spineCount_; ++i) bookBytes_ += catalog_.spineSize(i);
    const auto& meta = catalog_.metadata();
    if (meta.title && meta.title[0]) {
      // If OPF title is a raw hyphenated filename slug, parse it cleanly into Title + Author
      if (strchr(meta.title, ' ') == nullptr && strchr(meta.title, '-') != nullptr) {
        Library::parseFilenameTitleAuthor(meta.title, title_, sizeof(title_), author_, sizeof(author_));
      } else {
        snprintf(title_, sizeof(title_), "%s", meta.title);
      }
      composeLatinNfc(title_);
    }
    if (meta.author && meta.author[0]) {
      snprintf(author_, sizeof(author_), "%s", meta.author);
      composeLatinNfc(author_);
    }
    if (meta.language && meta.language[0]) params_.language = meta.language;

    ensureCoverCache();
  }
  if (spineCount_ == 0) return BookStatus::NotEpub;

  applyLayout(layout);

  open_ = true;
  loadedSpine_ = -1;
  page_ = 0;
  wantLastPage_ = false;
  travel_ = 1;
  lastSavedSpine_ = -1;
  lastSavedCharStart_ = 0xFFFFFFFFu;
  lastSavedPct_ = 0xFFu;
  pos_ = ReadingPosition{};
  loadProgress();
  return BookStatus::Ok;
}

BookStatus BookReader::relayout(const ReaderLayout& layout) {
  if (!open_) return BookStatus::NotFound;
  prebuildCancel();
  const uint32_t prevGen = generation_;
  const uint8_t prevFam = activeFamily_;
  applyLayout(layout);
  if (generation_ != prevGen || activeFamily_ != prevFam) {
    loadedSpine_ = -1;
    wantLastPage_ = false;
    preSettledSpine_ = -1;
  }
  return ensureChapter();
}

void BookReader::close() {
  prebuildCancel();
  if (open_) saveProgress(true);
  if (source_) storage_.closeBook();
  open_ = false;
  source_ = nullptr;
  cache_ = nullptr;
  loadedSpine_ = -1;
}

BookStatus BookReader::layoutChapter(const char* cacheName) {
  INKLING_LOG("layout spine %u", pos_.spine);
  scratch_.reset();
  index_.reset();
  PageCacheWriter writer;
  if (!writer.begin(*cache_, cacheName, generation_, index_)) return BookStatus::IoError;

  static char href[256];
  uint32_t totalChars = 0;
  BookStatus st;
  if (isText_) {
    st = ChapterLayout::layoutPlainText(*source_, params_, scratch_, writer, nullptr, &totalChars);
  } else {
    ZipEntry entry;
    st = catalog_.spineHref(pos_.spine, href, sizeof(href));
    if (st == BookStatus::Ok) st = catalog_.spineEntry(pos_.spine, &entry);
    if (st == BookStatus::Ok) {
      parse_.reset();
      parseUsed_ = parseUsed_ || parse_.capacity() > 0;
      st = ChapterLayout::layout(*source_, catalog_.zip(), entry, href, params_, scratch_, writer, nullptr,
                                 &totalChars, parse_.capacity() ? &parse_ : nullptr);
    }
  }
  writer.setTotalChars(totalChars);
  const bool written = writer.finish();
  scratch_.reset();
  if (st != BookStatus::Ok) return st;
  return written ? BookStatus::Ok : BookStatus::IoError;
}

BookStatus BookReader::ensureChapter() {
  prebuildCancel();
  for (uint16_t guard = 0; guard <= spineCount_; ++guard) {
    if (loadedSpine_ != pos_.spine) {
      char name[48];
      freeink::book::pageCacheName(pos_.spine, generation_, name, sizeof(name));
      index_.reset();
      BookStatus st = chapter_.open(*cache_, name, generation_, index_);
      if (st != BookStatus::Ok) {
        st = layoutChapter(name);
        if (st != BookStatus::Ok) {
          INKLING_LOG("layout spine %u: %s", pos_.spine, freeink::book::bookStatusName(st));
          return st;
        }
        index_.reset();
        st = chapter_.open(*cache_, name, generation_, index_);
        if (st != BookStatus::Ok) {
          INKLING_LOG("reopen spine %u after layout: %s", pos_.spine, freeink::book::bookStatusName(st));
          return st;
        }
      }
      loadedSpine_ = pos_.spine;
      const uint32_t count = chapter_.pageCount();
      if (wantLastPage_) {
        page_ = count ? count - 1 : 0;
      } else {
        page_ = chapter_.pageForChar(pos_.charStart);
      }
      wantLastPage_ = false;
    }
    if (chapter_.pageCount() > 0) return BookStatus::Ok;

    if (travel_ > 0 && pos_.spine + 1 < spineCount_) {
      pos_.spine++;
      pos_.charStart = 0;
    } else if (travel_ < 0 && pos_.spine > 0) {
      pos_.spine--;
      wantLastPage_ = true;
    } else {
      return BookStatus::Ok;
    }
  }
  return BookStatus::Ok;
}

BookStatus BookReader::render(const freeink::book::FrameTarget& frame) {
  if (!open_) return BookStatus::NotFound;
  BookStatus st = ensureChapter();
  if (parseUsed_ && frame.framebuffer == parseBuffer_) {
    memset(frame.framebuffer, 0xFF, static_cast<size_t>(frame.widthBytes) * frame.height);
  }
  parseUsed_ = false;
  if (st != BookStatus::Ok) return st;
  const uint32_t count = chapter_.pageCount();
  if (count == 0) return BookStatus::Ok;
  if (page_ >= count) page_ = count - 1;

  const size_t mark = scratch_.mark();
  Page page;
  st = chapter_.readPage(page_, scratch_, &page);
  if (st == BookStatus::Ok) {
    pos_.charStart = page.charStart;
    if (isText_) {
      PageRenderer::renderText(page, fonts_, frame);
    } else {
      st = PageRenderer::render(page, fonts_, *source_, catalog_.zip(), scratch_, frame);
    }
  }
  scratch_.release(mark);
  if (st != BookStatus::Ok) {
    INKLING_LOG("render spine %u page %u/%u: %s", pos_.spine, static_cast<unsigned>(page_),
                static_cast<unsigned>(count), freeink::book::bookStatusName(st));
  }
  return st;
}

// ---------------------------------------------------------------------------
// Background pagination of the next chapter
// ---------------------------------------------------------------------------

namespace {
// A chapter this big does not finish between two page turns: leave it to the
// ordinary "layout when you get there" path.
constexpr uint32_t kPrebuildMaxBytes = 400u * 1024u;
}  // namespace

bool BookReader::prebuildWanted() {
  if (preSpine_ >= 0) return true;
  if (!open_ || isText_ || !cache_ || loadedSpine_ != pos_.spine) return false;
  const int32_t next = static_cast<int32_t>(pos_.spine) + 1;
  if (next >= static_cast<int32_t>(spineCount_) || preSettledSpine_ == next) return false;
  char name[48];
  freeink::book::pageCacheName(static_cast<uint16_t>(next), generation_, name, sizeof(name));
  if (cache_->exists(name) || catalog_.spineSize(static_cast<size_t>(next)) > kPrebuildMaxBytes) {
    preSettledSpine_ = next;
    return false;
  }
  return true;
}

bool BookReader::prebuildStep() {
  if (preSpine_ < 0) {
    if (!prebuildWanted()) return false;
    const uint16_t next = static_cast<uint16_t>(pos_.spine + 1);
    freeink::book::pageCacheName(next, generation_, preName_, sizeof(preName_));
    static char href[256];
    ZipEntry entry;
    BookStatus st = catalog_.spineHref(next, href, sizeof(href));
    if (st == BookStatus::Ok) st = catalog_.spineEntry(next, &entry);
    if (st != BookStatus::Ok) {
      preSettledSpine_ = next;
      return false;
    }
    INKLING_LOG("prebuild spine %u", next);
    // The open chapter keeps its index at the bottom of index_; the writer's
    // goes above it and is released when the build ends.
    preIndexMark_ = index_.mark();
    scratch_.reset();
    parse_.reset();
    parseUsed_ = parseUsed_ || parse_.capacity() > 0;
    preSpine_ = next;
    if (!preWriter_.begin(*cache_, preName_, generation_, index_)) {
      prebuildEnd(false);
      preSettledSpine_ = next;
      return false;
    }
    st = preSession_.begin(*source_, &catalog_.zip(), *source_, entry, href, params_, scratch_, preWriter_,
                           parse_.capacity() ? &parse_ : nullptr);
    if (st != BookStatus::Ok) {
      prebuildEnd(false);
      preSettledSpine_ = next;
      return false;
    }
    return true;
  }

  const BookStatus st = preSession_.step(1);
  if (st != BookStatus::Ok || preWriter_.failed()) {
    INKLING_LOG("prebuild spine %d failed: %s", static_cast<int>(preSpine_), freeink::book::bookStatusName(st));
    const int32_t spine = preSpine_;
    prebuildEnd(false);
    preSettledSpine_ = spine;  // the foreground path will report the error, if any
    return false;
  }
  if (!preSession_.done()) return true;
  const int32_t spine = preSpine_;
  prebuildEnd(true);
  preSettledSpine_ = spine;
  return false;
}

void BookReader::prebuildCancel() {
  if (preSpine_ < 0) return;
  // Interrupted by the reader: retry at the next pause, but not forever.
  if (preAbortSpine_ != preSpine_) {
    preAbortSpine_ = preSpine_;
    preAborts_ = 0;
  }
  if (++preAborts_ >= 2) preSettledSpine_ = preSpine_;
  prebuildEnd(false);
}

void BookReader::prebuildEnd(bool keep) {
  if (preSpine_ < 0) return;
  bool written = false;
  if (keep) {
    preWriter_.setTotalChars(preSession_.totalChars());
    written = preWriter_.finish();
    INKLING_LOG("prebuild spine %d: %u pages%s", static_cast<int>(preSpine_),
                static_cast<unsigned>(preWriter_.pageCount()), written ? "" : " (write failed)");
  } else {
    // finish() is the only way to close the writer's stream; the result is
    // then removed so a half chapter is never mistaken for a whole one.
    preWriter_.finish();
  }
  preSession_.abort();
  if (!written && cache_) cache_->remove(preName_);
  scratch_.reset();
  parse_.reset();
  index_.release(preIndexMark_);
  preSpine_ = -1;
}

bool BookReader::nextPage(bool* chapterFlipped) {
  if (chapterFlipped) *chapterFlipped = false;
  if (!open_) return false;
  travel_ = 1;
  if (loadedSpine_ == pos_.spine && page_ + 1 < chapter_.pageCount()) {
    page_++;
    pos_.charStart = chapter_.charStart(page_);
    return true;
  }
  if (pos_.spine + 1 < spineCount_) {
    pos_.spine++;
    pos_.charStart = 0;
    wantLastPage_ = false;
    if (chapterFlipped) *chapterFlipped = true;
    return true;
  }
  return false;
}

bool BookReader::prevPage() {
  if (!open_) return false;
  travel_ = -1;
  if (loadedSpine_ == pos_.spine && page_ > 0) {
    page_--;
    pos_.charStart = chapter_.charStart(page_);
    return true;
  }
  if (pos_.spine > 0) {
    pos_.spine--;
    wantLastPage_ = true;
    return true;
  }
  return false;
}

void BookReader::currentChapterTitle(char* out, size_t cap) const {
  if (!out || cap == 0) return;
  out[0] = '\0';
  if (!open_) return;
  const int idx = currentTocIndex();
  if (idx >= 0) {
    tocEntry(static_cast<size_t>(idx), out, cap, nullptr);
  }
  if (!out[0]) {
    snprintf(out, cap, "Capitolo %u/%u", static_cast<unsigned>(pos_.spine + 1), static_cast<unsigned>(spineCount_));
  }
}

size_t BookReader::tocCount() const { return open_ && !isText_ ? catalog_.tocCount() : 0; }

int BookReader::tocEntry(size_t index, char* title, size_t titleCap, uint8_t* depth) const {
  if (index >= tocCount()) return -1;
  BookCatalog::TocItem item;
  char fragment[8];
  if (catalog_.tocItem(index, &item, title, titleCap, fragment, sizeof(fragment)) != BookStatus::Ok) return -1;
  if (title && titleCap > 0 && strchr(title, ' ') == nullptr && strchr(title, '-') != nullptr) {
    char dummyAuthor[36];
    Library::parseFilenameTitleAuthor(title, title, static_cast<uint32_t>(titleCap), dummyAuthor, sizeof(dummyAuthor));
  }
  if (depth) *depth = item.depth;
  return item.spineIndex;
}

int BookReader::currentTocIndex() const {
  return open_ && !isText_ ? catalog_.tocIndexForSpine(pos_.spine) : -1;
}

void BookReader::jumpToChapter(uint16_t spine) {
  if (!open_ || spine >= spineCount_) return;
  pos_.spine = spine;
  pos_.charStart = 0;
  wantLastPage_ = false;
  travel_ = 1;
  if (loadedSpine_ == spine) page_ = 0;
}

void BookReader::jumpRelativePercent(int deltaPct) {
  if (!open_) return;
  if (isText_ || spineCount_ <= 1) {
    const uint32_t total = chapter_.pageCount();
    if (total == 0) return;
    int32_t step = static_cast<int32_t>((static_cast<int64_t>(total) * deltaPct) / 100);
    if (step == 0) step = (deltaPct > 0) ? 1 : -1;
    int32_t target = static_cast<int32_t>(page_) + step;
    if (target < 0) target = 0;
    if (target >= static_cast<int32_t>(total)) target = static_cast<int32_t>(total - 1);
    page_ = static_cast<uint32_t>(target);
    pos_.charStart = chapter_.charStart(page_);
    return;
  }

  int targetPct = static_cast<int>(percent()) + deltaPct;
  if (targetPct < 0) targetPct = 0;
  if (targetPct > 99) targetPct = 99;
  const uint64_t targetBytes = (bookBytes_ * static_cast<uint64_t>(targetPct)) / 100u;
  uint64_t acc = 0;
  uint16_t chosen = 0;
  for (uint16_t i = 0; i < spineCount_; ++i) {
    const uint32_t sz = catalog_.spineSize(i);
    if (acc + sz > targetBytes) {
      chosen = i;
      break;
    }
    acc += sz;
    chosen = i;
  }
  jumpToChapter(chosen);
}

uint8_t BookReader::percent() const {
  if (!open_ || bookBytes_ == 0) return 0;
  const uint32_t count = chapter_.pageCount();
  const double chapterFrac = (loadedSpine_ == pos_.spine && count) ? static_cast<double>(page_ + 1) / count : 0.0;
  if (isText_) return static_cast<uint8_t>(chapterFrac * 100.0);
  uint64_t before = 0;
  for (uint16_t i = 0; i < pos_.spine; ++i) before += catalog_.spineSize(i);
  const double done = static_cast<double>(before) + chapterFrac * catalog_.spineSize(pos_.spine);
  const double pct = done * 100.0 / static_cast<double>(bookBytes_);
  return static_cast<uint8_t>(pct > 100.0 ? 100.0 : pct);
}

bool BookReader::atEnd() const {
  return open_ && loadedSpine_ == spineCount_ - 1 && page_ + 1 >= chapter_.pageCount();
}

void BookReader::saveProgress(bool forceSummary) {
  if (!open_) return;
  const bool posChanged =
      (static_cast<int32_t>(pos_.spine) != lastSavedSpine_) || (pos_.charStart != lastSavedCharStart_);
  if (posChanged || forceSummary) {
    char path[64];
    snprintf(path, sizeof(path), "%s/pos.bin", cacheDir_);
    const SavedPosition saved{kPositionMagic, generation_, pos_.spine, 0, pos_.charStart};
    storage_.writeFile(path, &saved, sizeof(saved));
    lastSavedSpine_ = static_cast<int32_t>(pos_.spine);
    lastSavedCharStart_ = pos_.charStart;
  }

  // Persist the summary record only when percentage changes or on book open/close to minimize SD SPI latency.
  const uint8_t pct = percent();
  if (forceSummary || pct != lastSavedPct_) {
    BookSummaryRecord sum{};
    sum.progressPct = pct;
    sum.started = 1;
    sum.spine = pos_.spine;
    sum.spineCount = spineCount_;
    snprintf(sum.title, sizeof(sum.title), "%s", title_);
    snprintf(sum.author, sizeof(sum.author), "%s", author_);
    char sumPath[64];
    Library::summaryPathFor(path_, sumPath, sizeof(sumPath));
    storage_.writeFile(sumPath, &sum, sizeof(sum));
    lastSavedPct_ = pct;
  }
}

void BookReader::loadProgress() {
  char path[64];
  snprintf(path, sizeof(path), "%s/pos.bin", cacheDir_);
  SavedPosition saved{};
  if (storage_.readFile(path, &saved, sizeof(saved)) != static_cast<int32_t>(sizeof(saved))) return;
  if (saved.magic != kPositionMagic || saved.spine >= spineCount_) return;
  pos_.spine = saved.spine;
  pos_.charStart = saved.charStart;
}

namespace {

constexpr uint32_t kSleepCoverMagic = 0x56434B4Au;  // "IKCV" v2
constexpr uint16_t kSleepCoverMaxW = 440;
constexpr uint16_t kSleepCoverMaxH = 540;

struct SleepCoverHeader {
  uint32_t magic;
  uint16_t width;
  uint16_t height;
};

struct ThumbDitherSink {
  uint8_t* dst;  // kCoverBytes (736 bytes, 64x92, 8 bytes/row)
  uint16_t offsetX;
  uint16_t offsetY;
  int16_t err0[ui::canvas::kCoverW + 4] = {};
  int16_t err1[ui::canvas::kCoverW + 4] = {};
  int16_t err2[ui::canvas::kCoverW + 4] = {};

  static bool onRow(void* user, uint16_t y, const uint8_t* gray, uint16_t width) {
    auto* self = static_cast<ThumbDitherSink*>(user);
    const uint16_t dy = static_cast<uint16_t>(self->offsetY + y);
    if (dy >= ui::canvas::kCoverH) return false;
    uint8_t rowBits[ui::canvas::kCoverW / 8] = {};
    if (width > ui::canvas::kCoverW) width = ui::canvas::kCoverW;
    freeink::book::ditherRowAtkinson(gray, width, self->err0, self->err1, self->err2, rowBits);
    uint8_t* dstRow = self->dst + dy * (ui::canvas::kCoverW / 8);
    for (uint16_t x = 0; x < width; ++x) {
      const uint16_t dx = static_cast<uint16_t>(self->offsetX + x);
      if (dx < ui::canvas::kCoverW && (rowBits[x >> 3] & (0x80u >> (x & 7)))) {
        dstRow[dx >> 3] |= static_cast<uint8_t>(0x80u >> (dx & 7));
      }
    }
    return true;
  }
};

struct StreamCoverSink {
  freeink::book::CacheStorage* cache;
  int16_t* err0;
  int16_t* err1;
  int16_t* err2;
  uint16_t maxW;

  static bool onRow(void* user, uint16_t, const uint8_t* gray, uint16_t width) {
    auto* self = static_cast<StreamCoverSink*>(user);
    if (width > self->maxW) width = self->maxW;
    uint8_t rowBits[64] = {};
    freeink::book::ditherRowAtkinson(gray, width, self->err0, self->err1, self->err2, rowBits);
    const uint32_t rowBytes = (width + 7u) / 8u;
    return self->cache->write(rowBits, rowBytes);
  }
};

struct DirectTargetSink {
  freeink::ui::DrawTarget* target;
  int16_t baseX;
  int16_t baseY;
  int16_t* err0;
  int16_t* err1;
  int16_t* err2;
  uint16_t maxW;

  static bool onRow(void* user, uint16_t y, const uint8_t* gray, uint16_t width) {
    auto* self = static_cast<DirectTargetSink*>(user);
    if (width > self->maxW) width = self->maxW;
    uint8_t rowBits[64] = {};
    freeink::book::ditherRowAtkinson(gray, width, self->err0, self->err1, self->err2, rowBits);
    freeink::ui::BitmapRef ref{};
    ref.data = rowBits;
    ref.width = width;
    ref.height = 1;
    ref.format = freeink::ui::BitmapFormat::BW1;
    self->target->bitmap(
        freeink::ui::Rect{self->baseX, static_cast<int16_t>(self->baseY + y), static_cast<int16_t>(width), 1},
        ref, freeink::ui::BitmapMode::Center, freeink::ui::Paint::solid(freeink::ui::Color::Black));
    return true;
  }
};

}  // namespace

bool BookReader::resolveCoverEntry(ZipEntry* out) {
  if (!source_ || isText_ || !catalog_.isOpen()) return false;
  scratch_.reset();
  const bool found = catalog_.findCoverOrFirstImage(*source_, scratch_, out);
  scratch_.reset();
  return found;
}

void BookReader::ensureCoverCache() {
  if (!source_ || isText_ || !catalog_.isOpen()) return;

  char thumbPath[64];
  Library::coverThumbPathFor(path_, thumbPath, sizeof(thumbPath));
  char sleepName[40];
  Library::coverSleepNameFor(path_, sleepName, sizeof(sleepName));

  uint8_t probeByte = 0;
  const bool haveThumb = storage_.readFile(thumbPath, &probeByte, 1) > 0;
  freeink::book::CacheStorage* inkCache = storage_.cacheAt("/.inkling");
  SleepCoverHeader existingHdr{};
  const bool haveSleep =
      inkCache &&
      inkCache->readAt(sleepName, 0, &existingHdr, sizeof(existingHdr)) ==
          static_cast<int32_t>(sizeof(existingHdr)) &&
      existingHdr.magic == kSleepCoverMagic && existingHdr.width > 0 && existingHdr.height > 0;

  // Restore book's own cache directory before touching catalog_ (which shares
  // the platform's CacheStorage singleton)!
  cache_ = storage_.cacheAt(cacheDir_);
  if (haveThumb && haveSleep) {
    return;
  }

  ZipEntry coverZip{};
  if (!resolveCoverEntry(&coverZip)) {
    return;
  }

  if (!haveThumb) {
    uint8_t thumbBits[ui::canvas::kCoverBytes] = {};
    ThumbDitherSink sink{};
    sink.dst = thumbBits;
    scratch_.reset();
    uint16_t outW = ui::canvas::kCoverW;
    uint16_t outH = ui::canvas::kCoverH;
    const BookStatus st = freeink::book::ImageRenderer::renderEntry(
        *source_, coverZip, ui::canvas::kCoverW, ui::canvas::kCoverH, scratch_,
        ThumbDitherSink::onRow, &sink, &outW, &outH, /*stretchToBox=*/true);
    scratch_.reset();
    if (st == BookStatus::Ok) {
      storage_.writeFile(thumbPath, thumbBits, sizeof(thumbBits));
    }
  }

  if (!haveSleep) {
    inkCache = storage_.cacheAt("/.inkling");
    scratch_.reset();
    const size_t errCap = kSleepCoverMaxW + 4u;
    int16_t* err0 = scratch_.allocArray<int16_t>(errCap);
    int16_t* err1 = scratch_.allocArray<int16_t>(errCap);
    int16_t* err2 = scratch_.allocArray<int16_t>(errCap);
    if (inkCache && err0 && err1 && err2) {
      uint16_t outW = 0, outH = 0;
      struct ProbeOnly {
        static bool stopRow(void*, uint16_t, const uint8_t*, uint16_t) { return false; }
      };
      freeink::book::ImageRenderer::renderEntry(*source_, coverZip, kSleepCoverMaxW,
                                                kSleepCoverMaxH, scratch_, ProbeOnly::stopRow,
                                                nullptr, &outW, &outH, false);
      if (outW > 0 && outH > 0 && inkCache->beginWrite(sleepName)) {
        const SleepCoverHeader hdr{kSleepCoverMagic, outW, outH};
        inkCache->write(&hdr, sizeof(hdr));
        memset(err0, 0, errCap * sizeof(int16_t));
        memset(err1, 0, errCap * sizeof(int16_t));
        memset(err2, 0, errCap * sizeof(int16_t));
        StreamCoverSink sink{inkCache, err0, err1, err2, kSleepCoverMaxW};
        const BookStatus st = freeink::book::ImageRenderer::renderEntry(
            *source_, coverZip, kSleepCoverMaxW, kSleepCoverMaxH, scratch_,
            StreamCoverSink::onRow, &sink, &outW, &outH, false);
        if (!inkCache->endWrite() || st != BookStatus::Ok) {
          inkCache->remove(sleepName);
        }
      }
    }
    scratch_.reset();
  }

  // Restore cache_ pointer to the book's own cache directory!
  cache_ = storage_.cacheAt(cacheDir_);
}

void BookReader::precacheLibrary(Library& library) {
  if (open_) return;
  storage_.mkdirs("/.inkling");
  bool updatedSummaries = false;
  ReaderLayout defaultLayout;
  for (int i = 0; i < library.count(); ++i) {
    const auto& entry = library.at(i);
    if (endsWithNoCase(entry.path, ".txt") || endsWithNoCase(entry.path, ".md")) continue;

    char thumbPath[64];
    Library::coverThumbPathFor(entry.path, thumbPath, sizeof(thumbPath));
    char sumPath[64];
    Library::summaryPathFor(entry.path, sumPath, sizeof(sumPath));
    uint8_t dummy = 0;
    const bool hasThumb = storage_.readFile(thumbPath, &dummy, 1) > 0;
    const bool hasSum = storage_.readFile(sumPath, &dummy, 1) > 0;
    if (hasThumb && hasSum) continue;

    if (open(entry.path, defaultLayout) == BookStatus::Ok) {
      if (!hasSum) {
        BookSummaryRecord sum{};
        sum.progressPct = 0;
        sum.started = 0;
        sum.spine = 0;
        sum.spineCount = spineCount_;
        snprintf(sum.title, sizeof(sum.title), "%s", title_);
        snprintf(sum.author, sizeof(sum.author), "%s", author_);
        storage_.writeFile(sumPath, &sum, sizeof(sum));
        updatedSummaries = true;
      }
      // Close without marking `started = 1` if it wasn't started before!
      if (source_) storage_.closeBook();
      open_ = false;
      source_ = nullptr;
      cache_ = nullptr;
      loadedSpine_ = -1;
    }
  }
  if (updatedSummaries) {
    library.refreshSummaries(storage_);
  }
}

bool BookReader::drawCoverInBox(freeink::ui::DrawTarget& t, const char* bookPath,
                                freeink::ui::Rect box) {
  if (!bookPath || !bookPath[0] || box.width < 32 || box.height < 32) return false;
  if (endsWithNoCase(bookPath, ".txt") || endsWithNoCase(bookPath, ".md")) return false;

  char sleepName[40];
  Library::coverSleepNameFor(bookPath, sleepName, sizeof(sleepName));
  freeink::book::CacheStorage* inkCache = storage_.cacheAt("/.inkling");
  SleepCoverHeader hdr{};
  const bool cachedOk =
      inkCache &&
      inkCache->readAt(sleepName, 0, &hdr, sizeof(hdr)) == static_cast<int32_t>(sizeof(hdr)) &&
      hdr.magic == kSleepCoverMagic && hdr.width > 0 && hdr.height > 0 &&
      hdr.width <= kSleepCoverMaxW && hdr.height <= kSleepCoverMaxH;

  // Case 1: The cached 1:1 Atkinson bitmap fits directly inside `box` (Portrait lock screen).
  if (cachedOk && hdr.width <= box.width - 8 && hdr.height <= box.height - 8) {
    const int16_t drawW = static_cast<int16_t>(hdr.width);
    const int16_t drawH = static_cast<int16_t>(hdr.height);
    const int16_t drawX = static_cast<int16_t>(box.x + (box.width - drawW) / 2);
    const int16_t drawY = static_cast<int16_t>(box.y + (box.height - drawH) / 2);

    const freeink::ui::Rect frameRect{static_cast<int16_t>(drawX - 4), static_cast<int16_t>(drawY - 4),
                                      static_cast<int16_t>(drawW + 8), static_cast<int16_t>(drawH + 8)};
    ui::canvas::fastFill(
        t, freeink::ui::Rect{static_cast<int16_t>(frameRect.x + 5), static_cast<int16_t>(frameRect.y + 5),
                             frameRect.width, frameRect.height},
        freeink::ui::Paint::solid(freeink::ui::Color::Black), 6);
    ui::canvas::fastFill(t, frameRect, freeink::ui::Paint::solid(freeink::ui::Color::White), 6);

    const uint32_t rowBytes = (hdr.width + 7u) / 8u;
    constexpr uint16_t kChunkRows = 32;
    uint8_t chunk[kChunkRows * 64];
    uint32_t fileOff = sizeof(SleepCoverHeader);
    for (uint16_t ry = 0; ry < hdr.height; ry += kChunkRows) {
      const uint16_t rows = (hdr.height - ry < kChunkRows) ? (hdr.height - ry) : kChunkRows;
      const uint32_t bytes = rows * rowBytes;
      if (inkCache->readAt(sleepName, fileOff, chunk, bytes) != static_cast<int32_t>(bytes)) break;
      fileOff += bytes;
      freeink::ui::BitmapRef ref{};
      ref.data = chunk;
      ref.width = hdr.width;
      ref.height = rows;
      ref.format = freeink::ui::BitmapFormat::BW1;
      t.bitmap(freeink::ui::Rect{drawX, static_cast<int16_t>(drawY + ry), drawW, static_cast<int16_t>(rows)},
               ref, freeink::ui::BitmapMode::Center,
               freeink::ui::Paint::solid(freeink::ui::Color::Black));
    }
    ui::canvas::fastStroke(t, frameRect, freeink::ui::Paint::solid(freeink::ui::Color::Black), 2, 6);
    if (open_) cache_ = storage_.cacheAt(cacheDir_);
    return true;
  }

  // Case 2: Live 1:1 decode fitted to `box` (e.g. Landscape lock screen where box is smaller).
  const bool wasOpen = open_;
  if (!wasOpen) {
    ReaderLayout layout;
    if (open(bookPath, layout) != BookStatus::Ok) return false;
  } else {
    cache_ = storage_.cacheAt(cacheDir_);
  }
  ZipEntry coverZip{};
  if (!resolveCoverEntry(&coverZip)) {
    if (!wasOpen) close();
    return false;
  }

  const uint16_t maxW = static_cast<uint16_t>(box.width - 12);
  const uint16_t maxH = static_cast<uint16_t>(box.height - 12);
  uint16_t outW = 0, outH = 0;
  struct ProbeOnly {
    static bool stopRow(void*, uint16_t, const uint8_t*, uint16_t) { return false; }
  };
  scratch_.reset();
  freeink::book::ImageRenderer::renderEntry(*source_, coverZip, maxW, maxH, scratch_,
                                            ProbeOnly::stopRow, nullptr, &outW, &outH, false);
  scratch_.reset();
  if (outW == 0 || outH == 0) {
    if (!wasOpen) close();
    return false;
  }

  const int16_t drawX = static_cast<int16_t>(box.x + (box.width - outW) / 2);
  const int16_t drawY = static_cast<int16_t>(box.y + (box.height - outH) / 2);
  const freeink::ui::Rect frameRect{static_cast<int16_t>(drawX - 4), static_cast<int16_t>(drawY - 4),
                                    static_cast<int16_t>(outW + 8), static_cast<int16_t>(outH + 8)};
  ui::canvas::fastFill(
      t, freeink::ui::Rect{static_cast<int16_t>(frameRect.x + 5), static_cast<int16_t>(frameRect.y + 5),
                           frameRect.width, frameRect.height},
      freeink::ui::Paint::solid(freeink::ui::Color::Black), 6);
  ui::canvas::fastFill(t, frameRect, freeink::ui::Paint::solid(freeink::ui::Color::White), 6);

  const size_t errCap = outW + 4u;
  int16_t* err0 = scratch_.allocArray<int16_t>(errCap);
  int16_t* err1 = scratch_.allocArray<int16_t>(errCap);
  int16_t* err2 = scratch_.allocArray<int16_t>(errCap);
  bool ok = false;
  if (err0 && err1 && err2) {
    memset(err0, 0, errCap * sizeof(int16_t));
    memset(err1, 0, errCap * sizeof(int16_t));
    memset(err2, 0, errCap * sizeof(int16_t));
    DirectTargetSink sink{&t, drawX, drawY, err0, err1, err2, outW};
    ok = freeink::book::ImageRenderer::renderEntry(*source_, coverZip, maxW, maxH, scratch_,
                                                   DirectTargetSink::onRow, &sink, nullptr,
                                                   nullptr, false) == BookStatus::Ok;
  }
  scratch_.reset();
  ui::canvas::fastStroke(t, frameRect, freeink::ui::Paint::solid(freeink::ui::Color::Black), 2, 6);
  if (!wasOpen) {
    if (source_) storage_.closeBook();
    open_ = false;
    source_ = nullptr;
    cache_ = nullptr;
    loadedSpine_ = -1;
  } else {
    cache_ = storage_.cacheAt(cacheDir_);
  }
  return ok;
}

// ---------------------------------------------------------------------------
// Standalone image files (lock-screen pictures)
// ---------------------------------------------------------------------------

namespace {

// Atkinson-dithers grayscale rows straight onto the target, up to 800 px wide.
struct WideTargetSink {
  freeink::ui::DrawTarget* target;
  int16_t baseX;
  int16_t baseY;
  int16_t* err0;
  int16_t* err1;
  int16_t* err2;
  uint16_t maxW;

  static bool onRow(void* user, uint16_t y, const uint8_t* gray, uint16_t width) {
    auto* self = static_cast<WideTargetSink*>(user);
    if (width > self->maxW) width = self->maxW;
    uint8_t rowBits[100] = {};
    if (width > sizeof(rowBits) * 8) width = sizeof(rowBits) * 8;
    freeink::book::ditherRowAtkinson(gray, width, self->err0, self->err1, self->err2, rowBits);
    freeink::ui::BitmapRef ref{};
    ref.data = rowBits;
    ref.width = width;
    ref.height = 1;
    ref.format = freeink::ui::BitmapFormat::BW1;
    self->target->bitmap(
        freeink::ui::Rect{self->baseX, static_cast<int16_t>(self->baseY + y), static_cast<int16_t>(width), 1},
        ref, freeink::ui::BitmapMode::Center, freeink::ui::Paint::solid(freeink::ui::Color::Black));
    return true;
  }
};

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

}  // namespace

bool BookReader::drawImageFile(freeink::ui::DrawTarget& t, const char* path, freeink::ui::Rect box) {
  if (open_ || !path || !path[0] || box.width < 16 || box.height < 16) return false;
  const uint16_t maxW = static_cast<uint16_t>(box.width > 800 ? 800 : box.width);
  const uint16_t maxH = static_cast<uint16_t>(box.height);
  scratch_.reset();
  bool ok = false;

  if (endsWithNoCase(path, ".bmp")) {
    // Uncompressed BMP, 1/8/24/32 bits per pixel, either row order.
    uint8_t head[54];
    if (storage_.readFileAt(path, 0, head, sizeof(head)) != static_cast<int32_t>(sizeof(head)) || head[0] != 'B' ||
        head[1] != 'M') {
      return false;
    }
    const uint32_t dataOffset = le32(head + 10);
    const uint32_t dibSize = le32(head + 14);
    const int32_t srcW = static_cast<int32_t>(le32(head + 18));
    const int32_t rawH = static_cast<int32_t>(le32(head + 22));
    const uint16_t bpp = le16(head + 28);
    const uint32_t compression = le32(head + 30);
    const int32_t srcH = rawH < 0 ? -rawH : rawH;
    const bool bottomUp = rawH > 0;
    if (srcW <= 0 || srcH <= 0 || srcW > 2000 || srcH > 4000) return false;
    if (!(bpp == 1 || bpp == 8 || bpp == 24 || bpp == 32) || (compression != 0 && compression != 3)) return false;

    int32_t outW = srcW, outH = srcH;
    if (outW > maxW) {
      outH = outH * maxW / outW;
      outW = maxW;
    }
    if (outH > maxH) {
      outW = outW * maxH / outH;
      outH = maxH;
    }
    if (outW < 1 || outH < 1) return false;

    const uint32_t rowBytes = ((static_cast<uint32_t>(srcW) * bpp + 31u) / 32u) * 4u;
    uint8_t* row = scratch_.allocArray<uint8_t>(rowBytes);
    uint8_t* gray = scratch_.allocArray<uint8_t>(static_cast<size_t>(outW));
    uint8_t* palette = bpp <= 8 ? scratch_.allocArray<uint8_t>(256) : nullptr;  // gray per index
    const size_t errCap = static_cast<size_t>(outW) + 4u;
    int16_t* err0 = scratch_.allocArray<int16_t>(errCap);
    int16_t* err1 = scratch_.allocArray<int16_t>(errCap);
    int16_t* err2 = scratch_.allocArray<int16_t>(errCap);
    if (row && gray && err0 && err1 && err2 && (bpp > 8 || palette)) {
      memset(err0, 0, errCap * sizeof(int16_t));
      memset(err1, 0, errCap * sizeof(int16_t));
      memset(err2, 0, errCap * sizeof(int16_t));
      if (palette) {
        const uint32_t entries = bpp == 1 ? 2u : 256u;
        uint8_t bgra[4];
        for (uint32_t i = 0; i < entries; ++i) {
          palette[i] = static_cast<uint8_t>(i * 255u / (entries - 1));
          if (storage_.readFileAt(path, 14 + dibSize + i * 4, bgra, 4) == 4) {
            palette[i] = static_cast<uint8_t>((bgra[2] * 77u + bgra[1] * 150u + bgra[0] * 29u) >> 8);
          }
        }
      }
      WideTargetSink sink{&t,
                          static_cast<int16_t>(box.x + (box.width - outW) / 2),
                          static_cast<int16_t>(box.y + (box.height - outH) / 2),
                          err0,
                          err1,
                          err2,
                          static_cast<uint16_t>(outW)};
      ok = true;
      for (int32_t oy = 0; oy < outH && ok; ++oy) {
        const int32_t sy = oy * srcH / outH;
        const uint32_t fileRow = static_cast<uint32_t>(bottomUp ? srcH - 1 - sy : sy);
        if (storage_.readFileAt(path, dataOffset + fileRow * rowBytes, row, rowBytes) !=
            static_cast<int32_t>(rowBytes)) {
          ok = false;
          break;
        }
        for (int32_t ox = 0; ox < outW; ++ox) {
          const int32_t sx = ox * srcW / outW;
          if (bpp == 24 || bpp == 32) {
            const uint8_t* p = row + sx * (bpp / 8);
            gray[ox] = static_cast<uint8_t>((p[2] * 77u + p[1] * 150u + p[0] * 29u) >> 8);
          } else if (bpp == 8) {
            gray[ox] = palette[row[sx]];
          } else {
            gray[ox] = palette[(row[sx >> 3] >> (7 - (sx & 7))) & 1u];
          }
        }
        WideTargetSink::onRow(&sink, static_cast<uint16_t>(oy), gray, static_cast<uint16_t>(outW));
      }
    }
    scratch_.reset();
    return ok;
  }

  // PNG and JPEG go through the book engine's streaming decoders, with the
  // file standing in for a stored container entry.
  freeink::book::BookSource* source = storage_.openBook(path);
  if (!source) return false;
  const ZipEntry entry = freeink::book::ZipEntryReader::rawEntry(static_cast<uint32_t>(source->size()));
  uint16_t outW = 0, outH = 0;
  struct ProbeOnly {
    static bool stopRow(void*, uint16_t, const uint8_t*, uint16_t) { return false; }
  };
  freeink::book::ImageRenderer::renderEntry(*source, entry, maxW, maxH, scratch_, ProbeOnly::stopRow, nullptr, &outW,
                                            &outH, false);
  scratch_.reset();
  if (outW > 0 && outH > 0) {
    const size_t errCap = outW + 4u;
    int16_t* err0 = scratch_.allocArray<int16_t>(errCap);
    int16_t* err1 = scratch_.allocArray<int16_t>(errCap);
    int16_t* err2 = scratch_.allocArray<int16_t>(errCap);
    if (err0 && err1 && err2) {
      memset(err0, 0, errCap * sizeof(int16_t));
      memset(err1, 0, errCap * sizeof(int16_t));
      memset(err2, 0, errCap * sizeof(int16_t));
      WideTargetSink sink{&t,
                          static_cast<int16_t>(box.x + (box.width - outW) / 2),
                          static_cast<int16_t>(box.y + (box.height - outH) / 2),
                          err0,
                          err1,
                          err2,
                          outW};
      ok = freeink::book::ImageRenderer::renderEntry(*source, entry, maxW, maxH, scratch_, WideTargetSink::onRow,
                                                     &sink, nullptr, nullptr, false) == BookStatus::Ok;
    }
  }
  scratch_.reset();
  storage_.closeBook();
  return ok;
}

}  // namespace inkling
