#include "App.h"

#include <cstdio>
#include <cstring>

namespace inkling {

namespace {

using freeink::ui::RefreshHint;

// Status line under the page: page in chapter, book percentage.
constexpr int16_t kStatusLineHeight = 34;
constexpr uint8_t kFullRefreshEvery = 8;

constexpr freeink::ui::ActionId kListAction = 1;

// Reading sizes in pixels on the X3's ~250 ppi panel (30 px is about 8.5 pt).
constexpr uint16_t kFontSizes[] = {24, 27, 30, 33, 36, 40};
constexpr uint8_t kFontSizeCount = sizeof(kFontSizes) / sizeof(kFontSizes[0]);

constexpr const char* kPrefsPath = "/.inkling/prefs.bin";
constexpr const char* kLastBookPath = "/.inkling/last.txt";
// '1' while a book reopened at boot has not drawn its first page: if that
// crashes, the next boot sees the lock and goes to the library instead of
// crashing again.
constexpr const char* kResumeLockPath = "/.inkling/resume.lock";

struct Prefs {
  uint32_t magic;
  uint8_t fontStep;
  uint8_t reserved[3];
};
constexpr uint32_t kPrefsMagic = 0x46525049;  // "IPRF"

// Reader menu rows.
enum MenuItem : int { kMenuContinue, kMenuToc, kMenuBigger, kMenuSmaller, kMenuLibrary, kMenuCount };

}  // namespace

void InklingApp::Menu::reset(int n, int select) {
  count = n;
  selected = (select >= 0 && select < n) ? select : 0;
  nav.reset(selected);
  nav.followPending = true;
}

void InklingApp::Menu::move(int delta) {
  if (count <= 0) return;
  selected = (selected + delta + count) % count;
  nav.selected = selected;
  nav.followPending = true;
}

InklingApp::InklingApp(Storage& storage, BookReader* reader, DeviceStatus& status, UiApp& ui,
                       const freeink::book::FrameTarget& frame)
    : storage_(storage), reader_(reader), status_(status), ui_(ui), frame_(frame) {}

void InklingApp::begin() {
  storage_.mkdirs("/.inkling");
  loadPrefs();
  library_.scan(storage_);
  INKLING_LOG("library: %d books", library_.count());

  char last[128] = {};
  char lock = '0';
  storage_.readFile(kResumeLockPath, &lock, 1);
  const int32_t n = storage_.readFile(kLastBookPath, last, sizeof(last) - 1);
  if (lock == '1') {
    INKLING_LOG("resume skipped: last reopen did not finish");
    setResumeLock(false);
  } else if (n > 0 && reader_) {
    last[n] = '\0';
    for (int i = 0; i < library_.count(); ++i) {
      if (strcmp(library_.at(i).path, last) == 0 && openBook(last, true)) return;
    }
  }
  showLibrary();
}

void InklingApp::showLibrary() {
  // Keep the cursor on the book just closed, if any.
  int select = 0;
  if (reader_ && reader_->path()[0]) {
    for (int i = 0; i < library_.count(); ++i) {
      if (strcmp(library_.at(i).path, reader_->path()) == 0) select = i;
    }
  }
  libraryMenu_.reset(library_.count() + 2, select);  // + "Diagnostica", "Aggiornamento Wi-Fi"
  mode_ = Mode::Library;
  ui_.setScreen(libraryScreen, this, RefreshHint::Full);
}

void InklingApp::showDiagnostic() {
  mode_ = Mode::Diagnostic;
  ui_.setScreen(diagnosticScreen, &status_, RefreshHint::Full);
}

void InklingApp::showMessage(const char* text) {
  snprintf(message_, sizeof(message_), "%s", text);
  mode_ = Mode::Message;
  ui_.setScreen(messageScreen, message_, RefreshHint::Full);
}

void InklingApp::flush(RefreshHint hint) {
  if (!ui_.invalidated()) return;
  ui_.render(freeink::ui::InputSnapshot{});
  if (present_) present_(hint, presentUser_);
}

void InklingApp::update() {
  if (!ui_.invalidated()) return;
  flush(ui_.refreshHint());
}

void InklingApp::shutdown() {
  if (reader_) reader_->close();
}

void InklingApp::loadPrefs() {
  Prefs p{};
  if (storage_.readFile(kPrefsPath, &p, sizeof(p)) == static_cast<int32_t>(sizeof(p)) && p.magic == kPrefsMagic &&
      p.fontStep < kFontSizeCount) {
    fontStep_ = p.fontStep;
  }
}

void InklingApp::savePrefs() {
  const Prefs p{kPrefsMagic, fontStep_, {0, 0, 0}};
  storage_.writeFile(kPrefsPath, &p, sizeof(p));
}

void InklingApp::setResumeLock(bool locked) {
  const char c = locked ? '1' : '0';
  storage_.writeFile(kResumeLockPath, &c, 1);
}

void InklingApp::openSelected() {
  const int books = library_.count();
  const int row = libraryMenu_.selected;
  if (row == books) {
    showDiagnostic();
    return;
  }
  if (row == books + 1) {
    if (request_) {
      showMessage("Riavvio in modalita' Wi-Fi...");
      flush(RefreshHint::Fast);
      request_(Request::WifiUpdate, requestUser_);
    } else {
      showMessage("Aggiornamento Wi-Fi non disponibile");
    }
    return;
  }
  if (!reader_) {
    showMessage("Memoria insufficiente per aprire i libri");
    return;
  }
  openBook(library_.at(row).path, false);
}

bool InklingApp::openBook(const char* path, bool resuming) {
  // Opening can take seconds the first time (indexing + first chapter
  // layout): say so on the panel before starting.
  showMessage("Apro il libro...");
  flush(RefreshHint::Fast);

  if (resuming) setResumeLock(true);
  INKLING_LOG("open %s (font %u px)", path, kFontSizes[fontStep_]);
  ReaderLayout layout;
  layout.pageHeight = static_cast<int16_t>(frame_.width - kStatusLineHeight);  // portrait: logical height
  layout.fontSizePx = kFontSizes[fontStep_];
  const auto st = reader_->open(path, layout);
  if (st != freeink::book::BookStatus::Ok) {
    if (resuming) setResumeLock(false);
    char text[96];
    snprintf(text, sizeof(text), "Impossibile aprire il libro (%s)", freeink::book::bookStatusName(st));
    showMessage(text);
    return false;
  }
  storage_.writeFile(kLastBookPath, path, static_cast<uint32_t>(strlen(path)));
  const auto prep = reader_->prepare();  // first chapter layout, before the UI frame starts
  INKLING_LOG("opened, first chapter: %s", freeink::book::bookStatusName(prep));
  resumePending_ = resuming;
  turnsSinceFull_ = 0;
  showReader(RefreshHint::Full);
  return true;
}

void InklingApp::closeBook() {
  reader_->close();
  storage_.writeFile(kLastBookPath, "", 0);  // back in the library on the next boot
  showLibrary();
}

void InklingApp::showReader(RefreshHint hint) {
  mode_ = Mode::Reader;
  ui_.setScreen(readerScreen, this, hint);
}

void InklingApp::turnPage(bool forward) {
  if (!(forward ? reader_->nextPage() : reader_->prevPage())) return;
  // Lay out a new chapter now, outside the UI frame (it may borrow the
  // framebuffer as parse arena).
  reader_->prepare();
  if (++turnsSinceFull_ >= kFullRefreshEvery) {
    turnsSinceFull_ = 0;
    ui_.invalidate(RefreshHint::Full);
  } else {
    ui_.invalidate(RefreshHint::Fast);
  }
}

void InklingApp::showReaderMenu() {
  readerMenu_.reset(kMenuCount, kMenuContinue);
  mode_ = Mode::ReaderMenu;
  ui_.setScreen(readerMenuScreen, this, RefreshHint::Fast);
}

void InklingApp::showToc() {
  const int entries = static_cast<int>(reader_->tocCount());
  if (entries == 0) {
    showReader(RefreshHint::Fast);  // a TXT, or an EPUB without a TOC
    return;
  }
  tocMenu_.reset(entries, reader_->currentTocIndex());
  mode_ = Mode::Toc;
  ui_.setScreen(tocScreen, this, RefreshHint::Fast);
}

void InklingApp::changeFontSize(int delta) {
  const int next = static_cast<int>(fontStep_) + delta;
  if (next < 0 || next >= kFontSizeCount) {
    showReader(RefreshHint::Fast);
    return;
  }
  fontStep_ = static_cast<uint8_t>(next);
  savePrefs();
  // Reopen at the new size: close() saves the position as a character
  // offset, which lands on the same sentence after the relayout.
  char path[128];
  snprintf(path, sizeof(path), "%s", reader_->path());
  reader_->close();
  openBook(path, false);
}

void InklingApp::runMenuItem(int item) {
  switch (item) {
    case kMenuContinue: showReader(RefreshHint::Fast); break;
    case kMenuToc: showToc(); break;
    case kMenuBigger: changeFontSize(1); break;
    case kMenuSmaller: changeFontSize(-1); break;
    case kMenuLibrary: closeBook(); break;
    default: break;
  }
}

void InklingApp::onButton(Button button) {
  const bool up = button == Button::Up || button == Button::Left;
  const bool down = button == Button::Down || button == Button::Right;
  switch (mode_) {
    case Mode::Library:
      if (up || down) {
        libraryMenu_.move(up ? -1 : 1);
        ui_.invalidate(RefreshHint::Fast);
      }
      if (button == Button::Confirm) openSelected();
      break;
    case Mode::Reader:
      if (down) turnPage(true);
      if (up) turnPage(false);
      if (button == Button::Confirm) showReaderMenu();
      if (button == Button::Back) closeBook();
      break;
    case Mode::ReaderMenu:
      if (up || down) {
        readerMenu_.move(up ? -1 : 1);
        ui_.invalidate(RefreshHint::Fast);
      }
      if (button == Button::Confirm) runMenuItem(readerMenu_.selected);
      if (button == Button::Back) showReader(RefreshHint::Fast);
      break;
    case Mode::Toc:
      if (up || down) {
        tocMenu_.move(up ? -1 : 1);
        ui_.invalidate(RefreshHint::Fast);
      }
      if (button == Button::Confirm) {
        char title[8];
        const int spine = reader_->tocEntry(static_cast<size_t>(tocMenu_.selected), title, sizeof(title), nullptr);
        if (spine >= 0) {
          reader_->jumpToChapter(static_cast<uint16_t>(spine));
          reader_->prepare();
        }
        turnsSinceFull_ = 0;
        showReader(RefreshHint::Full);
      }
      if (button == Button::Back) showReaderMenu();
      break;
    case Mode::Diagnostic:
      if (button == Button::Back) showLibrary();
      else ui_.invalidate(RefreshHint::Fast);  // diagnostic shows the last key
      break;
    case Mode::Message:
      if (button == Button::Back || button == Button::Confirm) showLibrary();
      break;
  }
}

void InklingApp::libraryScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  const int books = self.library_.count();

