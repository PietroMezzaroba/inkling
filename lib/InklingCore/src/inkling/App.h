#pragma once

#include <cstdint>

#include "inkling/DeviceStatus.h"
#include "inkling/Orientation.h"
#include "inkling/Platform.h"
#include "inkling/Screens.h"
#include "inkling/desk/DeskDashboard.h"
#include "inkling/dict/Dictionary.h"
#include "inkling/oracle/OracleDeck.h"
#include "inkling/reader/BookReader.h"
#include "inkling/reader/Library.h"
#include "inkling/rpg/TextRpgEngine.h"
#include "inkling/settings/InklingSettings.h"
#include "inkling/stats/ReadingStats.h"
#include "inkling/study/Flashcards.h"
#include "inkling/ui/CyberCanvas.h"

namespace inkling {

// Physical keys, already debounced by the platform. Power is handled by the
// firmware (power-off), never by the app.
enum class Button : uint8_t { Back, Confirm, Left, Right, Up, Down };

// Maps a key, named as printed for the 0° orientation, to the direction it
// points to on screen in orientation `o` (so "the upper key" always means up).
Button spatialButton(Button b, Orientation o);

// The whole Inkling OS UI as a state machine over semantic buttons.
class InklingApp {
 public:
  enum class Mode : uint8_t {
    Home,
    Library,
    DeskAmbient,
    Oracle,
    TextRpg,
    Settings,
    Reader,
    ReaderMenu,
    Toc,
    Diagnostic,
    Message,
    Marks,       // bookmarks and highlights of the open book
    Select,      // word cursor on the page: clip a passage / look a word up
    Links,       // link cursor on the page: footnotes, cross-references
    Definition,  // a dictionary entry
    Flashcards,
    Stats,
  };

  using PresentFn = void (*)(freeink::ui::RefreshHint hint, void* user);

  InklingApp(Storage& storage, BookReader* reader, DeviceStatus& status, UiApp& ui,
             const freeink::book::FrameTarget& frame);

  void setPresenter(PresentFn fn, void* user) {
    present_ = fn;
    presentUser_ = user;
  }

  enum class Request : uint8_t { WifiUpdate };
  using RequestFn = void (*)(Request request, void* user);
  void setRequestHandler(RequestFn fn, void* user) {
    request_ = fn;
    requestUser_ = user;
  }

  // Startup: loads prefs, scans the library, pre-caches EPUB covers, then either
  // resumes the active book (if powered off while reading) or opens Home.
  void begin();
  void showHome();
  void showLibrary();
  void showDeskAmbient();
  void showOracle();
  void showTextRpg();
  // fromReader: opened from the reader menu, so Back returns there.
  void showSettings(bool fromReader = false);
  void showFlashcards();
  void showStats();
  void onButton(Button button);
  void onImuSample(float ax, float ay, float az);

  void update();
  void shutdown();

  // Work that waits for a pause: saving the reading position and the reading
  // statistics, and paginating the next chapter. The platform calls idleTick()
  // when the panel is idle and no key is down; each call is one short slice
  // and returns true while more remains. `idleMs` = time since the last key.
  bool hasIdleWork(uint32_t idleMs);
  bool idleTick(uint32_t idleMs);

  Mode mode() const { return mode_; }
  void showDiagnostic();
  void refreshDiagnostic() {
    if (mode_ == Mode::Diagnostic || mode_ == Mode::Home || mode_ == Mode::Settings) {
      ui_.invalidate(freeink::ui::RefreshHint::Fast);
    }
  }
  void showMessage(const char* text);

  const DeskDashboard& desk() const { return desk_; }
  DeskDashboard& desk() { return desk_; }
  const OracleDeck& oracle() const { return oracle_; }
  const TextRpgEngine& rpg() const { return rpg_; }
  const Library& library() const { return library_; }
  const DeviceStatus& status() const { return status_; }
  const ReadingStats& stats() const { return stats_; }
  // The next picture of /sleep (or a sleep*.bmp in the card root) for the
  // lock screen, advancing the rotation. False when there is none.
  bool nextSleepImage(char* path, uint32_t cap);
  BookReader* reader() const { return reader_; }
  const InklingSettings& settings() const { return settings_; }
  InklingSettings& settings() { return settings_; }
  int activeBookIndex() const;
  ReaderLayout currentLayout() const;
  bool loadBookThumb(const char* bookPath, uint8_t outBits[ui::canvas::kCoverBytes]) const;

