#include "CyberCanvas.h"

#include <cstdio>
#include <cstring>

namespace inkling {
namespace ui {
namespace canvas {

namespace {

uint8_t* g_fb = nullptr;
int16_t g_pw = 792;
int16_t g_ph = 528;
int16_t g_pwb = 99;
freeink::ui::Orientation g_orientation = freeink::ui::Orientation::Portrait;

int16_t isqrt32(int32_t n) {
  if (n <= 0) return 0;
  int32_t x = n;
  int32_t y = (x + 1) >> 1;
  while (y < x) {
    x = y;
    y = (x + n / x) >> 1;
  }
  return static_cast<int16_t>(x);
}

// ---------------------------------------------------------------------------
// Orientation-generic span blitter.
//
// Logical (x, y) -> panel (px, py), identical to FreeInkUI's DisplayTarget:
//   Portrait                  (0°)   px = y,          py = ph-1-x
//   PortraitInverted          (180°) px = pw-1-y,     py = x
//   LandscapeClockwise        (90°)  px = pw-1-x,     py = ph-1-y
//   LandscapeCounterClockwise (270°) px = x,          py = y   (panel native)
//
// In the portrait pair a logical COLUMN is a contiguous run of a panel row; in
// the landscape pair a logical ROW is. Filled shapes are swept along whichever
// logical axis gives contiguous runs, so every orientation gets byte/memset
// speed. The other axis falls back to one bit per panel row (only used for
// thin features: circles, 1 px lines).
// ---------------------------------------------------------------------------

bool isPortraitOrient() {
  return g_orientation == freeink::ui::Orientation::Portrait ||
         g_orientation == freeink::ui::Orientation::PortraitInverted;
}

int16_t logicalW() { return isPortraitOrient() ? g_ph : g_pw; }
int16_t logicalH() { return isPortraitOrient() ? g_pw : g_ph; }

constexpr uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

uint8_t ditherThreshold(Color c) {
  switch (c) {
    case Color::DarkGray:
      return 12;
    case Color::LightGray:
      return 4;
    default:
      return 8;  // Gray / anything else: 50 %
  }
}

// Ink bits of one panel byte (8 pixels) on panel row `py` for a Bayer threshold.
// The 4x4 matrix repeats twice per byte, so the mask only depends on the row.
uint8_t panelInkMask(int16_t py, uint8_t threshold) {
  const uint8_t* row = kBayer[py & 3];
  uint8_t m = 0;
  for (int b = 0; b < 8; ++b) {
    if (row[b & 3] < threshold) m = static_cast<uint8_t>(m | (0x80u >> b));
  }
  return m;
}

// Contiguous run [px0, px1) on panel row `py`.
void panelRun(int16_t py, int16_t px0, int16_t px1, Color color) {
  if (py < 0 || py >= g_ph) return;
  if (px0 < 0) px0 = 0;
  if (px1 > g_pw) px1 = g_pw;
  if (px0 >= px1) return;
  uint8_t* const row = g_fb + static_cast<int32_t>(py) * g_pwb;
  const int16_t b0 = static_cast<int16_t>(px0 >> 3);
  const int16_t b1 = static_cast<int16_t>((px1 - 1) >> 3);
  uint8_t m0 = static_cast<uint8_t>(0xFFu >> (px0 & 7));
  uint8_t m1 = static_cast<uint8_t>(0xFFu << (7 - ((px1 - 1) & 7)));
  if (b0 == b1) {
    m0 = static_cast<uint8_t>(m0 & m1);
  }

  if (color == Color::Black) {
    row[b0] = static_cast<uint8_t>(row[b0] & ~m0);
    if (b0 != b1) {
      if (b1 - b0 > 1) memset(row + b0 + 1, 0x00, static_cast<size_t>(b1 - b0 - 1));
      row[b1] = static_cast<uint8_t>(row[b1] & ~m1);
    }
    return;
  }
  if (color == Color::White) {
    row[b0] = static_cast<uint8_t>(row[b0] | m0);
    if (b0 != b1) {
      if (b1 - b0 > 1) memset(row + b0 + 1, 0xFF, static_cast<size_t>(b1 - b0 - 1));
      row[b1] = static_cast<uint8_t>(row[b1] | m1);
    }
    return;
  }
  // Dithered gray: ink only, the background shows through (same as DisplayTarget).
  const uint8_t ink = panelInkMask(py, ditherThreshold(color));
  if (ink == 0) return;
  row[b0] = static_cast<uint8_t>(row[b0] & ~(m0 & ink));
  if (b0 != b1) {
    const uint8_t keep = static_cast<uint8_t>(~ink);
    for (int16_t b = static_cast<int16_t>(b0 + 1); b < b1; ++b) row[b] = static_cast<uint8_t>(row[b] & keep);
    row[b1] = static_cast<uint8_t>(row[b1] & ~(m1 & ink));
  }
}

// One-pixel-wide column [py0, py1) at panel x `px` (one bit per row).
void panelCol(int16_t px, int16_t py0, int16_t py1, Color color) {
  if (px < 0 || px >= g_pw) return;
  if (py0 < 0) py0 = 0;
  if (py1 > g_ph) py1 = g_ph;
  if (py0 >= py1) return;
  const uint8_t bit = static_cast<uint8_t>(0x80u >> (px & 7));
  uint8_t* p = g_fb + static_cast<int32_t>(py0) * g_pwb + (px >> 3);
  if (color == Color::Black) {
    for (int16_t y = py0; y < py1; ++y, p += g_pwb) *p = static_cast<uint8_t>(*p & ~bit);
  } else if (color == Color::White) {
    for (int16_t y = py0; y < py1; ++y, p += g_pwb) *p = static_cast<uint8_t>(*p | bit);
  } else {
    const uint8_t thr = ditherThreshold(color);
    for (int16_t y = py0; y < py1; ++y, p += g_pwb) {
      if (kBayer[y & 3][px & 3] < thr) *p = static_cast<uint8_t>(*p & ~bit);
    }
  }
}

// Logical vertical span y in [y0, y1) at column x.
void fillSpanY(int16_t x, int16_t y0, int16_t y1, Color color) {
  if (!g_fb || color == Color::Transparent) return;
  const int16_t lw = logicalW();
  const int16_t lh = logicalH();
  if (x < 0 || x >= lw) return;
  if (y0 < 0) y0 = 0;
  if (y1 > lh) y1 = lh;
  if (y0 >= y1) return;
  switch (g_orientation) {
    case freeink::ui::Orientation::Portrait:
      panelRun(static_cast<int16_t>(g_ph - 1 - x), y0, y1, color);
      break;
    case freeink::ui::Orientation::PortraitInverted:
      panelRun(x, static_cast<int16_t>(g_pw - y1), static_cast<int16_t>(g_pw - y0), color);
      break;
    case freeink::ui::Orientation::LandscapeClockwise:
      panelCol(static_cast<int16_t>(g_pw - 1 - x), static_cast<int16_t>(g_ph - y1), static_cast<int16_t>(g_ph - y0),
               color);
      break;
    case freeink::ui::Orientation::LandscapeCounterClockwise:
    default:
      panelCol(x, y0, y1, color);
      break;
  }
}

// Logical horizontal span x in [x0, x1) on row y.
void fillSpanX(int16_t y, int16_t x0, int16_t x1, Color color) {
  if (!g_fb || color == Color::Transparent) return;
  const int16_t lw = logicalW();
  const int16_t lh = logicalH();
  if (y < 0 || y >= lh) return;
  if (x0 < 0) x0 = 0;
  if (x1 > lw) x1 = lw;
  if (x0 >= x1) return;
  switch (g_orientation) {
    case freeink::ui::Orientation::Portrait:
      panelCol(y, static_cast<int16_t>(g_ph - x1), static_cast<int16_t>(g_ph - x0), color);
      break;
    case freeink::ui::Orientation::PortraitInverted:
      panelCol(static_cast<int16_t>(g_pw - 1 - y), x0, x1, color);
      break;
    case freeink::ui::Orientation::LandscapeClockwise:
      panelRun(static_cast<int16_t>(g_ph - 1 - y), static_cast<int16_t>(g_pw - x1), static_cast<int16_t>(g_pw - x0),
               color);
      break;
    case freeink::ui::Orientation::LandscapeCounterClockwise:
    default:
      panelRun(y, x0, x1, color);
      break;
  }
}

// Clips a rounded rectangle to its span at column x (portrait sweep).
void roundedSpanAtX(const Rect& rect, int16_t r, int16_t x, int16_t& y0, int16_t& y1) {
  y0 = rect.y;
  y1 = rect.bottom();
  if (r <= 0) return;
  const int16_t halfW = static_cast<int16_t>(rect.width / 2);
  const int16_t halfH = static_cast<int16_t>(rect.height / 2);
  if (r > halfW) r = halfW;
  if (r > halfH) r = halfH;
  if (r <= 0) return;

  int16_t dx = 0;
  if (x < rect.x + r) {
    dx = static_cast<int16_t>((rect.x + r) - x);
  } else if (x >= rect.right() - r) {
    dx = static_cast<int16_t>(x - (rect.right() - 1 - r));
  }
  if (dx > 0) {
    const int16_t maxDy = isqrt32(static_cast<int32_t>(r) * r - static_cast<int32_t>(dx) * dx);
    const int16_t inset = static_cast<int16_t>(r - maxDy);
    y0 = static_cast<int16_t>(rect.y + inset);
    y1 = static_cast<int16_t>(rect.bottom() - inset);
  }
}

// Same, transposed: span of row y (landscape sweep).
void roundedSpanAtY(const Rect& rect, int16_t r, int16_t y, int16_t& x0, int16_t& x1) {
  const Rect t{rect.y, rect.x, rect.height, rect.width};
  roundedSpanAtX(t, r, y, x0, x1);
}

// Fills the rounded rect `r` with `color` sweeping the contiguous axis.
void sweepFill(const Rect& r, int16_t radius, Color color) {
  if (isPortraitOrient()) {
    const int16_t xEnd = r.right();
    for (int16_t x = r.x; x < xEnd; ++x) {
      int16_t y0, y1;
      roundedSpanAtX(r, radius, x, y0, y1);
      if (y0 < y1) fillSpanY(x, y0, y1, color);
    }
  } else {
    const int16_t yEnd = r.bottom();
    for (int16_t y = r.y; y < yEnd; ++y) {
      int16_t x0, x1;
      roundedSpanAtY(r, radius, y, x0, x1);
      if (x0 < x1) fillSpanX(y, x0, x1, color);
    }
  }
}

uint32_t hashStr(const char* s) {
  if (!s) return 0;
  uint32_t h = 2166136261u;
  for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
  return h;
}

bool containsNoCase(const char* haystack, const char* needle) {
  if (!haystack || !needle || !*needle) return false;
  const size_t n = strlen(needle);
  for (const char* p = haystack; *p; ++p) {
    bool ok = true;
    for (size_t i = 0; i < n; ++i) {
      char a = p[i];
      char b = needle[i];
      if (!a) return false;
      if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
      if (a != b) {
        ok = false;
        break;
      }
    }
    if (ok) return true;
  }
  return false;
}

void plotPx(DrawTarget& t, int16_t x, int16_t y, Color c) {
  if (g_fb) {
    fillSpanY(x, y, static_cast<int16_t>(y + 1), c);
    return;
  }
  t.fill(Rect{x, y, 1, 1}, Paint::solid(c));
}

void drawStar4(DrawTarget& t, int16_t cx, int16_t cy, int16_t arm, Color c) {
  fastFill(t, Rect{static_cast<int16_t>(cx - arm), cy, static_cast<int16_t>(arm * 2 + 1), 1}, Paint::solid(c));
  fastFill(t, Rect{cx, static_cast<int16_t>(cy - arm), 1, static_cast<int16_t>(arm * 2 + 1)}, Paint::solid(c));
  if (arm >= 3) {
    fastFill(t, Rect{static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy - 1), 3, 3}, Paint::solid(c));
  }
}

void drawDiamond(DrawTarget& t, int16_t cx, int16_t cy, int16_t r, Color c) {
  for (int16_t dx = -r; dx <= r; ++dx) {
    const int16_t span = static_cast<int16_t>(r - (dx < 0 ? -dx : dx));
    if (g_fb) {
      fillSpanY(static_cast<int16_t>(cx + dx), static_cast<int16_t>(cy - span),
                static_cast<int16_t>(cy + span + 1), c);
    } else {
      t.fill(Rect{static_cast<int16_t>(cx + dx), static_cast<int16_t>(cy - span), 1,
                  static_cast<int16_t>(span * 2 + 1)},
             Paint::solid(c));
    }
  }
}

}  // namespace

void bindFramebuffer(uint8_t* fb, int16_t panelWidth, int16_t panelHeight, int16_t panelWidthBytes) {
  g_fb = fb;
  g_pw = panelWidth;
  g_ph = panelHeight;
  g_pwb = panelWidthBytes;
}

void setCanvasOrientation(freeink::ui::Orientation orient) {
  g_orientation = orient;
}

freeink::ui::Orientation canvasOrientation() {
  return g_orientation;
}

int16_t screenW() { return logicalW(); }
int16_t screenH() { return logicalH(); }
bool isLandscape() { return !isPortraitOrient();
}

void clearScreen() {
  if (!g_fb) return;
  memset(g_fb, 0xFF, static_cast<size_t>(g_pwb) * g_ph);
}

void invertScreen() {
  if (!g_fb) return;
  const size_t total = static_cast<size_t>(g_pwb) * g_ph;
  size_t i = 0;
  for (; i + 4 <= total; i += 4) {
    uint32_t w;
    memcpy(&w, g_fb + i, 4);
    w ^= 0xFFFFFFFFu;
    memcpy(g_fb + i, &w, 4);
  }
  for (; i < total; ++i) g_fb[i] ^= 0xFFu;
}

void clearBandY(int16_t y0, int16_t y1) {
  if (!g_fb) return;
  if (y0 < 0) y0 = 0;
  if (y1 > logicalH()) y1 = logicalH();
  if (y0 >= y1) return;
  sweepFill(Rect{0, y0, logicalW(), static_cast<int16_t>(y1 - y0)}, 0, Color::White);
}

void clearRect(Rect r) {
  if (!g_fb || r.empty()) return;
  sweepFill(r, 0, Color::White);
}

void fastFill(DrawTarget& t, Rect r, Paint paint, uint8_t radius) {
  if (r.empty() || paint.kind == freeink::ui::PaintKind::None || paint.color == Color::Transparent) return;
  if (!g_fb || paint.kind == freeink::ui::PaintKind::Bitmap) {
    t.fill(r, paint, radius);
    return;
  }
  sweepFill(r, radius, paint.color);
}

void fastStroke(DrawTarget& t, Rect r, Paint paint, uint8_t width, uint8_t radius) {
  if (r.empty() || width == 0 || paint.kind == freeink::ui::PaintKind::None || paint.color == Color::Transparent) return;
  if (!g_fb) {
    t.stroke(r, paint, width, radius);
    return;
  }
  const Color c = paint.color;
  const Rect inner = r.inset(freeink::ui::Insets{static_cast<int16_t>(width), static_cast<int16_t>(width),
                                                 static_cast<int16_t>(width), static_cast<int16_t>(width)});
  const int16_t innerRadius = static_cast<int16_t>(radius > width ? radius - width : 0);
  if (isPortraitOrient()) {
    for (int16_t x = r.x; x < r.right(); ++x) {
      int16_t o0, o1;
      roundedSpanAtX(r, radius, x, o0, o1);
      if (o0 >= o1) continue;
      if (inner.empty() || x < inner.x || x >= inner.right()) {
        fillSpanY(x, o0, o1, c);
        continue;
      }
      int16_t i0, i1;
      roundedSpanAtX(inner, innerRadius, x, i0, i1);
      if (i0 >= i1) {
        fillSpanY(x, o0, o1, c);
      } else {
        if (o0 < i0) fillSpanY(x, o0, i0, c);
        if (i1 < o1) fillSpanY(x, i1, o1, c);
      }
    }
  } else {
    for (int16_t y = r.y; y < r.bottom(); ++y) {
      int16_t o0, o1;
      roundedSpanAtY(r, radius, y, o0, o1);
      if (o0 >= o1) continue;
      if (inner.empty() || y < inner.y || y >= inner.bottom()) {
        fillSpanX(y, o0, o1, c);
        continue;
      }
      int16_t i0, i1;
      roundedSpanAtY(inner, innerRadius, y, i0, i1);
      if (i0 >= i1) {
        fillSpanX(y, o0, o1, c);
      } else {
        if (o0 < i0) fillSpanX(y, o0, i0, c);
        if (i1 < o1) fillSpanX(y, i1, o1, c);
      }
    }
  }
}

void invertRect(Rect r) {
  if (!g_fb) return;
  const int16_t lw = logicalW();
  const int16_t lh = logicalH();
  int16_t x0 = r.x < 0 ? 0 : r.x;
  int16_t y0 = r.y < 0 ? 0 : r.y;
  int16_t x1 = static_cast<int16_t>(r.x + r.width);
  int16_t y1 = static_cast<int16_t>(r.y + r.height);
  if (x1 > lw) x1 = lw;
  if (y1 > lh) y1 = lh;
  for (int16_t y = y0; y < y1; ++y) {
    for (int16_t x = x0; x < x1; ++x) {
      int16_t px = x, py = y;
      switch (g_orientation) {
        case freeink::ui::Orientation::Portrait:
          px = y;
          py = static_cast<int16_t>(g_ph - 1 - x);
          break;
        case freeink::ui::Orientation::PortraitInverted:
          px = static_cast<int16_t>(g_pw - 1 - y);
          py = x;
          break;
        case freeink::ui::Orientation::LandscapeClockwise:
          px = static_cast<int16_t>(g_pw - 1 - x);
          py = static_cast<int16_t>(g_ph - 1 - y);
          break;
        case freeink::ui::Orientation::LandscapeCounterClockwise:
        default:
          break;
      }
      g_fb[static_cast<int32_t>(py) * g_pwb + (px >> 3)] ^= static_cast<uint8_t>(0x80u >> (px & 7));
    }
  }
}

int drawWrapped(DrawTarget& t, Rect r, const char* text, FontSlot slot, Color color, int firstLine, int* linesFit) {
  static constexpr int16_t kLineHeights[] = {17, 21, 26, 31, 38};
  const int16_t lineH = kLineHeights[slot <= kFontHero ? slot : kFontBody];
  const int fit = r.height / lineH > 0 ? r.height / lineH : 1;
  if (linesFit) *linesFit = fit;
  if (!text || !*text || r.width < 8) return 0;

  char line[160];
  size_t lineLen = 0;
  int lineNo = 0;
  auto flush = [&] {
    line[lineLen] = '\0';
    if (lineNo >= firstLine && lineNo < firstLine + fit && lineLen > 0) {
      drawText(t, Rect{r.x, static_cast<int16_t>(r.y + (lineNo - firstLine) * lineH), r.width, lineH}, line, slot,
               color, TextAlign::Left, false);
    }
    ++lineNo;
    lineLen = 0;
  };

  const char* p = text;
  while (*p) {
    if (*p == '\n') {
      flush();
      ++p;
      continue;
    }
    if (*p == ' ') {
      ++p;
      continue;
    }
    const char* wordEnd = p;
    while (*wordEnd && *wordEnd != ' ' && *wordEnd != '\n') ++wordEnd;
    size_t wordLen = static_cast<size_t>(wordEnd - p);
    if (wordLen > 60) {  // an unbreakable run: cut it, on a character boundary
      wordLen = 60;
      while (wordLen > 1 && (static_cast<uint8_t>(p[wordLen]) & 0xC0) == 0x80) --wordLen;
    }
    // Would the line still fit with this word added?
    const size_t withSpace = lineLen ? lineLen + 1 : 0;
    if (withSpace + wordLen + 1 < sizeof(line)) {
      const size_t before = lineLen;
      if (lineLen) line[lineLen++] = ' ';
      memcpy(line + lineLen, p, wordLen);
      lineLen += wordLen;
      line[lineLen] = '\0';
      if (before > 0 && textWidth(t, line, slot) > r.width) {
        lineLen = before;
        flush();
        memcpy(line, p, wordLen);
        lineLen = wordLen;
      }
    } else {
      flush();
      memcpy(line, p, wordLen);
      lineLen = wordLen;
    }
    p += wordLen;
  }
  if (lineLen > 0) flush();
  return lineNo;
}

void drawText(DrawTarget& t, Rect r, const char* str, FontSlot slot, Color color, TextAlign align, bool ellipsis) {
  (void)ellipsis;
  if (!str || !*str || r.empty()) return;
  TextStyle style;
  style.font = static_cast<freeink::ui::FontId>(slot);
  style.color = color;
  style.align = align;
  style.maxLines = 1;
  t.text(r, str, style);
}

int16_t textWidth(DrawTarget& t, const char* str, FontSlot slot) {
  if (!str || !*str) return 0;
  TextStyle style;
  style.font = static_cast<freeink::ui::FontId>(slot);
  return t.measureText(style.font, str, style).width;
}

FontSlot fitFont(DrawTarget& t, const char* str, int16_t width, FontSlot preferred) {
  static constexpr FontSlot kOrder[] = {kFontTitle, kFontBody, kFontSmall, kFontMicro};
  bool started = false;
  for (FontSlot f : kOrder) {
    if (f == preferred) started = true;
    if (started && textWidth(t, str, f) <= width) return f;
  }
  return kFontMicro;
}

void fillDotGrid(DrawTarget& t, Rect r, int16_t step, Color dotColor) {
  if (step < 2) step = 2;
  for (int16_t y = static_cast<int16_t>(r.y + 2); y < r.bottom() - 1; y = static_cast<int16_t>(y + step)) {
    for (int16_t x = static_cast<int16_t>(r.x + 2); x < r.right() - 1; x = static_cast<int16_t>(x + step)) {
      plotPx(t, x, y, dotColor);
    }
  }
}

void fillScanlines(DrawTarget& t, Rect r, int16_t period, Color color) {
  if (period < 2) period = 2;
  for (int16_t y = r.y; y < r.bottom(); ++y) {
    for (int16_t x = r.x; x < r.right(); ++x) {
      if (((x + y) % period) == 0) plotPx(t, x, y, color);
    }
  }
}

void fillCircle(DrawTarget& t, int16_t cx, int16_t cy, int16_t r, Color color) {
  if (r <= 0) return;
  const int32_t r2 = static_cast<int32_t>(r) * r;
  for (int16_t dx = -r; dx <= r; ++dx) {
    const int16_t span = isqrt32(r2 - static_cast<int32_t>(dx) * dx);
    if (g_fb) {
      fillSpanY(static_cast<int16_t>(cx + dx), static_cast<int16_t>(cy - span),
                static_cast<int16_t>(cy + span + 1), color);
    } else {
      t.fill(Rect{static_cast<int16_t>(cx + dx), static_cast<int16_t>(cy - span), 1,
                  static_cast<int16_t>(span * 2 + 1)},
             Paint::solid(color));
    }
  }
}

void strokeCircle(DrawTarget& t, int16_t cx, int16_t cy, int16_t r, int16_t thickness, Color color) {
  if (r <= 0 || thickness <= 0) return;
  const int16_t rIn = static_cast<int16_t>(r > thickness ? r - thickness : 0);
  const int32_t out2 = static_cast<int32_t>(r) * r;
  const int32_t in2 = static_cast<int32_t>(rIn) * rIn;
  for (int16_t dx = -r; dx <= r; ++dx) {
    const int32_t dx2 = static_cast<int32_t>(dx) * dx;
    const int16_t outSpan = isqrt32(out2 - dx2);
    const int16_t x = static_cast<int16_t>(cx + dx);
    if (dx2 >= in2 || rIn == 0) {
      if (g_fb) {
        fillSpanY(x, static_cast<int16_t>(cy - outSpan), static_cast<int16_t>(cy + outSpan + 1), color);
      } else {
        t.fill(Rect{x, static_cast<int16_t>(cy - outSpan), 1, static_cast<int16_t>(outSpan * 2 + 1)},
               Paint::solid(color));
      }
    } else {
      const int16_t inSpan = isqrt32(in2 - dx2);
      if (g_fb) {
        fillSpanY(x, static_cast<int16_t>(cy - outSpan), static_cast<int16_t>(cy - inSpan + 1), color);
        fillSpanY(x, static_cast<int16_t>(cy + inSpan), static_cast<int16_t>(cy + outSpan + 1), color);
      } else {
        t.fill(Rect{x, static_cast<int16_t>(cy - outSpan), 1, static_cast<int16_t>(outSpan - inSpan + 1)},
               Paint::solid(color));
        t.fill(Rect{x, static_cast<int16_t>(cy + inSpan), 1, static_cast<int16_t>(outSpan - inSpan + 1)},
               Paint::solid(color));
      }
    }
  }
}

void drawCard(DrawTarget& t, Rect r, uint8_t radius, int16_t shadow, bool inverted, bool techCorners) {
  if (r.empty()) return;
  if (shadow > 0) {
    const Rect sr{static_cast<int16_t>(r.x + shadow), static_cast<int16_t>(r.y + shadow), r.width, r.height};
    fastFill(t, sr, Paint::dither(Color::DarkGray), radius);
  }
  fastFill(t, r, Paint::solid(inverted ? Color::Black : Color::White), radius);
  fastStroke(t, r, Paint::solid(Color::Black), 2, radius);

  if (techCorners && r.width > 24 && r.height > 24) {
    const Color c = inverted ? Color::White : Color::Black;
    const int16_t inset = 5;
    const int16_t arm = 6;
    // Top-left & bottom-right micro registration marks
    fastFill(t, Rect{static_cast<int16_t>(r.x + inset), static_cast<int16_t>(r.y + inset), static_cast<int16_t>(arm + 1), 1},
             Paint::solid(c));
    fastFill(t, Rect{static_cast<int16_t>(r.x + inset), static_cast<int16_t>(r.y + inset), 1, static_cast<int16_t>(arm + 1)},
             Paint::solid(c));

    fastFill(t,
             Rect{static_cast<int16_t>(r.right() - 1 - inset - arm), static_cast<int16_t>(r.bottom() - 1 - inset),
                  static_cast<int16_t>(arm + 1), 1},
             Paint::solid(c));
    fastFill(t,
             Rect{static_cast<int16_t>(r.right() - 1 - inset), static_cast<int16_t>(r.bottom() - 1 - inset - arm), 1,
                  static_cast<int16_t>(arm + 1)},
             Paint::solid(c));
  }
}

int16_t drawPill(DrawTarget& t, int16_t x, int16_t y, int16_t height, const char* text, FontSlot slot, bool inverted,
                 int16_t padH) {
  const int16_t tw = textWidth(t, text, slot);
  const int16_t w = static_cast<int16_t>(tw + padH * 2);
  const Rect r{x, y, w, height};
  const uint8_t rad = static_cast<uint8_t>(height / 2);
  fastFill(t, r, Paint::solid(inverted ? Color::Black : Color::White), rad);
  fastStroke(t, r, Paint::solid(inverted ? Color::White : Color::Black), 1, rad);
  drawText(t, r, text, slot, inverted ? Color::White : Color::Black, TextAlign::Center, false);
  return w;
}

void drawSegmentedBar(DrawTarget& t, Rect r, uint8_t pct, uint8_t segments, bool onDark) {
  if (r.empty() || segments == 0) return;
  if (pct > 100) pct = 100;
  const Color fg = onDark ? Color::White : Color::Black;
  const Color bg = onDark ? Color::Black : Color::White;

  fastFill(t, r, Paint::solid(bg), 3);
  fastStroke(t, r, Paint::solid(fg), 1, 3);

  const int16_t innerW = static_cast<int16_t>(r.width - 6);
  const int16_t innerH = static_cast<int16_t>(r.height - 6);
  if (innerW <= segments || innerH <= 1) return;

  const int16_t gap = 2;
  const int16_t segW = static_cast<int16_t>((innerW - (segments - 1) * gap) / segments);
  const uint8_t activeSegs = static_cast<uint8_t>((pct * segments + 49) / 100);

  for (uint8_t i = 0; i < segments; ++i) {
    const int16_t sx = static_cast<int16_t>(r.x + 3 + i * (segW + gap));
    const Rect sr{sx, static_cast<int16_t>(r.y + 3), segW, innerH};
    if (i < activeSegs) {
      fastFill(t, sr, Paint::solid(fg));
    } else {
      fastFill(t, sr, Paint::dither(Color::LightGray));
    }
  }
}

void drawProgressBar(DrawTarget& t, Rect r, uint8_t pct, bool onDark) {
  if (r.empty()) return;
  if (pct > 100) pct = 100;
  const Color fg = onDark ? Color::White : Color::Black;
  const Color bg = onDark ? Color::Black : Color::White;
  const uint8_t rad = static_cast<uint8_t>(r.height / 2);

  fastFill(t, r, Paint::solid(bg), rad);
  fastStroke(t, r, Paint::solid(fg), 2, rad);

  const int16_t maxFill = static_cast<int16_t>(r.width - 6);
  const int16_t fillW = static_cast<int16_t>((maxFill * pct) / 100);
  if (fillW > 0) {
    const Rect fr{static_cast<int16_t>(r.x + 3), static_cast<int16_t>(r.y + 3), fillW,
                  static_cast<int16_t>(r.height - 6)};
    fastFill(t, fr, Paint::solid(fg), static_cast<uint8_t>(rad > 2 ? rad - 2 : 0));
  }
}

void drawIcon16(DrawTarget& t, int16_t x, int16_t y, Icon icon, Color color) {
  const Paint p = Paint::solid(color);
  switch (icon) {
    case Icon::Book:
      fastStroke(t, Rect{static_cast<int16_t>(x + 1), static_cast<int16_t>(y + 2), 14, 12}, p, 2, 2);
      fastFill(t, Rect{static_cast<int16_t>(x + 7), static_cast<int16_t>(y + 2), 2, 12}, p);
      break;
    case Icon::Flame:
      drawDiamond(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 9), 5, color);
      t.triangle(Point{static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 1)},
                 Point{static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 9)},
                 Point{static_cast<int16_t>(x + 13), static_cast<int16_t>(y + 9)}, p);
      fastFill(t, Rect{static_cast<int16_t>(x + 7), static_cast<int16_t>(y + 9), 3, 3},
               Paint::solid(color == Color::Black ? Color::White : Color::Black));
      break;
    case Icon::Crystal:
      drawDiamond(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 8), 6, color);
      drawDiamond(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 8), 2,
                  color == Color::Black ? Color::White : Color::Black);
      break;
    case Icon::Heart:
      fillCircle(t, static_cast<int16_t>(x + 5), static_cast<int16_t>(y + 6), 3, color);
      fillCircle(t, static_cast<int16_t>(x + 11), static_cast<int16_t>(y + 6), 3, color);
      t.triangle(Point{static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 7)},
                 Point{static_cast<int16_t>(x + 14), static_cast<int16_t>(y + 7)},
                 Point{static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 14)}, p);
      break;
    case Icon::Drop:
      fillCircle(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 10), 4, color);
      t.triangle(Point{static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 2)},
                 Point{static_cast<int16_t>(x + 4), static_cast<int16_t>(y + 10)},
                 Point{static_cast<int16_t>(x + 12), static_cast<int16_t>(y + 10)}, p);
      break;
    case Icon::Spark:
      drawStar4(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 8), 6, color);
      break;
    case Icon::Compass:
      strokeCircle(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 8), 6, 2, color);
      drawDiamond(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 8), 2, color);
      break;
    case Icon::Sliders:
      for (int i = 0; i < 3; ++i) {
        const int16_t ry = static_cast<int16_t>(y + 3 + i * 5);
        fastFill(t, Rect{static_cast<int16_t>(x + 2), static_cast<int16_t>(ry - 1), 13, 2}, p);
        const int16_t kx = static_cast<int16_t>(x + (i == 1 ? 10 : 5));
        fillCircle(t, kx, ry, 2, color);
      }
      break;
    case Icon::Wifi:
      strokeCircle(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 12), 8, 2, color);
      strokeCircle(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 12), 4, 2, color);
      fillCircle(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 12), 2, color);
      break;
    case Icon::Moon:
      fillCircle(t, static_cast<int16_t>(x + 8), static_cast<int16_t>(y + 8), 6, color);
      fillCircle(t, static_cast<int16_t>(x + 11), static_cast<int16_t>(y + 6), 5,
                 color == Color::Black ? Color::White : Color::Black);
      break;
    case Icon::Check:
      t.line(Point{static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 8)},
             Point{static_cast<int16_t>(x + 7), static_cast<int16_t>(y + 12)}, 2, p);
      t.line(Point{static_cast<int16_t>(x + 7), static_cast<int16_t>(y + 12)},
             Point{static_cast<int16_t>(x + 13), static_cast<int16_t>(y + 4)}, 2, p);
      break;
  }
}

