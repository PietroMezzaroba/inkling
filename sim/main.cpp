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

int runTour(const std::string& cardDir, const std::string& outDir) {
  Fonts fonts;
  if (fonts.notoSerifReg.empty()) {
    std::fprintf(stderr, "fonts not found in %s\n", INKLING_FONT_DIR);
    return 1;
  }
  sim::HostStorage storage(cardDir);
  // Start tour on the Home screen by clearing any left-over resume pointer & prefs.
  storage.writeFile("/.inkling/last.txt", "", 0);
  storage.writeFile("/.inkling/prefs.bin", "", 0);

  inkling::BookReader reader(storage, readerMemory);
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

  using B = inkling::Button;
  auto step = [&](const char* label, B button) {
    rec.label = label;
    app.onButton(button);
    app.update();
  };

  // 1. Boot into Home Command Center
  rec.label = "home";
  app.begin();
  app.update();

  // 2. Navigate to Library tile on Home Dock and open Visual Library
  step("home-dock-library", B::Down);
  step("library", B::Confirm);
  step("library-down", B::Down);
  step("library-up", B::Up);

  // 3. Open first book and turn pages (feeding the Pet)
  step("open-book", B::Confirm);
  step("reader-next-1", B::Right);
  step("reader-next-2", B::Right);

  // 4. Open Floating Reader Overlay Menu over the live book page
  step("reader-menu-overlay", B::Confirm);
  step("menu-down-toc", B::Down);  // "Indice dei capitoli"
  step("toc", B::Confirm);
  step("toc-down", B::Down);
  step("toc-jump", B::Confirm);

  // 5. Reopen overlay menu, increase text size in-place, and apply with Back
  step("menu-again", B::Confirm);
  step("menu-down-1", B::Down);
  step("menu-down-size", B::Down);  // "Dimensione Testo"
  step("bigger-font-live", B::Right);
  step("apply-layout", B::Back);

  // 6. Close book back to Home (now showing updated progress & Pet stats!)
  step("back-to-home", B::Back);

  // 7. Test Desk Ambient Mode (Split-Flap Clock, Weather, Mini-Calendar, GitHub Heatmap & Moon Phase)
  rec.label = "desk-dashboard-heatmap";
  app.showDeskAmbient();
  app.update();

  step("desk-dashboard-moon", B::Right);
  step("desk-back-home", B::Back);

  // 8. Test L'Oracolo & Oblique Strategies (Daily Stoic & Brian Eno)
  rec.label = "oracle-daily-stoic";
  app.showOracle();
  app.update();

  step("oracle-oblique-strategies", B::Right);
  app.oracle().onShake();
  rec.label = "oracle-imu-shake";
  app.update();
  step("oracle-back-home", B::Back);

  // 9. Test Micro-RPG La Cripta dei Tomi (Procedural text crawler & combat)
  rec.label = "rpg-crypt-room";
  app.showTextRpg();
  app.update();

  step("rpg-combat-action", B::Confirm);
  step("rpg-back-home", B::Back);

  // 10. Open Interactive Pet Sanctuary & play Hacker Matrix: Breach!
  step("home-dock-sanctuary", B::Down);
  step("pet-sanctuary", B::Confirm);
  step("pet-feed-crystal", B::Confirm);  // Feed 1 Ink Crystal!
  step("pet-select-breach", B::Down);
  step("breach-start", B::Confirm);      // Launch Hacker Matrix: Breach!
  step("breach-move-up", B::Up);
  step("breach-move-right", B::Right);
  step("breach-emp-blast", B::Confirm);  // Fire EMP Wave!
  step("breach-back-sanctuary", B::Back);
  step("pet-select-lamp", B::Down);
  step("pet-night-lamp", B::Confirm);    // Toggle Astral Night Lamp!
  step("pet-back-home", B::Back);

  // 8. Open System Diagnostics & IMU Radar from Home
  rec.label = "diagnostic";
  app.showDiagnostic();
  app.update();

  // 9. Render Collectible Sleep Poster with live Pet & Book telemetry
  rec.label = "sleep-poster";
  ui.setScreen(inkling::sleepScreen, &app, freeink::ui::RefreshHint::Full);
  app.update();

  // 10. Render Futuristic Wi-Fi Cyber-Net & Web Portal Screen
  inkling::OtaView otaView{};
  otaView.network = "Vodafone-C33359337";
  std::snprintf(otaView.address, sizeof(otaView.address), "192.168.1.10");
  otaView.state = "Daily Ink: ANSA + HackerNews";
  otaView.version = "v2.0-cyber";
  otaView.progress = 80;
  rec.label = "ota-update";
  ui.setScreen(inkling::otaScreen, &otaView, freeink::ui::RefreshHint::Fast);
  app.update();

  // 11. Evolve Pet to Final Stage (Arconte Astrale) and render Sanctuary & Sleep Poster
  inkling::LocalTime evoNow{true, 2026, 11, 2, 18, 25};
  app.pet().toggleNightLamp(storage);  // switch back to daytime habitat
  for (int p = 0; p < 520; ++p) {
    app.pet().onPageRead(storage, evoNow, (p % 25) == 24, false);
  }
  rec.label = "evo-archon-sanctuary";
  app.showPetSanctuary();
  app.update();

  rec.label = "evo-archon-sleep";
  ui.setScreen(inkling::sleepScreen, &app, freeink::ui::RefreshHint::Full);
  app.update();

  // 12. Open "K2. La verità. Storia di un caso." (Walter Bonatti) to showcase narrative prose & Italian accents
  app.showLibrary();
  app.update();
  step("lib-select-1", B::Down);
  step("lib-select-2", B::Down);
  step("lib-select-k2", B::Down);
  step("k2-open", B::Confirm);
  reader.jumpToChapter(8);
  reader.prepare();
  rec.label = "k2-prose-page1";
  ui.invalidate(freeink::ui::RefreshHint::Full);
  app.update();
  step("k2-prose-page2", B::Right);
  step("k2-prose-page3", B::Right);
  step("k2-prose-page4", B::Right);

  // 13. Showcase Studio Tipografico: enable Bionic Focus Reading & render page
  step("k2-studio-open", B::Confirm);
  step("k2-studio-row1", B::Down);
  step("k2-studio-row2", B::Down);
  step("k2-studio-row3", B::Down);
  step("k2-studio-row4-bionic", B::Down);  // Row 4: Lettura Veloce (Bionic Focus)
  step("k2-enable-bionic", B::Right);
  step("k2-bionic-focus-page", B::Back);   // Apply & return to book!

  // 14. Showcase Night Mode (Inversione E-Ink) on Bionic page
  step("k2-studio-night-open", B::Confirm);
  for (int i = 0; i < 7; ++i) app.onButton(B::Down);
  step("k2-enable-night", B::Right);
  step("k2-night-mode-page", B::Back);

  // 15. Showcase Daily Ink Briefing (Markdown Newspaper with H2/H3 + Blockquote + Bionic Focus)
  step("k2-studio-day-open", B::Confirm);
  for (int i = 0; i < 7; ++i) app.onButton(B::Down);
  step("k2-disable-night", B::Right);
  step("k2-back-day", B::Back);

  const char* sampleBriefing =
      "# DAILY INK BRIEFING\n\n"
      "> Edizione E-Ink del 02/11/2026 ore 21:30 — Generata via Cyber-Net Wi-Fi per Xteink X3.\n\n"
      "# PRIMA PAGINA ITALIA (ANSA)\n\n"
      "## 1. Volano a 17 miliardi i ricavi trimestrali di Anthropic\n\n"
      "La societa' californiana di intelligenza artificiale scalda i motori per lo sbarco dei record al Nasdaq con una crescita esponenziale dei servizi enterprise.\n\n"
      "## 2. Spazio europeo, nuovo satellite di osservazione terrestre in orbita\n\n"
      "Lanciato con successo il nuovo modulo ad altissima risoluzione per il monitoraggio climatico del Mediterraneo.\n\n"
      "# HACKER NEWS // SILICON VALLEY\n\n"
      "## 1. Egoless Mathematics in the AI Era (terrytao.wordpress.com)\n\n"
      "## 2. Show HN: Ultra-fast 1-bit E-Ink rendering on ESP32-C3 RISC-V\n\n";
  storage.writeFile("/Daily Ink Briefing - Redazione Cyber-Net.md", sampleBriefing, std::strlen(sampleBriefing));
  reader.open("/Daily Ink Briefing - Redazione Cyber-Net.md", app.currentLayout());
  reader.prepare();
  rec.label = "daily-ink-briefing";
  ui.invalidate(freeink::ui::RefreshHint::Full);
  app.update();

  // 16. Test Settings Studio: Display Auto-Rotation, Rotation Lock, Inverted Keys, Sleep Timer
  rec.label = "settings-studio";
  app.showSettings();
  app.update();
  step("settings-cycle-rotation", B::Right);  // Switch to Blocco 0°
  step("settings-down-invert", B::Down);
  step("settings-cycle-invert", B::Right);    // Toggle key inversion
  step("settings-down-sleep", B::Down);
  step("settings-cycle-sleep", B::Right);     // Cycle auto-sleep timer
  step("settings-back-home", B::Back);        // Save & return to home

  app.shutdown();
  return 0;
}

