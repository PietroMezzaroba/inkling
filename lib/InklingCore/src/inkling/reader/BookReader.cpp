#include "BookReader.h"

#include "Library.h"
#include "hyph_it.h"
#include "inkling/text/Nfc.h"
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
    0x4E535204u,  // "NSR" v4 (Noto Serif)
    0x4E534E04u,  // "NSN" v4 (Noto Sans)
    0x414C4704u,  // "ALG" v4 (Alegreya)
    0x4F445904u,  // "ODY" v4 (OpenDyslexic)
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
  index_.init(mem_.index, sizeof(mem_.index));
  hyphIt_.init(freeink::book::k_hyph_it, freeink::book::k_hyph_it_size);
  hyphEn_.init(freeink::book::k_hyph_en_us, freeink::book::k_hyph_en_us_size);
}

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
  const uint32_t prevGen = generation_;
  const uint8_t prevFam = activeFamily_;
  applyLayout(layout);
  if (generation_ != prevGen || activeFamily_ != prevFam) {
    loadedSpine_ = -1;
    wantLastPage_ = false;
  }
  return ensureChapter();
}

void BookReader::close() {
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
  saveProgress();
  return st;
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

}  // namespace inkling
