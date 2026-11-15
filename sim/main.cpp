// Inkling host simulator: runs the same InklingCore code as the firmware on an
// X3-sized 1-bit framebuffer and saves frames as PNG.
//
//   inkling-sim <card-dir> <out-dir>     scripted tour, one PNG per frame
//   inkling-sim --check <card-dir>       open and page through every book,
//                                        report status and arena peaks only

#include <FreeInkUIDisplayTarget.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "HostStorage.h"
#include "PngWriter.h"
#include "inkling/App.h"
#include "inkling/Screens.h"
#include "inkling/UiGlyphFallback.h"
#include "inkling/ui/InklingFonts.h"

#ifndef INKLING_FONT_DIR
#define INKLING_FONT_DIR "assets/fonts"
#endif

namespace {

// Native X3 panel: 792x528, 1 bpp, MSB first, 0 = ink.
constexpr int16_t kPanelW = 792;
constexpr int16_t kPanelH = 528;
constexpr int16_t kPanelWBytes = kPanelW / 8;

struct Canvas {
  std::vector<uint8_t> fb = std::vector<uint8_t>(static_cast<size_t>(kPanelWBytes) * kPanelH, 0xFF);
  freeink::ui::DisplayTarget target{fb.data(), kPanelW, kPanelH, kPanelWBytes};

  // Saves the LOGICAL screen (what the user sees, upright) for any orientation.
  bool save(const std::string& path) const {
    const int outW = target.logicalWidth();
    const int outH = target.logicalHeight();
    std::vector<uint8_t> gray(static_cast<size_t>(outW) * outH, 0xFF);
    for (int y = 0; y < outH; ++y) {
      for (int x = 0; x < outW; ++x) {
        int px = x, py = y;
        switch (target.orientation()) {
          case freeink::ui::Orientation::Portrait:
            px = y;
            py = kPanelH - 1 - x;
            break;
          case freeink::ui::Orientation::PortraitInverted:
            px = kPanelW - 1 - y;
            py = x;
            break;
          case freeink::ui::Orientation::LandscapeClockwise:
            px = kPanelW - 1 - x;
            py = kPanelH - 1 - y;
            break;
          case freeink::ui::Orientation::LandscapeCounterClockwise:
            break;
        }
        const uint8_t byte = fb[static_cast<size_t>(py) * kPanelWBytes + (px >> 3)];
        const bool ink = (byte & (0x80 >> (px & 7))) == 0;
        gray[static_cast<size_t>(y) * outW + x] = ink ? 0x10 : 0xF4;
      }
    }
    return sim::writeGrayPng(path.c_str(), outW, outH, gray);
  }
};

std::vector<uint8_t> loadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

// The firmware's embedded fonts by default; INKLING_FONT_DIR in the environment
// points at another set (the unsubsetted originals, to compare renderings).
std::string fontDir() {
  const char* env = std::getenv("INKLING_FONT_DIR");
  return env && env[0] ? env : INKLING_FONT_DIR;
}

struct Fonts {
  std::vector<uint8_t> notoSerifReg = loadFile(fontDir() + "/NotoSerif-Regular.ttf");
  std::vector<uint8_t> notoSerifIt = loadFile(fontDir() + "/NotoSerif-Italic.ttf");
  std::vector<uint8_t> notoSansReg = loadFile(fontDir() + "/NotoSans-Regular.ttf");
  std::vector<uint8_t> notoSansIt = loadFile(fontDir() + "/NotoSans-Italic.ttf");
  std::vector<uint8_t> alegreyaReg = loadFile(fontDir() + "/Alegreya-Regular.ttf");
  std::vector<uint8_t> alegreyaIt = loadFile(fontDir() + "/Alegreya-Italic.ttf");
  std::vector<uint8_t> dyslexicReg = loadFile(fontDir() + "/OpenDyslexic-Regular.otf");
  std::vector<uint8_t> dyslexicIt = loadFile(fontDir() + "/OpenDyslexic-Italic.otf");

