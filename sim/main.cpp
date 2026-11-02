// Inkling host simulator: runs the same InklingCore code as the firmware on an
// X3-sized 1-bit framebuffer and saves frames as PNG.
//
//   inkling-sim <card-dir> <out-dir>     scripted tour, one PNG per frame
//   inkling-sim --check <card-dir>       open and page through every book,
//                                        report status and arena peaks only

#include <FreeInkUIDisplayTarget.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "HostStorage.h"
#include "PngWriter.h"
#include "inkling/App.h"
#include "inkling/Screens.h"
#include "inkling/UiGlyphFallback.h"

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

  // Saves the panel as the reader sees it (portrait), rotating back out of
  // the landscape-native buffer exactly like DisplayTarget's Portrait mapping.
  bool save(const std::string& path) const {
    const int outW = target.logicalWidth();
    const int outH = target.logicalHeight();
    std::vector<uint8_t> gray(static_cast<size_t>(outW) * outH, 0xFF);
    for (int y = 0; y < outH; ++y) {
      for (int x = 0; x < outW; ++x) {
        int px = x, py = y;
        if (outW != kPanelW) {
          px = y;
          py = kPanelH - 1 - x;
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
  std::vector<uint8_t> regular = loadFile(std::string(INKLING_FONT_DIR) + "/Alegreya-Regular.ttf");
  std::vector<uint8_t> italic = loadFile(std::string(INKLING_FONT_DIR) + "/Alegreya-Italic.ttf");
};

freeink::book::FrameTarget frameFor(Canvas& canvas) {
  freeink::book::FrameTarget frame{canvas.fb.data(), kPanelW, kPanelH, kPanelWBytes};
  frame.format = freeink::book::FrameFormat::Mono1Dithered;
  frame.rotation = freeink::book::FrameRotation::Portrait;
  return frame;
}

// Saves every presented frame as <out>/NN-<label>.png.
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

inkling::ReaderMemory readerMemory;  // same budget as the device
// Glyph caches at their ideal size, as on a device with heap to spare.
alignas(16) uint8_t glyphsRegular[inkling::GlyphCacheSizes::kRegularIdeal];
alignas(16) uint8_t glyphsItalic[inkling::GlyphCacheSizes::kItalic];

int runTour(const std::string& cardDir, const std::string& outDir) {
  Fonts fonts;
  if (fonts.regular.empty()) {
    std::fprintf(stderr, "fonts not found in %s\n", INKLING_FONT_DIR);
    return 1;
  }
  sim::HostStorage storage(cardDir);
  inkling::BookReader reader(storage, readerMemory);
  reader.initFonts(fonts.regular.data(), static_cast<uint32_t>(fonts.regular.size()), fonts.italic.data(),
                   static_cast<uint32_t>(fonts.italic.size()), glyphsRegular, sizeof(glyphsRegular), glyphsItalic,
                   sizeof(glyphsItalic));

  Canvas canvas;
  reader.setParseBuffer(canvas.fb.data(), canvas.fb.size());  // as on the device
  static inkling::UiGlyphFallback uiFallback;  // accents in menus, as on the device
  uiFallback.setFont(&reader.regularFont());
  canvas.target.setGlyphFallback(&uiFallback);
  inkling::DeviceStatus status;
  inkling::UiApp ui(canvas.target, canvas.target.deviceContext());
  ui.setClearColor(freeink::ui::Color::White);
  inkling::InklingApp app(storage, &reader, status, ui, frameFor(canvas));
  Recorder rec{&canvas, outDir, "", 0};
  app.setPresenter(Recorder::present, &rec);

  using B = inkling::Button;
  auto step = [&](const char* label, B button) {
    rec.label = label;
    app.onButton(button);
    app.update();
  };

  rec.label = "library";
  app.begin();
  app.update();
  step("library-down", B::Down);
  step("library-up", B::Up);
  step("open", B::Confirm);
  step("next", B::Right);
  step("menu", B::Confirm);
  step("menu-down", B::Down);          // "Indice dei capitoli"
  step("toc", B::Confirm);
  step("toc-down", B::Down);
  step("toc-jump", B::Confirm);        // second chapter
  step("menu-again", B::Confirm);
  step("menu-down", B::Down);
  step("menu-down", B::Down);          // "Testo piu' grande"
  step("bigger", B::Confirm);
  step("back-to-library", B::Back);

  // A fresh boot with a last book recorded: reopens it directly.
  app.shutdown();
  inkling::InklingApp rebooted(storage, &reader, status, ui, frameFor(canvas));
  rebooted.setPresenter(Recorder::present, &rec);
  rec.label = "reboot-library";  // closed with Back above: boot shows the library
  rebooted.begin();
  rebooted.update();
  rec.label = "reopen";
  rebooted.onButton(B::Confirm);
  rebooted.update();
  rebooted.shutdown();
  inkling::InklingApp resumed(storage, &reader, status, ui, frameFor(canvas));
  resumed.setPresenter(Recorder::present, &rec);
  rec.label = "reboot-resume";  // powered off while reading: boot reopens the book
  resumed.begin();
  resumed.update();
  resumed.shutdown();
  return 0;
}

int runCheck(const std::string& cardDir) {
  Fonts fonts;
  sim::HostStorage storage(cardDir);
  inkling::BookReader reader(storage, readerMemory);
  reader.initFonts(fonts.regular.data(), static_cast<uint32_t>(fonts.regular.size()), fonts.italic.data(),
                   static_cast<uint32_t>(fonts.italic.size()), glyphsRegular, sizeof(glyphsRegular), glyphsItalic,
                   sizeof(glyphsItalic));
  Canvas canvas;
  reader.setParseBuffer(canvas.fb.data(), canvas.fb.size());  // as on the device
  const auto frame = frameFor(canvas);

  static inkling::Library library;
  library.scan(storage);
  int failures = 0;
  for (int i = 0; i < library.count(); ++i) {
    inkling::ReaderLayout layout;
    layout.pageHeight = static_cast<int16_t>(kPanelW - 34);
    auto st = reader.open(library.at(i).path, layout);
    uint32_t pages = 0;
    while (st == freeink::book::BookStatus::Ok) {
      st = reader.render(frame);
      ++pages;
      if (st != freeink::book::BookStatus::Ok || !reader.nextPage()) break;
    }
    std::printf("%-3s %-40.40s %5u pages  %3u%%  %s\n", st == freeink::book::BookStatus::Ok ? "OK" : "ERR",
                library.at(i).name, pages, reader.percent(), freeink::book::bookStatusName(st));
    if (st != freeink::book::BookStatus::Ok) ++failures;
    reader.close();
  }
  std::printf("arena peaks: scratch %zu / %zu, catalog %zu / %zu, index %zu / %zu, parse %zu / %zu bytes\n",
              reader.scratchHighWater(), inkling::ReaderMemory::kScratch, reader.catalogHighWater(),
              inkling::ReaderMemory::kCatalog, reader.indexHighWater(), inkling::ReaderMemory::kIndex,
              reader.parseHighWater(), canvas.fb.size());
  return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--check") return runCheck(argv[2]);
  if (argc == 3) return runTour(argv[1], argv[2]);
  std::fprintf(stderr, "usage: %s <card-dir> <out-dir> | --check <card-dir>\n", argv[0]);
  return 2;
}
