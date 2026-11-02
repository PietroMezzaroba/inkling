#pragma once

#include <cstdint>

#include "inkling/DeviceStatus.h"
#include "inkling/Platform.h"
#include "inkling/Screens.h"
#include "inkling/reader/BookReader.h"
#include "inkling/reader/Library.h"

namespace inkling {

// Physical keys, already debounced by the platform. Power is handled by the
// firmware (power-off), never by the app.
enum class Button : uint8_t { Back, Confirm, Left, Right, Up, Down };

// The whole Inkling UI as a state machine over semantic buttons. Platform code
// owns the hardware, feeds buttons in, and pushes frames out through `present`.
class InklingApp {
 public:
  enum class Mode : uint8_t { Library, Reader, ReaderMenu, Toc, Diagnostic, Message };

  // Pushes the framebuffer to the panel (no-op or PNG dump in the simulator).
  using PresentFn = void (*)(freeink::ui::RefreshHint hint, void* user);

  // `reader` may be null (out of memory): the library and diagnostics still
  // work, opening a book explains the problem.
  InklingApp(Storage& storage, BookReader* reader, DeviceStatus& status, UiApp& ui,
             const freeink::book::FrameTarget& frame);

  void setPresenter(PresentFn fn, void* user) {
    present_ = fn;
    presentUser_ = user;
  }

  // Things only the platform can do (restart into another boot mode).
  enum class Request : uint8_t { WifiUpdate };
  using RequestFn = void (*)(Request request, void* user);
  void setRequestHandler(RequestFn fn, void* user) {
    request_ = fn;
    requestUser_ = user;
  }

  // Startup: scans the card, then reopens the last book (unless reopening it
  // crashed last time) or shows the library.
  void begin();
  void showLibrary();
  void onButton(Button button);
  // Renders and presents if anything changed since the last frame.
  void update();
  // Saves the reading position; call before the power is cut.
  void shutdown();

  Mode mode() const { return mode_; }
  void showDiagnostic();
  // Redraws the diagnostic screen if it is showing (new sensor readings).
  void refreshDiagnostic() {
    if (mode_ == Mode::Diagnostic) ui_.invalidate(freeink::ui::RefreshHint::Fast);
  }
  void showMessage(const char* text);

  // Screen functions read the app through the `user` pointer.
  static void libraryScreen(UiApp::ScreenType& screen, void* user);
  static void readerScreen(UiApp::ScreenType& screen, void* user);
  static void readerMenuScreen(UiApp::ScreenType& screen, void* user);
  static void tocScreen(UiApp::ScreenType& screen, void* user);

 private:
  // A vertical menu with wrap-around selection (library, reader menu, TOC).
  struct Menu {
    int selected = 0;
    int count = 0;
    freeink::ui::ListNav nav;
    void reset(int n, int select = 0);
    void move(int delta);
  };

  void openSelected();
  bool openBook(const char* path, bool resuming);
  void closeBook();
  void flush(freeink::ui::RefreshHint hint);
  void turnPage(bool forward);
  void showReader(freeink::ui::RefreshHint hint);
  void showReaderMenu();
  void showToc();
  void runMenuItem(int item);
  void changeFontSize(int delta);
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

  Mode mode_ = Mode::Library;
  Library library_;
  Menu libraryMenu_, readerMenu_, tocMenu_;
  char message_[96] = {};
  freeink::book::BookStatus lastRenderStatus_ = freeink::book::BookStatus::Ok;
  // E-paper ghosting: every Nth page turn is a full refresh.
  uint8_t turnsSinceFull_ = 0;
  uint8_t fontStep_ = 2;        // index into the font size ladder (see App.cpp)
  bool resumePending_ = false;  // a resumed book has not drawn its first page yet
};

}  // namespace inkling
