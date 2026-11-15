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
constexpr const char* kReadingStatsPath = "/.inkling/reading.bin";
// last.txt: the book to reopen at boot (cleared when the user closes it).
// recent.txt: the most recent book, never cleared, for "Continua" and Home.
constexpr const char* kLastBookPath = "/.inkling/last.txt";
constexpr const char* kRecentBookPath = "/.inkling/recent.txt";
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

struct ReadingStatsRecord {
  uint32_t magic;
  uint32_t totalPages;
  int32_t lastReadDay;
  uint16_t dailyPages;
  uint16_t streakDays;
};
constexpr uint32_t kReadingStatsMagic = 0x54534452u;  // "RDST"
constexpr uint16_t kDailyPageGoal = 15;

enum ReaderMenuItem : int {
  kMenuContinue = 0,
  kMenuToc = 1,
  kMenuFontSize = 2,
  kMenuFontFamily = 3,
  kMenuFocusReading = 4,
  kMenuSpacing = 5,
  kMenuAlignment = 6,
  kMenuNightMode = 7,
  kMenuSettings = 8,
  kMenuHome = 9,
  kMenuCount = 10,
};

enum SettingsItem : int {
  kSetRotation = 0,
  kSetInvertKeys = 1,
  kSetAutoSleep = 2,
  kSetRefreshCadence = 3,
  kSetDeskWidget = 4,
  kSetTheme = 5,
  kSetWifiOta = 6,
  kSetDiagnostic = 7,
  kSetCount = 8,
};

// Library rows after the books: Diagnostica, Wi-Fi.
constexpr int kLibrarySystemRows = 2;