  static freeink::ui::ListItem items[Library::kMaxBooks + 2];
  for (int i = 0; i < books; ++i) {
    items[i] = freeink::ui::ListItem{};
    items[i].label = self.library_.at(i).name;
    items[i].actionValue = static_cast<int16_t>(i);
  }
  // System rows stay reachable even with no books on the card.
  items[books] = freeink::ui::ListItem{};
  items[books].label = "Diagnostica";
  items[books].actionValue = static_cast<int16_t>(books);
  items[books + 1] = freeink::ui::ListItem{};
  items[books + 1].label = "Aggiornamento Wi-Fi";
  items[books + 1].actionValue = static_cast<int16_t>(books + 1);

  static char subtitle[24];
  if (books == 0) {
    snprintf(subtitle, sizeof(subtitle), "nessun libro");
  } else {
    snprintf(subtitle, sizeof(subtitle), "%d %s", books, books == 1 ? "libro" : "libri");
  }
  screen.header("Libreria", nullptr, subtitle);

  freeink::ui::ListProps list;
  list.items = items;
  list.count = static_cast<uint16_t>(books + 2);
  list.selectedIndex = static_cast<int16_t>(self.libraryMenu_.selected);
  list.topIndex = static_cast<uint16_t>(self.libraryMenu_.nav.top);
  list.nav = &self.libraryMenu_.nav;
  list.action = kListAction;
  screen.list(list);
}

