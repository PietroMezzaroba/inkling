#pragma once

#include <FreeInkUIBookFont.h>

namespace inkling {

// Glyphs the UI's ASCII bitmap font lacks (accented letters in titles and
// menus) drawn from the reading face. Alegreya has a smaller x-height than the
// UI's Noto Sans, so it is rendered a bit larger to sit evenly in a word.
class UiGlyphFallback final : public freeink::ui::RuntimeGlyphSource {
 public:
  void setFont(freeink::book::TtfFont* font) { inner_.setFont(font); }

  bool glyph(uint32_t codepoint, uint16_t pixelSize, Glyph* out) override {
    return inner_.glyph(codepoint, scaled(pixelSize), out);
  }
  int16_t advance(uint32_t codepoint, uint16_t pixelSize) override {
    return inner_.advance(codepoint, scaled(pixelSize));
  }

 private:
  static uint16_t scaled(uint16_t px) { return static_cast<uint16_t>(px * kScalePct / 100); }
  static constexpr uint16_t kScalePct = 122;
  freeink::ui::TtfGlyphSource inner_;
};

}  // namespace inkling
