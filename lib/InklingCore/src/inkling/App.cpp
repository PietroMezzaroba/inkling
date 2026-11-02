#include "App.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "inkling/ui/CyberCanvas.h"

namespace inkling {

using namespace inkling::ui;
using namespace inkling::ui::canvas;

namespace {

using freeink::ui::RefreshHint;

// Futuristic status HUD height at the bottom of the reader page.
constexpr int16_t kStatusLineHeight = 38;
// Only run a full scrub every 24th page turn so reading stays ultra-fluid.
constexpr uint8_t kFullRefreshEvery = 24;

// CrossPet-calibrated em-box heights in pixels on the X3's 250 ppi panel.
// In stb_truetype, sizePx spans (ascent - descent): 42 px matches CrossPet's 14-16pt Noto Serif.
constexpr uint16_t kFontSizes[] = {30, 34, 38, 42, 46, 52, 58, 66};
constexpr uint8_t kFontSizeCount = sizeof(kFontSizes) / sizeof(kFontSizes[0]);
static const char* const kFontSizeLabels[kFontSizeCount] = {
    "XS", "S", "M-", "Medio", "Grande", "XL", "XXL", "MAX",
};

struct SpacingPreset {
  const char* label;
  uint16_t lineSpacingPct;
  uint16_t paraSpacingPct;
  int16_t marginH;
  int16_t marginTop;
};

static constexpr SpacingPreset kSpacingPresets[] = {
    {"Classico (102%)", 102, 100, 22, 20},
    {"Compatto (92%)", 92, 65, 14, 14},
    {"Arioso (116%)", 116, 115, 22, 22},
    {"Zen Ampio (128%)", 128, 130, 34, 26},
    {"Focus Largo", 116, 100, 14, 18},
};
constexpr uint8_t kSpacingPresetCount = sizeof(kSpacingPresets) / sizeof(kSpacingPresets[0]);

struct AlignPreset {
  const char* label;
  bool justify;
  bool hyphenation;
  bool publisherCss;
};

static constexpr AlignPreset kAlignPresets[] = {
    {"Giustificato + Sillab.", true, true, false},
    {"Sinistra + Sillab.", false, true, false},
    {"Sinistra (No Sillab.)", false, false, false},
    {"Stili CSS Editoriali", true, true, true},
};
constexpr uint8_t kAlignPresetCount = sizeof(kAlignPresets) / sizeof(kAlignPresets[0]);

constexpr const char* kPrefsPath = "/.inkling/prefs.bin";
constexpr const char* kLastBookPath = "/.inkling/last.txt";
constexpr const char* kResumeLockPath = "/.inkling/resume.lock";

struct Prefs {
  uint32_t magic;
  uint8_t fontStep;
  uint8_t fontFamily;
  uint8_t spacingStep;
  uint8_t alignStep;
  uint8_t focusReading;
  uint8_t nightMode;
  uint8_t reserved[2];
};
constexpr uint32_t kPrefsMagic = 0x334B4E49u;  // "INK3"

enum ReaderMenuItem : int {
  kMenuContinue = 0,
  kMenuToc = 1,
  kMenuFontSize = 2,
  kMenuFontFamily = 3,
  kMenuFocusReading = 4,
  kMenuSpacing = 5,
  kMenuAlignment = 6,
  kMenuNightMode = 7,
  kMenuHome = 8,
  kMenuCount = 9,
};

enum HomeDockItem : int {
  kDockRead = 0,
  kDockLibrary = 1,
  kDockSanctuary = 2,
  kDockSystem = 3,
  kDockCount = 4,
};

enum SanctuaryAction : int {
  kActFeed = 0,
  kActPet = 1,
  kActLamp = 2,
  kActRead = 3,
  kActCount = 4,
};

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
    : storage_(storage), reader_(reader), status_(status), ui_(ui), frame_(frame) {
  canvas::bindFramebuffer(frame_.framebuffer, frame_.width, frame_.height, frame_.widthBytes);
}

LocalTime InklingApp::currentLocalTime() const {
  LocalTime t{};
  t.valid = status_.clockValid;
  t.year = status_.year;
  t.month = status_.month;
  t.day = status_.day;
  t.hour = status_.hour;
  t.minute = status_.minute;
  return t;
}

int InklingApp::activeBookIndex() const {
  if (library_.count() == 0) return -1;
  if (reader_ && reader_->path()[0]) {
    for (int i = 0; i < library_.count(); ++i) {
      if (strcmp(library_.at(i).path, reader_->path()) == 0) return i;
    }
  }
  if (lastBookPath_[0]) {
    for (int i = 0; i < library_.count(); ++i) {
      if (strcmp(library_.at(i).path, lastBookPath_) == 0) return i;
    }
  }
  // Pick the most recently started book, or index 0.
  for (int i = 0; i < library_.count(); ++i) {
    if (library_.at(i).started) return i;
  }
  return 0;
}

void InklingApp::begin() {
  storage_.mkdirs("/.inkling");
  loadPrefs();
  library_.scan(storage_);
  pet_.load(storage_, currentLocalTime());
  homeDock_.reset(kDockCount, 0);
  sanctuaryMenu_.reset(kActCount, 0);
  INKLING_LOG("library: %d books, pet lv %u (%lu pages)", library_.count(),
              static_cast<unsigned>(pet_.state().level), static_cast<unsigned long>(pet_.state().totalPagesRead));

  char last[128] = {};
  char lock = '0';
  storage_.readFile(kResumeLockPath, &lock, 1);
  const int32_t n = storage_.readFile(kLastBookPath, last, sizeof(last) - 1);
  if (n > 0) {
    last[n] = '\0';
    snprintf(lastBookPath_, sizeof(lastBookPath_), "%s", last);
  }

  if (lock == '1') {
    INKLING_LOG("resume skipped: last reopen did not finish");
    setResumeLock(false);
  } else if (n > 0 && reader_) {
    for (int i = 0; i < library_.count(); ++i) {
      if (strcmp(library_.at(i).path, last) == 0 && openBook(last, true)) return;
    }
  }
  showHome();
}

void InklingApp::showHome() {
  pet_.syncTime(currentLocalTime());
  pet_.stepAnim();
  if (homeDock_.count != kDockCount) homeDock_.reset(kDockCount, 0);
  cursorOnlyRedraw_ = false;
  mode_ = Mode::Home;
  ui_.setScreen(homeScreen, this, RefreshHint::Fast);
}

void InklingApp::showLibrary() {
  int select = activeBookIndex();
  if (select < 0) select = 0;
  // Rows: books + "Santuario Pet", "Hacker Matrix: Breach", "Diagnostica", "Aggiornamento Wi-Fi"
  libraryMenu_.reset(library_.count() + 4, select);
  cursorOnlyRedraw_ = false;
  mode_ = Mode::Library;
  ui_.setScreen(libraryScreen, this, RefreshHint::Fast);
}

void InklingApp::showPetSanctuary() {
  pet_.syncTime(currentLocalTime());
  pet_.stepAnim();
  if (sanctuaryMenu_.count != kActCount) sanctuaryMenu_.reset(kActCount, 0);
  cursorOnlyRedraw_ = false;
  mode_ = Mode::PetSanctuary;
  ui_.setScreen(petSanctuaryScreen, this, RefreshHint::Fast);
}

void InklingApp::showBreachGame() {
  cursorOnlyRedraw_ = false;
  mode_ = Mode::BreachGame;
  ui_.setScreen(breachGameScreen, this, RefreshHint::Fast);
}

void InklingApp::showDiagnostic() {
  cursorOnlyRedraw_ = false;
  mode_ = Mode::Diagnostic;
  ui_.setScreen(diagnosticScreen, &status_, RefreshHint::Fast);
}

void InklingApp::showMessage(const char* text) {
  snprintf(message_, sizeof(message_), "%s", text);
  cursorOnlyRedraw_ = false;
  mode_ = Mode::Message;
  ui_.setScreen(messageScreen, message_, RefreshHint::Fast);
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
  pet_.save(storage_);
}

void InklingApp::loadPrefs() {
  Prefs p{};
  const int32_t n = storage_.readFile(kPrefsPath, &p, sizeof(p));
  if (n == static_cast<int32_t>(sizeof(p)) && p.magic == kPrefsMagic) {
    if (p.fontStep < kFontSizeCount) fontStep_ = p.fontStep;
    if (p.fontFamily < kFontFamilyCount) fontFamily_ = p.fontFamily;
    if (p.spacingStep < kSpacingPresetCount) spacingStep_ = p.spacingStep;
    if (p.alignStep < kAlignPresetCount) alignStep_ = p.alignStep;
    focusReading_ = (p.focusReading != 0);
    nightMode_ = (p.nightMode != 0);
  }
}

void InklingApp::savePrefs() {
  const Prefs p{
      kPrefsMagic,
      fontStep_,
      fontFamily_,
      spacingStep_,
      alignStep_,
      static_cast<uint8_t>(focusReading_ ? 1 : 0),
      static_cast<uint8_t>(nightMode_ ? 1 : 0),
      {0, 0},
  };
  storage_.writeFile(kPrefsPath, &p, sizeof(p));
}

ReaderLayout InklingApp::currentLayout() const {
  ReaderLayout layout;
  layout.pageHeight = static_cast<int16_t>(frame_.width - kStatusLineHeight);
  layout.fontFamily = fontFamily_ < kFontFamilyCount ? fontFamily_ : 0;
  layout.fontSizePx = kFontSizes[fontStep_ < kFontSizeCount ? fontStep_ : 3];
  const auto& sp = kSpacingPresets[spacingStep_ < kSpacingPresetCount ? spacingStep_ : 0];
  layout.lineSpacingPct = sp.lineSpacingPct;
  layout.paragraphSpacingPct = sp.paraSpacingPct;
  layout.marginH = sp.marginH;
  layout.marginTop = sp.marginTop;
  layout.marginBottom = 14;
  const auto& al = kAlignPresets[alignStep_ < kAlignPresetCount ? alignStep_ : 0];
  layout.justify = al.justify;
  layout.hyphenation = al.hyphenation;
  layout.publisherCss = al.publisherCss;
  layout.focusReading = focusReading_;
  return layout;
}

void InklingApp::setResumeLock(bool locked) {
  const char c = locked ? '1' : '0';
  storage_.writeFile(kResumeLockPath, &c, 1);
}

void InklingApp::openActiveOrFirstBook() {
  if (!reader_) {
    showMessage("Memoria insufficiente per aprire i libri");
    return;
  }
  const int idx = activeBookIndex();
  if (idx < 0 || idx >= library_.count()) {
    showLibrary();
    return;
  }
  openBook(library_.at(idx).path, false);
}

void InklingApp::openSelected() {
  const int books = library_.count();
  const int row = libraryMenu_.selected;
  if (row == books) {
    showPetSanctuary();
    return;
  }
  if (row == books + 1) {
    showBreachGame();
    return;
  }
  if (row == books + 2) {
    showDiagnostic();
    return;
  }
  if (row == books + 3) {
    if (request_) {
      showMessage("Avvio Cyber-Net Wi-Fi & Portale Web...");
      flush(RefreshHint::Fast);
      request_(Request::WifiUpdate, requestUser_);
    } else {
      showMessage("Cyber-Net Wi-Fi non disponibile");
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
  bool alreadyStarted = false;
  for (int i = 0; i < library_.count(); ++i) {
    if (strcmp(library_.at(i).path, path) == 0 && library_.at(i).started) {
      alreadyStarted = true;
      break;
    }
  }
  if (!alreadyStarted) {
    showMessage("Sincronizzazione volume in corso...");
    flush(RefreshHint::Fast);
  }

  if (resuming) setResumeLock(true);
  const ReaderLayout layout = currentLayout();
  INKLING_LOG("open %s (font %s %u px, focus=%d, hyph=%d)", path, fontFamilyName(layout.fontFamily),
              layout.fontSizePx, layout.focusReading ? 1 : 0, layout.hyphenation ? 1 : 0);
  const auto st = reader_->open(path, layout);
  if (st != freeink::book::BookStatus::Ok) {
    if (resuming) setResumeLock(false);
    char text[96];
    snprintf(text, sizeof(text), "Impossibile aprire il libro (%s)", freeink::book::bookStatusName(st));
    showMessage(text);
    return false;
  }
  snprintf(lastBookPath_, sizeof(lastBookPath_), "%s", path);
  storage_.writeFile(kLastBookPath, path, static_cast<uint32_t>(strlen(path)));
  const auto prep = reader_->prepare();
  INKLING_LOG("opened, first chapter: %s", freeink::book::bookStatusName(prep));
  library_.refreshSummaries(storage_);
  pet_.resetSessionPages();
  resumePending_ = resuming;
  turnsSinceFull_ = 0;
  showReader(RefreshHint::Fast);
  return true;
}

void InklingApp::closeBook() {
  if (reader_) reader_->close();
  pet_.save(storage_);
  storage_.writeFile(kLastBookPath, "", 0);
  library_.refreshSummaries(storage_);
  showHome();
}

void InklingApp::showReader(RefreshHint hint) {
  cursorOnlyRedraw_ = false;
  mode_ = Mode::Reader;
  ui_.setScreen(readerScreen, this, hint);
}

void InklingApp::turnPage(bool forward) {
  bool chapterFlipped = false;
  if (!(forward ? reader_->nextPage(&chapterFlipped) : reader_->prevPage())) return;
  reader_->prepare();

  if (forward) {
    pet_.onPageRead(storage_, currentLocalTime(), chapterFlipped, reader_->atEnd());
  } else {
    pet_.consumeBannerTick();
  }

  if (++turnsSinceFull_ >= kFullRefreshEvery) {
    turnsSinceFull_ = 0;
    ui_.invalidate(RefreshHint::Full);
  } else {
    ui_.invalidate(RefreshHint::Fast);
  }
}

void InklingApp::showReaderMenu() {
  // When opening ReaderMenu directly from Reader, the book page is ALREADY in
  // frame_.framebuffer — no need to re-decode or re-rasterize the book page!
  needsReaderBackdrop_ = (mode_ != Mode::Reader);
  cursorOnlyRedraw_ = false;
  layoutDirty_ = false;
  readerMenu_.reset(kMenuCount, kMenuContinue);
  mode_ = Mode::ReaderMenu;
  ui_.setScreen(readerMenuScreen, this, RefreshHint::Fast);
}

void InklingApp::exitReaderMenu() {
  if (layoutDirty_ && reader_ && reader_->isOpen()) {
    reader_->relayout(currentLayout());
    turnsSinceFull_ = 0;
  }
  layoutDirty_ = false;
  showReader(RefreshHint::Fast);
}

void InklingApp::showToc() {
  if (layoutDirty_ && reader_ && reader_->isOpen()) {
    reader_->relayout(currentLayout());
    layoutDirty_ = false;
  }
  const int entries = static_cast<int>(reader_->tocCount());
  if (entries == 0) {
    showReader(RefreshHint::Fast);
    return;
  }
  cursorOnlyRedraw_ = false;
  tocMenu_.reset(entries, reader_->currentTocIndex());
  mode_ = Mode::Toc;
  ui_.setScreen(tocScreen, this, RefreshHint::Fast);
}

void InklingApp::adjustMenuItem(int item, int delta) {
  switch (item) {
    case kMenuContinue:
      if (reader_ && reader_->isOpen()) {
        if (layoutDirty_) {
          reader_->relayout(currentLayout());
          layoutDirty_ = false;
        }
        reader_->jumpRelativePercent(delta > 0 ? 10 : -10);
        reader_->prepare();
        showReader(RefreshHint::Fast);
      }
      return;
    case kMenuToc:
      if (reader_ && reader_->isOpen()) {
        if (layoutDirty_) {
          reader_->relayout(currentLayout());
          layoutDirty_ = false;
        }
        const int nextSpine = static_cast<int>(reader_->currentSpine()) + (delta > 0 ? 1 : -1);
        if (nextSpine >= 0 && nextSpine < static_cast<int>(reader_->spineCount())) {
          reader_->jumpToChapter(static_cast<uint16_t>(nextSpine));
          reader_->prepare();
          showReader(RefreshHint::Fast);
        }
      }
      return;
    case kMenuFontSize: {
      const int next = (static_cast<int>(fontStep_) + delta + kFontSizeCount) % kFontSizeCount;
      fontStep_ = static_cast<uint8_t>(next);
      layoutDirty_ = true;
      break;
    }
    case kMenuFontFamily: {
      const int next = (static_cast<int>(fontFamily_) + delta + kFontFamilyCount) % kFontFamilyCount;
      fontFamily_ = static_cast<uint8_t>(next);
      layoutDirty_ = true;
      break;
    }
    case kMenuFocusReading:
      focusReading_ = !focusReading_;
      layoutDirty_ = true;
      break;
    case kMenuSpacing: {
      const int next = (static_cast<int>(spacingStep_) + delta + kSpacingPresetCount) % kSpacingPresetCount;
      spacingStep_ = static_cast<uint8_t>(next);
      layoutDirty_ = true;
      break;
    }
    case kMenuAlignment: {
      const int next = (static_cast<int>(alignStep_) + delta + kAlignPresetCount) % kAlignPresetCount;
      alignStep_ = static_cast<uint8_t>(next);
      layoutDirty_ = true;
      break;
    }
    case kMenuNightMode:
      nightMode_ = !nightMode_;
      break;
    case kMenuHome:
      if (reader_) reader_->close();
      showPetSanctuary();
      return;
    default:
      return;
  }
  savePrefs();
  cursorOnlyRedraw_ = true;
  ui_.invalidate(RefreshHint::Fast);
}

void InklingApp::runMenuItem(int item) {
  switch (item) {
    case kMenuContinue:     exitReaderMenu(); break;
    case kMenuToc:          showToc(); break;
    case kMenuFontSize:     adjustMenuItem(kMenuFontSize, 1); break;
    case kMenuFontFamily:   adjustMenuItem(kMenuFontFamily, 1); break;
    case kMenuFocusReading: adjustMenuItem(kMenuFocusReading, 1); break;
    case kMenuSpacing:      adjustMenuItem(kMenuSpacing, 1); break;
    case kMenuAlignment:    adjustMenuItem(kMenuAlignment, 1); break;
    case kMenuNightMode:    adjustMenuItem(kMenuNightMode, 1); break;
    case kMenuHome:         closeBook(); break;
    default:                break;
  }
}

void InklingApp::runHomeDockItem(int item) {
  switch (item) {
    case kDockRead:      openActiveOrFirstBook(); break;
    case kDockLibrary:   showLibrary(); break;
    case kDockSanctuary: showPetSanctuary(); break;
    case kDockSystem:    showDiagnostic(); break;
    default:             break;
  }
}

void InklingApp::runSanctuaryAction(int item) {
  switch (item) {
    case kActFeed:
      pet_.feedInkCrystal(storage_);
      cursorOnlyRedraw_ = false;
      ui_.invalidate(RefreshHint::Fast);
      break;
    case kActPet:
      pet_.interact(storage_, false);
      showBreachGame();
      break;
    case kActLamp:
      pet_.toggleNightLamp(storage_);
      cursorOnlyRedraw_ = false;
      ui_.invalidate(RefreshHint::Fast);
      break;
    case kActRead:
      openActiveOrFirstBook();
      break;
    default:
      break;
  }
}

void InklingApp::onImuSample(float ax, float ay, float az) {
  if (imuCooldown_ > 0) --imuCooldown_;
  const float dx = ax - prevAx_;
  const float dy = ay - prevAy_;
  const float dz = az - prevAz_;
  prevAx_ = ax;
  prevAy_ = ay;
  prevAz_ = az;

  const float deltaSq = dx * dx + dy * dy + dz * dz;
  if (deltaSq > 0.65f && imuCooldown_ == 0) {
    imuCooldown_ = 8;
    if (mode_ == Mode::Home || mode_ == Mode::PetSanctuary) {
      pet_.interact(storage_, true);
      cursorOnlyRedraw_ = false;
      ui_.invalidate(RefreshHint::Fast);
    } else if (mode_ == Mode::BreachGame) {
      breach_.triggerEmpBlast();
      cursorOnlyRedraw_ = true;
      ui_.invalidate(RefreshHint::Fast);
    }
  }
}

void InklingApp::onButton(Button button) {
  const bool up = button == Button::Up || button == Button::Left;
  const bool down = button == Button::Down || button == Button::Right;
  switch (mode_) {
    case Mode::Home:
      if (up || down) {
        homeDock_.move(up ? -1 : 1);
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Confirm) {
        runHomeDockItem(homeDock_.selected);
      } else if (button == Button::Back) {
        showLibrary();
      }
      break;

    case Mode::Library:
      if (button == Button::Up) {
        libraryMenu_.move(-1);
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Down || button == Button::Right) {
        libraryMenu_.move(1);
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Confirm) {
        openSelected();
      } else if (button == Button::Back || button == Button::Left) {
        showHome();
      }
      break;

    case Mode::PetSanctuary:
      if (up || down) {
        sanctuaryMenu_.move(up ? -1 : 1);
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Confirm) {
        runSanctuaryAction(sanctuaryMenu_.selected);
      } else if (button == Button::Back) {
        showHome();
      }
      break;

    case Mode::BreachGame: {
      bool moved = false;
      bool cleared = false;
      if (button == Button::Up) {
        cleared = breach_.movePlayer(0, -1);
        moved = true;
      } else if (button == Button::Down) {
        cleared = breach_.movePlayer(0, 1);
        moved = true;
      } else if (button == Button::Left) {
        cleared = breach_.movePlayer(-1, 0);
        moved = true;
      } else if (button == Button::Right) {
        cleared = breach_.movePlayer(1, 0);
        moved = true;
      } else if (button == Button::Confirm) {
        breach_.triggerEmpBlast();
        moved = true;
      } else if (button == Button::Back) {
        showPetSanctuary();
        break;
      }
      if (cleared) {
        // Reward Kuro-Ink with +1 Ink Crystal & XP for every hacked Subnet!
        pet_.onPageRead(storage_, currentLocalTime(), true, false);
        pet_.interact(storage_, false);
      }
      if (moved) {
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      }
      break;
    }

    case Mode::Reader:
      if (down) turnPage(true);
      if (up) turnPage(false);
      if (button == Button::Confirm) showReaderMenu();
      if (button == Button::Back) closeBook();
      break;

    case Mode::ReaderMenu:
      if (button == Button::Up) {
        readerMenu_.move(-1);
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Down) {
        readerMenu_.move(1);
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Left) {
        adjustMenuItem(readerMenu_.selected, -1);
      } else if (button == Button::Right) {
        adjustMenuItem(readerMenu_.selected, 1);
      } else if (button == Button::Confirm) {
        runMenuItem(readerMenu_.selected);
      } else if (button == Button::Back) {
        exitReaderMenu();
      }
      break;

    case Mode::Toc:
      if (up || down) {
        tocMenu_.move(up ? -1 : 1);
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Confirm) {
        char title[8];
        const int spine = reader_->tocEntry(static_cast<size_t>(tocMenu_.selected), title, sizeof(title), nullptr);
        if (spine >= 0) {
          reader_->jumpToChapter(static_cast<uint16_t>(spine));
          reader_->prepare();
        }
        turnsSinceFull_ = 0;
        showReader(RefreshHint::Fast);
      } else if (button == Button::Back) {
        showReaderMenu();
      }
      break;

    case Mode::Diagnostic:
      if (button == Button::Back) {
        showHome();
      } else if (button == Button::Confirm && request_) {
        showMessage("Avvio Cyber-Net Wi-Fi & Portale Web...");
        flush(RefreshHint::Fast);
        request_(Request::WifiUpdate, requestUser_);
      } else {
        ui_.invalidate(RefreshHint::Fast);
      }
      break;

    case Mode::Message:
      if (button == Button::Back || button == Button::Confirm) showHome();
      break;
  }
}

// ---------------------------------------------------------------------------
// Screen 1: HOME COMMAND CENTER
// ---------------------------------------------------------------------------
void InklingApp::homeScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = kScreenW;
  const int activeIdx = self.activeBookIndex();

  if (self.cursorOnlyRedraw_) {
    clearBandY(458, 746);
  } else {
    clearScreen();
    const LocalTime now = self.currentLocalTime();

    drawTopHud(t, self.status_, self.pet_, "HOME");

    // 1. Hero Pet Habitat & Telemetry Card (y=48..304)
    const Rect petCard{14, 48, static_cast<int16_t>(w - 28), 254};
    drawCard(t, petCard, 10, 4, false, true);

    // Left: Interactive Habitat Window
    const Rect habRect{26, 60, 212, 182};
    drawHabitatScene(t, habRect, self.pet_, self.status_, false);

    // Right: Pet Status & Gauges
    const int16_t rx = 250;
    const int16_t rw = static_cast<int16_t>(w - rx - 26);

    char stageBadge[48];
    snprintf(stageBadge, sizeof(stageBadge), "LV.%u · %s", static_cast<unsigned>(self.pet_.state().level),
             self.pet_.stageName());
    drawPill(t, rx, 60, 24, stageBadge, kFontMicro, true, 10);

    char moodLine[56];
    snprintf(moodLine, sizeof(moodLine), "%s · %s", self.pet_.moodLabel(now), self.pet_.archetypeName());
    drawText(t, Rect{rx, 88, rw, 22}, moodLine, kFontSmall, Color::Black);

    // Gauge 1: Inchiostro (Satiety)
    char g1[32];
    snprintf(g1, sizeof(g1), "Inchiostro %u%%", static_cast<unsigned>(self.pet_.state().satiety));
    drawText(t, Rect{rx, 114, rw, 18}, g1, kFontMicro, Color::Black);
    drawSegmentedBar(t, Rect{rx, 134, rw, 14}, self.pet_.state().satiety, 10, false);

    // Gauge 2: Risonanza (Mood)
    char g2[32];
    snprintf(g2, sizeof(g2), "Risonanza %u%%", static_cast<unsigned>(self.pet_.state().mood));
    drawText(t, Rect{rx, 152, rw, 18}, g2, kFontMicro, Color::Black);
    drawSegmentedBar(t, Rect{rx, 172, rw, 14}, self.pet_.state().mood, 10, false);

    // Gauge 3: Daily Reading Goal
    char g3[48];
    snprintf(g3, sizeof(g3), "Oggi: %u/%u pag (%lu tot)", static_cast<unsigned>(self.pet_.state().dailyPages),
             static_cast<unsigned>(self.pet_.state().dailyGoal),
             static_cast<unsigned long>(self.pet_.state().totalPagesRead));
    drawText(t, Rect{rx, 190, rw, 18}, g3, kFontMicro, Color::Black);
    drawProgressBar(t, Rect{rx, 210, rw, 14}, self.pet_.dailyGoalPct(), false);

    // Bottom Speech Bubble Strip inside Hero Card (crisp white card + black badge for 100% legibility)
    const Rect bubble{26, 250, static_cast<int16_t>(w - 52), 42};
    fastFill(t, bubble, Paint::solid(Color::White), 6);
    fastStroke(t, bubble, Paint::solid(Color::Black), 2, 6);
    fastFill(t, Rect{26, 250, 32, 42}, Paint::solid(Color::Black), 6);
    drawIcon16(t, 34, 263, Icon::Spark, Color::White);
    drawText(t, Rect{68, 254, static_cast<int16_t>(w - 104), 34}, self.pet_.currentDialogue(now), kFontSmall,
             Color::Black);

    // 2. "In Lettura Ora" Featured Book Card (y=314..450)
    const Rect bookCard{14, 314, static_cast<int16_t>(w - 28), 134};
    drawCard(t, bookCard, 10, 4, false, false);

    if (activeIdx >= 0 && activeIdx < self.library_.count()) {
      const auto& b = self.library_.at(activeIdx);
      drawBookCover(t, 28, 330, b.name, b.author, nullptr);

      const int16_t bx = 108;
      const int16_t bw = static_cast<int16_t>(w - bx - 28);
      drawPill(t, bx, 328, 22, b.started ? "IN LETTURA ORA" : "PROSSIMA LETTURA", kFontMicro, true, 10);

      char pctBadge[16];
      snprintf(pctBadge, sizeof(pctBadge), "%u%%", static_cast<unsigned>(b.progressPct));
      drawText(t, Rect{static_cast<int16_t>(bx + bw - 60), 328, 60, 22}, pctBadge, kFontBody, Color::Black,
               TextAlign::Right, false);

      drawText(t, Rect{bx, 356, bw, 28}, b.name, kFontTitle, Color::Black);
      drawText(t, Rect{bx, 384, bw, 22}, b.author, kFontSmall, Color::Black);
      drawProgressBar(t, Rect{bx, 414, bw, 16}, b.progressPct, false);
    } else {
      drawIcon16(t, 34, 368, Icon::Book, Color::Black);
      drawText(t, Rect{62, 352, static_cast<int16_t>(w - 90), 28}, "Nessun libro sulla scheda SD", kFontTitle,
               Color::Black);
      drawText(t, Rect{62, 384, static_cast<int16_t>(w - 90), 24},
               "Copia i tuoi file .epub o .txt sulla microSD.", kFontSmall, Color::Black);
    }
  }

  // 3. Interactive 2x2 Cyber-Dock Grid (y=462..740)
  static const char* const kDockTitles[kDockCount] = {
      "CONTINUA LETTURA",
      "LIBRERIA VOLUMI",
      "SANTUARIO PET",
      "SISTEMA & WI-FI",
  };
  static const Icon kDockIcons[kDockCount] = {
      Icon::Book,
      Icon::Compass,
      Icon::Crystal,
      Icon::Sliders,
  };

  char sub0[40], sub1[40], sub2[40], sub3[40];
  snprintf(sub0, sizeof(sub0), activeIdx >= 0 ? "Riprendi da %u%%" : "Apri catalogo",
           activeIdx >= 0 ? static_cast<unsigned>(self.library_.at(activeIdx).progressPct) : 0u);
  snprintf(sub1, sizeof(sub1), "%d %s su SD", self.library_.count(),
           self.library_.count() == 1 ? "libro" : "libri");
  snprintf(sub2, sizeof(sub2), "%u Cristalli · %s", static_cast<unsigned>(self.pet_.state().inkCrystals),
           self.pet_.stageName());
  snprintf(sub3, sizeof(sub3), "Sensori · Aggiorna OTA");
  const char* const subs[kDockCount] = {sub0, sub1, sub2, sub3};

  const int16_t tileW = static_cast<int16_t>((w - 40) / 2);
  const int16_t tileH = 128;

  for (int i = 0; i < kDockCount; ++i) {
    const int col = i & 1;
    const int row = i >> 1;
    const int16_t tx = static_cast<int16_t>(14 + col * (tileW + 12));
    const int16_t ty = static_cast<int16_t>(462 + row * (tileH + 14));
    const bool sel = (self.homeDock_.selected == i);
    const Rect tr{tx, ty, tileW, tileH};

    drawCard(t, tr, 10, sel ? 5 : 3, sel, sel);
    const Color fg = sel ? Color::White : Color::Black;

    drawIcon16(t, static_cast<int16_t>(tx + 16), static_cast<int16_t>(ty + 18), kDockIcons[i], fg);
    if (sel) {
      drawPill(t, static_cast<int16_t>(tx + tileW - 68), static_cast<int16_t>(ty + 14), 22, "ATTIVO", kFontMicro,
               false, 8);
    }
    drawText(t, Rect{static_cast<int16_t>(tx + 16), static_cast<int16_t>(ty + 48), static_cast<int16_t>(tileW - 32), 28},
             kDockTitles[i], kFontBody, fg);
    drawText(t, Rect{static_cast<int16_t>(tx + 16), static_cast<int16_t>(ty + 80), static_cast<int16_t>(tileW - 32), 24},
             subs[i], kFontSmall, fg);
  }

  if (!self.cursorOnlyRedraw_) {
    drawBottomKeyBar(t, "FRECCE", "Sposta", "OK", "Apri", "BACK", "Libreria");
  }
  self.cursorOnlyRedraw_ = false;
}

// ---------------------------------------------------------------------------
// Screen 2: VISUAL BOOK CATALOG (LIBRARY)
// ---------------------------------------------------------------------------
void InklingApp::libraryScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = kScreenW;
  const int books = self.library_.count();
  const int totalRows = books + 4;
  const int sel = self.libraryMenu_.selected;

  if (self.cursorOnlyRedraw_) {
    clearBandY(42, 750);
  } else {
    clearScreen();
    drawTopHud(t, self.status_, self.pet_, "LIBRERIA");
  }

  // Subheader bar with total books & current selection index
  char countTxt[48];
  snprintf(countTxt, sizeof(countTxt), "CATALOGO (%d %s)  ·  VOCE %d/%d", books, books == 1 ? "VOLUME" : "VOLUMI",
           sel + 1, totalRows);
  drawText(t, Rect{18, 46, static_cast<int16_t>(w - 36), 24}, countTxt, kFontMicro, Color::Black);

  // Show a window of 4 rich cards per screen
  constexpr int kPageCards = 4;
  int first = (sel / kPageCards) * kPageCards;
  if (first > totalRows - kPageCards) first = totalRows - kPageCards;
  if (first < 0) first = 0;

  const int16_t cardH = 154;
  const int16_t gapY = 12;
  const int16_t startY = 76;

  for (int slot = 0; slot < kPageCards; ++slot) {
    const int idx = first + slot;
    if (idx >= totalRows) break;
    const bool active = (idx == sel);
    const int16_t cy = static_cast<int16_t>(startY + slot * (cardH + gapY));
    const Rect cr{16, cy, static_cast<int16_t>(w - 32), cardH};

    drawCard(t, cr, 10, active ? 5 : 3, false, active);

    // Left selection rail when active
    if (active) {
      fastFill(t, Rect{16, cy, 12, cardH}, Paint::solid(Color::Black), 6);
    }

    if (idx < books) {
      const auto& b = self.library_.at(idx);
      drawBookCover(t, 36, static_cast<int16_t>(cy + 16), b.name, b.author, nullptr);

      const int16_t tx = 116;
      const int16_t tw = static_cast<int16_t>(w - tx - 32);

      // Top badge: IN CORSO / DA LEGGERE / COMPLETATO
      const char* statusTag = b.progressPct >= 99 ? "COMPLETATO" : (b.started ? "IN LETTURA" : "NUOVO VOLUME");
      drawPill(t, tx, static_cast<int16_t>(cy + 14), 22, statusTag, kFontMicro, active, 10);

      char pctStr[16];
      snprintf(pctStr, sizeof(pctStr), "%u%%", static_cast<unsigned>(b.progressPct));
      drawText(t, Rect{static_cast<int16_t>(tx + tw - 60), static_cast<int16_t>(cy + 14), 60, 22}, pctStr, kFontBody,
               Color::Black, TextAlign::Right, false);

      drawText(t, Rect{tx, static_cast<int16_t>(cy + 42), tw, 30}, b.name, kFontTitle, Color::Black);
      drawText(t, Rect{tx, static_cast<int16_t>(cy + 74), tw, 24}, b.author, kFontSmall, Color::Black);
      drawProgressBar(t, Rect{tx, static_cast<int16_t>(cy + 108), tw, 16}, b.progressPct, false);
    } else {
      // System & Companion Action Cards at the bottom of the library
      const int sysIdx = idx - books;
      const char* sysTitle = "";
      const char* sysSub = "";
      const char* sysBadge = "MODULO SISTEMA";
      Icon sysIcon = Icon::Sliders;
      if (sysIdx == 0) {
        sysTitle = "Santuario di Kuro-Ink";
        sysSub = "Nutri il tuo Pet con i Cristalli d'Inchiostro e vedi l'evoluzione";
        sysBadge = "COMPAGNO PET";
        sysIcon = Icon::Crystal;
      } else if (sysIdx == 1) {
        sysTitle = "Hacker Matrix: Breach";
        sysSub = "Infiltrazione neurale cyberpunk · Ruba i Data-Core e vinci Cristalli";
        sysBadge = "CYBER GAME";
        sysIcon = Icon::Flame;
      } else if (sysIdx == 2) {
        sysTitle = "Diagnostica & Giroscopio IMU";
        sysSub = "Stato batteria, sensori 3 assi, memoria SRAM e cambio slot";
        sysBadge = "TELEMETRIA";
        sysIcon = Icon::Compass;
      } else {
        sysTitle = "Cyber-Net Wi-Fi & Daily Ink";
        sysSub = "Scarica Giornale ANSA/Tech · Portale Web Carica Libri · OTA";
        sysBadge = "CYBER-NET HUB";
        sysIcon = Icon::Wifi;
      }

      const Rect iconBox{36, static_cast<int16_t>(cy + 26), 64, 88};
      fastFill(t, iconBox, Paint::solid(active ? Color::Black : Color::White), 8);
      fastStroke(t, iconBox, Paint::solid(Color::Black), 2, 8);
      drawIcon16(t, 60, static_cast<int16_t>(cy + 62), sysIcon, active ? Color::White : Color::Black);

      const int16_t tx = 116;
      const int16_t tw = static_cast<int16_t>(w - tx - 32);
      drawPill(t, tx, static_cast<int16_t>(cy + 18), 22, sysBadge, kFontMicro, active, 10);
      drawText(t, Rect{tx, static_cast<int16_t>(cy + 50), tw, 30}, sysTitle, kFontTitle, Color::Black);
      drawText(t, Rect{tx, static_cast<int16_t>(cy + 84), tw, 24}, sysSub, kFontSmall, Color::Black);
    }
  }

  if (!self.cursorOnlyRedraw_) {
    drawBottomKeyBar(t, "SU/GIU", "Scorri", "OK", "Apri", "BACK", "Home");
  }
  self.cursorOnlyRedraw_ = false;
}

// ---------------------------------------------------------------------------
// Screen 3: INTERACTIVE PET SANCTUARY & EVOLUTION TREE
// ---------------------------------------------------------------------------
void InklingApp::petSanctuaryScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = kScreenW;
  const int16_t bw = static_cast<int16_t>((w - 40) / 2);