void InklingApp::readerScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);

  // The page goes straight into the framebuffer (already cleared by the UI).
  self.lastRenderStatus_ = self.reader_->render(self.frame_);
  if (self.resumePending_ && self.lastRenderStatus_ == freeink::book::BookStatus::Ok) {
    self.resumePending_ = false;
    self.setResumeLock(false);
  }

  static char line[128];
  if (self.lastRenderStatus_ != freeink::book::BookStatus::Ok) {
    snprintf(line, sizeof(line), "Errore pagina: %s", freeink::book::bookStatusName(self.lastRenderStatus_));
  } else {
    snprintf(line, sizeof(line), "%u/%u  -  %u%%", static_cast<unsigned>(self.reader_->pageInChapter()),
             static_cast<unsigned>(self.reader_->pagesInChapter()), static_cast<unsigned>(self.reader_->percent()));
  }
  const freeink::ui::Rect body = screen.body();
  const freeink::ui::Rect status{body.x, static_cast<int16_t>(body.bottom() - kStatusLineHeight), body.width,
                                 kStatusLineHeight};
  freeink::ui::TextStyle style = screen.theme().smallText;
  style.align = freeink::ui::TextAlign::Center;
  screen.target().text(status, line, style);
}

void InklingApp::readerMenuScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  static char bigger[40], smaller[40];
  const uint8_t step = self.fontStep_;
  // Each row names the size it leads to; the current one is in the header.
  if (step + 1 < kFontSizeCount) {
    snprintf(bigger, sizeof(bigger), "Testo più grande (%u px)", kFontSizes[step + 1]);
  } else {
    snprintf(bigger, sizeof(bigger), "Testo più grande");
  }
  if (step > 0) {
    snprintf(smaller, sizeof(smaller), "Testo più piccolo (%u px)", kFontSizes[step - 1]);
  } else {
    snprintf(smaller, sizeof(smaller), "Testo più piccolo");
  }

  static freeink::ui::ListItem items[kMenuCount];
  const char* labels[kMenuCount] = {"Continua a leggere", "Indice dei capitoli", bigger, smaller,
                                    "Torna alla libreria"};
  for (int i = 0; i < kMenuCount; ++i) {
    items[i] = freeink::ui::ListItem{};
    items[i].label = labels[i];
    items[i].actionValue = static_cast<int16_t>(i);
  }
  items[kMenuToc].enabled = self.reader_->tocCount() > 0;
  items[kMenuBigger].enabled = step + 1 < kFontSizeCount;
  items[kMenuSmaller].enabled = step > 0;

  static char subtitle[24];
  snprintf(subtitle, sizeof(subtitle), "%u%%  -  %u px", static_cast<unsigned>(self.reader_->percent()),
           kFontSizes[step]);
  screen.header(self.reader_->title(), nullptr, subtitle);

  freeink::ui::ListProps list;
  list.items = items;
  list.count = kMenuCount;
  list.selectedIndex = static_cast<int16_t>(self.readerMenu_.selected);
  list.topIndex = static_cast<uint16_t>(self.readerMenu_.nav.top);
  list.nav = &self.readerMenu_.nav;
  list.action = kListAction;
  screen.list(list);
}

