#include "BookReader.h"

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

namespace {

// Bump whenever the bundled faces change: it is part of every cache key.
constexpr uint32_t kFontFingerprint = 0x414C4701;  // "ALG" v1

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

// "/books/Aria sottile.epub" -> "Aria sottile"
void titleFromPath(const char* path, char* out, size_t cap) {
  const char* base = strrchr(path, '/');
  base = base ? base + 1 : path;
  snprintf(out, cap, "%s", base);
  char* dot = strrchr(out, '.');
  if (dot && dot != out) *dot = '\0';
}

struct SavedPosition {
  uint32_t magic;
  uint32_t generation;  // informational: positions are layout independent
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
  glyphsRegular_.init(mem_.glyphsRegular, sizeof(mem_.glyphsRegular));
  glyphsItalic_.init(mem_.glyphsItalic, sizeof(mem_.glyphsItalic));
}

bool BookReader::initFonts(const uint8_t* regular, uint32_t regularLen, const uint8_t* italic, uint32_t italicLen) {
  if (!regular_.init(regular, regularLen, glyphsRegular_)) return false;
  fonts_.add(&regular_, freeink::book::StyleNone);
  // Italic is optional; bold is synthesized from whichever face is active.
  if (italic && italic_.init(italic, italicLen, glyphsItalic_)) fonts_.add(&italic_, freeink::book::StyleItalic);
  return true;
}

BookStatus BookReader::open(const char* path, const ReaderLayout& layout) {
  close();

  source_ = storage_.openBook(path);
  if (!source_) return BookStatus::IoError;
  isText_ = endsWithNoCase(path, ".txt") || endsWithNoCase(path, ".md");

  // One cache folder per book file; the size is in the key so a replaced file
  // with the same name starts fresh.
  char sizeTag[24];
  snprintf(sizeTag, sizeof(sizeTag), "#%llu", static_cast<unsigned long long>(source_->size()));
  snprintf(cacheDir_, sizeof(cacheDir_), "%s/%08lx", kCacheRoot,
           static_cast<unsigned long>(fnv1a(sizeTag, fnv1a(path))));
  storage_.mkdirs(cacheDir_);
  cache_ = storage_.cacheAt(cacheDir_);
  if (!cache_) return BookStatus::IoError;

  titleFromPath(path, title_, sizeof(title_));
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
      // First open (or the file changed): index the container once.
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
    if (meta.title && meta.title[0]) snprintf(title_, sizeof(title_), "%s", meta.title);
    if (meta.language && meta.language[0]) params_.language = meta.language;
    params_.stylesheet = catalog_.stylesheet();
  }
  if (spineCount_ == 0) return BookStatus::NotEpub;

  params_.pageWidth = layout.pageWidth;
  params_.pageHeight = layout.pageHeight;
  params_.marginLeft = layout.marginH;
  params_.marginRight = layout.marginH;
  params_.marginTop = layout.marginTop;
  params_.marginBottom = layout.marginBottom;
  params_.baseSizePx = layout.fontSizePx;
  params_.defaultAlign = freeink::book::TextAlign::Justify;
  params_.font = &fonts_;
  generation_ = freeink::book::layoutGenerationHash(params_, kFontFingerprint);

  open_ = true;
  loadedSpine_ = -1;
  page_ = 0;
  wantLastPage_ = false;
  travel_ = 1;
  pos_ = ReadingPosition{};
  loadProgress();
  return BookStatus::Ok;
}

void BookReader::close() {
  if (open_) saveProgress();
  if (source_) storage_.closeBook();
  open_ = false;
  source_ = nullptr;
  cache_ = nullptr;
  loadedSpine_ = -1;
}

BookStatus BookReader::layoutChapter(const char* cacheName) {
  scratch_.reset();
  // The writer's page index must not share an arena with layout: layout
  // rewinds its scratch between pages and would overwrite index chunks
  // allocated mid-chapter. The index arena is idle until the chapter reopens.
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
  // Bounded walk: empty chapters (cover pages with nothing to lay out) are
  // skipped in the direction of travel.
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
      return BookStatus::Ok;  // nothing left to show in this direction
    }
  }
  return BookStatus::Ok;
}

BookStatus BookReader::render(const freeink::book::FrameTarget& frame) {
  if (!open_) return BookStatus::NotFound;
  BookStatus st = ensureChapter();
  // If layout borrowed the framebuffer for parsing, wipe what it left behind.
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
  // Saved on every page: the battery can be cut at any moment.
  saveProgress();
  return st;
}

bool BookReader::nextPage() {
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

void BookReader::saveProgress() {
  if (!open_) return;
  char path[64];
  snprintf(path, sizeof(path), "%s/pos.bin", cacheDir_);
  const SavedPosition saved{kPositionMagic, generation_, pos_.spine, 0, pos_.charStart};
  storage_.writeFile(path, &saved, sizeof(saved));
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