  if (self.cursorOnlyRedraw_) {
    clearBandY(604, 746);
  } else {
    clearScreen();
    const LocalTime now = self.currentLocalTime();

    drawTopHud(t, self.status_, self.pet_, "SANTUARIO");

    // 1. Large Sanctuary Habitat Card (y=48..324)
    const Rect stageCard{14, 48, static_cast<int16_t>(w - 28), 276};
    drawHabitatScene(t, stageCard, self.pet_, self.status_, true);

    // Floating Header Badges inside Sanctuary
    char lvTag[48];
    snprintf(lvTag, sizeof(lvTag), "%s · LV.%u", self.pet_.stageName(),
             static_cast<unsigned>(self.pet_.state().level));
    drawPill(t, 28, 60, 26, lvTag, kFontMicro, true, 12);

    char archTag[48];
    snprintf(archTag, sizeof(archTag), "CLASSE: %s", self.pet_.archetypeName());
    drawPill(t, static_cast<int16_t>(w - 236), 60, 26, archTag, kFontMicro, false, 10);

    // Speech banner overlay at the bottom of the habitat
    const Rect speech{26, 274, static_cast<int16_t>(w - 52), 40};
    fastFill(t, speech, Paint::solid(Color::White), 6);
    fastStroke(t, speech, Paint::solid(Color::Black), 2, 6);
    drawText(t, Rect{38, 278, static_cast<int16_t>(w - 76), 32}, self.pet_.currentDialogue(now), kFontSmall,
             Color::Black, TextAlign::Center);

    // 2. Evolution Chain & Stage Progress Card (y=334..422)
    const Rect evoCard{14, 334, static_cast<int16_t>(w - 28), 88};
    drawCard(t, evoCard, 8, 3, false, false);

    static const char* const kStgShort[5] = {"Spora", "Goccia", "Glifo", "Custode", "Arconte"};
    const int curStg = static_cast<int>(self.pet_.stage());
    const int16_t stepX = static_cast<int16_t>((w - 88) / 4);
    for (int i = 0; i < 5; ++i) {
      const int16_t nx = static_cast<int16_t>(44 + i * stepX);
      const int16_t ny = 360;
      if (i < 4) {
        fastFill(t, Rect{nx, static_cast<int16_t>(ny - 1), stepX, 2}, Paint::solid(Color::Black));
      }
      fillCircle(t, nx, ny, i <= curStg ? 9 : 7, i <= curStg ? Color::Black : Color::White);
      strokeCircle(t, nx, ny, 9, 2, Color::Black);
      if (i == curStg) fillCircle(t, nx, ny, 3, Color::White);
      drawText(t, Rect{static_cast<int16_t>(nx - 36), 374, 72, 20}, kStgShort[i], kFontMicro, Color::Black,
               TextAlign::Center, false);
    }

    char nextEvoTxt[64];
    if (curStg < 4) {
      snprintf(nextEvoTxt, sizeof(nextEvoTxt), "Prossima evoluzione: %lu / %u pag (%u%%)",
               static_cast<unsigned long>(self.pet_.state().totalPagesRead),
               static_cast<unsigned>(self.pet_.pagesForNextStage()),
               static_cast<unsigned>(self.pet_.stageProgressPct()));
    } else {
      snprintf(nextEvoTxt, sizeof(nextEvoTxt), "Forma Finale Astrale Raggiunta! (%lu pag)",
               static_cast<unsigned long>(self.pet_.state().totalPagesRead));
    }
    drawText(t, Rect{28, 396, static_cast<int16_t>(w - 56), 20}, nextEvoTxt, kFontMicro, Color::Black,
             TextAlign::Center, false);

    // 3. Telemetry Bento Grid (4 metric cards, y=432..594)
    const int16_t bh = 74;

    auto drawMetric = [&](int col, int row, const char* label, const char* bigVal, uint8_t barPct) {
      const int16_t mx = static_cast<int16_t>(14 + col * (bw + 12));
      const int16_t my = static_cast<int16_t>(432 + row * (bh + 10));
      drawCard(t, Rect{mx, my, bw, bh}, 8, 3, false, false);
      drawText(t, Rect{static_cast<int16_t>(mx + 12), static_cast<int16_t>(my + 8), static_cast<int16_t>(bw - 24), 18},
               label, kFontMicro, Color::Black);
      drawText(t, Rect{static_cast<int16_t>(mx + 12), static_cast<int16_t>(my + 26), static_cast<int16_t>(bw - 24), 26},
               bigVal, kFontBody, Color::Black);
      drawSegmentedBar(t,
                       Rect{static_cast<int16_t>(mx + 12), static_cast<int16_t>(my + 54),
                            static_cast<int16_t>(bw - 24), 12},
                       barPct, 8, false);
    };

    char v0[32], v1[32], v2[32], v3[32];
    snprintf(v0, sizeof(v0), "%u%% Sazio", static_cast<unsigned>(self.pet_.state().satiety));
    snprintf(v1, sizeof(v1), "%u%% Felice", static_cast<unsigned>(self.pet_.state().mood));
    snprintf(v2, sizeof(v2), "%lu pag (%u cap)", static_cast<unsigned long>(self.pet_.state().totalPagesRead),
             static_cast<unsigned>(self.pet_.state().chaptersFinished));
    snprintf(v3, sizeof(v3), "%u Cristalli", static_cast<unsigned>(self.pet_.state().inkCrystals));

    drawMetric(0, 0, "INCHIOSTRO VITALE", v0, self.pet_.state().satiety);
    drawMetric(1, 0, "RISONANZA EMOTIVA", v1, self.pet_.state().mood);
    drawMetric(0, 1, "ARCHIVIO LETTURA", v2, self.pet_.dailyGoalPct());
    drawMetric(
        1, 1, "RISERVA CRISTALLI", v3,
        static_cast<uint8_t>(self.pet_.state().inkCrystals * 10 > 100 ? 100 : self.pet_.state().inkCrystals * 10));
  }

