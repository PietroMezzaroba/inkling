// InklingApp: the reader's tools (bookmarks, clippings, dictionary, footnotes)
// and the two Home apps added with them (flashcards, statistics).

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "App.h"
#include "inkling/ui/CyberCanvas.h"

namespace inkling {

using namespace inkling::ui;
using namespace inkling::ui::canvas;

namespace {

using freeink::ui::RefreshHint;

constexpr int16_t kStatusLineHeight = 38;  // reader status bar (same as App.cpp)
constexpr int16_t kHudH = 40;
constexpr int16_t kKeyBarH = 38;
constexpr int16_t kMargin = 14;
constexpr size_t kDefinitionCap = 1800;
constexpr size_t kClippingCap = 2400;
constexpr uint16_t kNoAnchor = 0xFFFF;

int16_t contentTop() { return static_cast<int16_t>(kHudH + 6); }
int16_t contentBottom() { return static_cast<int16_t>(screenH() - kKeyBarH - 6); }

int pageFirst(int sel, int count, int perPage) {
  if (perPage <= 0) return 0;
  int first = (sel / perPage) * perPage;
  if (first > count - perPage) first = count - perPage;
  return first < 0 ? 0 : first;
}

constexpr int16_t kMarkRowH = 64;
constexpr int16_t kMarkGap = 8;

int marksPerPage() {
  const int16_t listTop = static_cast<int16_t>(contentTop() + 2 + 64 + 12);
  const int perPage = (contentBottom() - listTop + kMarkGap) / (kMarkRowH + kMarkGap);
  return perPage < 1 ? 1 : perPage;
}

// A file name the card accepts, from a book title.
void fileNameFrom(const char* title, char* out, size_t cap) {
  size_t n = 0;
  for (const char* p = title; *p && n + 1 < cap && n < 60; ++p) {
    const char c = *p;
    const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                     c == '|' || static_cast<uint8_t>(c) < 0x20;
    out[n++] = bad ? '-' : c;
  }
  while (n > 0 && (static_cast<uint8_t>(out[n - 1]) & 0xC0) == 0x80) --n;  // cut inside a character
  if (n > 0 && static_cast<uint8_t>(out[n - 1]) >= 0xC0) --n;
  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) --n;
  if (n == 0) {
    snprintf(out, cap, "Libro");
    return;
  }
  out[n] = '\0';
}

bool isImageName(const char* name) {
  const size_t n = strlen(name);
  auto ends = [&](const char* ext) {
    const size_t m = strlen(ext);
    if (n < m) return false;
    for (size_t i = 0; i < m; ++i) {
      char c = name[n - m + i];
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
      if (c != ext[i]) return false;
    }
    return true;
  };
  return name[0] != '.' && (ends(".bmp") || ends(".png") || ends(".jpg") || ends(".jpeg"));
}

struct SleepImageScan {
  const char* dir;
  bool rootOnlySleep;  // in the card root only files called sleep*.*
  int wanted;          // index to return, -1 = just count
  int count;
  char* out;
  uint32_t cap;
};

void onSleepImage(const char* name, bool isDir, void* user) {
  auto& s = *static_cast<SleepImageScan*>(user);
  if (isDir || !isImageName(name)) return;
  if (s.rootOnlySleep && strncmp(name, "sleep", 5) != 0 && strncmp(name, "Sleep", 5) != 0) return;
  if (s.count == s.wanted && s.out) {
    snprintf(s.out, s.cap, "%s%s%s", s.dir, s.dir[1] ? "/" : "", name);
  }
  ++s.count;
}

}  // namespace

int32_t InklingApp::today() const {
  return status_.clockValid ? dayNumber(status_.year, status_.month, status_.day) : 0;
}

bool InklingApp::nextSleepImage(char* path, uint32_t cap) {
  if (!path || cap == 0) return false;
  path[0] = '\0';
  auto scan = [&](int wanted) {
    SleepImageScan s{"/sleep", false, wanted, 0, path, cap};
    storage_.listDir("/sleep", onSleepImage, &s);
    s.dir = "/";
    s.rootOnlySleep = true;
    storage_.listDir("/", onSleepImage, &s);
    return s.count;
  };
  const int count = scan(-1);
  if (count <= 0) return false;
  const int index = settings_.sleepImageIndex % count;
  scan(index);
  if (!path[0]) return false;
  settings_.sleepImageIndex = static_cast<uint8_t>((index + 1) % count);
  settings_.save(storage_);
  return true;
}

// ---------------------------------------------------------------------------
// The page, with what the reader marked on it
// ---------------------------------------------------------------------------

void InklingApp::drawReaderPage(DrawTarget& t) {
  clearScreen();
  lastRenderStatus_ = reader_->render(frame_);
  if (lastRenderStatus_ != freeink::book::BookStatus::Ok || marks_.count() == 0) return;
  reader_->underlineHighlights(t, marks_.data(), marks_.count());
  if (marks_.bookmarkOn(reader_->currentSpine(), reader_->pageCharStart(), reader_->pageCharEnd()) >= 0) {
    // A ribbon hanging from the top edge.
    const int16_t x = static_cast<int16_t>(screenW() - 40);
    fastFill(t, Rect{x, 0, 20, 30}, Paint::solid(Color::Black));
    for (int16_t r = 0; r < 10; ++r) {
      fastFill(t, Rect{static_cast<int16_t>(x + 9 - r), static_cast<int16_t>(20 + r), static_cast<int16_t>(2 * r + 2), 1},
               Paint::solid(Color::White));
    }
  }
}

