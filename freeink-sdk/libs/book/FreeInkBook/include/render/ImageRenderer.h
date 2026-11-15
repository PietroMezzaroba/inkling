#pragma once

// FreeInk SDK — streaming image rendering for FreeInkBook (Phase 4b).
//
// Decodes a container image (PNG via pngle, JPEG via TJpgDec) and delivers
// it as 8-bit grayscale rows already scaled (nearest-neighbor, no upscaling
// beyond the requested placement) to the caller, top to bottom. The full
// image never exists in memory: PNG decodes scanline-by-scanline, JPEG in
// MCU bands, and each finished source row maps to zero or more output rows.
//
// E-paper targets then quantize each row: ditherRowOrdered() gives 1-bit
// output via a Bayer 4×4 matrix; grayscale panels can consume the rows
// directly.
//
// Interlaced PNGs are rejected (BookStatus::Unsupported) — their pixels
// arrive out of order and would require a full-image buffer.

#include <stdint.h>

#include "BookArena.h"
#include "BookStorage.h"
#include "BookTypes.h"
#include "epub/ZipCatalog.h"
#include "layout/ChapterLayout.h"

namespace freeink {
namespace book {

// Return false to stop rendering early. `gray` holds `width` pixels
// (0 = black, 255 = white) and is valid only during the call.
using ImageRowFn = bool (*)(void* user, uint16_t y, const uint8_t* gray, uint16_t width);

class ImageRenderer {
 public:
  // Renders `image` (a placement produced by layout or read from the page
  // cache) at its recorded size. Decode state comes from `scratch` and is
  // released before returning; pngle additionally uses bounded transient
  // heap for its scanline buffer.
  static BookStatus render(BookSource& source, const ZipCatalog& zip, const PageImage& image,
                           Arena& scratch, ImageRowFn rowFn, void* user);

  // Renders any image ZipEntry directly, fitting within `maxWidth` x `maxHeight`
  // (preserving aspect ratio unless `stretchToBox` is true). Writes the chosen
  // output dimensions to `outWidth`/`outHeight` when non-null.
  static BookStatus renderEntry(BookSource& source, const ZipEntry& entry, uint16_t maxWidth,
                                uint16_t maxHeight, Arena& scratch, ImageRowFn rowFn, void* user,
                                uint16_t* outWidth = nullptr, uint16_t* outHeight = nullptr,
                                bool stretchToBox = false);
};

// Quantizes one grayscale row to 1 bit per pixel (MSB first, 1 = black)
// with ordered Bayer 4×4 dithering. `outBits` needs (width+7)/8 bytes.
void ditherRowOrdered(const uint8_t* gray, uint16_t width, uint16_t y, uint8_t* outBits);

// Quantizes one grayscale row to 1 bit per pixel (MSB first, 1 = black)
// using Atkinson error diffusion + e-paper contrast curve.
// `err0`, `err1`, `err2` must each have at least `width + 4` entries.
void ditherRowAtkinson(const uint8_t* gray, uint16_t width, int16_t* err0, int16_t* err1,
                       int16_t* err2, uint8_t* outBits);

}  // namespace book
}  // namespace freeink