  // 4. Interactive Sanctuary Actions (4 tactile buttons, y=608..740)
  static const char* const kActions[kActCount] = {
      "NUTRI (-1 CRISTALLO)",
      "MATRIX: BREACH (GIOCA)",
      "LAMPADA ASTRALE",
      "TORNA A LEGGERE",
  };
  static const Icon kActIcons[kActCount] = {Icon::Crystal, Icon::Flame, Icon::Moon, Icon::Book};

  for (int i = 0; i < kActCount; ++i) {
    const int col = i & 1;
    const int row = i >> 1;
    const int16_t ax = static_cast<int16_t>(14 + col * (bw + 12));
    const int16_t ay = static_cast<int16_t>(608 + row * 62);
    const bool sel = (self.sanctuaryMenu_.selected == i);
    const Rect ar{ax, ay, bw, 52};

    drawCard(t, ar, 8, sel ? 4 : 2, sel, false);
    drawIcon16(t, static_cast<int16_t>(ax + 12), static_cast<int16_t>(ay + 18), kActIcons[i],
               sel ? Color::White : Color::Black);
    drawText(t, Rect{static_cast<int16_t>(ax + 34), static_cast<int16_t>(ay + 12), static_cast<int16_t>(bw - 42), 28},
             kActions[i], kFontSmall, sel ? Color::White : Color::Black);
  }