void InklingApp::drawReaderBar(DrawTarget& t, const char* hint) {
  const int16_t w = screenW();
  const int16_t y = static_cast<int16_t>(screenH() - kStatusLineHeight);
  const Rect bar{0, y, w, kStatusLineHeight};
  fastFill(t, bar, Paint::solid(Color::White));
  fastFill(t, Rect{0, y, w, 2}, Paint::solid(Color::Black));
  drawText(t, Rect{14, static_cast<int16_t>(y + 8), static_cast<int16_t>(w - 28), 24}, hint, kFontSmall, Color::Black);
  if (nightMode_) invertRect(bar);
}

// ---------------------------------------------------------------------------
// Bookmarks and highlights
// ---------------------------------------------------------------------------

void InklingApp::toggleBookmark() {
  if (!reader_ || !reader_->isOpen()) return;
  const int existing =
      marks_.bookmarkOn(reader_->currentSpine(), reader_->pageCharStart(), reader_->pageCharEnd());
  if (existing >= 0) {
    marks_.remove(storage_, existing);
    return;
  }
  Mark m;
  m.type = Mark::Bookmark;
  m.spine = reader_->currentSpine();
  m.charStart = reader_->pageCharStart();
  m.pct = reader_->percent();
  if (reader_->pageWordsText(0, 7, m.text, sizeof(m.text)) == 0) snprintf(m.text, sizeof(m.text), "Pagina senza testo");
  marks_.add(storage_, m);
}

void InklingApp::showMarks() {
  applyPendingLayout();
  marksMenu_.reset(marks_.count() > 0 ? marks_.count() : 1, 0);
  enterMode(Mode::Marks, marksScreen, this, RefreshHint::Clean);
}

void InklingApp::marksScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = screenW();
  const int count = self.marks_.count();
  const int sel = self.marksMenu_.selected;

  clearScreen();
  drawTopHud(t, self.status_, "SEGNALIBRI");
  drawBottomKeyBar(t, "OK", "Vai", "DX", "Elimina", "BACK", "Menu");

  const Rect hdr{kMargin, static_cast<int16_t>(contentTop() + 2), static_cast<int16_t>(w - 2 * kMargin), 64};
  drawCard(t, hdr, 8, 3, false, false);
  drawText(t, Rect{static_cast<int16_t>(hdr.x + 14), static_cast<int16_t>(hdr.y + 6), static_cast<int16_t>(hdr.width - 28), 28},
           self.reader_->title(), kFontTitle, Color::Black);
  char sub[64];
  const int highlights = self.marks_.highlightCount();
  snprintf(sub, sizeof(sub), "%d segnalibri · %d ritagli", count - highlights, highlights);
  drawText(t, Rect{static_cast<int16_t>(hdr.x + 14), static_cast<int16_t>(hdr.y + 36), static_cast<int16_t>(hdr.width - 28), 22},
           sub, kFontSmall, Color::Black);

  const int16_t listTop = static_cast<int16_t>(hdr.bottom() + 12);
  if (count == 0) {
    const Rect box{kMargin, listTop, static_cast<int16_t>(w - 2 * kMargin), 150};
    drawCard(t, box, 8, 2, false, false);
    drawWrapped(t, Rect{static_cast<int16_t>(box.x + 16), static_cast<int16_t>(box.y + 14), static_cast<int16_t>(box.width - 32), 120},
                "Ancora niente. Dal menu di lettura: OK su SEGNALIBRO segna la pagina, RITAGLIA TESTO salva un "
                "passaggio qui e nel file /Ritagli della scheda.",
                kFontSmall);
    return;
  }

  const int perPage = marksPerPage();
  const int first = pageFirst(sel, count, perPage);
  for (int slot = 0; slot < perPage; ++slot) {
    const int idx = first + slot;
    if (idx >= count) break;
    const Mark& m = self.marks_.at(idx);
    const bool active = idx == sel;
    const int16_t ry = static_cast<int16_t>(listTop + slot * (kMarkRowH + kMarkGap));
    const Rect rr{kMargin, ry, static_cast<int16_t>(w - 2 * kMargin), kMarkRowH};
    drawCard(t, rr, 8, active ? 4 : 2, active, false);
    const Color fg = active ? Color::White : Color::Black;
    char tag[24];
    snprintf(tag, sizeof(tag), "%s · %u%%", m.type == Mark::Highlight ? "RITAGLIO" : "SEGNALIBRO",
             static_cast<unsigned>(m.pct));
    drawPill(t, static_cast<int16_t>(rr.x + 12), static_cast<int16_t>(ry + 6), 22, tag, kFontMicro, !active, 8);
    drawText(t, Rect{static_cast<int16_t>(rr.x + 14), static_cast<int16_t>(ry + 34), static_cast<int16_t>(rr.width - 28), 24},
             m.text, kFontSmall, fg);
  }
}

// ---------------------------------------------------------------------------
// Word cursor: clip a passage, look a word up
// ---------------------------------------------------------------------------

void InklingApp::enterSelect(SelectPurpose purpose) {
  if (!reader_ || !reader_->isOpen()) return;
  applyPendingLayout();
  if (!selWords_) {
    selWords_ = static_cast<BookReader::PageWord*>(malloc(sizeof(BookReader::PageWord) * BookReader::kMaxPageWords));
  }
  selCount_ = selWords_ ? reader_->pageWords(selWords_, BookReader::kMaxPageWords) : 0;
  if (selCount_ == 0) {
    leaveSelect();
    readerNotice_ = "Nessun testo in questa pagina";
    showReader(RefreshHint::Clean);
    return;
  }
  selPurpose_ = purpose;
  selCursor_ = 0;
  selAnchor_ = kNoAnchor;
  selDrawn_ = false;
  enterMode(Mode::Select, selectScreen, this, RefreshHint::Clean);
}

void InklingApp::leaveSelect() {
  free(selWords_);
  selWords_ = nullptr;
  selCount_ = 0;
  selAnchor_ = kNoAnchor;
  selDrawn_ = false;
  free(definition_);
  definition_ = nullptr;
}