void InklingApp::tocScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  const int count = self.tocMenu_.count;

  // Only a window of entries around the selection is materialized: an
  // omnibus TOC can have hundreds of rows, and titles stay on the card.
  constexpr int kWindow = 24;
  constexpr int kTitleLen = 56;
  static freeink::ui::ListItem items[kWindow];
  static char titles[kWindow][kTitleLen];
  int first = self.tocMenu_.selected - kWindow / 2;
  if (first > count - kWindow) first = count - kWindow;
  if (first < 0) first = 0;
  const int n = count - first < kWindow ? count - first : kWindow;
  for (int i = 0; i < n; ++i) {
    char raw[kTitleLen];
    uint8_t depth = 0;
    raw[0] = '\0';
    self.reader_->tocEntry(static_cast<size_t>(first + i), raw, sizeof(raw), &depth);
    // Indent sub-chapters a little; deep nesting is flattened to two levels.
    snprintf(titles[i], kTitleLen, "%s%s", depth > 0 ? "   " : "", raw[0] ? raw : "(senza titolo)");
    items[i] = freeink::ui::ListItem{};
    items[i].label = titles[i];
    items[i].actionValue = static_cast<int16_t>(first + i);
  }

  screen.header("Indice", nullptr, self.reader_->title());

  freeink::ui::ListProps list;
  list.items = items;
  list.count = static_cast<uint16_t>(count);
  list.itemsWindowFirst = static_cast<uint16_t>(first);
  list.itemsWindowCount = static_cast<uint16_t>(n);
  list.selectedIndex = static_cast<int16_t>(self.tocMenu_.selected);
  list.topIndex = static_cast<uint16_t>(self.tocMenu_.nav.top);
  list.nav = &self.tocMenu_.nav;
  list.action = kListAction;
  screen.list(list);
}

}  // namespace inkling