  // Orientation plumbing. The app keeps two values: the DESIRED orientation
  // (rotation lock or accelerometer) and the EFFECTIVE one the panel uses.
  // They differ only when the desired one is landscape and the current screen
  // is portrait-only (desk stand, oracle, RPG): those stay at 0° until the user
  // goes back to a screen that has a landscape layout.
  //
  // The handler must rotate the platform's DisplayTarget and then call
  // onOrientationChanged(); without a handler the app stays at 0°.
  using OrientationFn = void (*)(freeink::ui::Orientation orient, void* user);
  void setOrientationHandler(OrientationFn fn, void* user) {
    orientationFn_ = fn;
    orientationUser_ = user;
  }
  void setDesiredOrientation(freeink::ui::Orientation orient);
  freeink::ui::Orientation desiredOrientation() const { return desiredOrientation_; }
  void onOrientationChanged(freeink::ui::Orientation orient);
  static bool modeSupportsLandscape(Mode mode);
  freeink::ui::Orientation orientation() const { return orientation_; }

  // Screen functions read the app through the `user` pointer.
  static void homeScreen(UiApp::ScreenType& screen, void* user);
  static void libraryScreen(UiApp::ScreenType& screen, void* user);
  static void deskAmbientScreen(UiApp::ScreenType& screen, void* user);
  static void oracleScreen(UiApp::ScreenType& screen, void* user);
  static void textRpgScreen(UiApp::ScreenType& screen, void* user);
  static void settingsScreen(UiApp::ScreenType& screen, void* user);
  static void readerScreen(UiApp::ScreenType& screen, void* user);
  static void readerMenuScreen(UiApp::ScreenType& screen, void* user);
  static void tocScreen(UiApp::ScreenType& screen, void* user);
  static void marksScreen(UiApp::ScreenType& screen, void* user);
  static void selectScreen(UiApp::ScreenType& screen, void* user);
  static void linksScreen(UiApp::ScreenType& screen, void* user);
  static void definitionScreen(UiApp::ScreenType& screen, void* user);
  static void flashcardsScreen(UiApp::ScreenType& screen, void* user);
  static void statsScreen(UiApp::ScreenType& screen, void* user);

 private:
  struct Menu {
    int selected = 0;
    int count = 0;
    freeink::ui::ListNav nav;
    void reset(int n, int select = 0);
    void move(int delta);
  };

  LocalTime currentLocalTime() const;
  void openSelected();
  void openActiveOrFirstBook();
  bool openBook(const char* path, bool resuming);
  void closeBook();
  void applyPendingLayout();
  void flush(freeink::ui::RefreshHint hint);
  void turnPage(bool forward);
  void showReader(freeink::ui::RefreshHint hint);
  void showReaderMenu();
  void exitReaderMenu();
  void showToc();
  void adjustMenuItem(int item, int delta);
  void runMenuItem(int item);
  void runHomeDockItem(int item);
  void adjustSettingItem(int item, int delta);
  void runSettingItem(int item);
  void loadPrefs();
  void savePrefs();
  void saveStats();
  void recordPageRead();
  void setResumeLock(bool locked);
  void enterMode(Mode mode, UiApp::ScreenFn fn, void* user, freeink::ui::RefreshHint hint);
  void applyOrientation();
  int homeDockColumns() const;

  // Reader tools (AppTools.cpp).
  enum class SelectPurpose : uint8_t { Clip, Lookup };
  void drawReaderPage(freeink::ui::DrawTarget& t);
  void drawReaderBar(freeink::ui::DrawTarget& t, const char* hint);
  void toggleBookmark();
  void showMarks();
  void enterSelect(SelectPurpose purpose);
  void leaveSelect();
  void moveSelection(Button button);
  void confirmSelection();
  void saveClipping(uint16_t first, uint16_t last);
  void lookUpWord(uint16_t word);
  void enterLinks();
  void followSelectedLink();
  void onToolButton(Button button);
  int32_t today() const;