void InklingApp::moveSelection(Button button) {
  if (selCount_ == 0) return;
  uint16_t next = selCursor_;
  if (button == Button::Left) {
    if (next > 0) --next;
  } else if (button == Button::Right) {
    if (next + 1 < selCount_) ++next;
  } else {
    // The word on the line above/below that sits closest horizontally.
    const BookReader::PageWord& cur = selWords_[selCursor_];
    const int line = static_cast<int>(cur.line) + (button == Button::Down ? 1 : -1);
    const int center = cur.x + cur.w / 2;
    int best = -1, bestDistance = 0;
    for (uint16_t i = 0; i < selCount_; ++i) {
      if (selWords_[i].line != line) continue;
      const int d = abs(selWords_[i].x + selWords_[i].w / 2 - center);
      if (best < 0 || d < bestDistance) {
        best = i;
        bestDistance = d;
      }
    }
    if (best >= 0) next = static_cast<uint16_t>(best);
  }
  if (next == selCursor_) return;
  selCursor_ = next;
  cursorOnlyRedraw_ = true;
  ui_.invalidate(RefreshHint::Fast);
}

void InklingApp::confirmSelection() {
  if (selCount_ == 0) return;
  if (selPurpose_ == SelectPurpose::Lookup) {
    lookUpWord(selCursor_);
    return;
  }
  if (selAnchor_ == kNoAnchor) {
    selAnchor_ = selCursor_;
    cursorOnlyRedraw_ = true;
    ui_.invalidate(RefreshHint::Fast);
    return;
  }
  const uint16_t first = selAnchor_ < selCursor_ ? selAnchor_ : selCursor_;
  const uint16_t last = selAnchor_ < selCursor_ ? selCursor_ : selAnchor_;
  saveClipping(first, last);
}

void InklingApp::saveClipping(uint16_t first, uint16_t last) {
  char* text = static_cast<char*>(malloc(kClippingCap));
  bool saved = false;
  if (text && reader_->pageWordsText(first, last, text, kClippingCap) > 0) {
    // /Ritagli/<book>.md: a Markdown file per book, one quotation per clipping.
    char name[72], path[100];
    fileNameFrom(reader_->title(), name, sizeof(name));
    snprintf(path, sizeof(path), "/Ritagli/%s.md", name);
    storage_.mkdirs("/Ritagli");
    char line[224];
    saved = true;
    if (storage_.fileSize(path) < 0) {
      const int n = snprintf(line, sizeof(line), "# Ritagli — %s\n\n%s\n\n", reader_->title(), reader_->author());
      saved = storage_.appendFile(path, line, static_cast<uint32_t>(n < static_cast<int>(sizeof(line)) ? n : sizeof(line) - 1));
      // The list of clipping files, for scripts/ritagli.sh (the Wi-Fi portal
      // serves files but does not list folders).
      const int m = snprintf(line, sizeof(line), "%s\n", path);
      storage_.appendFile("/Ritagli/indice.txt", line, static_cast<uint32_t>(m));
    }
    saved = saved && storage_.appendFile(path, "> ", 2) &&
            storage_.appendFile(path, text, static_cast<uint32_t>(strlen(text)));
    char chapter[64], date[48];
    reader_->currentChapterTitle(chapter, sizeof(chapter));
    const LocalTime now = currentLocalTime();
    formatItalianDate(now, date, sizeof(date), true);
    int n;
    if (date[0]) {
      n = snprintf(line, sizeof(line), "\n\n— %s · %u%% · %s, %02u:%02u\n\n", chapter,
                   static_cast<unsigned>(reader_->percent()), date, now.hour, now.minute);
    } else {
      n = snprintf(line, sizeof(line), "\n\n— %s · %u%%\n\n", chapter, static_cast<unsigned>(reader_->percent()));
    }
    saved = saved && storage_.appendFile(path, line, static_cast<uint32_t>(n < static_cast<int>(sizeof(line)) ? n : sizeof(line) - 1));

    Mark m;
    if (saved && reader_->describePassage(first, last, &m)) marks_.add(storage_, m);
  }
  free(text);
  leaveSelect();
  readerNotice_ = saved ? "Ritaglio salvato in /Ritagli" : "Ritaglio non salvato (scheda?)";
  showReader(RefreshHint::Clean);
}

void InklingApp::lookUpWord(uint16_t word) {
  char text[96];
  reader_->pageWordsText(word, word, text, sizeof(text));
  const size_t n = strlen(text);
  // Hyphenated across two lines: the word is this fragment and the next.
  if (n > 1 && text[n - 1] == '-' && word + 1 < selCount_ && selWords_[word + 1].line != selWords_[word].line) {
    reader_->pageWordsText(word, static_cast<uint16_t>(word + 1), text, sizeof(text));
  }
  if (!definition_) definition_ = static_cast<char*>(malloc(kDefinitionCap));
  if (!definition_) return;

  // The first lookup may index the dictionary (a few seconds): say so.
  static bool warmed = false;
  if (!warmed && dictionary_.open(storage_) == Dictionary::Result::Found) {
    warmed = true;
    showMessage("Apro il dizionario...");
    flush(RefreshHint::Fast);
  }
  definitionResult_ =
      dictionary_.lookup(storage_, text, definitionHead_, sizeof(definitionHead_), definition_, kDefinitionCap);
  switch (definitionResult_) {
    case Dictionary::Result::Found:
    case Dictionary::Result::Nearest:
      break;
    case Dictionary::Result::NotFound:
      snprintf(definitionHead_, sizeof(definitionHead_), "%s", text);
      snprintf(definition_, kDefinitionCap, "Nessuna voce per questa parola in «%s».", dictionary_.name());
      break;
    case Dictionary::Result::Compressed:
      snprintf(definitionHead_, sizeof(definitionHead_), "Dizionario compresso");
      snprintf(definition_, kDefinitionCap,
               "Il dizionario sulla scheda è compresso. Sul computer decomprimi il file .dict.dz (è un gzip: "
               "rinominalo .dict.gz ed estrailo) e, se c'è, anche .idx.gz; poi ricopia i file .ifo, .idx e .dict "
               "nella cartella /dizionari.");
      break;
    case Dictionary::Result::NoDictionary:
      snprintf(definitionHead_, sizeof(definitionHead_), "Nessun dizionario");
      snprintf(definition_, kDefinitionCap,
               "Copia un dizionario in formato StarDict nella cartella /dizionari della microSD: i tre file "
               ".ifo, .idx e .dict (non compressi), anche dentro una loro cartella.\nDal portale Wi-Fi o "
               "togliendo la scheda.");
      break;
    default:
      snprintf(definitionHead_, sizeof(definitionHead_), "%s", text);
      snprintf(definition_, kDefinitionCap, "Il dizionario non si legge (file incompleti o formato non previsto).");
      break;
  }
  definitionLine_ = 0;
  enterMode(Mode::Definition, definitionScreen, this, RefreshHint::Clean);
}