  if (!self.cursorOnlyRedraw_) {
    drawBottomKeyBar(t, "FRECCE", "Scegli azione", "OK", "Esegui", "BACK", "Home");
  }
  self.cursorOnlyRedraw_ = false;
}

// ---------------------------------------------------------------------------
// Screen 3B: HACKER MATRIX: BREACH (Cyberpunk Neural Infiltration Game)
// ---------------------------------------------------------------------------
void InklingApp::breachGameScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = kScreenW;
  const auto& b = self.breach_;

  if (self.cursorOnlyRedraw_) {
    clearBandY(44, 704);
  } else {
    clearScreen();
    drawTopHud(t, self.status_, self.pet_, "MATRIX // BREACH");
  }

  // 1. Cyber-Deck & ICE Trace Telemetry Card (y=46..184)
  const Rect hudCard{14, 46, static_cast<int16_t>(w - 28), 138};
  drawCard(t, hudCard, 10, 4, false, true);

  char lvBadge[36];
  snprintf(lvBadge, sizeof(lvBadge), "SOTTO-RETE %02u", static_cast<unsigned>(b.level()));
  drawPill(t, 28, 56, 26, lvBadge, kFontSmall, true, 12);

  char scoreTxt[64];
  snprintf(scoreTxt, sizeof(scoreTxt), "CORE: %u/%u  ·  EMP: %u  ·  %lu PT",
           static_cast<unsigned>(b.totalCores() - b.coresLeft()), static_cast<unsigned>(b.totalCores()),
           static_cast<unsigned>(b.empCharges()), static_cast<unsigned long>(b.score()));
  drawText(t, Rect{196, 56, static_cast<int16_t>(w - 224), 26}, scoreTxt, kFontSmall, Color::Black, TextAlign::Right);