  void registerAll(inkling::BookReader& reader, uint8_t* regCache, size_t regCap, uint8_t* itCache, size_t itCap) {
    reader.initFonts(notoSerifReg.data(), static_cast<uint32_t>(notoSerifReg.size()), notoSerifIt.data(),
                     static_cast<uint32_t>(notoSerifIt.size()), regCache, regCap, itCache, itCap);
    reader.registerFontFamily(1, notoSansReg.data(), static_cast<uint32_t>(notoSansReg.size()), notoSansIt.data(),
                              static_cast<uint32_t>(notoSansIt.size()));
    reader.registerFontFamily(2, alegreyaReg.data(), static_cast<uint32_t>(alegreyaReg.size()), alegreyaIt.data(),
                              static_cast<uint32_t>(alegreyaIt.size()));
    reader.registerFontFamily(3, dyslexicReg.data(), static_cast<uint32_t>(dyslexicReg.size()), dyslexicIt.data(),
                              static_cast<uint32_t>(dyslexicIt.size()));
  }
};

freeink::book::FrameTarget frameFor(Canvas& canvas) {
  freeink::book::FrameTarget frame{canvas.fb.data(), kPanelW, kPanelH, kPanelWBytes};
  frame.format = freeink::book::FrameFormat::Mono1Dithered;
  frame.rotation = freeink::book::FrameRotation::Portrait;
  return frame;
}

struct Recorder {
  Canvas* canvas;
  std::string outDir;
  std::string label;
  int frame = 0;