namespace {

// The cursor box of word `i`: down to the next line at most (so stacked lines
// of a selection do not overlap) and, inside a selection, across the space
// to the next word.
Rect wordRect(const BookReader::PageWord* words, uint16_t count, uint16_t i, bool bridge) {
  const BookReader::PageWord& w = words[i];
  int16_t height = static_cast<int16_t>(w.h + 3);
  int16_t width = static_cast<int16_t>(w.w);
  for (uint16_t j = static_cast<uint16_t>(i + 1); j < count; ++j) {
    if (words[j].line == w.line) {
      if (bridge && j == i + 1 && words[j].x > w.x) width = static_cast<int16_t>(words[j].x - w.x);
      continue;
    }
    const int16_t pitch = static_cast<int16_t>(words[j].y - w.y);
    if (pitch > 4 && pitch < height) height = pitch;
    break;
  }
  return Rect{w.x, static_cast<int16_t>(w.y - 1), width, height};
}

}  // namespace

void InklingApp::selectScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  if (!self.selWords_ || self.selCount_ == 0) return;
  auto paint = [&](uint16_t lo, uint16_t hi) {
    for (uint16_t i = lo; i <= hi; ++i) invertRect(wordRect(self.selWords_, self.selCount_, i, i < hi));
  };

  if (!self.cursorOnlyRedraw_) {
    self.drawReaderPage(t);
    if (self.nightMode_) invertScreen();
  } else if (self.selDrawn_) {
    paint(self.selDrawnLo_, self.selDrawnHi_);  // inverting again restores the page
  }
  uint16_t lo = self.selCursor_, hi = self.selCursor_;
  if (self.selAnchor_ != kNoAnchor) {
    lo = self.selAnchor_ < self.selCursor_ ? self.selAnchor_ : self.selCursor_;
    hi = self.selAnchor_ < self.selCursor_ ? self.selCursor_ : self.selAnchor_;
  }
  paint(lo, hi);
  self.selDrawnLo_ = lo;
  self.selDrawnHi_ = hi;
  self.selDrawn_ = true;

  const char* hint;
  if (self.selPurpose_ == SelectPurpose::Lookup) {
    hint = "DIZIONARIO · frecce: parola · OK: cerca · BACK: esci";
  } else if (self.selAnchor_ == kNoAnchor) {
    hint = "RITAGLIO · frecce: prima parola · OK: inizio · BACK: esci";
  } else {
    hint = "RITAGLIO · frecce: ultima parola · OK: salva · BACK: annulla";
  }
  self.drawReaderBar(t, hint);
  self.cursorOnlyRedraw_ = false;
}

void InklingApp::definitionScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = screenW();
  clearScreen();
  drawTopHud(t, self.status_, "DIZIONARIO");

  const Rect hdr{kMargin, static_cast<int16_t>(contentTop() + 2), static_cast<int16_t>(w - 2 * kMargin), 70};
  drawCard(t, hdr, 8, 3, false, true);
  drawText(t, Rect{static_cast<int16_t>(hdr.x + 16), static_cast<int16_t>(hdr.y + 6), static_cast<int16_t>(hdr.width - 32), 30},
           self.definitionHead_, kFontTitle, Color::Black);
  const char* tag = self.definitionResult_ == Dictionary::Result::Found
                        ? self.dictionary_.name()
                        : (self.definitionResult_ == Dictionary::Result::Nearest ? "VOCE PIÙ VICINA" : "DIZIONARIO");
  drawPill(t, static_cast<int16_t>(hdr.x + 16), static_cast<int16_t>(hdr.y + 40), 22, tag, kFontMicro, true, 10);

  const Rect body{static_cast<int16_t>(kMargin + 8), static_cast<int16_t>(hdr.bottom() + 14),
                  static_cast<int16_t>(w - 2 * kMargin - 16), static_cast<int16_t>(contentBottom() - hdr.bottom() - 18)};
  self.definitionLines_ =
      drawWrapped(t, body, self.definition_ ? self.definition_ : "", kFontBody, Color::Black, self.definitionLine_,
                  &self.definitionFit_);
  const bool more = self.definitionLine_ + self.definitionFit_ < self.definitionLines_;
  if (more || self.definitionLine_ > 0) {
    drawBottomKeyBar(t, "SU/GIU", more ? "Continua" : "Indietro", "BACK", "Testo");
  } else {
    drawBottomKeyBar(t, "BACK", "Torna al testo", "OK", "Altra parola");
  }
}

// ---------------------------------------------------------------------------
// Link cursor: footnotes and cross-references
// ---------------------------------------------------------------------------