  // ICE Trace Alert Bar
  char traceLbl[48];
  snprintf(traceLbl, sizeof(traceLbl), "TRACCIAMENTO ICE: %u%%", static_cast<unsigned>(b.iceTrace()));
  drawText(t, Rect{28, 86, 220, 20}, traceLbl, kFontMicro, Color::Black);
  drawSegmentedBar(t, Rect{236, 88, static_cast<int16_t>(w - 264), 16}, b.iceTrace(), 10, false);

  // Live Hacker Terminal Strip inside HUD Card
  const Rect termBox{26, 114, static_cast<int16_t>(w - 52), 56};
  fastFill(t, termBox, Paint::solid(Color::Black), 8);
  drawIcon16(t, 40, 134, Icon::Flame, Color::White);
  drawText(t, Rect{66, 128, static_cast<int16_t>(w - 102), 30}, b.statusLine(), kFontSmall, Color::White);

  // 2. Central 6x6 Neural Circuit Grid (y=194..696)
  const Rect gridCard{14, 194, static_cast<int16_t>(w - 28), 502};
  drawCard(t, gridCard, 12, 4, false, true);

  const int16_t nodeSize = 68;
  const int16_t gap = 12;
  const int16_t startX = 30;
  const int16_t startY = 212;

  // Draw circuit traces connecting adjacent node centers
  for (int gy = 0; gy < BreachGame::kGridH; ++gy) {
    const int16_t cy = static_cast<int16_t>(startY + gy * (nodeSize + gap) + nodeSize / 2);
    fastFill(t, Rect{static_cast<int16_t>(startX + nodeSize / 2), static_cast<int16_t>(cy - 1),
                     static_cast<int16_t>((BreachGame::kGridW - 1) * (nodeSize + gap)), 3},
             Paint::solid(Color::Black));
  }
  for (int gx = 0; gx < BreachGame::kGridW; ++gx) {
    const int16_t cx = static_cast<int16_t>(startX + gx * (nodeSize + gap) + nodeSize / 2);
    fastFill(t, Rect{static_cast<int16_t>(cx - 1), static_cast<int16_t>(startY + nodeSize / 2), 3,
                     static_cast<int16_t>((BreachGame::kGridH - 1) * (nodeSize + gap))},
             Paint::solid(Color::Black));
  }

