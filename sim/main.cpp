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

struct Fonts {
  std::vector<uint8_t> notoSerifReg = loadFile(std::string(INKLING_FONT_DIR) + "/NotoSerif-Regular.ttf");
  std::vector<uint8_t> notoSerifIt = loadFile(std::string(INKLING_FONT_DIR) + "/NotoSerif-Italic.ttf");
  std::vector<uint8_t> notoSansReg = loadFile(std::string(INKLING_FONT_DIR) + "/NotoSans-Regular.ttf");
  std::vector<uint8_t> notoSansIt = loadFile(std::string(INKLING_FONT_DIR) + "/NotoSans-Italic.ttf");
  std::vector<uint8_t> alegreyaReg = loadFile(std::string(INKLING_FONT_DIR) + "/Alegreya-Regular.ttf");
  std::vector<uint8_t> alegreyaIt = loadFile(std::string(INKLING_FONT_DIR) + "/Alegreya-Italic.ttf");
  std::vector<uint8_t> dyslexicReg = loadFile(std::string(INKLING_FONT_DIR) + "/OpenDyslexic-Regular.otf");
  std::vector<uint8_t> dyslexicIt = loadFile(std::string(INKLING_FONT_DIR) + "/OpenDyslexic-Italic.otf");

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
    snprintf(name, sizeof(name), "%s/%02d-%s.png", self.outDir.c_str(), self.frame++, self.label.c_str());
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
    press(B::Down, 2);
    step("menu-fontsize", B::Right);
    step("apply", B::Back);
    step("menu3", B::Confirm);
    press(B::Down, 7);
    step("menu-night", B::Right);
    step("night-page", B::Back);
    app.onButton(B::Confirm);
    press(B::Down, 7);
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
int runWalk(const std::string& cardDir, int mode) {
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
        if (spine >= 14) break;
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

}  // namespace

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--walk") return runWalk(argv[2], std::atoi(argv[3]));
  if (argc == 3 && std::string(argv[1]) == "--check") return runCheck(argv[2]);
  if (argc == 3) return runTour(argv[1], argv[2]);
  std::fprintf(stderr, "usage: %s <card-dir> <out-dir> | --check <card-dir>\n", argv[0]);
  return 2;
}