void InklingApp::enterLinks() {
  if (!reader_ || !reader_->isOpen()) return;
  applyPendingLayout();
  linkBoxCount_ = reader_->pageLinks(linkBoxes_, sizeof(linkBoxes_) / sizeof(linkBoxes_[0]), &linkCount_);
  if (linkBoxCount_ == 0 || linkCount_ == 0) return;  // the menu already says there are none
  linkSel_ = 0;
  linkDrawn_ = false;
  enterMode(Mode::Links, linksScreen, this, RefreshHint::Clean);
}

void InklingApp::linksScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  auto paint = [&](uint8_t id) {
    for (uint16_t i = 0; i < self.linkBoxCount_; ++i) {
      const auto& b = self.linkBoxes_[i];
      if (b.id != id) continue;
      invertRect(Rect{static_cast<int16_t>(b.x - 2), static_cast<int16_t>(b.y - 1), static_cast<int16_t>(b.w + 4),
                      static_cast<int16_t>(b.h + 2)});
    }
  };
  static uint8_t drawnId = 0;
  if (!self.cursorOnlyRedraw_) {
    self.drawReaderPage(t);
    if (self.nightMode_) invertScreen();
  } else if (self.linkDrawn_) {
    paint(drawnId);
  }
  paint(self.linkSel_);
  drawnId = self.linkSel_;
  self.linkDrawn_ = true;
  char hint[72];
  snprintf(hint, sizeof(hint), "NOTA O LINK %u di %u · frecce: scegli · OK: apri · BACK: esci",
           static_cast<unsigned>(self.linkSel_ + 1), static_cast<unsigned>(self.linkCount_));
  self.drawReaderBar(t, hint);
  self.cursorOnlyRedraw_ = false;
}

void InklingApp::followSelectedLink() {
  const ReadingPosition from = reader_->position();
  char url[96];
  switch (reader_->followLink(linkSel_, url, sizeof(url))) {
    case BookReader::LinkResult::Jumped:
      hasLinkReturn_ = true;
      linkReturn_ = from;
      turnsSinceLink_ = 0;
      turnsSinceFull_ = 0;
      reader_->prepare();
      showReader(RefreshHint::Full);
      break;
    case BookReader::LinkResult::External:
      readerNotice_ = "Link a un sito: qui non si apre";
      showReader(RefreshHint::Clean);
      break;
    default:
      reader_->jumpTo(from);
      reader_->prepare();
      readerNotice_ = "Destinazione non trovata nel libro";
      showReader(RefreshHint::Clean);
      break;
  }
}

// ---------------------------------------------------------------------------
// Flashcards
// ---------------------------------------------------------------------------

void InklingApp::showFlashcards() {
  cards_.end();
  cards_.scan(storage_);
  deckMenu_.reset(cards_.deckCount() > 0 ? cards_.deckCount() : 1, 0);
  enterMode(Mode::Flashcards, flashcardsScreen, this, RefreshHint::Clean);
}

