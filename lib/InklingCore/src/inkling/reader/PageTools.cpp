// The page on screen as words and links: what bookmarks, highlights, the
// dictionary and footnotes work on. The page cache already holds every run of
// text with its position, so nothing here parses the book again.

#include <render/ImageRenderer.h>

#include <cstdio>
#include <cstring>

#include "BookReader.h"
#include "inkling/ui/CyberCanvas.h"

namespace inkling {

using freeink::book::BookStatus;
using freeink::book::Page;
using freeink::book::PageLink;
using freeink::book::PageTextRun;

namespace {

constexpr uint32_t kFnvOffset = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;

uint32_t nextCodepoint(const char* s, uint16_t len, uint16_t& i) {
  const uint8_t c = static_cast<uint8_t>(s[i++]);
  if (c < 0x80) return c;
  int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
  uint32_t cp = (extra == 3) ? (c & 0x07u) : (extra == 2) ? (c & 0x0Fu) : (extra == 1) ? (c & 0x1Fu) : 0xFFFDu;
  while (extra-- > 0 && i < len) cp = (cp << 6) | (static_cast<uint8_t>(s[i++]) & 0x3Fu);
  return cp;
}

bool isSpace(uint32_t cp) {
  return cp == 0x20 || cp == 0x09 || cp == 0xA0 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x3000;
}

// "ﬁ" and friends (U+FB00..FB04) as the letters they stand for, or nullptr.
const char* ligatureLetters(const char* s, uint16_t len, uint16_t i) {
  if (i + 2 >= len || static_cast<uint8_t>(s[i]) != 0xEF || static_cast<uint8_t>(s[i + 1]) != 0xAC) return nullptr;
  switch (static_cast<uint8_t>(s[i + 2])) {
    case 0x80: return "ff";
    case 0x81: return "fi";
    case 0x82: return "fl";
    case 0x83: return "ffi";
    case 0x84: return "ffl";
    default:   return nullptr;
  }
}

// A fragment that ends a line with "letter + hyphen" continues on the next.
bool continuesAfterHyphen(const char* text, uint16_t len, bool lineEnd) {
  if (!lineEnd || len < 2 || text[len - 1] != '-') return false;
  const uint8_t before = static_cast<uint8_t>(text[len - 2]);
  return before >= 0x80 || (before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z');
}

}  // namespace

uint32_t BookReader::wordHash(const char* text, uint16_t len, uint32_t seed) {
  // Letters and digits only, ASCII folded to lower case: the same word hashes
  // the same with or without punctuation, a line-end hyphen or a ligature.
  uint32_t h = seed;
  auto mix = [&h](uint8_t b) { h = (h ^ b) * kFnvPrime; };
  for (uint16_t i = 0; i < len;) {
    if (const char* letters = ligatureLetters(text, len, i)) {
      for (; *letters; ++letters) mix(static_cast<uint8_t>(*letters));
      i = static_cast<uint16_t>(i + 3);
      continue;
    }
    const uint8_t c = static_cast<uint8_t>(text[i++]);
    if (c >= 0x80) {
      // Typographic quotes and dashes (U+2010..U+203F) are punctuation too.
      if (c == 0xE2 && i + 1 < len && static_cast<uint8_t>(text[i]) == 0x80) {
        i = static_cast<uint16_t>(i + 2);
        continue;
      }
      mix(c);
    } else if (c >= 'A' && c <= 'Z') {
      mix(static_cast<uint8_t>(c - 'A' + 'a'));
    } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      mix(c);
    }
  }
  return h;
}

uint16_t BookReader::walkWords(WordFn fn, void* user) {
  if (!open_ || loadedSpine_ != static_cast<int32_t>(pos_.spine) || chapter_.pageCount() == 0) return 0;
  const size_t marked = scratch_.mark();
  Page page;
  if (chapter_.readPage(page_, scratch_, &page) != BookStatus::Ok) {
    scratch_.release(marked);
    return 0;
  }

  struct Word {
    int16_t x0 = 0, x1 = 0, baseline = 0;
    uint16_t size = 0;
    uint16_t len = 0;
    uint8_t line = 0;
    char text[96];
  };
  Word cur, pend;
  bool haveCur = false, havePend = false, anyLine = false;
  int16_t lineBaseline = 0;
  uint8_t line = 0;
  uint16_t emitted = 0;

  auto flushPend = [&](bool lineEnd) {
    if (!havePend) return;
    havePend = false;
    PageWord box;
    const int16_t ascent = static_cast<int16_t>((pend.size * 4) / 5);
    box.x = static_cast<int16_t>(pend.x0 - 1);
    box.y = static_cast<int16_t>(pend.baseline - ascent);
    box.w = static_cast<uint16_t>(pend.x1 - pend.x0 + 2);
    box.h = static_cast<uint8_t>(pend.size > 250 ? 250 : pend.size);
    box.line = pend.line;
    fn(user, emitted++, box, pend.text, pend.len, lineEnd);
  };
  auto finishCur = [&] {
    if (!haveCur) return;
    haveCur = false;
    flushPend(pend.line != cur.line);
    pend = cur;
    havePend = true;
  };
  auto token = [&](int16_t x0, int16_t x1, const PageTextRun& run, const char* text, uint16_t len) {
    if (!anyLine || run.baselineY - lineBaseline > static_cast<int16_t>(run.sizePx / 2)) {
      if (anyLine && line < 255) ++line;
      anyLine = true;
      lineBaseline = run.baselineY;
    }
    const int16_t tolerance = static_cast<int16_t>(run.sizePx / 8 > 2 ? run.sizePx / 8 : 2);
    if (haveCur && cur.line == line && x0 >= cur.x0 && x0 - cur.x1 <= tolerance) {
      cur.x1 = x1;  // same word, next style (the bold half of focus reading)
    } else {
      finishCur();
      haveCur = true;
      cur.x0 = x0;
      cur.x1 = x1;
      cur.baseline = lineBaseline;
      cur.size = run.sizePx;
      cur.line = line;
      cur.len = 0;
    }
    const uint16_t room = static_cast<uint16_t>(sizeof(cur.text) - cur.len);
    const uint16_t n = len < room ? len : room;
    memcpy(cur.text + cur.len, text, n);
    cur.len = static_cast<uint16_t>(cur.len + n);
  };

  for (uint16_t r = 0; r < page.runCount; ++r) {
    const PageTextRun& run = page.runs[r];
    int32_t x = run.x;
    uint32_t prev = 0;
    bool inToken = false;
    int32_t tokenX = 0;
    uint16_t tokenStart = 0;
    for (uint16_t i = 0; i < run.len;) {
      const uint16_t at = i;
      const uint32_t cp = nextCodepoint(run.text, run.len, i);
      int32_t advance = fonts_.advance(cp, run.sizePx, run.styleFlags);
      if (prev != 0) advance += fonts_.kerning(prev, cp, run.sizePx, run.styleFlags);
      prev = cp;
      if (isSpace(cp)) {
        if (inToken) {
          token(static_cast<int16_t>(tokenX), static_cast<int16_t>(x), run, run.text + tokenStart,
                static_cast<uint16_t>(at - tokenStart));
          inToken = false;
        }
      } else if (!inToken) {
        inToken = true;
        tokenX = x;
        tokenStart = at;
      }
      x += advance;
    }
    if (inToken) {
      token(static_cast<int16_t>(tokenX), static_cast<int16_t>(x), run, run.text + tokenStart,
            static_cast<uint16_t>(run.len - tokenStart));
    }
  }
  finishCur();
  flushPend(true);
  scratch_.release(marked);
  return emitted;
}

uint16_t BookReader::pageWords(PageWord* out, uint16_t cap) {
  struct Ctx {
    PageWord* out;
    uint16_t cap;
    uint16_t n;
  } ctx{out, cap, 0};
  walkWords(
      [](void* user, uint16_t, const PageWord& box, const char*, uint16_t, bool) {
        auto& c = *static_cast<Ctx*>(user);
        if (c.n < c.cap) c.out[c.n++] = box;
      },
      &ctx);
  return ctx.n;
}

size_t BookReader::pageWordsText(uint16_t first, uint16_t last, char* out, size_t cap) {
  if (!out || cap == 0) return 0;
  struct Ctx {
    uint16_t first, last;
    char* out;
    size_t cap, len;
    bool joinNext, truncated;
  } ctx{first, last, out, cap, 0, false, false};
  walkWords(
      [](void* user, uint16_t index, const PageWord&, const char* text, uint16_t len, bool lineEnd) {
        auto& c = *static_cast<Ctx*>(user);
        if (index < c.first || index > c.last) return;
        auto put = [&c](char ch) {
          if (c.len + 1 < c.cap) {
            c.out[c.len++] = ch;
          } else {
            c.truncated = true;
          }
        };
        if (c.len > 0 && !c.joinNext) put(' ');
        c.joinNext = index < c.last && continuesAfterHyphen(text, len, lineEnd);
        const uint16_t end = c.joinNext ? static_cast<uint16_t>(len - 1) : len;
        for (uint16_t i = 0; i < end;) {
          if (const char* letters = ligatureLetters(text, end, i)) {
            for (; *letters; ++letters) put(*letters);
            i = static_cast<uint16_t>(i + 3);
          } else {
            put(text[i++]);
          }
        }
      },
      &ctx);
  if (ctx.truncated) {  // never leave half a UTF-8 sequence at the cut
    while (ctx.len > 0 && (static_cast<uint8_t>(out[ctx.len - 1]) & 0xC0) == 0x80) --ctx.len;
    if (ctx.len > 0 && static_cast<uint8_t>(out[ctx.len - 1]) >= 0xC0) --ctx.len;
  }
  out[ctx.len] = '\0';
  return ctx.len;
}

bool BookReader::describePassage(uint16_t first, uint16_t last, Mark* mark) {
  if (!mark || last < first) return false;
  struct Ctx {
    uint16_t first, last;
    Mark* mark;
    uint32_t hash;
    uint16_t words;
    bool open;
  } ctx{first, last, mark, kFnvOffset, 0, false};
  walkWords(
      [](void* user, uint16_t index, const PageWord&, const char* text, uint16_t len, bool lineEnd) {
        auto& c = *static_cast<Ctx*>(user);
        if (index < c.first || index > c.last) return;
        c.hash = wordHash(text, len, c.open ? c.hash : kFnvOffset);
        c.open = index < c.last && continuesAfterHyphen(text, len, lineEnd);
        if (c.open) return;
        if (c.words < 3) c.mark->firstHash[c.words] = c.hash;
        c.mark->lastHash = c.hash;
        ++c.words;
      },
      &ctx);
  if (ctx.words == 0) return false;
  mark->type = Mark::Highlight;
  mark->spine = pos_.spine;
  mark->charStart = pos_.charStart;
  mark->pct = percent();
  mark->wordCount = static_cast<uint8_t>(ctx.words > 255 ? 255 : ctx.words);
  pageWordsText(first, last, mark->text, sizeof(mark->text));
  return true;
}

uint8_t BookReader::underlineHighlights(freeink::ui::DrawTarget& t, const Mark* marks, int count) {
  if (!open_ || !marks || count <= 0) return 0;
  // A passage sits within a page or so of where its page started when it was
  // marked, whatever the layout is now.
  constexpr uint32_t kWindowChars = 5000;
  bool any = false;
  for (int i = 0; i < count && !any; ++i) {
    const Mark& m = marks[i];
    if (m.type != Mark::Highlight || m.spine != pos_.spine) continue;
    const uint32_t delta = m.charStart > pos_.charStart ? m.charStart - pos_.charStart : pos_.charStart - m.charStart;
    any = delta <= kWindowChars;
  }
  if (!any) return 0;

  const size_t marked = scratch_.mark();
  struct Ctx {
    PageWord* boxes;
    uint32_t* hashes;
    uint16_t* firstFrag;
    uint16_t* lastFrag;
    uint16_t fragments, words;
    uint32_t hash;
    uint16_t start;
    bool open;
  } ctx{};
  ctx.boxes = scratch_.allocArray<PageWord>(kMaxPageWords);
  ctx.hashes = scratch_.allocArray<uint32_t>(kMaxPageWords);
  ctx.firstFrag = scratch_.allocArray<uint16_t>(kMaxPageWords);
  ctx.lastFrag = scratch_.allocArray<uint16_t>(kMaxPageWords);
  if (!ctx.boxes || !ctx.hashes || !ctx.firstFrag || !ctx.lastFrag) {
    scratch_.release(marked);
    return 0;
  }
  ctx.hash = kFnvOffset;
  walkWords(
      [](void* user, uint16_t index, const PageWord& box, const char* text, uint16_t len, bool lineEnd) {
        auto& c = *static_cast<Ctx*>(user);
        if (index >= kMaxPageWords) return;
        c.boxes[index] = box;
        c.fragments = static_cast<uint16_t>(index + 1);
        if (!c.open) c.start = index;
        c.hash = wordHash(text, len, c.open ? c.hash : kFnvOffset);
        c.open = continuesAfterHyphen(text, len, lineEnd);
        if (c.open) return;
        c.hashes[c.words] = c.hash;
        c.firstFrag[c.words] = c.start;
        c.lastFrag[c.words] = index;
        ++c.words;
      },
      &ctx);
  if (ctx.open && ctx.words < kMaxPageWords) {  // the page ends inside a hyphenated word
    ctx.hashes[ctx.words] = ctx.hash;
    ctx.firstFrag[ctx.words] = ctx.start;
    ctx.lastFrag[ctx.words] = static_cast<uint16_t>(ctx.fragments - 1);
    ++ctx.words;
  }

  uint8_t found = 0;
  for (int i = 0; i < count; ++i) {
    const Mark& m = marks[i];
    if (m.type != Mark::Highlight || m.spine != pos_.spine || m.wordCount == 0) continue;
    const uint32_t delta = m.charStart > pos_.charStart ? m.charStart - pos_.charStart : pos_.charStart - m.charStart;
    if (delta > kWindowChars) continue;
    const uint16_t need = m.wordCount < 3 ? m.wordCount : 3;
    for (uint16_t j = 0; j < ctx.words; ++j) {
      uint16_t k = 0;
      while (k < need && j + k < ctx.words && ctx.hashes[j + k] == m.firstHash[k]) ++k;
      const bool runsOffPage = j + k == ctx.words && k >= 2;
      if (k < need && !runsOffPage) continue;
      uint16_t end = static_cast<uint16_t>(j + m.wordCount - 1);
      if (end >= ctx.words) {
        end = static_cast<uint16_t>(ctx.words - 1);  // continues on the next page
      } else if (ctx.hashes[end] != m.lastHash) {
        continue;  // same opening words, another sentence
      }
      for (uint16_t f = ctx.firstFrag[j]; f <= ctx.lastFrag[end]; ++f) {
        const PageWord& b = ctx.boxes[f];
        const bool joins = f < ctx.lastFrag[end] && ctx.boxes[f + 1].line == b.line;
        const int16_t x1 = joins ? ctx.boxes[f + 1].x : static_cast<int16_t>(b.x + b.w);
        const int16_t y = static_cast<int16_t>(b.y + (b.h * 4) / 5 + 4);
        ui::canvas::fastFill(t, freeink::ui::Rect{b.x, y, static_cast<int16_t>(x1 - b.x), 2},
                             freeink::ui::Paint::solid(freeink::ui::Color::Black));
      }
      ++found;
      break;
    }
  }
  scratch_.release(marked);
  return found;
}

uint32_t BookReader::pageCharEnd() const {
  if (open_ && loadedSpine_ == static_cast<int32_t>(pos_.spine) && page_ + 1 < chapter_.pageCount()) {
    return chapter_.charStart(page_ + 1);
  }
  return 0xFFFFFFFFu;
}

void BookReader::jumpTo(const ReadingPosition& position) {
  if (!open_ || position.spine >= spineCount_) return;
  prebuildCancel();
  pos_ = position;
  wantLastPage_ = false;
  travel_ = 1;
  if (loadedSpine_ == static_cast<int32_t>(pos_.spine)) {
    page_ = chapter_.pageForChar(pos_.charStart);
    pos_.charStart = chapter_.charStart(page_);
  }
}

uint16_t BookReader::pageLinks(PageLinkBox* out, uint16_t cap, uint8_t* linkCountOut) {
  if (linkCountOut) *linkCountOut = 0;
  if (!open_ || loadedSpine_ != static_cast<int32_t>(pos_.spine) || chapter_.pageCount() == 0) return 0;
  const size_t marked = scratch_.mark();
  Page page;
  uint16_t n = 0;
  uint8_t ids = 0;
  if (chapter_.readPage(page_, scratch_, &page) == BookStatus::Ok) {
    for (uint16_t l = 0; l < page.linkCount; ++l) {
      const PageLink& link = page.links[l];
      const bool sameAsPrevious = l > 0 && strcmp(link.target, page.links[l - 1].target) == 0 &&
                                  strcmp(link.fragment, page.links[l - 1].fragment) == 0;
      if (!sameAsPrevious) ++ids;
      if (out && n < cap) {
        out[n].x = link.x;
        out[n].y = link.y;
        out[n].w = link.width;
        out[n].h = link.height;
        out[n].id = static_cast<uint8_t>(ids - 1);
        ++n;
      }
    }
  }
  scratch_.release(marked);
  if (linkCountOut) *linkCountOut = ids;
  return n;
}

BookReader::LinkResult BookReader::followLink(uint8_t id, char* urlOut, size_t urlCap) {
  if (urlOut && urlCap) urlOut[0] = '\0';
  if (!open_ || isText_ || loadedSpine_ != static_cast<int32_t>(pos_.spine)) return LinkResult::Missing;
  static char target[256];
  char fragment[96];
  target[0] = fragment[0] = '\0';
  bool have = false;
  {
    const size_t marked = scratch_.mark();
    Page page;
    if (chapter_.readPage(page_, scratch_, &page) == BookStatus::Ok) {
      uint8_t ids = 0;
      for (uint16_t l = 0; l < page.linkCount && !have; ++l) {
        const PageLink& link = page.links[l];
        const bool sameAsPrevious = l > 0 && strcmp(link.target, page.links[l - 1].target) == 0 &&
                                    strcmp(link.fragment, page.links[l - 1].fragment) == 0;
        if (!sameAsPrevious) ++ids;
        if (static_cast<uint8_t>(ids - 1) == id) {
          snprintf(target, sizeof(target), "%s", link.target);
          snprintf(fragment, sizeof(fragment), "%s", link.fragment);
          have = true;
        }
      }
    }
    scratch_.release(marked);
  }
  if (!have) return LinkResult::Missing;
  if (strstr(target, "://") != nullptr || strncmp(target, "mailto:", 7) == 0) {
    if (urlOut && urlCap) snprintf(urlOut, urlCap, "%s", target);
    return LinkResult::External;
  }

  int spine = pos_.spine;
  if (target[0]) {
    spine = catalog_.spineIndexForHref(target);
    if (spine < 0 || spine >= static_cast<int>(spineCount_)) return LinkResult::Missing;
  } else if (!fragment[0]) {
    return LinkResult::Missing;
  }
  prebuildCancel();
  pos_.spine = static_cast<uint16_t>(spine);
  pos_.charStart = 0;
  wantLastPage_ = false;
  travel_ = 1;
  if (ensureChapter() != BookStatus::Ok) return LinkResult::Missing;
  if (loadedSpine_ == static_cast<int32_t>(pos_.spine)) {
    uint32_t at = 0;
    page_ = 0;
    if (fragment[0] && chapter_.charForAnchor(freeink::book::ZipCatalog::hashPath(fragment), &at)) {
      page_ = chapter_.pageForChar(at);
    }
    pos_.charStart = chapter_.charStart(page_);
  }
  return LinkResult::Jumped;
}

}  // namespace inkling