void drawTopHud(DrawTarget& t, const DeviceStatus& status, const char* sectionTag) {
  const int16_t w = screenW();
  const Rect bar{0, 0, w, 40};
  fastFill(t, bar, Paint::solid(Color::Black));

  // Left brand emblem + active section pill
  drawDiamond(t, 16, 20, 5, Color::White);
  plotPx(t, 16, 20, Color::Black);
  drawText(t, Rect{26, 4, 110, 32}, "INKLING", kFontBody, Color::White, TextAlign::Left, false);

  if (sectionTag && *sectionTag) {
    drawPill(t, 118, 8, 24, sectionTag, kFontMicro, false, 10);
  }

  // Right cluster: [clock] [battery]
  const int16_t clockX = static_cast<int16_t>(w - 74 - 68);
  char clockBuf[16];
  if (status.clockValid) {
    snprintf(clockBuf, sizeof(clockBuf), "%02u:%02u", status.hour, status.minute);
  } else {
    snprintf(clockBuf, sizeof(clockBuf), "--:--");
  }
  drawText(t, Rect{clockX, 6, 58, 28}, clockBuf, kFontMicro, Color::White, TextAlign::Right, false);

  // Graphical Battery Capsule
  const int16_t bx = static_cast<int16_t>(w - 74);
  const int16_t by = 11;
  const Rect battRect{bx, by, 34, 18};
  fastStroke(t, battRect, Paint::solid(Color::White), 2, 3);
  fastFill(t, Rect{static_cast<int16_t>(bx + 34), static_cast<int16_t>(by + 5), 3, 8}, Paint::solid(Color::White));

  const bool battKnown = status.batteryPercent >= 0 && status.batteryPercent <= 100;
  const int16_t pct = battKnown ? status.batteryPercent : 0;
  const int16_t fillW = static_cast<int16_t>((26 * pct) / 100);
  if (fillW > 0) {
    fastFill(t, Rect{static_cast<int16_t>(bx + 4), static_cast<int16_t>(by + 4), fillW, 10}, Paint::solid(Color::White));
  }
  char battTxt[12];
  if (battKnown) {
    snprintf(battTxt, sizeof(battTxt), "%d%%", static_cast<int>(pct));
  } else {
    snprintf(battTxt, sizeof(battTxt), "--");
  }
  drawText(t, Rect{static_cast<int16_t>(bx + 39), 6, 34, 28}, battTxt, kFontMicro, Color::White, TextAlign::Left,
           false);
}