void InklingApp::flashcardsScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = screenW();
  auto& cards = self.cards_;
  clearScreen();
  drawTopHud(t, self.status_, "FLASHCARD");
  const int16_t top = static_cast<int16_t>(contentTop() + 2);
  const int16_t bottom = contentBottom();
  const int16_t cw = static_cast<int16_t>(w - 2 * kMargin);

  if (!cards.active()) {
    drawBottomKeyBar(t, "SU/GIU", "Mazzo", "OK", "Ripassa", "BACK", "Home");
    if (cards.deckCount() == 0) {
      const Rect box{kMargin, top, cw, 250};
      drawCard(t, box, 10, 3, false, true);
      drawText(t, Rect{static_cast<int16_t>(box.x + 16), static_cast<int16_t>(box.y + 12), static_cast<int16_t>(cw - 32), 30},
               "Nessun mazzo sulla scheda", kFontTitle, Color::Black);
      drawWrapped(t, Rect{static_cast<int16_t>(box.x + 16), static_cast<int16_t>(box.y + 54), static_cast<int16_t>(cw - 32), 180},
                  "Crea la cartella /flashcards sulla microSD e copiaci un file .csv per mazzo, una carta per "
                  "riga:\ndomanda,risposta\nVanno bene anche il punto e virgola, il tab e i mazzi di CrossPet. "
                  "Le carte tornano a intervalli crescenti man mano che le ricordi.",
                  kFontSmall);
      return;
    }
    const int16_t rowH = 70, gap = 10;
    int perPage = (bottom - top + gap) / (rowH + gap);
    if (perPage < 1) perPage = 1;
    const int sel = self.deckMenu_.selected;
    const int first = pageFirst(sel, cards.deckCount(), perPage);
    for (int slot = 0; slot < perPage; ++slot) {
      const int idx = first + slot;
      if (idx >= cards.deckCount()) break;
      const bool active = idx == sel;
      const Rect rr{kMargin, static_cast<int16_t>(top + slot * (rowH + gap)), cw, rowH};
      drawCard(t, rr, 8, active ? 4 : 2, active, false);
      const Color fg = active ? Color::White : Color::Black;
      drawIcon16(t, static_cast<int16_t>(rr.x + 16), static_cast<int16_t>(rr.y + 26), Icon::Check, fg);
      drawText(t, Rect{static_cast<int16_t>(rr.x + 44), static_cast<int16_t>(rr.y + 8), static_cast<int16_t>(cw - 60), 30},
               cards.deck(idx).name, kFontTitle, fg);
      drawText(t, Rect{static_cast<int16_t>(rr.x + 44), static_cast<int16_t>(rr.y + 40), static_cast<int16_t>(cw - 60), 22},
               cards.deck(idx).path, kFontMicro, fg);
    }
    return;
  }

  if (cards.finished()) {
    drawBottomKeyBar(t, "OK", "Mazzi", "BACK", "Mazzi");
    const Rect box{kMargin, top, cw, 250};
    drawCard(t, box, 10, 4, false, true);
    drawText(t, Rect{static_cast<int16_t>(box.x + 16), static_cast<int16_t>(box.y + 12), static_cast<int16_t>(cw - 32), 30},
             cards.reviewed() > 0 ? "Sessione finita" : "Niente da ripassare oggi", kFontTitle, Color::Black);
    char line[96];
    snprintf(line, sizeof(line), "%s", cards.deckName());
    drawText(t, Rect{static_cast<int16_t>(box.x + 16), static_cast<int16_t>(box.y + 50), static_cast<int16_t>(cw - 32), 26}, line,
             kFontBody, Color::Black);
    snprintf(line, sizeof(line), "Carte ripassate adesso: %d", cards.reviewed());
    drawText(t, Rect{static_cast<int16_t>(box.x + 16), static_cast<int16_t>(box.y + 92), static_cast<int16_t>(cw - 32), 24}, line,
             kFontSmall, Color::Black);
    snprintf(line, sizeof(line), "Carte già viste: %d su %d", cards.learnedCards(), cards.deckCards());
    drawText(t, Rect{static_cast<int16_t>(box.x + 16), static_cast<int16_t>(box.y + 120), static_cast<int16_t>(cw - 32), 24}, line,
             kFontSmall, Color::Black);
    drawProgressBar(t, Rect{static_cast<int16_t>(box.x + 16), static_cast<int16_t>(box.y + 152), static_cast<int16_t>(cw - 32), 14},
                    static_cast<uint8_t>(cards.deckCards() ? cards.learnedCards() * 100 / cards.deckCards() : 0), false);
    snprintf(line, sizeof(line), "Da ripassare entro domani: %d", cards.dueTomorrow());
    drawText(t, Rect{static_cast<int16_t>(box.x + 16), static_cast<int16_t>(box.y + 182), static_cast<int16_t>(cw - 32), 24}, line,
             kFontSmall, Color::Black);
    return;
  }

  // A card: the question, and under it the answer once revealed.
  char head[64];
  snprintf(head, sizeof(head), "%s", cards.deckName());
  drawText(t, Rect{static_cast<int16_t>(kMargin + 4), top, static_cast<int16_t>(cw - 150), 24}, head, kFontSmall, Color::Black);
  char left[32];
  snprintf(left, sizeof(left), "restano %d", cards.remaining());
  const int16_t leftW = static_cast<int16_t>(textWidth(t, left, kFontMicro) + 20);
  drawPill(t, static_cast<int16_t>(w - kMargin - leftW), top, 22, left, kFontMicro, true, 10);

  const bool back = cards.showingBack();
  const int16_t gradeH = back ? 64 : 0;
  const Rect card{kMargin, static_cast<int16_t>(top + 32), cw, static_cast<int16_t>(bottom - top - 32 - gradeH - (back ? 10 : 0))};
  drawCard(t, card, 12, 5, false, true);
  const int16_t tx = static_cast<int16_t>(card.x + 20);
  const int16_t tw = static_cast<int16_t>(card.width - 40);
  const int16_t frontH = static_cast<int16_t>(back ? (card.height - 40) * 2 / 5 : card.height - 40);
  drawPill(t, tx, static_cast<int16_t>(card.y + 14), 22, "DOMANDA", kFontMicro, true, 10);
  drawWrapped(t, Rect{tx, static_cast<int16_t>(card.y + 46), tw, static_cast<int16_t>(frontH - 30)}, cards.front(), kFontTitle);
  if (!back) {
    drawBottomKeyBar(t, "OK", "Mostra la risposta", "BACK", "Esci");
    return;
  }
  const int16_t divY = static_cast<int16_t>(card.y + 20 + frontH);
  fastFill(t, Rect{tx, divY, tw, 2}, Paint::solid(Color::Black));
  drawPill(t, tx, static_cast<int16_t>(divY + 12), 22, "RISPOSTA", kFontMicro, false, 10);
  drawWrapped(t, Rect{tx, static_cast<int16_t>(divY + 44), tw, static_cast<int16_t>(card.bottom() - divY - 54)}, cards.back(),
              kFontBody);

  // Four answers, one per key, each with when the card would come back.
  struct GradeKey {
    const char* key;
    const char* label;
    Flashcards::Grade grade;
  };
  static const GradeKey kGrades[4] = {
      {"SX", "Ancora", Flashcards::Grade::Again},
      {"GIU", "Difficile", Flashcards::Grade::Hard},
      {"OK", "Bene", Flashcards::Grade::Good},
      {"DX", "Facile", Flashcards::Grade::Easy},
  };
  const int16_t gy = static_cast<int16_t>(bottom - gradeH);
  const int16_t gw = static_cast<int16_t>((cw - 3 * 8) / 4);
  for (int i = 0; i < 4; ++i) {
    const Rect g{static_cast<int16_t>(kMargin + i * (gw + 8)), gy, gw, gradeH};
    const bool primary = i == 2;
    fastFill(t, g, Paint::solid(primary ? Color::Black : Color::White), 8);
    fastStroke(t, g, Paint::solid(Color::Black), 2, 8);
    const Color fg = primary ? Color::White : Color::Black;
    char first[24];
    snprintf(first, sizeof(first), "%s %s", kGrades[i].key, kGrades[i].label);
    drawText(t, Rect{g.x, static_cast<int16_t>(g.y + 8), g.width, 24}, first, fitFont(t, first, static_cast<int16_t>(gw - 8), kFontBody),
             fg, TextAlign::Center, false);
    const unsigned days = cards.intervalFor(kGrades[i].grade);
    char when[24];
    if (days == 0) {
      snprintf(when, sizeof(when), "subito");
    } else {
      snprintf(when, sizeof(when), "tra %u g", days);
    }
    drawText(t, Rect{g.x, static_cast<int16_t>(g.y + 36), g.width, 20}, when, kFontMicro, fg, TextAlign::Center, false);
  }
  drawBottomKeyBar(t, "TASTI", "Quanto la sapevi?", "BACK", "Esci");
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

