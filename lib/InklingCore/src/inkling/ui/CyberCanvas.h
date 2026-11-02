#pragma once

#include <FreeInkUI.h>
#include <FreeInkUIDisplayTarget.h>

#include <cstdint>

#include "inkling/DeviceStatus.h"
#include "inkling/pet/PetEngine.h"
#include "inkling/ui/InklingFonts.h"

namespace inkling {
namespace ui {

using freeink::ui::Color;
using freeink::ui::DrawTarget;
using freeink::ui::Paint;
using freeink::ui::Point;
using freeink::ui::Rect;
using freeink::ui::TextAlign;
using freeink::ui::TextStyle;

// High-contrast 1-bit futuristic drawing primitives for Xteink X3 (528x792 portrait).
namespace canvas {

static constexpr int16_t kScreenW = 528;
static constexpr int16_t kScreenH = 792;

// Binds the raw 1-bit panel framebuffer (792x528, 99 bytes/row) so CyberCanvas
// can execute span-based byte-level fills, clears, and strokes ~500x faster
// than per-pixel plotting on the 160 MHz ESP32-C3.
void bindFramebuffer(uint8_t* fb, int16_t panelWidth, int16_t panelHeight, int16_t panelWidthBytes);

// Full-screen white clear via memset (~0.12 ms vs ~60 ms per-pixel fill).
void clearScreen();

// Full-screen bitwise B/W inversion (~0.08 ms) for Night Reading Mode.
void invertScreen();

// Clears a horizontal portrait band y in [y0, y1) across the full width to white.
void clearBandY(int16_t y0, int16_t y1);

// Fast byte-span rounded rectangle fill & stroke (100% pixel-identical to DisplayTarget).
void fastFill(DrawTarget& t, Rect r, Paint paint, uint8_t radius = 0);
void fastStroke(DrawTarget& t, Rect r, Paint paint, uint8_t width, uint8_t radius = 0);

// Draws a single-line or clipped text helper using a specific Inkling FontSlot.
void drawText(DrawTarget& t, Rect r, const char* str, FontSlot slot, Color color = Color::Black,
              TextAlign align = TextAlign::Left, bool ellipsis = true);

// Measures pixel width of a string in the given slot.
int16_t textWidth(DrawTarget& t, const char* str, FontSlot slot);

// Fills a rectangle with a sparse dot-matrix grid pattern.
void fillDotGrid(DrawTarget& t, Rect r, int16_t step = 6, Color dotColor = Color::Black);

// Fills a rectangle with diagonal 45-degree scanlines.
void fillScanlines(DrawTarget& t, Rect r, int16_t period = 5, Color color = Color::Black);

// Draws a Neo-Brutalist card with a 45-degree halftone or solid drop shadow,
// crisp 2px border, and optional corner tech brackets.
void drawCard(DrawTarget& t, Rect r, uint8_t radius = 8, int16_t shadow = 4, bool inverted = false,
              bool techCorners = false);

// Draws a rounded capsule pill badge and returns its width.
int16_t drawPill(DrawTarget& t, int16_t x, int16_t y, int16_t height, const char* text, FontSlot slot,
                 bool inverted = true, int16_t padH = 10);

// Draws a segmented cyber telemetry bar [■■■■□□□□].
void drawSegmentedBar(DrawTarget& t, Rect r, uint8_t pct, uint8_t segments = 10, bool onDark = false);

// Draws a smooth progress bar with double border and dithered track.
void drawProgressBar(DrawTarget& t, Rect r, uint8_t pct, bool onDark = false);

// Draws a filled circle and/or ring on the target.
void fillCircle(DrawTarget& t, int16_t cx, int16_t cy, int16_t r, Color color);
void strokeCircle(DrawTarget& t, int16_t cx, int16_t cy, int16_t r, int16_t thickness, Color color);

// Draws the unified 38px Futuristic Top HUD Bar.
void drawTopHud(DrawTarget& t, const DeviceStatus& status, const PetEngine& pet, const char* sectionTag);

// Draws the unified 36px Tactile Keycap Bottom Bar.
void drawBottomKeyBar(DrawTarget& t, const char* k1, const char* l1, const char* k2, const char* l2,
                      const char* k3 = nullptr, const char* l3 = nullptr);

// Draws the complete animated/contextual Habitat scene with the Inkling creature inside `r`.
void drawHabitatScene(DrawTarget& t, Rect r, const PetEngine& pet, const DeviceStatus& status, bool largeMode);

// Draws just the Inkling sprite centered at (cx, cy) with scale (1 = mini, 2 = home, 3 = sanctuary/sleep).
void drawInklingSprite(DrawTarget& t, int16_t cx, int16_t cy, int16_t scale, PetStage stage, PetMood mood,
                       uint8_t animFrame, bool onDark = false);

// Draws a 64x92 book cover thumbnail: uses `coverBits` (64x92 1bpp, 736 bytes) if non-null,
// otherwise renders a procedural editorial hardcover based on `title` and `author`.
static constexpr uint16_t kCoverW = 64;
static constexpr uint16_t kCoverH = 92;
static constexpr uint16_t kCoverBytes = (kCoverW / 8) * kCoverH;  // 736 bytes
void drawBookCover(DrawTarget& t, int16_t x, int16_t y, const char* title, const char* author,
                   const uint8_t* coverBits = nullptr);

// Mini 16x16 pixel icons.
enum class Icon : uint8_t { Book, Flame, Crystal, Heart, Drop, Spark, Compass, Sliders, Wifi, Moon, Check };
void drawIcon16(DrawTarget& t, int16_t x, int16_t y, Icon icon, Color color = Color::Black);

}  // namespace canvas
}  // namespace ui
}  // namespace inkling