  static void present(freeink::ui::RefreshHint, void* user) {
    auto& self = *static_cast<Recorder*>(user);
    char name[160];
    snprintf(name, sizeof(name), "%s/%03d-%s.png", self.outDir.c_str(), self.frame++, self.label.c_str());
    if (self.canvas->save(name)) std::printf("%s\n", name);
  }
};

inkling::ReaderMemory readerMemory;
alignas(16) uint8_t readerIndex[inkling::IndexSizes::kIdeal];
alignas(16) uint8_t glyphsRegular[inkling::GlyphCacheSizes::kRegularIdeal];
alignas(16) uint8_t glyphsItalic[inkling::GlyphCacheSizes::kItalic];

void populateSimStatus(inkling::DeviceStatus& status) {
  status.deviceName = "Xteink X3";
  status.panelName = "UC8253";
  status.clockValid = true;
  status.year = 2026;
  status.month = 11;
  status.day = 2;
  status.hour = 18;
  status.minute = 25;
  status.batteryPercent = 87;
  status.charging = false;
  status.sdReady = true;
  status.imuReady = true;
  status.ax = 0.08f;
  status.ay = -0.94f;
  status.az = 0.22f;
  status.lastButton = "Confirm";
  status.buttonPresses = 14;
  status.freeHeap = 90112;
  status.largestBlock = 36864;
  status.otherFirmware = "inkling-updater (app1)";
}

// Rotates the simulated panel exactly like the firmware's setAppOrientation().
struct SimRotator {
  Canvas* canvas;
  inkling::UiApp* ui;
  inkling::InklingApp* app;
  static void rotate(freeink::ui::Orientation o, void* user) {
    auto& self = *static_cast<SimRotator*>(user);
    self.canvas->target.setOrientation(o);
    self.ui->setDevice(self.canvas->target.deviceContext());
    self.app->onOrientationChanged(o);
  }
};

int runTour(const std::string& cardDir, const std::string& outDir) {
  Fonts fonts;
  if (fonts.notoSerifReg.empty()) {
    std::fprintf(stderr, "fonts not found in %s\n", INKLING_FONT_DIR);
    return 1;
  }
  sim::HostStorage storage(cardDir);
  // Start on Home with default settings: no resume pointer, prefs or settings.
  storage.writeFile("/.inkling/last.txt", "", 0);
  storage.writeFile("/.inkling/prefs.bin", "", 0);
  storage.writeFile("/.inkling/settings.bin", "", 0);

  inkling::BookReader reader(storage, readerMemory);
  reader.setIndexBuffer(readerIndex, sizeof(readerIndex));
  fonts.registerAll(reader, glyphsRegular, sizeof(glyphsRegular), glyphsItalic, sizeof(glyphsItalic));

  Canvas canvas;
  inkling::ui::bindInklingFonts(canvas.target);
  reader.setParseBuffer(canvas.fb.data(), canvas.fb.size());
  static inkling::UiGlyphFallback uiFallback;
  uiFallback.setFont(&reader.regularFont());
  canvas.target.setGlyphFallback(&uiFallback);

  inkling::DeviceStatus status;
  populateSimStatus(status);

  inkling::UiApp ui(canvas.target, canvas.target.deviceContext());
  inkling::InklingApp app(storage, &reader, status, ui, frameFor(canvas));
  Recorder rec{&canvas, outDir, "", 0};
  app.setPresenter(Recorder::present, &rec);
  SimRotator rotator{&canvas, &ui, &app};
  app.setOrientationHandler(SimRotator::rotate, &rotator);

  using B = inkling::Button;
  using O = freeink::ui::Orientation;
  std::string prefix;
  auto step = [&](const char* label, B button) {
    rec.label = prefix + label;
    app.onButton(button);
    app.update();
  };
  auto shot = [&](const char* label) {
    rec.label = prefix + label;
    app.update();
  };
  auto press = [&](B button, int times) {
    for (int i = 0; i < times; ++i) app.onButton(button);
  };

  rec.label = "p0-home";
  app.begin();
  app.update();

  // Every screen with a landscape layout, in one orientation.
  auto menusTour = [&](const char* tag) {
    prefix = tag;
    app.showHome();
    shot("home");
    step("home-right", B::Right);
    step("home-down", B::Down);
    app.showLibrary();
    shot("library");
    step("library-down", B::Down);
    app.showLibrary();
    app.update();
    step("reader", B::Confirm);
    step("reader-next", B::Down);
    step("menu", B::Confirm);
    step("menu-down", B::Down);
    step("toc", B::Confirm);
    step("toc-down", B::Down);
    step("toc-jump", B::Confirm);
    step("menu2", B::Confirm);
    press(B::Down, 6);
    step("menu-fontsize", B::Right);
    step("apply", B::Back);
    step("menu3", B::Confirm);
    press(B::Down, 11);
    step("menu-night", B::Right);
    step("night-page", B::Back);
    app.onButton(B::Confirm);
    press(B::Down, 11);
    app.onButton(B::Right);
    app.onButton(B::Back);
    app.showSettings();
    shot("settings");
    press(B::Down, 5);
    step("settings-scrolled", B::Down);
    app.showDiagnostic();
    shot("diagnostic");
    app.showMessage("Sincronizzazione volume in corso...");
    shot("message");
    rec.label = prefix + "lock";
    ui.setScreen(inkling::sleepScreen, &app, freeink::ui::RefreshHint::Full);
    app.update();
    app.showHome();
    app.update();
  };

  menusTour("p0-");

  // Portrait-only screens (always at 0°).
  prefix = "p0-";
  app.showDeskAmbient();
  shot("desk");
  step("desk-widget", B::Right);

  // Capture the cover page (spine 0, page 0) of each book in portrait.
  for (int i = 0; i < app.library().count(); ++i) {
    app.showLibrary();
    app.update();
    if (reader.open(app.library().at(i).path, app.currentLayout()) == freeink::book::BookStatus::Ok) {
      reader.jumpToChapter(0);
      reader.prepare();
      rec.label = "cover-book-" + std::to_string(i);
      ui.setScreen(inkling::InklingApp::readerScreen, &app, freeink::ui::RefreshHint::Full);
      app.update();
      rec.label = "lock-book-" + std::to_string(i);
      ui.setScreen(inkling::sleepScreen, &app, freeink::ui::RefreshHint::Full);
      app.update();
    }
  }
  app.showHome();
  app.update();

  // Rotation locks from the Settings screen, as a user would do it.
  auto lockTo = [&](int rightPresses) {
    app.settings().rotationMode = inkling::RotationMode::Auto;
    app.showSettings();
    app.update();
    press(B::Right, rightPresses);
    app.update();
  };
  lockTo(3);  // Auto -> Auto 4 lati -> 0° -> 90°
  menusTour("l90-");
  prefix = "l90-";
  app.showDeskAmbient();
  shot("desk-portrait-fallback");
  app.showHome();
  shot("home-after-fallback");

  lockTo(5);  // ... -> 180° -> 270°
  menusTour("l270-");

  lockTo(4);  // 180°
  menusTour("i180-");

  // Auto 4 lati from the accelerometer, debounced like the firmware loop.
  prefix = "auto-";
  app.settings().rotationMode = inkling::RotationMode::AutoAll;
  app.setDesiredOrientation(O::Portrait);
  inkling::OrientationTracker tracker;
  O next;
  int changes = 0;
  for (int i = 0; i < 6; ++i) {
    if (tracker.update(-0.95f, 0.05f, 0.1f, true, app.desiredOrientation(), next)) {
      app.setDesiredOrientation(next);
      ++changes;
    }
  }
  shot("tilted");
  if (changes != 1 || !inkling::isLandscape(app.desiredOrientation())) {
    std::fprintf(stderr, "orientation tracker: expected one landscape switch, got %d\n", changes);
    return 1;
  }
  app.setDesiredOrientation(O::Portrait);
  app.update();

  app.shutdown();
  return 0;
}

int runCheck(const std::string& cardDir) {
  Fonts fonts;
  sim::HostStorage storage(cardDir);
  inkling::BookReader reader(storage, readerMemory);
  reader.setIndexBuffer(readerIndex, sizeof(readerIndex));
  fonts.registerAll(reader, glyphsRegular, sizeof(glyphsRegular), glyphsItalic, sizeof(glyphsItalic));
  Canvas canvas;
  inkling::ui::bindInklingFonts(canvas.target);
  reader.setParseBuffer(canvas.fb.data(), canvas.fb.size());
  const auto frame = frameFor(canvas);

  static inkling::Library library;
  library.scan(storage);
  int failures = 0;
  // Every book from its first page, in portrait and in landscape (page sizes
  // as InklingApp::currentLayout() makes them).
  for (int pass = 0; pass < 2; ++pass) {
    const bool landscape = pass == 1;
    auto passFrame = frame;
    passFrame.rotation = landscape ? freeink::book::FrameRotation::None : freeink::book::FrameRotation::Portrait;
    for (int i = 0; i < library.count(); ++i) {
      inkling::ReaderLayout layout;
      layout.pageWidth = landscape ? kPanelW : kPanelH;
      layout.pageHeight = static_cast<int16_t>((landscape ? kPanelH : kPanelW) - 38);
      if (landscape) layout.marginH = static_cast<int16_t>(layout.marginH + 28);
      auto st = reader.open(library.at(i).path, layout);
      if (st == freeink::book::BookStatus::Ok) reader.jumpToChapter(0);
      uint32_t pages = 0;
      while (st == freeink::book::BookStatus::Ok) {
        st = reader.render(passFrame);
        ++pages;
        if (st != freeink::book::BookStatus::Ok || !reader.nextPage()) break;
      }
      std::printf("%-3s %-4s %-40.40s %5u pages  %3u%%  %s\n", st == freeink::book::BookStatus::Ok ? "OK" : "ERR",
                  landscape ? "land" : "port", library.at(i).name, pages, reader.percent(),
                  freeink::book::bookStatusName(st));
      if (st != freeink::book::BookStatus::Ok) ++failures;
      reader.close();
    }
  }
  std::printf("arena peaks: scratch %zu / %zu, catalog %zu / %zu, index %zu / %zu, parse %zu / %zu bytes\n",
              reader.scratchHighWater(), inkling::ReaderMemory::kScratch, reader.catalogHighWater(),
              inkling::ReaderMemory::kCatalog, reader.indexHighWater(), sizeof(readerIndex),
              reader.parseHighWater(), canvas.fb.size());
  return failures ? 1 : 0;
}

// Pages every book through its first chapters and prints a checksum of each
// chapter's rendered pages. mode 0: plain; 1: the next chapter is paginated
// ahead while "idle" on every page; 2: the same, but interrupted after two
// slices by the next page turn. The three outputs must be identical.
int runWalk(const std::string& cardDir, int mode, int maxSpine, int family) {
  Fonts fonts;
  sim::HostStorage storage(cardDir);
  inkling::BookReader reader(storage, readerMemory);
  reader.setIndexBuffer(readerIndex, sizeof(readerIndex));
  fonts.registerAll(reader, glyphsRegular, sizeof(glyphsRegular), glyphsItalic, sizeof(glyphsItalic));
  Canvas canvas;
  inkling::ui::bindInklingFonts(canvas.target);
  reader.setParseBuffer(canvas.fb.data(), canvas.fb.size());
  const auto frame = frameFor(canvas);
  static inkling::Library library;
  library.scan(storage);
  for (int i = 0; i < library.count(); ++i) {
    inkling::ReaderLayout layout;
    layout.fontFamily = static_cast<uint8_t>(family);
    auto st = reader.open(library.at(i).path, layout);
    if (st != freeink::book::BookStatus::Ok) {
      std::printf("ERR open %s\n", library.at(i).name);
      continue;
    }
    reader.jumpToChapter(0);
    uint32_t hash = 2166136261u, pages = 0;
    uint16_t spine = 0;
    auto flushChapter = [&] {
      std::printf("%-28.28s spine %3u  %4u pages  %08x\n", library.at(i).name, spine, pages, hash);
      hash = 2166136261u;
      pages = 0;
    };
    while (true) {
      std::fill(canvas.fb.begin(), canvas.fb.end(), 0xFF);  // as readerScreen does
      st = reader.render(frame);
      if (st != freeink::book::BookStatus::Ok) {
        std::printf("ERR render %s\n", freeink::book::bookStatusName(st));
        break;
      }
      if (reader.currentSpine() != spine) {
        flushChapter();
        spine = reader.currentSpine();
        if (spine >= maxSpine) break;
      }
      for (uint8_t b : canvas.fb) hash = (hash ^ b) * 16777619u;
      ++pages;
      reader.saveProgress();
      if (mode == 1) {
        while (reader.prebuildStep()) {
        }
      } else if (mode == 2) {
        reader.prebuildStep();
        reader.prebuildStep();
      }
      if (!reader.nextPage()) {
        flushChapter();
        break;
      }
    }
    reader.close();
  }
  std::printf("arena peaks: scratch %zu, index %zu, parse %zu\n", reader.scratchHighWater(), reader.indexHighWater(),
              reader.parseHighWater());
  return 0;
}

// Scripted tour of the reader tools and the Home apps that came with them:
// bookmark, clipping, dictionary, footnote links, flashcards, statistics, a
// picture as lock screen. One PNG per frame, like runTour().
int runTools(const std::string& cardDir, const std::string& outDir, const std::string& bookMatch) {
  Fonts fonts;
  sim::HostStorage storage(cardDir);
  storage.writeFile("/.inkling/last.txt", "", 0);
  storage.writeFile("/.inkling/prefs.bin", "", 0);
  storage.writeFile("/.inkling/settings.bin", "", 0);

  inkling::BookReader reader(storage, readerMemory);
  reader.setIndexBuffer(readerIndex, sizeof(readerIndex));
  fonts.registerAll(reader, glyphsRegular, sizeof(glyphsRegular), glyphsItalic, sizeof(glyphsItalic));
  Canvas canvas;
  inkling::ui::bindInklingFonts(canvas.target);
  reader.setParseBuffer(canvas.fb.data(), canvas.fb.size());
  static inkling::UiGlyphFallback uiFallback;
  uiFallback.setFont(&reader.regularFont());
  canvas.target.setGlyphFallback(&uiFallback);
  inkling::DeviceStatus status;
  populateSimStatus(status);
  inkling::UiApp ui(canvas.target, canvas.target.deviceContext());
  inkling::InklingApp app(storage, &reader, status, ui, frameFor(canvas));
  Recorder rec{&canvas, outDir, "", 0};
  app.setPresenter(Recorder::present, &rec);
  SimRotator rotator{&canvas, &ui, &app};
  app.setOrientationHandler(SimRotator::rotate, &rotator);

  using B = inkling::Button;
  auto step = [&](const char* label, B button) {
    rec.label = label;
    status.uptimeMs += 40000;
    app.onButton(button);
    app.update();
    while (app.hasIdleWork(5000) && app.idleTick(5000)) {
    }
  };
  auto shot = [&](const char* label) {
    rec.label = label;
    app.update();
  };
  auto press = [&](B button, int times) {
    for (int i = 0; i < times; ++i) {
      status.uptimeMs += 40000;
      app.onButton(button);
      app.update();
    }
  };
  // Reader menu item `index`, from the page.
  auto menuItem = [&](int index) {
    rec.label = "menu";
    app.onButton(B::Confirm);
    for (int i = 0; i < index; ++i) app.onButton(B::Down);
    app.update();
  };

  rec.label = "home";
  app.begin();
  app.update();

  // Open the book whose name contains `bookMatch`, a few chapters in.
  int book = 0;
  for (int i = 0; i < app.library().count(); ++i) {
    if (std::string(app.library().at(i).name).find(bookMatch) != std::string::npos) book = i;
  }
  auto openBook = [&](const char* label) {
    app.showLibrary();  // the cursor starts on the active book
    app.update();
    const int from = app.activeBookIndex() < 0 ? 0 : app.activeBookIndex();
    press(book >= from ? B::Down : B::Up, book >= from ? book - from : from - book);
    step(label, B::Confirm);
  };
  openBook("reader");
  // To a page of running prose, wherever the chapters start.
  rec.label = "seek";
  for (int tries = 0; tries < 60; ++tries) {
    static inkling::BookReader::PageWord many[inkling::BookReader::kMaxPageWords];
    if (reader.pageWords(many, inkling::BookReader::kMaxPageWords) > 60) break;
    app.onButton(B::Down);
    app.update();
  }
  press(B::Down, 2);
  shot("reader-prose");

  // Bookmark the page, then see it in the menu and on the page.
  menuItem(2);
  step("bookmark-set", B::Confirm);
  step("bookmark-page", B::Back);

  // Clip a passage: start at the 4th word, end two lines further.
  menuItem(3);
  step("clip-start", B::Confirm);
  press(B::Right, 3);
  step("clip-anchor", B::Confirm);
  press(B::Right, 9);
  press(B::Down, 2);
  shot("clip-range");
  step("clip-saved", B::Confirm);

  // The same passage after a relayout (bigger type): still underlined.
  menuItem(6);
  app.onButton(B::Right);
  step("clip-relayout", B::Back);
  menuItem(6);
  app.onButton(B::Left);
  step("clip-back", B::Back);

  // The list of marks.
  menuItem(2);
  step("marks", B::Right);
  step("marks-down", B::Down);
  step("marks-jump", B::Confirm);

  // Dictionary: a few words in a row.
  menuItem(4);
  step("dict-cursor", B::Confirm);
  for (int i = 0; i < 6; ++i) {
    step(("dict-word-" + std::to_string(i)).c_str(), B::Confirm);
    step("dict-back", B::Back);
    press(B::Right, 2 + i);
  }
  step("dict-exit", B::Back);

  // Footnotes / links: the first page that has one.
  rec.label = "seek-link";
  for (int tries = 0; tries < 300; ++tries) {
    uint8_t links = 0;
    reader.pageLinks(nullptr, 0, &links);
    if (links > 0) break;
    app.onButton(B::Down);
    app.update();
  }
  shot("links-page");
  menuItem(5);
  shot("links-menu");
  step("links-cursor", B::Confirm);
  step("links-next", B::Down);
  step("links-followed", B::Confirm);
  step("links-next-page", B::Down);
  step("links-returned", B::Back);

  // Close the book; Home apps.
  step("closed", B::Back);
  app.showHome();
  shot("home-8");
  app.showFlashcards();
  shot("cards-decks");
  step("cards-front", B::Confirm);
  step("cards-back", B::Confirm);
  step("cards-good", B::Confirm);
  step("cards-back2", B::Confirm);
  step("cards-again", B::Left);
  for (int i = 0; i < 40; ++i) {
    app.onButton(B::Confirm);
    app.onButton(B::Right);
  }
  shot("cards-summary");
  step("cards-decks2", B::Confirm);
  step("cards-nothing-due", B::Confirm);
  app.showStats();
  shot("stats");

  // A picture as lock screen.
  app.settings().sleepScreen = inkling::SleepScreenMode::Images;
  rec.label = "lock-image";
  ui.setScreen(inkling::sleepScreen, &app, freeink::ui::RefreshHint::Full);
  app.update();
  app.settings().sleepScreen = inkling::SleepScreenMode::Poster;
  rec.label = "lock-poster";
  ui.setScreen(inkling::sleepScreen, &app, freeink::ui::RefreshHint::Full);
  app.update();
  app.settings().sleepScreen = inkling::SleepScreenMode::Cover;

  // The same in landscape.
  app.settings().rotationMode = inkling::RotationMode::LockLandscapeL;
  app.setDesiredOrientation(freeink::ui::Orientation::LandscapeClockwise);
  app.showHome();
  shot("land-home-8");
  app.showStats();
  shot("land-stats");
  app.showFlashcards();
  shot("land-cards-decks");
  openBook("land-reader");
  menuItem(3);
  step("land-clip", B::Confirm);
  press(B::Right, 5);
  app.onButton(B::Confirm);
  press(B::Down, 2);
  shot("land-clip-range");
  step("land-clip-cancel", B::Back);
  step("land-clip-exit", B::Back);
  menuItem(2);
  step("land-marks", B::Right);
  step("land-menu", B::Back);
  step("land-reader2", B::Back);
  menuItem(4);
  step("land-dict", B::Confirm);
  press(B::Right, 3);
  step("land-dict-word", B::Confirm);
  app.settings().sleepScreen = inkling::SleepScreenMode::Images;
  rec.label = "land-lock-image";
  ui.setScreen(inkling::sleepScreen, &app, freeink::ui::RefreshHint::Full);
  app.update();

  app.shutdown();
  return 0;
}

// Checks of the parts with no screen: dictionary lookups, flashcard
// scheduling over several days, reading statistics across days.
int runSelfTest(const std::string& cardDir) {
  sim::HostStorage storage(cardDir);
  int failures = 0;
  auto expect = [&](bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
  };

  // --- dictionary -----------------------------------------------------------
  {
    inkling::Dictionary dict;
    using R = inkling::Dictionary::Result;
    char head[64], def[600];
    auto look = [&](const char* word) { return dict.lookup(storage, word, head, sizeof(head), def, sizeof(def)); };
    expect(dict.open(storage) == R::Found, "dictionary found in a sub-folder of /dizionari");
    expect(look("aria") == R::Found && std::string(head) == "aria", "exact word");
    expect(std::string(def).find("<") == std::string::npos && std::string(def).find("Miscuglio") != std::string::npos,
           "markup removed from the definition");
    expect(look("Montagna,") == R::Found && std::string(head) == "montagna", "capital letter and punctuation");
    expect(look("dell\xE2\x80\x99" "aria") == R::Found && std::string(head) == "aria", "elision with a curly apostrophe");
    expect(look("\xC2\xAB" "vetta" "\xC2\xBB") == R::Found, "guillemets around the word");
    expect(look("everest") == R::Found && std::string(head) == "Everest", "headword with a capital");
    expect(std::string(def).find("pi\xC3\xB9" " alta") != std::string::npos, "named entity decoded");
    expect(look("montagne") == R::Nearest && std::string(head) == "montagna", "inflected form: nearest headword");
    expect(look("spedizioni") == R::Nearest && std::string(head) == "spedizione", "plural: nearest headword");
    expect(look("xilofono") == R::NotFound, "missing word");
    expect(look("zz0777") == R::Found && std::string(def) == "voce di riempimento 777", "word deep in the index");
    expect(look("zz1499") == R::Found, "last word of the index");
    expect(look("a") == R::NotFound, "before the first headword");
    inkling::Dictionary again;  // a second run reuses the side index
    expect(again.lookup(storage, "tempo", head, sizeof(head), def, sizeof(def)) == R::Found, "side index reused");
  }

  // --- flashcards -----------------------------------------------------------
  {
    using G = inkling::Flashcards::Grade;
    const int32_t day = 20000;
    inkling::Flashcards cards;
    cards.scan(storage);
    int deck = -1;
    for (int i = 0; i < cards.deckCount(); ++i) {
      if (std::string(cards.deck(i).name) == "Inglese") deck = i;
    }
    expect(deck >= 0, "semicolon deck listed");
    expect(cards.begin(storage, deck, day) && cards.deckCards() == 5 && cards.remaining() == 5, "five new cards");
    expect(std::string(cards.front()) == "mountain" && std::string(cards.back()) == "montagna", "first card");
    cards.grade(storage, G::Good);  // mountain: tomorrow
    cards.grade(storage, G::Easy);  // summit: in three days
    cards.grade(storage, G::Again); // rope: again this session
    expect(cards.remaining() == 3 && std::string(cards.front()) == "ice axe", "forgotten card goes to the end");
    cards.grade(storage, G::Good);  // ice axe
    expect(std::string(cards.front()) == "crampons; plural", "quoted field keeps its separator");
    cards.grade(storage, G::Good);  // crampons
    expect(std::string(cards.front()) == "rope" && cards.remaining() == 1, "forgotten card comes back");
    cards.grade(storage, G::Good);
    expect(cards.finished() && cards.reviewed() == 5, "session finished");
    cards.end();
    expect(cards.begin(storage, deck, day) && cards.finished(), "same day: nothing due");
    cards.end();
    expect(cards.begin(storage, deck, day + 1) && cards.remaining() == 4, "next day: the four 'good' cards");
    while (!cards.finished()) cards.grade(storage, G::Good);
    cards.end();
    expect(cards.begin(storage, deck, day + 3) && cards.remaining() == 1 && std::string(cards.front()) == "summit",
           "third day: the 'easy' card");
    cards.grade(storage, G::Good);
    cards.end();
    expect(cards.begin(storage, deck, day + 7) && cards.remaining() == 4, "sixth day after: the four again");
    cards.end();

    int crosspet = -1;
    for (int i = 0; i < cards.deckCount(); ++i) {
      if (std::string(cards.deck(i).name) == "Prova CrossPet") crosspet = i;
    }
    expect(crosspet >= 0 && cards.begin(storage, crosspet, day) && cards.deckCards() == 4, "CrossPet deck, header skipped");
    expect(std::string(cards.front()) == "What is 2+2?" && std::string(cards.back()) == "4", "id column ignored");
    cards.grade(storage, G::Good);
    expect(std::string(cards.front()) == "Bonjour  greeting" || std::string(cards.front()) == "Bonjour greeting",
           "hint marks removed");
    cards.grade(storage, G::Good);
    expect(std::string(cards.back()).find('\n') != std::string::npos, "/n becomes a line break");
    cards.end();
  }

  // --- reading statistics ---------------------------------------------------
  {
    storage.removeFile("/.inkling/reading.bin");
    inkling::ReadingStats stats;
    auto at = [](uint8_t dayOfMonth) {
      inkling::LocalTime t;
      t.valid = true;
      t.year = 2026;
      t.month = 11;
      t.day = dayOfMonth;
      t.hour = 21;
      return t;
    };
    for (int i = 0; i < 10; ++i) stats.onPage(at(10), 60);
    for (int i = 0; i < 4; ++i) stats.onPage(at(11), 90);
    expect(stats.todayPages(at(11)) == 4 && stats.todayMinutes(at(11)) == 6 && stats.streakDays(at(11)) == 2,
           "second day in a row");
    expect(stats.todayPages(at(12)) == 0 && stats.streakDays(at(12)) == 2, "next morning: nothing yet, streak alive");
    expect(stats.streakDays(at(13)) == 0, "a day skipped: streak over");
    stats.save(storage);
    inkling::ReadingStats loaded;
    loaded.load(storage);
    for (int i = 0; i < 7; ++i) loaded.onPage(at(14), 30);
    inkling::ReadingStats::Day week[7];
    loaded.lastWeek(at(15), week);
    expect(week[6].pages == 0 && week[5].pages == 7 && week[2].pages == 4 && week[1].pages == 10 && week[1].minutes == 10,
           "last week, day by day");
    expect(loaded.totalPages() == 21 && loaded.totalMinutes() == 19 && loaded.streakDays(at(14)) == 1, "totals");
    storage.removeFile("/.inkling/reading.bin");
  }

  std::printf("%s\n", failures ? "SELF-TEST FAILED" : "self-test passed");
  return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--selftest") return runSelfTest(argv[2]);
  if (argc >= 4 && std::string(argv[1]) == "--tools") return runTools(argv[2], argv[3], argc >= 5 ? argv[4] : "");
  if (argc >= 4 && std::string(argv[1]) == "--walk") {
    return runWalk(argv[2], std::atoi(argv[3]), argc >= 5 ? std::atoi(argv[4]) : 14,
                   argc >= 6 ? std::atoi(argv[5]) : 0);
  }
  if (argc == 3 && std::string(argv[1]) == "--check") return runCheck(argv[2]);
  if (argc == 3) return runTour(argv[1], argv[2]);
  std::fprintf(stderr,
               "usage: %s <card-dir> <out-dir> | --check <card-dir> | --walk <card-dir> <mode> [spines] [family]"
               " | --tools <card-dir> <out-dir> [book]\n",
               argv[0]);
  return 2;
}