void drawBottomKeyBar(DrawTarget& t, const char* k1, const char* l1, const char* k2, const char* l2, const char* k3,
                      const char* l3) {
  const int16_t w = screenW();
  const int16_t h = screenH();
  const int16_t barY = static_cast<int16_t>(h - 38);
  const Rect bar{0, barY, w, 38};

  fastFill(t, bar, Paint::solid(Color::White));
  fastFill(t, Rect{0, barY, w, 2}, Paint::solid(Color::Black));

  auto drawHint = [&](int16_t x, const char* key, const char* label) -> int16_t {
    if (!key || !label) return x;
    const int16_t kw = drawPill(t, x, static_cast<int16_t>(barY + 7), 24, key, kFontMicro, true, 8);
    const int16_t lx = static_cast<int16_t>(x + kw + 6);
    const int16_t lw = textWidth(t, label, kFontSmall);
    drawText(t, Rect{lx, static_cast<int16_t>(barY + 5), static_cast<int16_t>(lw + 8), 28}, label, kFontSmall,
             Color::Black, TextAlign::Left, false);
    return static_cast<int16_t>(lx + lw + 18);
  };

  int16_t cx = 14;
  cx = drawHint(cx, k1, l1);
  cx = drawHint(cx, k2, l2);
  if (k3 && l3) {
    const int16_t kw = textWidth(t, k3, kFontMicro) + 16;
    const int16_t lw = textWidth(t, l3, kFontSmall);
    const int16_t rx = static_cast<int16_t>(w - 14 - kw - 6 - lw);
    drawHint(rx > cx ? rx : cx, k3, l3);
  }
}