  Storage& storage_;
  BookReader* reader_;
  DeviceStatus& status_;
  UiApp& ui_;
  freeink::book::FrameTarget frame_;
  PresentFn present_ = nullptr;
  void* presentUser_ = nullptr;
  RequestFn request_ = nullptr;
  void* requestUser_ = nullptr;
  OrientationFn orientationFn_ = nullptr;
  void* orientationUser_ = nullptr;

  Mode mode_ = Mode::Home;
  freeink::ui::Orientation orientation_ = freeink::ui::Orientation::Portrait;
  freeink::ui::Orientation desiredOrientation_ = freeink::ui::Orientation::Portrait;
  InklingSettings settings_;
  Library library_;
  DeskDashboard desk_;
  OracleDeck oracle_;
  TextRpgEngine rpg_;
  Menu homeDock_, libraryMenu_, readerMenu_, tocMenu_, settingsMenu_, marksMenu_, deckMenu_;
  BookMarks marks_;
  ReadingStats stats_;
  Dictionary dictionary_;
  Flashcards cards_;
  char lastBookPath_[128] = {};
  char message_[96] = {};
  freeink::book::BookStatus lastRenderStatus_ = freeink::book::BookStatus::Ok;
  uint32_t lastTurnMs_ = 0;  // when the page on screen was turned to (0 = unknown)

  // Word cursor (Mode::Select). The boxes are allocated while it is in use.
  BookReader::PageWord* selWords_ = nullptr;
  uint16_t selCount_ = 0;
  uint16_t selCursor_ = 0;
  uint16_t selAnchor_ = 0xFFFF;  // first word of a passage being clipped
  uint16_t selDrawnLo_ = 0, selDrawnHi_ = 0;
  bool selDrawn_ = false;
  SelectPurpose selPurpose_ = SelectPurpose::Clip;

  // Link cursor (Mode::Links) and the way back from a followed link.
  BookReader::PageLinkBox linkBoxes_[24];
  uint16_t linkBoxCount_ = 0;
  uint8_t linkCount_ = 0;
  uint8_t linkSel_ = 0;
  bool linkDrawn_ = false;
  bool hasLinkReturn_ = false;
  uint8_t turnsSinceLink_ = 0;
  ReadingPosition linkReturn_;

  // Dictionary entry on screen (Mode::Definition); the text is allocated.
  char* definition_ = nullptr;
  char definitionHead_[64] = {};
  Dictionary::Result definitionResult_ = Dictionary::Result::NotFound;
  int definitionLine_ = 0;
  int definitionLines_ = 0;
  int definitionFit_ = 1;

  const char* readerNotice_ = nullptr;  // shown once in the reader's status bar
  float prevAx_ = 0.0f;
  float prevAy_ = 0.0f;
  float prevAz_ = 1.0f;
  uint8_t imuCooldown_ = 0;
  uint8_t turnsSinceFull_ = 0;
  uint8_t fontStep_ = 3;      // Default 42 px (CrossPet 14-16pt equivalent)
  uint8_t fontFamily_ = 0;    // Default 0 = Noto Serif
  uint8_t spacingStep_ = 0;   // Default 0 = Classico (Int. 102%, Marg. Medi)
  uint8_t alignStep_ = 0;     // Default 0 = Giustificato + Sillabazione IT/EN
  bool focusReading_ = false;
  bool nightMode_ = false;
  bool layoutDirty_ = false;
  bool resumePending_ = false;
  bool cursorOnlyRedraw_ = false;
  // A screen was just entered (or rotated) and has not been drawn whole yet:
  // keys that arrive before that frame must not turn it into a partial one.
  bool fullDrawPending_ = false;
  bool needsReaderBackdrop_ = false;
  bool progressDirty_ = false;  // page shown but position not yet on the card
  bool statsDirty_ = false;
  uint8_t pagesSinceStatsSave_ = 0;
  bool settingsFromReader_ = false;
  bool diagnosticFromSettings_ = false;
};

}  // namespace inkling