void InklingApp::showStats() { enterMode(Mode::Stats, statsScreen, this, RefreshHint::Clean); }

void InklingApp::statsScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = screenW();
  const LocalTime now = self.currentLocalTime();
  clearScreen();
  drawTopHud(t, self.status_, "STATISTICHE");
  drawBottomKeyBar(t, "BACK", "Home", "OK", "Home");

  // Three figures: today, the streak, the total.
  const int16_t top = static_cast<int16_t>(contentTop() + 2);
  const int16_t tileW = static_cast<int16_t>((w - 2 * kMargin - 2 * 10) / 3);
  const int16_t tileH = 112;
  char big[3][24], small[3][40];
  static const char* const kTags[3] = {"OGGI", "SERIE", "IN TUTTO"};
  snprintf(big[0], sizeof(big[0]), "%u pag", static_cast<unsigned>(self.stats_.todayPages(now)));
  snprintf(small[0], sizeof(small[0]), "%u min di lettura", static_cast<unsigned>(self.stats_.todayMinutes(now)));
  const unsigned streak = self.stats_.streakDays(now);
  snprintf(big[1], sizeof(big[1]), "%u %s", streak, streak == 1 ? "giorno" : "giorni");
  snprintf(small[1], sizeof(small[1]), "%s", streak > 0 ? "di fila con una pagina" : "leggi oggi per ripartire");
  snprintf(big[2], sizeof(big[2]), "%lu pag", static_cast<unsigned long>(self.stats_.totalPages()));
  const uint32_t minutes = self.stats_.totalMinutes();
  snprintf(small[2], sizeof(small[2]), "%lu h %02lu min", static_cast<unsigned long>(minutes / 60),
           static_cast<unsigned long>(minutes % 60));
  for (int i = 0; i < 3; ++i) {
    const Rect tile{static_cast<int16_t>(kMargin + i * (tileW + 10)), top, tileW, tileH};
    drawCard(t, tile, 8, 3, false, i == 0);
    drawPill(t, static_cast<int16_t>(tile.x + 12), static_cast<int16_t>(tile.y + 10), 22, kTags[i], kFontMicro, true, 10);
    drawText(t, Rect{static_cast<int16_t>(tile.x + 12), static_cast<int16_t>(tile.y + 42), static_cast<int16_t>(tileW - 20), 32}, big[i],
             fitFont(t, big[i], static_cast<int16_t>(tileW - 20), kFontTitle), Color::Black);
    drawText(t, Rect{static_cast<int16_t>(tile.x + 12), static_cast<int16_t>(tile.y + 78), static_cast<int16_t>(tileW - 20), 22}, small[i],
             fitFont(t, small[i], static_cast<int16_t>(tileW - 20), kFontSmall), Color::Black);
  }

  // Pages per day over the last week.
  const int16_t libH = 62;
  const Rect chart{kMargin, static_cast<int16_t>(top + tileH + 12), static_cast<int16_t>(w - 2 * kMargin),
                   static_cast<int16_t>(contentBottom() - (top + tileH + 12) - libH - 10)};
  drawCard(t, chart, 8, 3, false, false);
  drawText(t, Rect{static_cast<int16_t>(chart.x + 14), static_cast<int16_t>(chart.y + 8), 300, 22}, "PAGINE NEGLI ULTIMI 7 GIORNI",
           kFontMicro, Color::Black);
  ReadingStats::Day week[7];
  self.stats_.lastWeek(now, week);
  unsigned peak = 1;
  for (const auto& d : week) peak = d.pages > peak ? d.pages : peak;
  const int16_t barsTop = static_cast<int16_t>(chart.y + 60);
  const int16_t barsBottom = static_cast<int16_t>(chart.bottom() - 36);
  const int16_t slot = static_cast<int16_t>((chart.width - 28) / 7);
  static const char* const kDayLetters[7] = {"L", "M", "M", "G", "V", "S", "D"};
  const int todayWeekday = now.valid ? weekdayMondayFirst(now.year, now.month, now.day) : 6;
  for (int i = 0; i < 7; ++i) {
    const int16_t bx = static_cast<int16_t>(chart.x + 14 + i * slot + 6);
    const int16_t bw = static_cast<int16_t>(slot - 12);
    const int16_t full = static_cast<int16_t>(barsBottom - barsTop);
    int16_t bh = static_cast<int16_t>((static_cast<int32_t>(week[i].pages) * full) / static_cast<int32_t>(peak));
    if (week[i].pages > 0 && bh < 4) bh = 4;
    fastFill(t, Rect{bx, static_cast<int16_t>(barsBottom - 1), bw, 1}, Paint::solid(Color::Black));
    if (bh > 0) {
      fastFill(t, Rect{bx, static_cast<int16_t>(barsBottom - bh), bw, bh}, Paint::solid(i == 6 ? Color::Black : Color::LightGray), 3);
      fastStroke(t, Rect{bx, static_cast<int16_t>(barsBottom - bh), bw, bh}, Paint::solid(Color::Black), 1, 3);
      char value[8];
      snprintf(value, sizeof(value), "%u", static_cast<unsigned>(week[i].pages));
      drawText(t, Rect{bx, static_cast<int16_t>(barsBottom - bh - 20), bw, 18}, value, kFontMicro, Color::Black, TextAlign::Center,
               false);
    }
    const int weekday = ((todayWeekday - (6 - i)) % 7 + 7) % 7;
    drawText(t, Rect{bx, static_cast<int16_t>(barsBottom + 8), bw, 20}, kDayLetters[weekday], i == 6 ? kFontBody : kFontSmall,
             Color::Black, TextAlign::Center, false);
  }

  // The library in one line.
  int started = 0, finished = 0;
  for (int i = 0; i < self.library_.count(); ++i) {
    const auto& b = self.library_.at(i);
    if (b.progressPct >= 99) {
      ++finished;
    } else if (b.started) {
      ++started;
    }
  }
  const Rect lib{kMargin, static_cast<int16_t>(chart.bottom() + 10), static_cast<int16_t>(w - 2 * kMargin), libH};
  drawCard(t, lib, 8, 2, false, false);
  char libLine[80];
  snprintf(libLine, sizeof(libLine), "%d volumi sulla scheda · %d in lettura · %d finiti", self.library_.count(), started,
           finished);
  drawText(t, Rect{static_cast<int16_t>(lib.x + 14), static_cast<int16_t>(lib.y + 8), static_cast<int16_t>(lib.width - 28), 24}, libLine,
           fitFont(t, libLine, static_cast<int16_t>(lib.width - 28), kFontBody), Color::Black);
  char cardsLine[64];
  snprintf(cardsLine, sizeof(cardsLine), "%d %s di flashcard", self.cards_.deckCount(),
           self.cards_.deckCount() == 1 ? "mazzo" : "mazzi");
  drawText(t, Rect{static_cast<int16_t>(lib.x + 14), static_cast<int16_t>(lib.y + 34), static_cast<int16_t>(lib.width - 28), 22},
           cardsLine, kFontSmall, Color::Black);
}