int testPetEngine(sim::HostStorage& storage) {
  storage.mkdirs("/.inkling");
  storage.mkdirs("/.crosspoint/pet");
  storage.writeFile("/.inkling/pet.bin", "", 0);
  // Seed a legacy CrossPet JSON to verify automatic migration
  const char* legacyJson = R"({"level":3,"xp":240,"totalPagesRead":85,"currentStreak":4,"mood":88,"hunger":90})";
  storage.writeFile("/.crosspoint/pet/pet.json", legacyJson, std::strlen(legacyJson));

  inkling::PetEngine pet;
  inkling::LocalTime t0{true, 2026, 11, 2, 10, 0};
  pet.load(storage, t0);
  if (pet.state().totalPagesRead < 85 || pet.stage() != inkling::PetStage::Glyph) {
    std::fprintf(stderr, "PetEngine CrossPet migration failed (pages=%lu, stage=%u)\n",
                 static_cast<unsigned long>(pet.state().totalPagesRead), static_cast<unsigned>(pet.stage()));
    return 1;
  }
  const uint16_t beforeCrystals = pet.state().inkCrystals;
  for (int i = 0; i < 10; ++i) pet.onPageRead(storage, t0, i == 9, false);
  if (pet.state().inkCrystals <= beforeCrystals) {
    std::fprintf(stderr, "PetEngine crystal reward check failed\n");
    return 1;
  }
  if (!pet.feedInkCrystal(storage)) {
    std::fprintf(stderr, "PetEngine feedInkCrystal check failed\n");
    return 1;
  }
  // Clean up test files
  storage.writeFile("/.crosspoint/pet/pet.json", "", 0);
  storage.writeFile("/.inkling/pet.bin", "", 0);
  std::printf("OK  PetEngine migration, evolution & crystal economy verified\n");
  return 0;
}

int runCheck(const std::string& cardDir) {
  Fonts fonts;
  sim::HostStorage storage(cardDir);
  inkling::BookReader reader(storage, readerMemory);
  fonts.registerAll(reader, glyphsRegular, sizeof(glyphsRegular), glyphsItalic, sizeof(glyphsItalic));
  Canvas canvas;
  inkling::ui::bindInklingFonts(canvas.target);
  reader.setParseBuffer(canvas.fb.data(), canvas.fb.size());
  const auto frame = frameFor(canvas);

  static inkling::Library library;
  library.scan(storage);
  int failures = testPetEngine(storage);
  for (int i = 0; i < library.count(); ++i) {
    inkling::ReaderLayout layout;
    layout.pageHeight = static_cast<int16_t>(kPanelW - 38);
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