void drawBookCover(DrawTarget& t, int16_t x, int16_t y, const char* title, const char* author,
                   const uint8_t* coverBits) {
  const Rect r{x, y, kCoverW, kCoverH};
  // Hard shadow
  fastFill(t, Rect{static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 3), kCoverW, kCoverH},
           Paint::solid(Color::Black), 4);
  fastFill(t, r, Paint::solid(Color::White), 4);
  fastStroke(t, r, Paint::solid(Color::Black), 2, 4);

  if (coverBits != nullptr) {
    // Render real cached 1-bit cover thumbnail inside the frame (1:1 pixel blitting)
    freeink::ui::BitmapRef ref{};
    ref.data = coverBits;
    ref.width = kCoverW;
    ref.height = kCoverH;
    ref.format = freeink::ui::BitmapFormat::BW1;
    t.bitmap(r, ref, freeink::ui::BitmapMode::Center, Paint::solid(Color::Black));
    fastStroke(t, r, Paint::solid(Color::Black), 2, 4);
    return;
  }

  // Procedural Editorial Hardcover
  // Book spine rib on the left (7px)
  fastFill(t, Rect{static_cast<int16_t>(x + 2), static_cast<int16_t>(y + 2), 7, static_cast<int16_t>(kCoverH - 4)},
           Paint::solid(Color::Black));
  for (int16_t ry = static_cast<int16_t>(y + 12); ry < y + kCoverH - 10; ry += 16) {
    fastFill(t, Rect{static_cast<int16_t>(x + 3), ry, 5, 1}, Paint::solid(Color::White));
  }

  const int16_t artX = static_cast<int16_t>(x + 12);
  const int16_t artW = static_cast<int16_t>(kCoverW - 16);
  const int16_t cx = static_cast<int16_t>(artX + artW / 2);
  const int16_t cy = static_cast<int16_t>(y + 38);

  // Choose motif by title/topic or hash
  const bool alpine = containsNoCase(title, "k2") || containsNoCase(title, "aria") || containsNoCase(title, "morte") ||
                      containsNoCase(author, "bonatti") || containsNoCase(author, "krakauer") ||
                      containsNoCase(author, "simpson");
  const bool cyber = containsNoCase(title, "linux") || containsNoCase(title, "hack") || containsNoCase(title, "gnu");
  const uint32_t h = hashStr(title);

  if (alpine) {
    // Twin mountain peaks + sun
    fillCircle(t, static_cast<int16_t>(cx + 10), static_cast<int16_t>(y + 20), 6, Color::Black);
    t.triangle(Point{static_cast<int16_t>(artX + 2), static_cast<int16_t>(y + 58)},
               Point{static_cast<int16_t>(cx - 4), static_cast<int16_t>(y + 20)},
               Point{static_cast<int16_t>(artX + artW - 6), static_cast<int16_t>(y + 58)}, Paint::solid(Color::Black));
    t.triangle(Point{static_cast<int16_t>(cx - 2), static_cast<int16_t>(y + 58)},
               Point{static_cast<int16_t>(cx + 10), static_cast<int16_t>(y + 28)},
               Point{static_cast<int16_t>(artX + artW - 2), static_cast<int16_t>(y + 58)}, Paint::dither(Color::DarkGray));
  } else if (cyber) {
    // Terminal circuit matrix + hex core
    fillDotGrid(t, Rect{artX, static_cast<int16_t>(y + 12), artW, 44}, 5, Color::Black);
    fastFill(t, Rect{static_cast<int16_t>(cx - 12), static_cast<int16_t>(cy - 12), 24, 24},
             Paint::solid(Color::Black), 3);
    drawText(t, Rect{static_cast<int16_t>(cx - 12), static_cast<int16_t>(cy - 12), 24, 24}, ">_", kFontMicro,
             Color::White, TextAlign::Center, false);
  } else if ((h & 1) == 0) {
    // Concentric astrolabe rings
    strokeCircle(t, cx, cy, 17, 2, Color::Black);
    strokeCircle(t, cx, cy, 11, 1, Color::Black);
    drawDiamond(t, cx, cy, 5, Color::Black);
  } else {
    // Geometric op-art diamond emblem
    fillScanlines(t, Rect{artX, static_cast<int16_t>(y + 14), artW, 42}, 4, Color::Black);
    fillCircle(t, cx, cy, 13, Color::White);
    strokeCircle(t, cx, cy, 13, 2, Color::Black);
    drawStar4(t, cx, cy, 7, Color::Black);
  }

  // Bottom title monogram band
  const Rect band{artX, static_cast<int16_t>(y + kCoverH - 26), artW, 20};
  fastFill(t, band, Paint::solid(Color::Black), 2);
  char mono[6] = {};
  if (title && *title) {
    mono[0] = title[0];
    mono[1] = title[1] ? title[1] : '\0';
    mono[2] = title[1] && title[2] ? title[2] : '\0';
    for (int i = 0; mono[i]; ++i) {
      if (mono[i] >= 'a' && mono[i] <= 'z') mono[i] = static_cast<char>(mono[i] - 'a' + 'A');
    }
  }
  drawText(t, band, mono[0] ? mono : "BK", kFontMicro, Color::White, TextAlign::Center, false);
}

}  // namespace canvas
}  // namespace ui
}  // namespace inkling