  // Draw the 6x6 nodes
  for (int gy = 0; gy < BreachGame::kGridH; ++gy) {
    for (int gx = 0; gx < BreachGame::kGridW; ++gx) {
      const int16_t nx = static_cast<int16_t>(startX + gx * (nodeSize + gap));
      const int16_t ny = static_cast<int16_t>(startY + gy * (nodeSize + gap));
      const Rect nr{nx, ny, nodeSize, nodeSize};
      const bool isPlayer = (b.playerX() == gx && b.playerY() == gy);
      const auto* sent = b.sentinelAt(gx, gy);
      const auto cell = b.nodeAt(gx, gy);

      if (isPlayer) {
        drawCard(t, nr, 10, 4, true, true);
        drawInklingSprite(t, static_cast<int16_t>(nx + nodeSize / 2), static_cast<int16_t>(ny + 24), 1,
                          self.pet_.stage(), PetMood::Focused, self.pet_.animFrame(), true);
        drawText(t, Rect{nx, static_cast<int16_t>(ny + 42), nodeSize, 22}, "TU", kFontSmall, Color::White,
                 TextAlign::Center, false);
      } else if (sent != nullptr) {
        fastFill(t, nr, Paint::solid(Color::Black), 10);
        fastStroke(t, Rect{static_cast<int16_t>(nx + 4), static_cast<int16_t>(ny + 4),
                           static_cast<int16_t>(nodeSize - 8), static_cast<int16_t>(nodeSize - 8)},
                   Paint::solid(Color::White), 2, 6);
        drawIcon16(t, static_cast<int16_t>(nx + nodeSize / 2 - 8), static_cast<int16_t>(ny + 12), Icon::Compass,
                   Color::White);
        drawText(t, Rect{nx, static_cast<int16_t>(ny + 36), nodeSize, 24},
                 sent->stunTurns > 0 ? "STUN" : "ICE", kFontBody, Color::White, TextAlign::Center, false);
      } else if (cell == BreachGame::Node::DataCore) {
        drawCard(t, nr, 10, 3, false, true);
        drawIcon16(t, static_cast<int16_t>(nx + nodeSize / 2 - 8), static_cast<int16_t>(ny + 12), Icon::Crystal,
                   Color::Black);
        drawText(t, Rect{nx, static_cast<int16_t>(ny + 36), nodeSize, 24}, "CORE", kFontBody, Color::Black,
                 TextAlign::Center, false);
      } else if (cell == BreachGame::Node::Uplink) {
        const bool open = b.uplinkOpen();
        drawCard(t, nr, 10, open ? 4 : 2, open, true);
        const Color fg = open ? Color::White : Color::Black;
        drawIcon16(t, static_cast<int16_t>(nx + nodeSize / 2 - 8), static_cast<int16_t>(ny + 12),
                   open ? Icon::Check : Icon::Wifi, fg);
        drawText(t, Rect{nx, static_cast<int16_t>(ny + 36), nodeSize, 24}, open ? "EXIT" : "LOCK", kFontBody, fg,
                 TextAlign::Center, false);
      } else if (cell == BreachGame::Node::Firewall) {
        fastFill(t, nr, Paint::solid(Color::White), 8);
        fillScanlines(t, nr, 5, Color::Black);
        fastStroke(t, nr, Paint::solid(Color::Black), 2, 8);
        const Rect tag{static_cast<int16_t>(nx + 6), static_cast<int16_t>(ny + 22),
                       static_cast<int16_t>(nodeSize - 12), 24};
        fastFill(t, tag, Paint::solid(Color::White), 4);
        fastStroke(t, tag, Paint::solid(Color::Black), 1, 4);
        drawText(t, Rect{nx, static_cast<int16_t>(ny + 24), nodeSize, 20}, "LASER", kFontMicro, Color::Black,
                 TextAlign::Center, false);
      } else if (cell == BreachGame::Node::Switch) {
        drawCard(t, nr, 10, 2, false, false);
        drawIcon16(t, static_cast<int16_t>(nx + nodeSize / 2 - 8), static_cast<int16_t>(ny + 12), Icon::Sliders,
                   Color::Black);
        drawText(t, Rect{nx, static_cast<int16_t>(ny + 36), nodeSize, 24}, "SW", kFontBody, Color::Black,
                 TextAlign::Center, false);
      } else if (cell == BreachGame::Node::Emp) {
        drawCard(t, nr, 10, 2, false, false);
        drawIcon16(t, static_cast<int16_t>(nx + nodeSize / 2 - 8), static_cast<int16_t>(ny + 12), Icon::Spark,
                   Color::Black);
        drawText(t, Rect{nx, static_cast<int16_t>(ny + 36), nodeSize, 24}, "EMP", kFontBody, Color::Black,
                 TextAlign::Center, false);
      } else {
        // Empty circuit junction node
        const Rect pad{static_cast<int16_t>(nx + 18), static_cast<int16_t>(ny + 18), 32, 32};
        fastFill(t, pad, Paint::solid(Color::White), 8);
        fastStroke(t, pad, Paint::solid(Color::Black), 2, 8);
        fillCircle(t, static_cast<int16_t>(nx + nodeSize / 2), static_cast<int16_t>(ny + nodeSize / 2), 4,
                   Color::Black);
      }
    }
  }