enum HomeDockItem : int {
  kDockRead = 0,
  kDockLibrary = 1,
  kDockDesk = 2,
  kDockOracle = 3,
  kDockRpg = 4,
  kDockSystem = 5,
  kDockCount = 6,
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

Button spatialButton(Button b, Orientation o) {
  // Front keys Left/Right sit on the short edge below the screen at 0°, the
  // Up/Down rocker on the side: rotating the device rotates them with it.
  switch (o) {
    case Orientation::PortraitInverted:
      switch (b) {
        case Button::Left:  return Button::Right;
        case Button::Right: return Button::Left;
        case Button::Up:    return Button::Down;
        case Button::Down:  return Button::Up;
        default:            return b;
      }
    case Orientation::LandscapeClockwise:  // keys on the left, Left key on top
      switch (b) {
        case Button::Left:  return Button::Up;
        case Button::Right: return Button::Down;
        case Button::Up:    return Button::Right;
        case Button::Down:  return Button::Left;
        default:            return b;
      }
    case Orientation::LandscapeCounterClockwise:  // keys on the right, Right key on top
      switch (b) {
        case Button::Left:  return Button::Down;
        case Button::Right: return Button::Up;
        case Button::Up:    return Button::Left;
        case Button::Down:  return Button::Right;
        default:            return b;
      }
    case Orientation::Portrait:
    default:
      return b;
  }
}

bool InklingApp::modeSupportsLandscape(Mode mode) {
  (void)mode;
  return true;
}

void InklingApp::enterMode(Mode mode, UiApp::ScreenFn fn, void* user, RefreshHint hint) {
  mode_ = mode;
  applyOrientation();  // may rotate (and relayout the book) before the first frame
  cursorOnlyRedraw_ = false;
  ui_.setScreen(fn, user, hint);
}

void InklingApp::setDesiredOrientation(Orientation orient) {
  desiredOrientation_ = orient;
  applyOrientation();
}

void InklingApp::applyOrientation() {
  if (!orientationFn_) return;
  Orientation effective = desiredOrientation_;
  if (isLandscape(effective) && !modeSupportsLandscape(mode_)) effective = Orientation::Portrait;
  if (effective != orientation_) orientationFn_(effective, orientationUser_);
}

void InklingApp::onOrientationChanged(Orientation orient) {
  if (orient == orientation_) return;
  const bool shapeChanged = isLandscape(orient) != isLandscape(orientation_);
  orientation_ = orient;
  frame_.rotation = frameRotationFor(orient);
  canvas::setCanvasOrientation(orient);
  // The page size changes with the shape: lay the open chapter out again. The
  // reading position is a character offset, so the same sentence stays on screen.
  if (shapeChanged && reader_ && reader_->isOpen()) {
    reader_->relayout(currentLayout());
    reader_->prepare();
    layoutDirty_ = false;
    turnsSinceFull_ = 0;
  }
  // Every partial-redraw shortcut assumed the old pixel layout.
  needsReaderBackdrop_ = true;
  cursorOnlyRedraw_ = false;
  ui_.invalidate(RefreshHint::Full);
}

int InklingApp::homeDockColumns() const { return isLandscape() ? 3 : 2; }

bool InklingApp::loadBookThumb(const char* bookPath, uint8_t outBits[ui::canvas::kCoverBytes]) const {
  if (!bookPath || !bookPath[0]) return false;
  char thumbPath[64];
  Library::coverThumbPathFor(bookPath, thumbPath, sizeof(thumbPath));
  return storage_.readFile(thumbPath, outBits, ui::canvas::kCoverBytes) ==
         static_cast<int32_t>(ui::canvas::kCoverBytes);
}

InklingApp::InklingApp(Storage& storage, BookReader* reader, DeviceStatus& status, UiApp& ui,
                       const freeink::book::FrameTarget& frame)
    : storage_(storage), reader_(reader), status_(status), ui_(ui), frame_(frame) {
  canvas::bindFramebuffer(frame_.framebuffer, frame_.width, frame_.height, frame_.widthBytes);
  canvas::setCanvasOrientation(orientation_);
  frame_.rotation = frameRotationFor(orientation_);
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
  settings_.load(storage_);
  desk_.setSubWidget(static_cast<DeskSubWidget>(settings_.deskWidget));
  loadPrefs();
  nightMode_ = settings_.nightMode;  // settings.bin is the one source of truth
  Orientation locked;
  if (settings_.lockedOrientation(locked)) setDesiredOrientation(locked);
  library_.scan(storage_);
  if (reader_) {
    reader_->precacheLibrary(library_);
  }
  rpg_.load(storage_);
  homeDock_.reset(kDockCount, 0);
  settingsMenu_.reset(kSetCount, 0);
  INKLING_LOG("library: %d books, %lu pages read", library_.count(), static_cast<unsigned long>(totalPagesRead_));

  char last[128] = {};
  char lock = '0';
  storage_.readFile(kResumeLockPath, &lock, 1);
  const int32_t n = storage_.readFile(kLastBookPath, last, sizeof(last) - 1);
  if (n > 0) {
    last[n] = '\0';
    snprintf(lastBookPath_, sizeof(lastBookPath_), "%s", last);
  } else {
    char recent[128] = {};
    const int32_t r = storage_.readFile(kRecentBookPath, recent, sizeof(recent) - 1);
    if (r > 0) {
      recent[r] = '\0';
      snprintf(lastBookPath_, sizeof(lastBookPath_), "%s", recent);
    }
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
  if (homeDock_.count != kDockCount) homeDock_.reset(kDockCount, 0);
  enterMode(Mode::Home, homeScreen, this, RefreshHint::Clean);
}

void InklingApp::showLibrary() {
  int select = activeBookIndex();
  if (select < 0) select = 0;
  // Rows: books + "Diagnostica", "Wi-Fi: giornale e libri"
  libraryMenu_.reset(library_.count() + kLibrarySystemRows, select);
  enterMode(Mode::Library, libraryScreen, this, RefreshHint::Clean);
}

void InklingApp::showDeskAmbient() {
  enterMode(Mode::DeskAmbient, deskAmbientScreen, this, RefreshHint::Clean);
}

void InklingApp::showOracle() {
  enterMode(Mode::Oracle, oracleScreen, this, RefreshHint::Clean);
}

void InklingApp::showTextRpg() {
  enterMode(Mode::TextRpg, textRpgScreen, this, RefreshHint::Clean);
}

void InklingApp::showSettings(bool fromReader) {
  settingsFromReader_ = fromReader;
  diagnosticFromSettings_ = false;
  if (fromReader) applyPendingLayout();
  settingsMenu_.reset(kSetCount, 0);
  enterMode(Mode::Settings, settingsScreen, this, RefreshHint::Clean);
}

void InklingApp::showDiagnostic() {
  enterMode(Mode::Diagnostic, diagnosticScreen, &status_, RefreshHint::Clean);
}

void InklingApp::showMessage(const char* text) {
  snprintf(message_, sizeof(message_), "%s", text);
  enterMode(Mode::Message, messageScreen, message_, RefreshHint::Full);
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
  progressDirty_ = false;
  savePrefs();
  saveStats();
}

namespace {
// The next chapter is paginated only after the reader has settled on a page.
constexpr uint32_t kPrebuildAfterIdleMs = 1500;
// Statistics reach the card every this many pages (and when the book closes
// or the device goes to sleep), not on every page turn.
constexpr uint8_t kStatsSavePages = 10;
}  // namespace

bool InklingApp::hasIdleWork(uint32_t idleMs) {
  if (progressDirty_) return true;
  if (statsDirty_ && pagesSinceStatsSave_ >= kStatsSavePages) return true;
  return mode_ == Mode::Reader && idleMs >= kPrebuildAfterIdleMs && reader_ && reader_->prebuildWanted();
}

bool InklingApp::idleTick(uint32_t idleMs) {
  if (progressDirty_) {
    progressDirty_ = false;
    if (reader_) reader_->saveProgress();
    return true;
  }
  if (statsDirty_ && pagesSinceStatsSave_ >= kStatsSavePages) {
    saveStats();
    return true;
  }
  if (mode_ == Mode::Reader && idleMs >= kPrebuildAfterIdleMs && reader_ && reader_->prebuildWanted()) {
    // The build borrows the framebuffer: whatever is drawn next over the page
    // (the reader menu) has to draw the page again first.
    needsReaderBackdrop_ = true;
    return reader_->prebuildStep();
  }
  return false;
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
  ReadingStatsRecord rs{};
  if (storage_.readFile(kReadingStatsPath, &rs, sizeof(rs)) == static_cast<int32_t>(sizeof(rs)) &&
      rs.magic == kReadingStatsMagic) {
    totalPagesRead_ = rs.totalPages;
    lastReadDay_ = rs.lastReadDay;
    dailyPages_ = rs.dailyPages;
    streakDays_ = rs.streakDays;
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

void InklingApp::saveStats() {
  if (!statsDirty_) return;
  statsDirty_ = false;
  pagesSinceStatsSave_ = 0;
  rpg_.save(storage_);
  const ReadingStatsRecord rs{
      kReadingStatsMagic,
      totalPagesRead_,
      lastReadDay_,
      dailyPages_,
      streakDays_,
  };
  storage_.writeFile(kReadingStatsPath, &rs, sizeof(rs));
}

void InklingApp::recordPageRead() {
  ++totalPagesRead_;
  const LocalTime now = currentLocalTime();
  if (now.valid) {
    const int32_t today = dayNumber(now.year, now.month, now.day);
    if (lastReadDay_ != today) {
      if (lastReadDay_ == today - 1) {
        ++streakDays_;
      } else {
        streakDays_ = 1;
      }
      lastReadDay_ = today;
      dailyPages_ = 0;
    }
  } else if (streakDays_ == 0) {
    streakDays_ = 1;
  }
  ++dailyPages_;
  rpg_.grantReadingBonus(1);
  statsDirty_ = true;
  if (pagesSinceStatsSave_ < 255) ++pagesSinceStatsSave_;
}

ReaderLayout InklingApp::currentLayout() const {
  ReaderLayout layout;
  // frame_ describes the panel (792x528); the page is the logical screen
  // minus the status bar, so it swaps with the orientation.
  const bool landscape = isLandscape(orientation_);
  layout.pageWidth = landscape ? frame_.width : frame_.height;
  layout.pageHeight = static_cast<int16_t>((landscape ? frame_.height : frame_.width) - kStatusLineHeight);
  layout.fontFamily = fontFamily_ < kFontFamilyCount ? fontFamily_ : 0;
  layout.fontSizePx = kFontSizes[fontStep_ < kFontSizeCount ? fontStep_ : 3];
  const auto& sp = kSpacingPresets[spacingStep_ < kSpacingPresetCount ? spacingStep_ : 0];
  layout.lineSpacingPct = sp.lineSpacingPct;
  layout.paragraphSpacingPct = sp.paraSpacingPct;
  // Landscape lines are 50% longer: wider margins keep the measure readable.
  layout.marginH = static_cast<int16_t>(sp.marginH + (landscape ? 28 : 0));
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
    showDiagnostic();
    return;
  }
  if (row == books + 1) {
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
  storage_.writeFile(kRecentBookPath, path, static_cast<uint32_t>(strlen(path)));
  const auto prep = reader_->prepare();
  INKLING_LOG("opened, first chapter: %s", freeink::book::bookStatusName(prep));
  library_.refreshSummaries(storage_);
  resumePending_ = resuming;
  turnsSinceFull_ = 0;
  showReader(RefreshHint::Full);
  return true;
}

void InklingApp::closeBook() {
  applyPendingLayout();  // keep the user's last typography change
  if (reader_) reader_->close();
  progressDirty_ = false;
  savePrefs();
  saveStats();
  storage_.writeFile(kLastBookPath, "", 0);
  library_.refreshSummaries(storage_);
  showHome();
}

void InklingApp::applyPendingLayout() {
  if (layoutDirty_ && reader_ && reader_->isOpen()) {
    reader_->relayout(currentLayout());
    reader_->prepare();
    turnsSinceFull_ = 0;
  }
  layoutDirty_ = false;
}

void InklingApp::showReader(RefreshHint hint) {
  enterMode(Mode::Reader, readerScreen, this, hint);
}

void InklingApp::turnPage(bool forward) {
  bool chapterFlipped = false;
  if (!(forward ? reader_->nextPage(&chapterFlipped) : reader_->prevPage())) return;
  reader_->prepare();

  if (forward) {
    recordPageRead();
  }

  if (++turnsSinceFull_ >= settings_.fullRefreshPages()) {
    turnsSinceFull_ = 0;
    ui_.invalidate(RefreshHint::Full);
  } else {
    ui_.invalidate(RefreshHint::Fast);
  }
}

void InklingApp::showReaderMenu() {
  // When opening ReaderMenu directly from Reader, the book page is ALREADY in
  // frame_.framebuffer — no need to re-decode or re-rasterize the book page!
  needsReaderBackdrop_ = needsReaderBackdrop_ || (mode_ != Mode::Reader);
  readerMenu_.reset(kMenuCount, kMenuContinue);
  enterMode(Mode::ReaderMenu, readerMenuScreen, this, RefreshHint::Fast);
}

void InklingApp::exitReaderMenu() {
  applyPendingLayout();
  showReader(RefreshHint::Clean);
}

void InklingApp::showToc() {
  applyPendingLayout();
  const int entries = static_cast<int>(reader_->tocCount());
  if (entries == 0) {
    showReader(RefreshHint::Clean);
    return;
  }
  tocMenu_.reset(entries, reader_->currentTocIndex());
  enterMode(Mode::Toc, tocScreen, this, RefreshHint::Clean);
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
      settings_.nightMode = nightMode_;
      settings_.save(storage_);
      break;
    case kMenuSettings:
      showSettings(true);
      return;
    case kMenuHome:
      closeBook();
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
    case kMenuSettings:     showSettings(true); break;
    case kMenuHome:         closeBook(); break;
    default:                break;
  }
}

void InklingApp::runHomeDockItem(int item) {
  switch (item) {
    case kDockRead:    openActiveOrFirstBook(); break;
    case kDockLibrary: showLibrary(); break;
    case kDockDesk:    showDeskAmbient(); break;
    case kDockOracle:  showOracle(); break;
    case kDockRpg:     showTextRpg(); break;
    case kDockSystem:  showSettings(); break;
    default:           break;
  }
}

void InklingApp::adjustSettingItem(int item, int delta) {
  switch (item) {
    case kSetRotation: {
      settings_.cycleRotationMode(delta);
      Orientation locked;
      if (settings_.lockedOrientation(locked)) {
        setDesiredOrientation(locked);
      } else if (status_.imuReady) {
        // Automatic: follow how the device is held right now.
        Orientation now;
        if (OrientationTracker::classify(status_.ax, status_.ay, status_.az, settings_.autoAllowsLandscape(), now)) {
          setDesiredOrientation(now);
        } else if (!settings_.autoAllowsLandscape() && isLandscape(desiredOrientation_)) {
          setDesiredOrientation(Orientation::Portrait);
        }
      }
      break;
    }

    case kSetInvertKeys:
      settings_.cycleInvertKeys();
      break;

    case kSetAutoSleep:
      settings_.cycleAutoSleep(delta);
      break;

    case kSetRefreshCadence:
      settings_.cycleRefreshCadence(delta);
      break;

    case kSetDeskWidget:
      settings_.cycleDeskWidget(delta);
      desk_.setSubWidget(static_cast<DeskSubWidget>(settings_.deskWidget));
      break;

    case kSetTheme:
      settings_.toggleNightMode();
      nightMode_ = settings_.nightMode;
      savePrefs();
      break;

    case kSetWifiOta:
    case kSetDiagnostic:
      break;

    default:
      return;
  }
  settings_.save(storage_);
  cursorOnlyRedraw_ = false;
  ui_.invalidate(item == kSetTheme ? RefreshHint::Clean : RefreshHint::Fast);
}

void InklingApp::runSettingItem(int item) {
  switch (item) {
    case kSetWifiOta:
      settings_.save(storage_);
      if (request_) request_(Request::WifiUpdate, requestUser_);
      break;
    case kSetDiagnostic:
      diagnosticFromSettings_ = true;
      showDiagnostic();
      break;
    default:
      adjustSettingItem(item, 1);
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
    if (mode_ == Mode::Oracle) {
      oracle_.onShake();
      cursorOnlyRedraw_ = false;
      ui_.invalidate(RefreshHint::Fast);
    }
  }
}

namespace {

constexpr int16_t kHudH = 40;         // drawTopHud
constexpr int16_t kKeyBarH = 38;      // drawBottomKeyBar
constexpr int16_t kMargin = 14;
constexpr int16_t kGap = 12;
constexpr int16_t kMinCardH = 124;    // library cards: cover (92) + margins

int16_t contentTop() { return static_cast<int16_t>(kHudH + 6); }
int16_t contentBottom() { return static_cast<int16_t>(screenH() - kKeyBarH - 6); }

// First item of the page that contains `sel` when `perPage` items fit.
int pageFirst(int sel, int count, int perPage) {
  if (perPage <= 0) return 0;
  int first = (sel / perPage) * perPage;
  if (first > count - perPage) first = count - perPage;
  return first < 0 ? 0 : first;
}

int libraryPerPage() {
  const int cols = isLandscape() ? 2 : 1;
  const int16_t gapY = 10;
  const int16_t listTop = static_cast<int16_t>(contentTop() + 30);
  const int16_t listBottom = contentBottom();
  int rowsFit = (listBottom - listTop + gapY) / (kMinCardH + gapY);
  if (rowsFit < 1) rowsFit = 1;
  return rowsFit * cols;
}

int settingsPerPage() {
  const int cols = isLandscape() ? 2 : 1;
  const int16_t listTop = static_cast<int16_t>(contentTop() + 26);
  const int16_t listBottom = contentBottom();
  const int16_t gapY = 8;
  constexpr int16_t kCardMinH = 116;
  int rowsFit = (listBottom - listTop + gapY) / (kCardMinH + gapY);
  if (rowsFit < 1) rowsFit = 1;
  return rowsFit * cols;
}

int readerMenuPerPage() {
  const int16_t h = screenH();
  const int cols = isLandscape() ? 2 : 1;
  const int16_t modalBottom = static_cast<int16_t>(h - kKeyBarH - 16);
  const int16_t gridTop = 80;
  const int16_t gridBottom = static_cast<int16_t>(modalBottom - 14);
  const int16_t gapY = 12;
  constexpr int16_t kCardMinH = 116;
  int rowsFit = (gridBottom - gridTop + gapY) / (kCardMinH + gapY);
  if (rowsFit < 1) rowsFit = 1;
  return rowsFit * cols;
}

int tocPerPage() {
  const int16_t listTop = static_cast<int16_t>(contentTop() + 2 + 64 + 12);
  const int16_t listBottom = contentBottom();
  const int16_t rowH = 58;
  const int16_t gapY = 8;
  int perPage = (listBottom - listTop + gapY) / (rowH + gapY);
  return perPage < 1 ? 1 : perPage;
}

// "3/12" page indicator when a list does not fit one screen.
void drawPageDots(DrawTarget& t, int16_t rightX, int16_t y, int sel, int count, int perPage) {
  if (perPage <= 0 || count <= perPage) return;
  const int pages = (count + perPage - 1) / perPage;
  const int page = sel / perPage;
  const int16_t dot = 8;
  const int16_t step = 14;
  int16_t x = static_cast<int16_t>(rightX - pages * step);
  for (int i = 0; i < pages; ++i) {
    const Rect d{x, y, dot, dot};
    if (i == page) {
      fastFill(t, d, Paint::solid(Color::Black), 4);
    } else {
      fastStroke(t, d, Paint::solid(Color::Black), 1, 4);
    }
    x = static_cast<int16_t>(x + step);
  }
}

}  // namespace

void InklingApp::onButton(Button button) {
  const bool up = button == Button::Up || button == Button::Left;
  const bool down = button == Button::Down || button == Button::Right;
  switch (mode_) {
    case Mode::Home:
      if (button == Button::Up) {
        homeDock_.move(-homeDockColumns());
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Down) {
        homeDock_.move(homeDockColumns());
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Left) {
        homeDock_.move(-1);
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Right) {
        homeDock_.move(1);
        cursorOnlyRedraw_ = true;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Confirm) {
        runHomeDockItem(homeDock_.selected);
      } else if (button == Button::Back) {
        showLibrary();
      }
      break;

    case Mode::DeskAmbient:
      if (button == Button::Right || button == Button::Left) {
        desk_.cycleSubWidget();
        settings_.deskWidget = static_cast<DeskWidgetType>(desk_.activeSubWidget());
        settings_.save(storage_);
        cursorOnlyRedraw_ = false;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Confirm) {
        cursorOnlyRedraw_ = false;
        ui_.invalidate(RefreshHint::Clean);
      } else if (button == Button::Back) {
        showHome();
      }
      break;

    case Mode::Oracle:
      if (button == Button::Right || button == Button::Left) {
        oracle_.toggleDeck();
        cursorOnlyRedraw_ = false;
        ui_.invalidate(RefreshHint::Clean);
      } else if (button == Button::Confirm || button == Button::Down) {
        oracle_.nextCard();
        cursorOnlyRedraw_ = false;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Up) {
        oracle_.prevCard();
        cursorOnlyRedraw_ = false;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Back) {
        showHome();
      }
      break;

    case Mode::TextRpg:
      if (button == Button::Up) {
        rpg_.moveCursor(-1);
        cursorOnlyRedraw_ = false;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Down) {
        rpg_.moveCursor(1);
        cursorOnlyRedraw_ = false;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Confirm) {
        rpg_.confirmChoice();
        rpg_.save(storage_);
        cursorOnlyRedraw_ = false;
        ui_.invalidate(RefreshHint::Fast);
      } else if (button == Button::Back) {
        showHome();
      }
      break;

    case Mode::Library: {
      const int perPage = libraryPerPage();
      const int oldFirst = pageFirst(libraryMenu_.selected, libraryMenu_.count, perPage);
      if (button == Button::Up) {
        libraryMenu_.move(-1);
        const bool samePage = pageFirst(libraryMenu_.selected, libraryMenu_.count, perPage) == oldFirst;
        cursorOnlyRedraw_ = samePage;
        ui_.invalidate(samePage ? RefreshHint::Fast : RefreshHint::Clean);
      } else if (button == Button::Down || button == Button::Right) {
        libraryMenu_.move(1);
        const bool samePage = pageFirst(libraryMenu_.selected, libraryMenu_.count, perPage) == oldFirst;
        cursorOnlyRedraw_ = samePage;
        ui_.invalidate(samePage ? RefreshHint::Fast : RefreshHint::Clean);
      } else if (button == Button::Confirm) {
        openSelected();
      } else if (button == Button::Back || button == Button::Left) {
        showHome();
      }
      break;
    }

    case Mode::Settings: {
      const int perPage = settingsPerPage();
      const int oldFirst = pageFirst(settingsMenu_.selected, kSetCount, perPage);
      if (button == Button::Up) {
        settingsMenu_.move(-1);
        const bool samePage = pageFirst(settingsMenu_.selected, kSetCount, perPage) == oldFirst;
        cursorOnlyRedraw_ = samePage;
        ui_.invalidate(samePage ? RefreshHint::Fast : RefreshHint::Clean);
      } else if (button == Button::Down) {
        settingsMenu_.move(1);
        const bool samePage = pageFirst(settingsMenu_.selected, kSetCount, perPage) == oldFirst;
        cursorOnlyRedraw_ = samePage;
        ui_.invalidate(samePage ? RefreshHint::Fast : RefreshHint::Clean);
      } else if (button == Button::Left) {
        adjustSettingItem(settingsMenu_.selected, -1);
      } else if (button == Button::Right) {
        adjustSettingItem(settingsMenu_.selected, 1);
      } else if (button == Button::Confirm) {
        runSettingItem(settingsMenu_.selected);
      } else if (button == Button::Back) {
        settings_.save(storage_);
        if (settingsFromReader_ && reader_ && reader_->isOpen()) {
          showReaderMenu();
        } else {
          showHome();
        }
      }
      break;
    }

    case Mode::Reader:
      if (down) turnPage(true);
      if (up) turnPage(false);
      if (button == Button::Confirm) showReaderMenu();
      if (button == Button::Back) closeBook();
      break;

    case Mode::ReaderMenu: {
      const int perPage = readerMenuPerPage();
      const int oldFirst = pageFirst(readerMenu_.selected, kMenuCount, perPage);
      if (button == Button::Up) {
        readerMenu_.move(-1);
        const bool samePage = pageFirst(readerMenu_.selected, kMenuCount, perPage) == oldFirst;
        cursorOnlyRedraw_ = samePage;
        ui_.invalidate(samePage ? RefreshHint::Fast : RefreshHint::Clean);
      } else if (button == Button::Down) {
        readerMenu_.move(1);
        const bool samePage = pageFirst(readerMenu_.selected, kMenuCount, perPage) == oldFirst;
        cursorOnlyRedraw_ = samePage;
        ui_.invalidate(samePage ? RefreshHint::Fast : RefreshHint::Clean);
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
    }

    case Mode::Toc:
      if (up || down) {
        const int perPage = tocPerPage();
        const int oldFirst = pageFirst(tocMenu_.selected, tocMenu_.count, perPage);
        tocMenu_.move(up ? -1 : 1);
        const bool samePage = pageFirst(tocMenu_.selected, tocMenu_.count, perPage) == oldFirst;
        cursorOnlyRedraw_ = samePage;
        ui_.invalidate(samePage ? RefreshHint::Fast : RefreshHint::Clean);
      } else if (button == Button::Confirm) {
        char title[8];
        const int spine = reader_->tocEntry(static_cast<size_t>(tocMenu_.selected), title, sizeof(title), nullptr);
        if (spine >= 0) {
          reader_->jumpToChapter(static_cast<uint16_t>(spine));
          reader_->prepare();
        }
        turnsSinceFull_ = 0;
        showReader(RefreshHint::Full);
      } else if (button == Button::Back) {
        showReaderMenu();
      }
      break;

    case Mode::Diagnostic:
      if (button == Button::Back) {
        if (diagnosticFromSettings_) {
          diagnosticFromSettings_ = false;
          enterMode(Mode::Settings, settingsScreen, this, RefreshHint::Clean);
        } else {
          showHome();
        }
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
// Screen 1: HOME
// ---------------------------------------------------------------------------
void InklingApp::homeScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = screenW();
  const bool land = isLandscape();
  const int activeIdx = self.activeBookIndex();
  const int cols = self.homeDockColumns();
  const int rows = (kDockCount + cols - 1) / cols;

  // Geometry: clock + book cards on top (side by side in landscape), dock below.
  const int16_t cardH = 130;
  Rect clockCard, bookCard;
  if (land) {
    const int16_t cw = static_cast<int16_t>((w - 2 * kMargin - kGap) / 2);
    clockCard = Rect{kMargin, contentTop(), cw, cardH};
    bookCard = Rect{static_cast<int16_t>(kMargin + cw + kGap), contentTop(), cw, cardH};
  } else {
    clockCard = Rect{kMargin, contentTop(), static_cast<int16_t>(w - 2 * kMargin), cardH};
    bookCard = Rect{kMargin, static_cast<int16_t>(clockCard.bottom() + 10), clockCard.width, cardH};
  }
  const int16_t dockTop = static_cast<int16_t>(bookCard.bottom() + 12);
  const int16_t dockBottom = contentBottom();
  const int16_t rowGap = 10;
  const int16_t tileW = static_cast<int16_t>((w - 2 * kMargin - (cols - 1) * kGap) / cols);
  const int16_t tileH = static_cast<int16_t>((dockBottom - dockTop - (rows - 1) * rowGap) / rows);

  if (self.cursorOnlyRedraw_) {
    clearRect(Rect{0, static_cast<int16_t>(dockTop - 2), w, static_cast<int16_t>(dockBottom - dockTop + 8)});
  } else {
    clearScreen();
    drawTopHud(t, self.status_, "HOME");
    const LocalTime now = self.currentLocalTime();

    // Clock card: split-flap time + date on the left, today's reading on the right.
    drawCard(t, clockCard, 10, 4, false, true);
    char hStr[4] = "--", mStr[4] = "--";
    if (self.status_.clockValid) {
      snprintf(hStr, sizeof(hStr), "%02u", static_cast<unsigned>(self.status_.hour % 100));
      snprintf(mStr, sizeof(mStr), "%02u", static_cast<unsigned>(self.status_.minute % 100));
    }
    const int16_t fx = static_cast<int16_t>(clockCard.x + 12);
    const int16_t fy = static_cast<int16_t>(clockCard.y + 12);
    const int16_t flapW = 42;
    const int16_t flapH = 68;
    DeskDashboard::drawSplitFlapCard(t, fx, fy, flapW, flapH, hStr[0]);
    DeskDashboard::drawSplitFlapCard(t, static_cast<int16_t>(fx + 46), fy, flapW, flapH, hStr[1]);
    fillCircle(t, static_cast<int16_t>(fx + 96), static_cast<int16_t>(fy + 22), 3, Color::Black);
    fillCircle(t, static_cast<int16_t>(fx + 96), static_cast<int16_t>(fy + 46), 3, Color::Black);
    DeskDashboard::drawSplitFlapCard(t, static_cast<int16_t>(fx + 104), fy, flapW, flapH, mStr[0]);
    DeskDashboard::drawSplitFlapCard(t, static_cast<int16_t>(fx + 150), fy, flapW, flapH, mStr[1]);

    char date[48];
    formatItalianDate(now, date, sizeof(date));
    drawText(t, Rect{fx, static_cast<int16_t>(fy + flapH + 10), 196, 22}, date[0] ? date : "Ora non impostata",
             kFontSmall, Color::Black);
    char power[32];
    if (self.status_.batteryPercent >= 0) {
      snprintf(power, sizeof(power), "Batteria %d%%%s", self.status_.batteryPercent,
               self.status_.charging ? " · in carica" : "");
    } else {
      snprintf(power, sizeof(power), "%s", self.status_.charging ? "In carica" : "");
    }
    drawText(t, Rect{fx, static_cast<int16_t>(fy + flapH + 32), 196, 18}, power, kFontMicro, Color::Black);

    const int16_t rx = static_cast<int16_t>(clockCard.x + 222);
    const int16_t rw = static_cast<int16_t>(clockCard.right() - rx - 14);
    drawPill(t, rx, fy, 22, "OGGI", kFontMicro, true, 10);
    char pages[40];
    snprintf(pages, sizeof(pages), "%u / %u pagine", static_cast<unsigned>(self.dailyPages_),
             static_cast<unsigned>(kDailyPageGoal));
    drawText(t, Rect{rx, static_cast<int16_t>(fy + 30), rw, 26}, pages, kFontBody, Color::Black);
    const uint8_t goalPct = static_cast<uint8_t>(
        self.dailyPages_ >= kDailyPageGoal ? 100 : (self.dailyPages_ * 100u) / kDailyPageGoal);
    drawProgressBar(t, Rect{rx, static_cast<int16_t>(fy + 60), rw, 14}, goalPct, false);
    char streak[40];
    snprintf(streak, sizeof(streak), "Serie: %u %s", static_cast<unsigned>(self.streakDays_),
             self.streakDays_ == 1 ? "giorno" : "giorni");
    drawText(t, Rect{rx, static_cast<int16_t>(fy + 82), rw, 22}, streak, kFontSmall, Color::Black);
    char libLine[48];
    snprintf(libLine, sizeof(libLine), "%lu pag. lette · %d volumi",
             static_cast<unsigned long>(self.totalPagesRead_), self.library_.count());
    drawText(t, Rect{rx, static_cast<int16_t>(fy + 102), rw, 18}, libLine, kFontMicro, Color::Black);

    // Book card: the active (or next) book with its real dithered cover.
    drawCard(t, bookCard, 10, 3, false, false);
    if (activeIdx >= 0 && activeIdx < self.library_.count()) {
      const auto& b = self.library_.at(activeIdx);
      const int16_t coverY = static_cast<int16_t>(bookCard.y + (bookCard.height - kCoverH) / 2);
      uint8_t coverBits[kCoverBytes];
      const bool hasCover = self.loadBookThumb(b.path, coverBits);
      drawBookCover(t, static_cast<int16_t>(bookCard.x + 14), coverY, b.name, b.author,
                    hasCover ? coverBits : nullptr);
      const int16_t bx = static_cast<int16_t>(bookCard.x + 14 + kCoverW + 16);
      const int16_t bw = static_cast<int16_t>(bookCard.right() - bx - 14);
      drawPill(t, bx, static_cast<int16_t>(bookCard.y + 12), 22, b.started ? "IN LETTURA" : "DA INIZIARE",
               kFontMicro, true, 10);
      char pctBadge[16];
      snprintf(pctBadge, sizeof(pctBadge), "%u%%", static_cast<unsigned>(b.progressPct));
      drawText(t, Rect{static_cast<int16_t>(bx + bw - 60), static_cast<int16_t>(bookCard.y + 12), 60, 22}, pctBadge,
               kFontBody, Color::Black, TextAlign::Right, false);
      drawText(t, Rect{bx, static_cast<int16_t>(bookCard.y + 42), bw, 28}, b.name, kFontTitle, Color::Black);
      drawText(t, Rect{bx, static_cast<int16_t>(bookCard.y + 72), bw, 22}, b.author[0] ? b.author : " ", kFontSmall,
               Color::Black);
      drawProgressBar(t, Rect{bx, static_cast<int16_t>(bookCard.y + 102), bw, 14}, b.progressPct, false);
    } else {
      drawIcon16(t, static_cast<int16_t>(bookCard.x + 20), static_cast<int16_t>(bookCard.y + 30), Icon::Book,
                 Color::Black);
      drawText(t, Rect{static_cast<int16_t>(bookCard.x + 46), static_cast<int16_t>(bookCard.y + 24),
                       static_cast<int16_t>(bookCard.width - 60), 28},
               "Nessun libro sulla microSD", kFontTitle, Color::Black);
      drawText(t, Rect{static_cast<int16_t>(bookCard.x + 46), static_cast<int16_t>(bookCard.y + 58),
                       static_cast<int16_t>(bookCard.width - 60), 22},
               "Copia file .epub, .txt o .md, o usa il Wi-Fi.", kFontSmall, Color::Black);
    }
    drawBottomKeyBar(t, "FRECCE", "Sposta", "OK", "Apri", "BACK", "Libreria");
  }

  // Dock: 3x2 in portrait, 2x3 in landscape.
  static const char* const kDockTitles[kDockCount] = {
      "CONTINUA", "LIBRERIA", "OROLOGIO", "ORACOLO", "CRIPTA", "IMPOSTAZ.",
  };
  static const Icon kDockIcons[kDockCount] = {
      Icon::Book, Icon::Compass, Icon::Moon, Icon::Spark, Icon::Flame, Icon::Sliders,
  };
  char sub0[40], sub1[40], sub4[40];
  if (activeIdx >= 0) {
    snprintf(sub0, sizeof(sub0), "Riprendi da %u%%", static_cast<unsigned>(self.library_.at(activeIdx).progressPct));
  } else {
    snprintf(sub0, sizeof(sub0), "Nessun libro");
  }
  snprintf(sub1, sizeof(sub1), "%d %s su microSD", self.library_.count(),
           self.library_.count() == 1 ? "volume" : "volumi");
  snprintf(sub4, sizeof(sub4), "Dungeon RPG · Piano %d", self.rpg_.depth());
  const char* const subs[kDockCount] = {
      sub0, sub1, "Desk stand e calendario", "Stoici e Oblique Strategies", sub4, "Rotazione, sleep, Wi-Fi",
  };

  for (int i = 0; i < kDockCount; ++i) {
    const int col = i % cols;
    const int row = i / cols;
    const int16_t tx = static_cast<int16_t>(kMargin + col * (tileW + kGap));
    const int16_t ty = static_cast<int16_t>(dockTop + row * (tileH + rowGap));
    const bool sel = (self.homeDock_.selected == i);
    const Rect tr{tx, ty, tileW, tileH};
    drawCard(t, tr, 8, sel ? 4 : 2, sel, sel);
    const Color fg = sel ? Color::White : Color::Black;
    const int16_t textY = static_cast<int16_t>(ty + tileH - 62);
    drawIcon16(t, static_cast<int16_t>(tx + 14), static_cast<int16_t>(ty + 14), kDockIcons[i], fg);
    drawText(t, Rect{static_cast<int16_t>(tx + 14), textY, static_cast<int16_t>(tileW - 28), 26}, kDockTitles[i],
             kFontBody, fg);
    drawText(t, Rect{static_cast<int16_t>(tx + 14), static_cast<int16_t>(textY + 28), static_cast<int16_t>(tileW - 28),
                     22},
             subs[i], kFontSmall, fg);
  }
  self.cursorOnlyRedraw_ = false;
}

// ---------------------------------------------------------------------------
// Screen 2: LIBRARY (books, then the system shortcuts)
// ---------------------------------------------------------------------------
void InklingApp::libraryScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = screenW();
  const int books = self.library_.count();
  const int totalRows = books + kLibrarySystemRows;
  const int sel = self.libraryMenu_.selected;

  // Cards in one column (portrait) or two (landscape).
  const int cols = isLandscape() ? 2 : 1;
  const int16_t gapY = 10;
  const int16_t listTop = static_cast<int16_t>(contentTop() + 30);
  const int16_t listBottom = contentBottom();
  int rowsFit = (listBottom - listTop + gapY) / (kMinCardH + gapY);
  if (rowsFit < 1) rowsFit = 1;
  // Stretch the cards to use the whole height.
  const int16_t cardH = static_cast<int16_t>((listBottom - listTop - (rowsFit - 1) * gapY) / rowsFit);
  const int perPage = rowsFit * cols;
  const int16_t cardW = static_cast<int16_t>((w - 2 * kMargin - (cols - 1) * kGap) / cols);
  const int first = pageFirst(sel, totalRows, perPage);

  if (self.cursorOnlyRedraw_) {
    clearRect(Rect{0, contentTop(), w, static_cast<int16_t>(listBottom - contentTop() + 4)});
  } else {
    clearScreen();
    drawTopHud(t, self.status_, "LIBRERIA");
    drawBottomKeyBar(t, "SU/GIU", "Scorri", "OK", "Apri", "BACK", "Home");
  }

  char countTxt[48];
  snprintf(countTxt, sizeof(countTxt), "%d %s  ·  %d/%d", books, books == 1 ? "LIBRO" : "LIBRI", sel + 1, totalRows);
  drawText(t, Rect{18, static_cast<int16_t>(contentTop() + 2), static_cast<int16_t>(w - 140), 22}, countTxt,
           kFontMicro, Color::Black);
  drawPageDots(t, static_cast<int16_t>(w - 18), static_cast<int16_t>(contentTop() + 8), sel, totalRows, perPage);

  for (int slot = 0; slot < perPage; ++slot) {
    const int idx = first + slot;
    if (idx >= totalRows) break;
    const bool active = (idx == sel);
    const int16_t cx = static_cast<int16_t>(kMargin + (slot % cols) * (cardW + kGap));
    const int16_t cy = static_cast<int16_t>(listTop + (slot / cols) * (cardH + gapY));
    const Rect cr{cx, cy, cardW, cardH};
    drawCard(t, cr, 10, active ? 5 : 3, false, active);
    if (active) fastFill(t, Rect{cx, cy, 10, cardH}, Paint::solid(Color::Black), 5);

    const int16_t tx = static_cast<int16_t>(cx + 100);
    const int16_t tw = static_cast<int16_t>(cr.right() - tx - 14);
    if (idx < books) {
      const auto& b = self.library_.at(idx);
      uint8_t coverBits[kCoverBytes];
      const bool hasCover = self.loadBookThumb(b.path, coverBits);
      drawBookCover(t, static_cast<int16_t>(cx + 22), static_cast<int16_t>(cy + (cardH - kCoverH) / 2), b.name,
                    b.author, hasCover ? coverBits : nullptr);
      const char* tag = b.progressPct >= 99 ? "FINITO" : (b.started ? "IN LETTURA" : "NUOVO");
      drawPill(t, tx, static_cast<int16_t>(cy + 12), 22, tag, kFontMicro, active, 10);
      char pctStr[16];
      snprintf(pctStr, sizeof(pctStr), "%u%%", static_cast<unsigned>(b.progressPct));
      drawText(t, Rect{static_cast<int16_t>(tx + tw - 60), static_cast<int16_t>(cy + 12), 60, 22}, pctStr, kFontBody,
               Color::Black, TextAlign::Right, false);
      drawText(t, Rect{tx, static_cast<int16_t>(cy + 42), tw, 28}, b.name, kFontTitle, Color::Black);
      drawText(t, Rect{tx, static_cast<int16_t>(cy + 72), tw, 22}, b.author, kFontSmall, Color::Black);
      drawProgressBar(t, Rect{tx, static_cast<int16_t>(cy + 102), tw, 14}, b.progressPct, false);
    } else {
      static const char* const kTitles[kLibrarySystemRows] = {"Diagnostica", "Wi-Fi: giornale e libri"};
      static const char* const kSubs[kLibrarySystemRows] = {
          "Batteria, sensori, memoria, slot", "Portale web, Daily Ink e OTA"};
      static const char* const kBadges[kLibrarySystemRows] = {"SISTEMA", "WI-FI"};
      static const Icon kIcons[kLibrarySystemRows] = {Icon::Compass, Icon::Wifi};
      const int sys = idx - books;
      const Rect iconBox{static_cast<int16_t>(cx + 22), static_cast<int16_t>(cy + (cardH - 88) / 2), 64, 88};
      fastFill(t, iconBox, Paint::solid(active ? Color::Black : Color::White), 8);
      fastStroke(t, iconBox, Paint::solid(Color::Black), 2, 8);
      drawIcon16(t, static_cast<int16_t>(iconBox.x + 24), static_cast<int16_t>(iconBox.y + 36), kIcons[sys],
                 active ? Color::White : Color::Black);
      drawPill(t, tx, static_cast<int16_t>(cy + 12), 22, kBadges[sys], kFontMicro, active, 10);
      drawText(t, Rect{tx, static_cast<int16_t>(cy + 44), tw, 28}, kTitles[sys], kFontTitle, Color::Black);
      drawText(t, Rect{tx, static_cast<int16_t>(cy + 76), tw, 22}, kSubs[sys], kFontSmall, Color::Black);
    }
  }
  self.cursorOnlyRedraw_ = false;
}

// ---------------------------------------------------------------------------
// Screen 3: READER (page + status bar)
// ---------------------------------------------------------------------------
void InklingApp::readerScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = screenW();
  const int16_t h = screenH();

  clearScreen();
  self.lastRenderStatus_ = self.reader_->render(self.frame_);
  self.progressDirty_ = true;  // written by idleTick(), after the panel refresh
  self.needsReaderBackdrop_ = false;
  if (self.resumePending_ && self.lastRenderStatus_ == freeink::book::BookStatus::Ok) {
    self.resumePending_ = false;
    self.setResumeLock(false);
  }

  // Status bar.
  const int16_t hudY = static_cast<int16_t>(h - kStatusLineHeight);
  fastFill(t, Rect{0, hudY, w, kStatusLineHeight}, Paint::solid(Color::White));
  fastFill(t, Rect{0, hudY, w, 2}, Paint::solid(Color::Black));
  const uint8_t pct = self.reader_->percent();
  const int16_t progW = static_cast<int16_t>((static_cast<int32_t>(w) * pct) / 100);
  if (progW > 0) fastFill(t, Rect{0, static_cast<int16_t>(hudY + 2), progW, 3}, Paint::solid(Color::Black));

  if (self.lastRenderStatus_ != freeink::book::BookStatus::Ok) {
    char errLine[80];
    snprintf(errLine, sizeof(errLine), "Errore pagina: %s", freeink::book::bookStatusName(self.lastRenderStatus_));
    drawText(t, Rect{14, static_cast<int16_t>(hudY + 7), static_cast<int16_t>(w - 28), 26}, errLine, kFontSmall,
             Color::Black);
    if (self.nightMode_) invertScreen();
    return;
  }

  // Right: page in chapter, book %, clock. Measured, so the title gets the rest.
  char rightMeta[48];
  if (self.status_.clockValid) {
    snprintf(rightMeta, sizeof(rightMeta), "%u/%u · %u%% · %02u:%02u",
             static_cast<unsigned>(self.reader_->pageInChapter()),
             static_cast<unsigned>(self.reader_->pagesInChapter()), static_cast<unsigned>(pct), self.status_.hour,
             self.status_.minute);
  } else {
    snprintf(rightMeta, sizeof(rightMeta), "%u/%u · %u%%", static_cast<unsigned>(self.reader_->pageInChapter()),
             static_cast<unsigned>(self.reader_->pagesInChapter()), static_cast<unsigned>(pct));
  }
  const int16_t metaW = static_cast<int16_t>(textWidth(t, rightMeta, kFontMicro) + 20);
  const int16_t metaX = static_cast<int16_t>(w - 10 - metaW);
  drawPill(t, metaX, static_cast<int16_t>(hudY + 8), 24, rightMeta, kFontMicro, false, 10);

  const int16_t titleW = static_cast<int16_t>(metaX - 14 - 10);
  char chapTitle[64];
  self.reader_->currentChapterTitle(chapTitle, sizeof(chapTitle));
  drawText(t, Rect{14, static_cast<int16_t>(hudY + 8), titleW, 24}, chapTitle, kFontSmall, Color::Black);
  if (self.nightMode_) invertScreen();
}

// ---------------------------------------------------------------------------
// Screen 4: READER MENU (modal over the live page)
// ---------------------------------------------------------------------------
void InklingApp::readerMenuScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = screenW();
  const int16_t h = screenH();
  const int sel = self.readerMenu_.selected;
  const uint8_t step = self.fontStep_ < kFontSizeCount ? self.fontStep_ : 3;
  const uint8_t fam = self.fontFamily_ < kFontFamilyCount ? self.fontFamily_ : 0;
  const uint8_t spStep = self.spacingStep_ < kSpacingPresetCount ? self.spacingStep_ : 0;
  const uint8_t alStep = self.alignStep_ < kAlignPresetCount ? self.alignStep_ : 0;

  // Geometry: modal inset from the screen edges, above the key bar.
  const Rect modal{14, 12, static_cast<int16_t>(w - 28), static_cast<int16_t>(h - kKeyBarH - 12 - 16)};
  const Rect hdr{static_cast<int16_t>(modal.x + 12), static_cast<int16_t>(modal.y + 12),
                 static_cast<int16_t>(modal.width - 24), 44};
  const int cols = isLandscape() ? 2 : 1;
  const int16_t gridX = static_cast<int16_t>(modal.x + 14);
  const int16_t gridW = static_cast<int16_t>(modal.width - 28);
  const int16_t gridTop = static_cast<int16_t>(hdr.bottom() + 12);
  const int16_t gridBottom = static_cast<int16_t>(modal.bottom() - 14);
  const int16_t gapY = 12;
  constexpr int16_t kCardMinH = 116;
  int rowsFit = (gridBottom - gridTop + gapY) / (kCardMinH + gapY);
  if (rowsFit < 1) rowsFit = 1;
  const int perPage = rowsFit * cols;
  const int16_t cardH = static_cast<int16_t>((gridBottom - gridTop - (rowsFit - 1) * gapY) / rowsFit);
  const int16_t cardW = static_cast<int16_t>((gridW - (cols - 1) * kGap) / cols);

  if (self.cursorOnlyRedraw_) {
    clearRect(Rect{gridX, static_cast<int16_t>(gridTop - 2), static_cast<int16_t>(gridW + 6),
                   static_cast<int16_t>(gridBottom - gridTop + 8)});
  } else {
    // Coming from anywhere but the page itself (TOC, settings, a rotation),
    // the page under the modal has to be drawn again first.
    if (self.needsReaderBackdrop_ && self.reader_ && self.reader_->isOpen()) {
      clearScreen();
      self.reader_->render(self.frame_);
      if (self.nightMode_) invertScreen();
    }
    self.needsReaderBackdrop_ = false;
    drawCard(t, modal, 12, 6, false, true);
    fastFill(t, hdr, Paint::solid(Color::Black), 8);
    drawText(t, Rect{static_cast<int16_t>(hdr.x + 16), static_cast<int16_t>(hdr.y + 6), 300, 32}, "CONTROLLO LETTURA",
             kFontTitle, Color::White);
    drawBottomKeyBar(t, "SU/GIU", "Scorri", "SX/DX", "Cambia", "BACK", "Libro");
  }

  // Header badge: item index + book %. Redrawn on every move.
  char hdrBadge[32];
  snprintf(hdrBadge, sizeof(hdrBadge), "%d/%d · %u%%", sel + 1, static_cast<int>(kMenuCount),
           static_cast<unsigned>(self.reader_->percent()));
  const int16_t badgeW = static_cast<int16_t>(textWidth(t, hdrBadge, kFontSmall) + 20);
  fastFill(t, Rect{static_cast<int16_t>(hdr.right() - 170), static_cast<int16_t>(hdr.y + 4), 166, 36},
           Paint::solid(Color::Black));
  drawPill(t, static_cast<int16_t>(hdr.right() - 10 - badgeW), static_cast<int16_t>(hdr.y + 8), 28, hdrBadge,
           kFontSmall, false, 10);

  static const char* const kTitles[kMenuCount] = {
      "TORNA AL LIBRO", "INDICE CAPITOLI", "DIMENSIONE TESTO", "CARATTERE",    "LETTURA VELOCE",
      "SPAZIATURA",     "IMPAGINAZIONE",   "MODALITÀ NOTTE",   "IMPOSTAZIONI", "CHIUDI LIBRO",
  };
  static const Icon kRowIcons[kMenuCount] = {
      Icon::Book,    Icon::Compass, Icon::Spark, Icon::Sliders, Icon::Flame,
      Icon::Sliders, Icon::Check,   Icon::Moon,  Icon::Sliders, Icon::Book,
  };
  char v0[48], v1[48], v2[48], v3[48], v4[48], v5[48], v6[48], v7[48];
  snprintf(v0, sizeof(v0), "%s  ·  SX/DX ±10%%", self.layoutDirty_ ? "OK: applica" : "OK: continua");
  snprintf(v1, sizeof(v1), "Cap. %u di %u  ·  OK: apri", static_cast<unsigned>(self.reader_->currentSpine() + 1),
           static_cast<unsigned>(self.reader_->spineCount()));
  snprintf(v2, sizeof(v2), "<  %u px · %s  >", kFontSizes[step], kFontSizeLabels[step]);
  snprintf(v3, sizeof(v3), "<  %s  >", fontFamilyName(fam));
  snprintf(v4, sizeof(v4), "<  Bionic %s  >", self.focusReading_ ? "attiva" : "spenta");
  snprintf(v5, sizeof(v5), "<  %s  >", kSpacingPresets[spStep].label);
  snprintf(v6, sizeof(v6), "<  %s  >", kAlignPresets[alStep].label);
  snprintf(v7, sizeof(v7), "<  %s  >", self.nightMode_ ? "Scura" : "Chiara");
  const char* const values[kMenuCount] = {
      v0, v1, v2, v3, v4, v5, v6, v7, "Rotazione, sleep, Wi-Fi  ·  OK", "OK: chiudi e torna a Home",
  };

  const int first = pageFirst(sel, kMenuCount, perPage);
  for (int slot = 0; slot < perPage; ++slot) {
    const int idx = first + slot;
    if (idx >= kMenuCount) break;
    const bool active = (idx == sel);
    const int16_t cx = static_cast<int16_t>(gridX + (slot % cols) * (cardW + kGap));
    const int16_t cy = static_cast<int16_t>(gridTop + (slot / cols) * (cardH + gapY));
    const Rect cr{cx, cy, cardW, cardH};
    drawCard(t, cr, 10, active ? 5 : 2, active, active);
    const Color fg = active ? Color::White : Color::Black;
    drawIcon16(t, static_cast<int16_t>(cx + 18), static_cast<int16_t>(cy + 20), kRowIcons[idx], fg);
    const char* titleTxt = (idx == kMenuContinue && self.layoutDirty_) ? "APPLICA E TORNA" : kTitles[idx];
    drawText(t, Rect{static_cast<int16_t>(cx + 44), static_cast<int16_t>(cy + 12), static_cast<int16_t>(cardW - 58), 32},
             titleTxt, kFontTitle, fg);

    const Rect valBox{static_cast<int16_t>(cx + 14), static_cast<int16_t>(cy + cardH - 66),
                      static_cast<int16_t>(cardW - 28), 52};
    fastFill(t, valBox, Paint::solid(active ? Color::White : Color::Black), 8);
    const bool isSetting = idx >= kMenuFontSize && idx <= kMenuNightMode;
    const FontSlot vf =
        fitFont(t, values[idx], static_cast<int16_t>(valBox.width - 16), isSetting ? kFontTitle : kFontBody);
    drawText(t, valBox, values[idx], vf, active ? Color::Black : Color::White, TextAlign::Center, false);
  }
  self.cursorOnlyRedraw_ = false;
}

// ---------------------------------------------------------------------------
// Screen 5: TABLE OF CONTENTS
// ---------------------------------------------------------------------------
void InklingApp::tocScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = screenW();
  const int count = self.tocMenu_.count;
  const int sel = self.tocMenu_.selected;
  const int currentChap = self.reader_->currentTocIndex();

  const Rect hdrCard{kMargin, static_cast<int16_t>(contentTop() + 2), static_cast<int16_t>(w - 2 * kMargin), 64};
  const int16_t listTop = static_cast<int16_t>(hdrCard.bottom() + 12);
  const int16_t listBottom = contentBottom();
  const int16_t rowH = 58;
  const int16_t gapY = 8;
  int perPage = (listBottom - listTop + gapY) / (rowH + gapY);
  if (perPage < 1) perPage = 1;

  if (self.cursorOnlyRedraw_) {
    clearRect(Rect{0, static_cast<int16_t>(contentTop()), w, static_cast<int16_t>(listBottom - contentTop() + 4)});
  } else {
    clearScreen();
    drawTopHud(t, self.status_, "INDICE");
    drawBottomKeyBar(t, "SU/GIU", "Capitolo", "OK", "Vai", "BACK", "Menu");
  }

  drawCard(t, hdrCard, 8, 3, false, false);
  char countBadge[32];
  snprintf(countBadge, sizeof(countBadge), "%d / %d", sel + 1, count);
  const int16_t badgeW = static_cast<int16_t>(textWidth(t, countBadge, kFontMicro) + 20);
  drawPill(t, static_cast<int16_t>(hdrCard.right() - 12 - badgeW), static_cast<int16_t>(hdrCard.y + 10), 24,
           countBadge, kFontMicro, true, 10);
  drawText(t, Rect{static_cast<int16_t>(hdrCard.x + 14), static_cast<int16_t>(hdrCard.y + 6),
                   static_cast<int16_t>(hdrCard.width - badgeW - 40), 28},
           self.reader_->title(), kFontTitle, Color::Black);
  drawText(t, Rect{static_cast<int16_t>(hdrCard.x + 14), static_cast<int16_t>(hdrCard.y + 36),
                   static_cast<int16_t>(hdrCard.width - 28), 22},
           self.reader_->author(), kFontSmall, Color::Black);

  const int first = pageFirst(sel, count, perPage);
  for (int slot = 0; slot < perPage; ++slot) {
    const int idx = first + slot;
    if (idx >= count) break;
    const bool active = (idx == sel);
    const bool isCurrent = (idx == currentChap);
    char rawTitle[64] = {};
    uint8_t depth = 0;
    self.reader_->tocEntry(static_cast<size_t>(idx), rawTitle, sizeof(rawTitle), &depth);

    const int16_t ry = static_cast<int16_t>(listTop + slot * (rowH + gapY));
    const Rect rr{kMargin, ry, static_cast<int16_t>(w - 2 * kMargin), rowH};
    drawCard(t, rr, 8, active ? 4 : 2, active, false);
    const Color fg = active ? Color::White : Color::Black;
    char numBadge[16];
    snprintf(numBadge, sizeof(numBadge), "%02d", idx + 1);
    drawPill(t, static_cast<int16_t>(rr.x + 12), static_cast<int16_t>(ry + (rowH - 26) / 2), 26, numBadge, kFontMicro,
             !active, 8);
    const int16_t indent = static_cast<int16_t>(depth > 3 ? 3 * 16 : depth * 16);
    const int16_t tx = static_cast<int16_t>(rr.x + 64 + indent);
    const int16_t curW = isCurrent ? 104 : 0;
    const int16_t tw = static_cast<int16_t>(rr.right() - tx - 14 - curW);
    drawText(t, Rect{tx, static_cast<int16_t>(ry + (rowH - 30) / 2), tw, 30},
             rawTitle[0] ? rawTitle : "(Capitolo senza titolo)", kFontBody, fg);
    if (isCurrent) {
      drawPill(t, static_cast<int16_t>(rr.right() - 12 - 90), static_cast<int16_t>(ry + (rowH - 24) / 2), 24,
               "QUI", kFontMicro, !active, 10);
    }
  }
  self.cursorOnlyRedraw_ = false;
}

// ---------------------------------------------------------------------------
// Screen 6: SETTINGS
// ---------------------------------------------------------------------------
void InklingApp::settingsScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  const int16_t w = screenW();
  const int sel = self.settingsMenu_.selected;

  const int cols = isLandscape() ? 2 : 1;
  const int16_t listTop = static_cast<int16_t>(contentTop() + 26);
  const int16_t listBottom = contentBottom();
  const int16_t gapY = 8;
  constexpr int16_t kCardMinH = 116;
  int rowsFit = (listBottom - listTop + gapY) / (kCardMinH + gapY);
  if (rowsFit < 1) rowsFit = 1;
  const int perPage = rowsFit * cols;
  const int16_t cardH = static_cast<int16_t>((listBottom - listTop - (rowsFit - 1) * gapY) / rowsFit);
  const int16_t cardW = static_cast<int16_t>((w - 2 * kMargin - (cols - 1) * kGap) / cols);

  if (self.cursorOnlyRedraw_) {
    clearRect(Rect{0, static_cast<int16_t>(contentTop()), w, static_cast<int16_t>(listBottom - contentTop() + 4)});
  } else {
    clearScreen();
    drawTopHud(t, self.status_, "IMPOSTAZIONI");
    drawBottomKeyBar(t, "SX/DX", "Cambia", "OK", "Apri", "BACK", "Salva ed esci");
  }

  char countTxt[32];
  snprintf(countTxt, sizeof(countTxt), "VOCE %d/%d", sel + 1, static_cast<int>(kSetCount));
  drawText(t, Rect{18, static_cast<int16_t>(contentTop() + 2), 200, 22}, countTxt, kFontMicro, Color::Black);
  drawPageDots(t, static_cast<int16_t>(w - 18), static_cast<int16_t>(contentTop() + 8), sel, kSetCount, perPage);

  static const char* const kTitles[kSetCount] = {
      "ROTAZIONE SCHERMO", "TASTI E ROTAZIONE", "SPEGNIMENTO AUTOMATICO", "PULIZIA SCHERMO",
      "WIDGET OROLOGIO",   "TEMA",              "WI-FI E AGGIORNAMENTI",  "DIAGNOSTICA",
  };
  static const char* const kSubtitles[kSetCount] = {
      "Automatica col sensore o bloccata su un lato",
      "Le frecce indicano la direzione sullo schermo",
      "Dopo quanto tempo senza tasti va a riposo",
      "Ogni quante pagine un refresh completo",
      "Riquadro in basso nella schermata Orologio",
      "Carta chiara o testo bianco su nero",
      "Portale web, giornale Daily Ink, firmware",
      "Sensori, batteria, memoria e slot OTA",
  };
  static const Icon kIcons[kSetCount] = {
      Icon::Compass, Icon::Sliders, Icon::Moon, Icon::Spark, Icon::Compass, Icon::Sliders, Icon::Wifi, Icon::Sliders,
  };
  char v0[48], v1[48], v2[48], v3[48], v4[48], v5[48];
  snprintf(v0, sizeof(v0), "<  %s  >", self.settings_.rotationModeLabel());
  snprintf(v1, sizeof(v1), "<  %s  >", self.settings_.invertKeysLabel());
  snprintf(v2, sizeof(v2), "<  %s  >", self.settings_.autoSleepLabel());
  snprintf(v3, sizeof(v3), "<  %s  >", self.settings_.refreshCadenceLabel());
  snprintf(v4, sizeof(v4), "<  %s  >", self.settings_.deskWidgetLabel());
  snprintf(v5, sizeof(v5), "<  %s  >", self.settings_.nightModeLabel());
  const char* const values[kSetCount] = {v0, v1, v2, v3, v4, v5, "OK: avvia il Wi-Fi", "OK: apri"};

  const int first = pageFirst(sel, kSetCount, perPage);
  for (int slot = 0; slot < perPage; ++slot) {
    const int idx = first + slot;
    if (idx >= kSetCount) break;
    const bool isSel = (idx == sel);
    const int16_t cx = static_cast<int16_t>(kMargin + (slot % cols) * (cardW + kGap));
    const int16_t cy = static_cast<int16_t>(listTop + (slot / cols) * (cardH + gapY));
    const Rect cr{cx, cy, cardW, cardH};
    drawCard(t, cr, 8, isSel ? 4 : 2, false, isSel);
    if (isSel) fastFill(t, Rect{cx, cy, 8, cardH}, Paint::solid(Color::Black), 4);

    drawIcon16(t, static_cast<int16_t>(cx + 18), static_cast<int16_t>(cy + 16), kIcons[idx], Color::Black);
    drawText(t, Rect{static_cast<int16_t>(cx + 42), static_cast<int16_t>(cy + 10), static_cast<int16_t>(cardW - 56), 26},
             kTitles[idx], kFontBody, Color::Black);
    drawText(t, Rect{static_cast<int16_t>(cx + 18), static_cast<int16_t>(cy + 40), static_cast<int16_t>(cardW - 32), 20},
             kSubtitles[idx], kFontMicro, Color::Black);

    const Rect valR{static_cast<int16_t>(cx + 14), static_cast<int16_t>(cy + cardH - 56),
                    static_cast<int16_t>(cardW - 28), 44};
    fastFill(t, valR, Paint::solid(isSel ? Color::Black : Color::White), 6);
    fastStroke(t, valR, Paint::solid(Color::Black), isSel ? 3 : 1, 6);
    const FontSlot vf = fitFont(t, values[idx], static_cast<int16_t>(valR.width - 16), kFontTitle);
    drawText(t, valR, values[idx], vf, isSel ? Color::White : Color::Black, TextAlign::Center, false);
  }
  self.cursorOnlyRedraw_ = false;
}

void InklingApp::deskAmbientScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  DeskInfo info;
  info.streakDays = self.streakDays_;
  info.todayPages = self.dailyPages_;
  info.goalPages = kDailyPageGoal;
  info.totalPages = self.totalPagesRead_;
  uint8_t coverBits[kCoverBytes];
  const int idx = self.activeBookIndex();
  if (idx >= 0 && idx < self.library_.count()) {
    const auto& b = self.library_.at(idx);
    info.bookTitle = b.name;
    info.bookAuthor = b.author;
    info.bookPct = b.progressPct;
    if (self.loadBookThumb(b.path, coverBits)) {
      info.bookCoverBits = coverBits;
    }
  }
  self.desk_.render(t, self.status_, info);
}

void InklingApp::oracleScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  self.oracle_.render(t, self.status_);
}

void InklingApp::textRpgScreen(UiApp::ScreenType& screen, void* user) {
  auto& self = *static_cast<InklingApp*>(user);
  auto& t = screen.target();
  self.rpg_.render(t, self.status_);
}

}  // namespace inkling
