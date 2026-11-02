#include "App.h"

#include <cstdio>
#include <cstring>

namespace inkling {

namespace {

using freeink::ui::RefreshHint;

// Status line under the page: title, page in chapter, book percentage.
constexpr int16_t kStatusLineHeight = 34;
constexpr uint8_t kFullRefreshEvery = 8;

constexpr freeink::ui::ActionId kOpenBook = 1;

}  // namespace

InklingApp::InklingApp(Storage& storage, BookReader* reader, DeviceStatus& status, UiApp& ui,
                       const freeink::book::FrameTarget& frame)
    : storage_(storage), reader_(reader), status_(status), ui_(ui), frame_(frame) {}

void InklingApp::begin() {
  library_.scan(storage_);
  selected_ = 0;
  nav_.reset(0);
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

void InklingApp::moveSelection(int delta) {
  // After the books: "Diagnostica", then "Aggiornamento Wi-Fi".
  const int rows = library_.count() + 2;
  selected_ = (selected_ + delta + rows) % rows;
  nav_.selected = selected_;
  nav_.followPending = true;
  ui_.invalidate(RefreshHint::Fast);
}

void InklingApp::openSelected() {
  if (selected_ == library_.count()) {
    showDiagnostic();
    return;
  }
  if (selected_ == library_.count() + 1) {
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
  const Library::Entry& book = library_.at(selected_);

  // Opening can take seconds the first time (indexing + first chapter
  // layout): say so on the panel before starting.
  showMessage("Apro il libro...");
  flush(RefreshHint::Fast);

  ReaderLayout layout;
  layout.pageHeight = static_cast<int16_t>(frame_.width - kStatusLineHeight);  // portrait: logical height
  const auto st = reader_->open(book.path, layout);
  if (st != freeink::book::BookStatus::Ok) {
    char text[96];
    snprintf(text, sizeof(text), "Impossibile aprire il libro (%s)", freeink::book::bookStatusName(st));
    showMessage(text);
    return;
  }
  reader_->prepare();  // first chapter layout, before the UI frame starts
  mode_ = Mode::Reader;
  turnsSinceFull_ = 0;
  ui_.setScreen(readerScreen, this, RefreshHint::Full);
}

void InklingApp::closeBook() {
  reader_->close();
  begin();
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

void InklingApp::onButton(Button button) {
  switch (mode_) {
    case Mode::Library:
      if (button == Button::Up || button == Button::Left) moveSelection(-1);
      if (button == Button::Down || button == Button::Right) moveSelection(1);
      if (button == Button::Confirm) openSelected();
      break;
    case Mode::Reader:
      if (button == Button::Right || button == Button::Down) turnPage(true);
      if (button == Button::Left || button == Button::Up) turnPage(false);
      if (button == Button::Back) closeBook();
      break;
    case Mode::Diagnostic:
      if (button == Button::Back) begin();
      else ui_.invalidate(RefreshHint::Fast);  // diagnostic shows the last key
      break;
    case Mode::Message:
      if (button == Button::Back || button == Button::Confirm) begin();
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
  list.selectedIndex = static_cast<int16_t>(self.selected_);
  list.topIndex = static_cast<uint16_t>(self.nav_.top);
  list.nav = &self.nav_;
  list.action = kOpenBook;
  screen.list(list);
}

void InklingApp::readerScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);

  // The page goes straight into the framebuffer (already cleared by the UI).
  self.lastRenderStatus_ = self.reader_->render(self.frame_);

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

}  // namespace inkling