// ---------------------------------------------------------------------------
// Keys
// ---------------------------------------------------------------------------

void InklingApp::onToolButton(Button button) {
  switch (mode_) {
    case Mode::Marks: {
      const int count = marks_.count();
      if ((button == Button::Up || button == Button::Down) && count > 0) {
        marksMenu_.move(button == Button::Up ? -1 : 1);
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Confirm && count > 0) {
        const Mark& m = marks_.at(marksMenu_.selected);
        reader_->jumpTo(ReadingPosition{m.spine, m.charStart});
        reader_->prepare();
        turnsSinceFull_ = 0;
        showReader(RefreshHint::Full);
      } else if (button == Button::Right && count > 0) {
        const int sel = marksMenu_.selected;
        marks_.remove(storage_, sel);
        const int left = marks_.count();
        marksMenu_.reset(left > 0 ? left : 1, sel < left ? sel : (left > 0 ? left - 1 : 0));
        ui_.invalidate(RefreshHint::Clean);
      } else if (button == Button::Back) {
        showReaderMenu();
      }
      break;
    }

    case Mode::Select:
      if (button == Button::Confirm) {
        confirmSelection();
      } else if (button == Button::Back) {
        if (selPurpose_ == SelectPurpose::Clip && selAnchor_ != kNoAnchor) {
          selAnchor_ = kNoAnchor;  // start over, stay on the page
          cursorOnlyRedraw_ = true;
          ui_.invalidate(RefreshHint::Fast);
        } else {
          leaveSelect();
          showReader(RefreshHint::Clean);
        }
      } else {
        moveSelection(button);
      }
      break;

    case Mode::Links:
      if (button == Button::Confirm) {
        followSelectedLink();
      } else if (button == Button::Back) {
        showReader(RefreshHint::Clean);
      } else if (linkCount_ > 1) {
        const bool forward = button == Button::Down || button == Button::Right;
        linkSel_ = static_cast<uint8_t>((linkSel_ + (forward ? 1 : linkCount_ - 1)) % linkCount_);
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      }
      break;

    case Mode::Definition:
      if (button == Button::Down || button == Button::Right) {
        if (definitionLine_ + definitionFit_ < definitionLines_) {
          definitionLine_ += definitionFit_;
          ui_.invalidate(RefreshHint::Fast);
        }
      } else if (button == Button::Up || button == Button::Left) {
        if (definitionLine_ > 0) {
          definitionLine_ = definitionLine_ > definitionFit_ ? definitionLine_ - definitionFit_ : 0;
          ui_.invalidate(RefreshHint::Fast);
        }
      } else if (selWords_ && selCount_ > 0) {
        // Back to the page, the cursor where it was: another word, or Back again.
        selDrawn_ = false;
        enterMode(Mode::Select, selectScreen, this, RefreshHint::Clean);
      } else {
        showReader(RefreshHint::Clean);
      }
      break;

    case Mode::Flashcards:
      if (!cards_.active()) {
        if ((button == Button::Up || button == Button::Down) && cards_.deckCount() > 0) {
          deckMenu_.move(button == Button::Up ? -1 : 1);
          ui_.invalidate(RefreshHint::Fast);
        } else if (button == Button::Confirm && cards_.deckCount() > 0) {
          if (!cards_.begin(storage_, deckMenu_.selected, today())) {
            showMessage("Mazzo vuoto o non leggibile");
            return;
          }
          ui_.invalidate(RefreshHint::Clean);
        } else if (button == Button::Back) {
          showHome();
        }
      } else if (cards_.finished()) {
        if (button == Button::Confirm || button == Button::Back) showFlashcards();
      } else if (button == Button::Back) {
        showFlashcards();
      } else if (!cards_.showingBack()) {
        if (button == Button::Confirm || button == Button::Down || button == Button::Right) {
          cards_.reveal();
          ui_.invalidate(RefreshHint::Fast);
        }
      } else {
        Flashcards::Grade grade;
        if (button == Button::Left) {
          grade = Flashcards::Grade::Again;
        } else if (button == Button::Down) {
          grade = Flashcards::Grade::Hard;
        } else if (button == Button::Confirm) {
          grade = Flashcards::Grade::Good;
        } else if (button == Button::Right) {
          grade = Flashcards::Grade::Easy;
        } else {
          break;
        }
        cards_.grade(storage_, grade);
        ui_.invalidate(cards_.finished() ? RefreshHint::Clean : RefreshHint::Fast);
      }
      break;

    case Mode::Stats:
      if (button == Button::Back || button == Button::Confirm) showHome();
      break;

    default:
      break;
  }
}

}  // namespace inkling