  // Victory / Game Over Modal Banner over the Grid
  if (b.isLevelCleared() || b.isGameOver()) {
    const Rect banner{34, 370, static_cast<int16_t>(w - 68), 148};
    drawCard(t, banner, 12, 8, true, true);
    drawText(t, Rect{50, 392, static_cast<int16_t>(w - 100), 34},
             b.isLevelCleared() ? "SOTTO-RETE VIOLATA!" : "TRACCIAMENTO ICE 100%!", kFontTitle, Color::White,
             TextAlign::Center, false);
    drawText(t, Rect{50, 432, static_cast<int16_t>(w - 100), 28},
             b.isLevelCleared() ? "+1 Cristallo d'Inchiostro per Kuro-Ink!" : "Firewall ripristinati dal sistema.",
             kFontBody, Color::White, TextAlign::Center, false);
    drawPill(t, 124, 470, 32, b.isLevelCleared() ? "PREMI OK: PROSSIMO NODO" : "PREMI OK: RIPROVA BREACH",
             kFontSmall, false, 14);
  }

  if (!self.cursorOnlyRedraw_) {
    drawText(t, Rect{18, 706, static_cast<int16_t>(w - 36), 24},
             "CORE: Ruba tutti  ·  SW: Spegni Laser  ·  EMP: -35% Trace  ·  EXIT: Uscita", kFontMicro, Color::Black,
             TextAlign::Center, false);
    drawBottomKeyBar(t, "FRECCE", "Muovi", "OK/SCUOTI", "Onda EMP", "BACK", "Esci");
  }
  self.cursorOnlyRedraw_ = false;
}

// ---------------------------------------------------------------------------
// Screen 4: IMMERSIVE READER + CYBER-EDITORIAL FOOTER HUD
// ---------------------------------------------------------------------------
void InklingApp::readerScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = kScreenW;
  const int16_t h = kScreenH;

  clearScreen();
  // Render the book page directly into the framebuffer.
  self.lastRenderStatus_ = self.reader_->render(self.frame_);
  if (self.resumePending_ && self.lastRenderStatus_ == freeink::book::BookStatus::Ok) {
    self.resumePending_ = false;
    self.setResumeLock(false);
  }

  // Futuristic 38px Bottom HUD Bar
  const int16_t hudY = static_cast<int16_t>(h - kStatusLineHeight);
  fastFill(t, Rect{0, hudY, w, kStatusLineHeight}, Paint::solid(Color::White));
  fastFill(t, Rect{0, hudY, w, 2}, Paint::solid(Color::Black));

  // Thin full-width book progress bar right along the top of the HUD!
  const uint8_t pct = self.reader_->percent();
  const int16_t progW = static_cast<int16_t>((w * pct) / 100);
  if (progW > 0) {
    fastFill(t, Rect{0, static_cast<int16_t>(hudY + 2), progW, 3}, Paint::solid(Color::Black));
  }

  // Left: Mini Pet Companion icon + active milestone toast OR chapter title
  drawInklingSprite(t, 22, static_cast<int16_t>(hudY + 21), 1, self.pet_.stage(),
                    self.pet_.moodState(self.currentLocalTime()), self.pet_.animFrame(), false);

  if (self.lastRenderStatus_ != freeink::book::BookStatus::Ok) {
    char errLine[80];
    snprintf(errLine, sizeof(errLine), "Errore pagina: %s",
             freeink::book::bookStatusName(self.lastRenderStatus_));
    drawText(t, Rect{44, static_cast<int16_t>(hudY + 7), static_cast<int16_t>(w - 56), 26}, errLine, kFontSmall,
             Color::Black);
    return;
  }

  const char* banner = self.pet_.activeBanner();
  if (banner && banner[0]) {
    drawPill(t, 44, static_cast<int16_t>(hudY + 8), 24, banner, kFontMicro, true, 10);
  } else {
    char chapTitle[64];
    self.reader_->currentChapterTitle(chapTitle, sizeof(chapTitle));
    drawText(t, Rect{44, static_cast<int16_t>(hudY + 8), 250, 24}, chapTitle, kFontSmall, Color::Black);
  }

  // Right: Page in chapter, Book %, and Clock
  char rightMeta[64];
  if (self.status_.clockValid) {
    snprintf(rightMeta, sizeof(rightMeta), "%u/%u · %u%% · %02u:%02u",
             static_cast<unsigned>(self.reader_->pageInChapter()),
             static_cast<unsigned>(self.reader_->pagesInChapter()), static_cast<unsigned>(pct), self.status_.hour,
             self.status_.minute);
  } else {
    snprintf(rightMeta, sizeof(rightMeta), "Pag %u/%u  ·  %u%%",
             static_cast<unsigned>(self.reader_->pageInChapter()),
             static_cast<unsigned>(self.reader_->pagesInChapter()), static_cast<unsigned>(pct));
  }
  drawPill(t, static_cast<int16_t>(w - 196), static_cast<int16_t>(hudY + 8), 24, rightMeta, kFontMicro, false, 10);
  if (self.nightMode_) {
    invertScreen();
  }
}

