#pragma once

#include <cstdint>

#include "inkling/DeviceStatus.h"
#include "inkling/Platform.h"
#include "inkling/Screens.h"
#include "inkling/desk/DeskDashboard.h"
#include "inkling/game/BreachGame.h"
#include "inkling/oracle/OracleDeck.h"
#include "inkling/pet/PetEngine.h"
#include "inkling/reader/BookReader.h"
#include "inkling/reader/Library.h"
#include "inkling/rpg/TextRpgEngine.h"
#include "inkling/settings/InklingSettings.h"

namespace inkling {

// Physical keys, already debounced by the platform. Power is handled by the
// firmware (power-off), never by the app.
enum class Button : uint8_t { Back, Confirm, Left, Right, Up, Down };

// The whole Inkling OS UI as a state machine over semantic buttons.
class InklingApp {
 public:
  enum class Mode : uint8_t {
    Home,
    Library,
    DeskAmbient,
    Oracle,
    TextRpg,
    PetSanctuary,
    BreachGame,
    Settings,
    Reader,
    ReaderMenu,
    Toc,
    Diagnostic,
    Message,
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

  // Startup: loads prefs & Pet state, scans the library, then either resumes
  // the active book (if powered off while reading) or opens the Home Command Center.
  void begin();
  void showHome();
  void showLibrary();
  void showDeskAmbient();
  void showOracle();
  void showTextRpg();
  void showPetSanctuary();
  void showBreachGame();
  void showSettings();
  void onButton(Button button);
  // Feeds live IMU readings so a physical shake/tilt interacts with the Pet!
  void onImuSample(float ax, float ay, float az);

  void update();
  void shutdown();

  Mode mode() const { return mode_; }
  void showDiagnostic();
  void refreshDiagnostic() {
    if (mode_ == Mode::Diagnostic || mode_ == Mode::Home || mode_ == Mode::PetSanctuary || mode_ == Mode::Settings) {
      ui_.invalidate(freeink::ui::RefreshHint::Fast);
    }
  }
  void showMessage(const char* text);

  const PetEngine& pet() const { return pet_; }
  PetEngine& pet() { return pet_; }
  const DeskDashboard& desk() const { return desk_; }
  DeskDashboard& desk() { return desk_; }
  const OracleDeck& oracle() const { return oracle_; }
  OracleDeck& oracle() { return oracle_; }
  const TextRpgEngine& rpg() const { return rpg_; }
  TextRpgEngine& rpg() { return rpg_; }
  const Library& library() const { return library_; }
  const DeviceStatus& status() const { return status_; }
  const BookReader* reader() const { return reader_; }
  const InklingSettings& settings() const { return settings_; }
  InklingSettings& settings() { return settings_; }
  int activeBookIndex() const;
  ReaderLayout currentLayout() const;

  using OrientationFn = void (*)(freeink::ui::Orientation orient, void* user);
  void setOrientationHandler(OrientationFn fn, void* user) {
    orientationFn_ = fn;
    orientationUser_ = user;
  }
  void requestOrientation(freeink::ui::Orientation orient) {
    if (orientationFn_) orientationFn_(orient, orientationUser_);
  }
  void onOrientationChanged(freeink::ui::Orientation orient) {
    orientation_ = orient;
    frame_.rotation = (orient == freeink::ui::Orientation::Portrait ? freeink::book::FrameRotation::Portrait
                                                                    : freeink::book::FrameRotation::PortraitInverted);
    ui_.invalidate(freeink::ui::RefreshHint::Fast);
  }
  freeink::ui::Orientation orientation() const { return orientation_; }

  // Screen functions read the app through the `user` pointer.
  static void homeScreen(UiApp::ScreenType& screen, void* user);
  static void libraryScreen(UiApp::ScreenType& screen, void* user);
  static void deskAmbientScreen(UiApp::ScreenType& screen, void* user);
  static void oracleScreen(UiApp::ScreenType& screen, void* user);
  static void textRpgScreen(UiApp::ScreenType& screen, void* user);
  static void petSanctuaryScreen(UiApp::ScreenType& screen, void* user);
  static void breachGameScreen(UiApp::ScreenType& screen, void* user);
  static void settingsScreen(UiApp::ScreenType& screen, void* user);
  static void readerScreen(UiApp::ScreenType& screen, void* user);
  static void readerMenuScreen(UiApp::ScreenType& screen, void* user);
  static void tocScreen(UiApp::ScreenType& screen, void* user);

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
  void flush(freeink::ui::RefreshHint hint);
  void turnPage(bool forward);
  void showReader(freeink::ui::RefreshHint hint);
  void showReaderMenu();
  void exitReaderMenu();
  void showToc();
  void adjustMenuItem(int item, int delta);
  void runMenuItem(int item);
  void runHomeDockItem(int item);
  void runSanctuaryAction(int item);
  void adjustSettingItem(int item, int delta);
  void runSettingItem(int item);
  void loadPrefs();
  void savePrefs();
  void setResumeLock(bool locked);

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
  InklingSettings settings_;
  Library library_;
  PetEngine pet_;
  BreachGame breach_;
  DeskDashboard desk_;
  OracleDeck oracle_;
  TextRpgEngine rpg_;
  Menu homeDock_, libraryMenu_, sanctuaryMenu_, readerMenu_, tocMenu_, settingsMenu_;
  char lastBookPath_[128] = {};
  char message_[96] = {};
  freeink::book::BookStatus lastRenderStatus_ = freeink::book::BookStatus::Ok;
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
  bool needsReaderBackdrop_ = false;
  float prevAx_ = 0.0f, prevAy_ = 0.0f, prevAz_ = 1.0f;
  uint8_t imuCooldown_ = 0;
};

}  // namespace inkling