// ---------------------------------------------------------------------------
// Screen 5: FLOATING READER OVERLAY MODAL (over the live book page!)
// ---------------------------------------------------------------------------
void InklingApp::readerMenuScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = kScreenW;
  const int sel = self.readerMenu_.selected;
  const uint8_t step = self.fontStep_ < kFontSizeCount ? self.fontStep_ : 3;
  const uint8_t fam = self.fontFamily_ < kFontFamilyCount ? self.fontFamily_ : 0;
  const uint8_t spStep = self.spacingStep_ < kSpacingPresetCount ? self.spacingStep_ : 0;
  const uint8_t alStep = self.alignStep_ < kAlignPresetCount ? self.alignStep_ : 0;

  if (self.cursorOnlyRedraw_) {
    // Clear the header counter pill + 4 big cards inside the modal (~0.05 ms)!
    fastFill(t, Rect{30, 106, static_cast<int16_t>(w - 60), 618}, Paint::solid(Color::White));
  } else {
    // Only re-render the underlying book page if we didn't come straight from Mode::Reader!
    if (self.needsReaderBackdrop_ && self.reader_ && self.reader_->isOpen()) {
      clearScreen();
      self.reader_->render(self.frame_);
      if (self.nightMode_) invertScreen();
      self.needsReaderBackdrop_ = false;
    }

    // Floating Neo-Brutalist Overlay Modal Card
    const Rect modal{18, 36, static_cast<int16_t>(w - 36), 702};
    drawCard(t, modal, 12, 8, false, true);

    // Dark Header Bar inside modal
    const Rect hdr{32, 48, static_cast<int16_t>(w - 64), 48};
    fastFill(t, hdr, Paint::solid(Color::Black), 8);
    drawText(t, Rect{48, 56, 300, 32}, "CONTROLLO LETTURA", kFontTitle, Color::White);
  }

  // Right badge in header: current item index + book %
  fastFill(t, Rect{static_cast<int16_t>(w - 178), 52, 138, 38}, Paint::solid(Color::Black));
  char hdrBadge[32];
  snprintf(hdrBadge, sizeof(hdrBadge), "%d/%d · %u%%", sel + 1, static_cast<int>(kMenuCount),
           static_cast<unsigned>(self.reader_->percent()));
  drawPill(t, static_cast<int16_t>(w - 164), 58, 28, hdrBadge, kFontSmall, false, 10);

  // 4 Big, High-Legibility Cards per screen (using kFontTitle + kFontBody!)
  static const char* const kTitles[kMenuCount] = {
      "TORNA AL LIBRO",
      "INDICE CAPITOLI",
      "DIMENSIONE TESTO",
      "CARATTERE FONT",
      "LETTURA VELOCE",
      "SPAZIATURA PAGINA",
      "IMPAGINAZIONE",
      "MODALITÀ NOTTE",
      "CHIUDI LIBRO",
  };
  static const Icon kRowIcons[kMenuCount] = {
      Icon::Book,    Icon::Compass, Icon::Spark, Icon::Sliders, Icon::Flame,
      Icon::Sliders, Icon::Check,   Icon::Moon,  Icon::Crystal,
  };

  char v0[48], v1[48], v2[48], v3[48], v4[48], v5[48], v6[48], v7[48], v8[48];
  snprintf(v0, sizeof(v0), self.layoutDirty_ ? "OK: Applica   ·   SX/DX: -/+ 10%%"
                                             : "OK: Continua   ·   SX/DX: -/+ 10%%");
  snprintf(v1, sizeof(v1), "Cap. %u di %u   ·   OK: Apri indice",
           static_cast<unsigned>(self.reader_->currentSpine() + 1),
           static_cast<unsigned>(self.reader_->spineCount()));
  snprintf(v2, sizeof(v2), "<   %u PX  (%s)   >", kFontSizes[step], kFontSizeLabels[step]);
  snprintf(v3, sizeof(v3), "<   %s   >", fontFamilyName(fam));
  snprintf(v4, sizeof(v4), "<   Bionic: %s   >", self.focusReading_ ? "ATTIVA" : "OFF");
  snprintf(v5, sizeof(v5), "<   %s   >", kSpacingPresets[spStep].label);
  snprintf(v6, sizeof(v6), "<   %s   >", kAlignPresets[alStep].label);
  snprintf(v7, sizeof(v7), "<   %s   >", self.nightMode_ ? "ON (Scuro)" : "OFF (Chiaro)");
  snprintf(v8, sizeof(v8), "OK: Home   ·   SX/DX: Santuario Pet");

  const char* const values[kMenuCount] = {v0, v1, v2, v3, v4, v5, v6, v7, v8};

  constexpr int kVisCards = 4;
  int first = (sel / kVisCards) * kVisCards;
  if (first > kMenuCount - kVisCards) first = kMenuCount - kVisCards;
  if (first < 0) first = 0;

  const int16_t startY = 114;
  const int16_t cardH = 140;
  const int16_t gapY = 14;

  for (int slot = 0; slot < kVisCards; ++slot) {
    const int idx = first + slot;
    if (idx >= kMenuCount) break;
    const bool active = (idx == sel);
    const int16_t cy = static_cast<int16_t>(startY + slot * (cardH + gapY));
    const Rect cr{34, cy, static_cast<int16_t>(w - 68), cardH};

    drawCard(t, cr, 10, active ? 5 : 2, active, active);
    const Color fg = active ? Color::White : Color::Black;

    // Top row of card: Icon + Big Title (kFontTitle)
    drawIcon16(t, 54, static_cast<int16_t>(cy + 24), kRowIcons[idx], fg);
    const char* titleTxt = (idx == kMenuContinue && self.layoutDirty_) ? "APPLICA E TORNA AL LIBRO" : kTitles[idx];
    drawText(t, Rect{82, static_cast<int16_t>(cy + 16), static_cast<int16_t>(w - 136), 32}, titleTxt, kFontTitle, fg);

    // Bottom row of card: Giant Value Box / Pill (kFontTitle or kFontBody)
    const Rect valBox{52, static_cast<int16_t>(cy + 62), static_cast<int16_t>(w - 104), 58};
    fastFill(t, valBox, Paint::solid(active ? Color::White : Color::Black), 8);
    const Color valFg = active ? Color::Black : Color::White;
    drawText(t, Rect{64, static_cast<int16_t>(cy + 75), static_cast<int16_t>(w - 128), 32}, values[idx],
             (idx >= kMenuFontSize && idx <= kMenuNightMode) ? kFontTitle : kFontBody, valFg, TextAlign::Center);
  }

  if (!self.cursorOnlyRedraw_) {
    drawBottomKeyBar(t, "SU/GIU", "Scorri", "SX/DX/OK", "Cambia", "BACK", "Libro");
  }
  self.cursorOnlyRedraw_ = false;
}

// ---------------------------------------------------------------------------
// Screen 6: HIERARCHICAL TABLE OF CONTENTS (TOC)
// ---------------------------------------------------------------------------
void InklingApp::tocScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = kScreenW;
  const int count = self.tocMenu_.count;
  const int sel = self.tocMenu_.selected;
  const int currentChap = self.reader_->currentTocIndex();

  if (self.cursorOnlyRedraw_) {
    clearBandY(44, 750);
  } else {
    clearScreen();
    drawTopHud(t, self.status_, self.pet_, "INDICE");
  }

  // Book header banner
  const Rect hdrCard{16, 48, static_cast<int16_t>(w - 32), 68};
  drawCard(t, hdrCard, 8, 3, false, false);
  drawText(t, Rect{30, 54, static_cast<int16_t>(w - 160), 28}, self.reader_->title(), kFontTitle, Color::Black);
  char countBadge[32];
  snprintf(countBadge, sizeof(countBadge), "%d / %d CAP.", sel + 1, count);
  drawPill(t, static_cast<int16_t>(w - 134), 60, 24, countBadge, kFontMicro, true, 10);
  drawText(t, Rect{30, 84, static_cast<int16_t>(w - 60), 22}, self.reader_->author(), kFontSmall, Color::Black);

  // Window of 8 chapter cards
  constexpr int kVisRows = 8;
  int first = (sel / kVisRows) * kVisRows;
  if (first > count - kVisRows) first = count - kVisRows;
  if (first < 0) first = 0;

  const int16_t startY = 128;
  const int16_t rowH = 66;
  const int16_t gapY = 8;

  for (int slot = 0; slot < kVisRows; ++slot) {
    const int idx = first + slot;
    if (idx >= count) break;
    const bool active = (idx == sel);
    const bool isCurrent = (idx == currentChap);

    char rawTitle[64] = {};
    uint8_t depth = 0;
    self.reader_->tocEntry(static_cast<size_t>(idx), rawTitle, sizeof(rawTitle), &depth);

    const int16_t ry = static_cast<int16_t>(startY + slot * (rowH + gapY));
    const Rect rr{16, ry, static_cast<int16_t>(w - 32), rowH};
    drawCard(t, rr, 8, active ? 4 : 2, active, false);

    const Color fg = active ? Color::White : Color::Black;
    char numBadge[16];
    snprintf(numBadge, sizeof(numBadge), "%02d", idx + 1);
    drawPill(t, 28, static_cast<int16_t>(ry + 20), 26, numBadge, kFontMicro, !active, 8);

    const int16_t indent = static_cast<int16_t>(depth > 0 ? 18 : 0);
    const int16_t tx = static_cast<int16_t>(76 + indent);
    const int16_t tw = static_cast<int16_t>(w - tx - (isCurrent ? 104 : 30));
    drawText(t, Rect{tx, static_cast<int16_t>(ry + 18), tw, 30},
             rawTitle[0] ? rawTitle : "(Capitolo senza titolo)", kFontBody, fg);

    if (isCurrent) {
      drawPill(t, static_cast<int16_t>(w - 106), static_cast<int16_t>(ry + 21), 24, " IN CORSO", kFontMicro, !active,
               8);
    }
  }

  if (!self.cursorOnlyRedraw_) {
    drawBottomKeyBar(t, "SU/GIU", "Capitolo", "OK", "Vai a capitolo", "BACK", "Menu");
  }
  self.cursorOnlyRedraw_ = false;
}

}  // namespace inkling
