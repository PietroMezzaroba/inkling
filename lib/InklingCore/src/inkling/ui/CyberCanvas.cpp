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

uint8_t ditherInkMask(int16_t x, uint8_t threshold) {
  static constexpr uint8_t kBayer[4][4] = {
      {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const int xm = x & 3;
  uint8_t m = 0;
  for (int k = 0; k < 4; ++k) {
    if (kBayer[k][xm] < threshold) {
      m = static_cast<uint8_t>(m | (0x80u >> k) | (0x08u >> k));
    }
  }
  return m;
}

// Fills logical vertical span y in [y0, y1) at column x directly in the portrait framebuffer.
// In Portrait mode (px = y, py = g_ph - 1 - x), a vertical span in logical space is a
// contiguous horizontal byte range in row `py` of `g_fb`!
void fillSpanY(int16_t x, int16_t y0, int16_t y1, Color color) {
  if (!g_fb || color == Color::Transparent) return;
  if (x < 0 || x >= kScreenW) return;
  if (y0 < 0) y0 = 0;
  if (y1 > kScreenH) y1 = kScreenH;
  if (y0 >= y1) return;

  uint8_t* const row = g_fb + static_cast<int32_t>((g_ph - 1) - x) * g_pwb;
  const int16_t b0 = static_cast<int16_t>(y0 >> 3);
  const int16_t b1 = static_cast<int16_t>((y1 - 1) >> 3);
  const uint8_t m0 = static_cast<uint8_t>(0xFFu >> (y0 & 7));
  const uint8_t m1 = static_cast<uint8_t>(0xFFu << (7 - ((y1 - 1) & 7)));

  if (color == Color::Black) {
    if (b0 == b1) {
      row[b0] = static_cast<uint8_t>(row[b0] & ~(m0 & m1));
    } else {
      row[b0] = static_cast<uint8_t>(row[b0] & ~m0);
      if (b1 - b0 > 1) memset(row + b0 + 1, 0x00, static_cast<size_t>(b1 - b0 - 1));
      row[b1] = static_cast<uint8_t>(row[b1] & ~m1);
    }
  } else if (color == Color::White) {
    if (b0 == b1) {
      row[b0] = static_cast<uint8_t>(row[b0] | (m0 & m1));
    } else {
      row[b0] = static_cast<uint8_t>(row[b0] | m0);
      if (b1 - b0 > 1) memset(row + b0 + 1, 0xFF, static_cast<size_t>(b1 - b0 - 1));
      row[b1] = static_cast<uint8_t>(row[b1] | m1);
    }
  } else {
    const uint8_t ink = ditherInkMask(x, color == Color::DarkGray ? 12 : 4);
    if (ink == 0) return;
    if (b0 == b1) {
      row[b0] = static_cast<uint8_t>(row[b0] & ~(m0 & m1 & ink));
    } else {
      row[b0] = static_cast<uint8_t>(row[b0] & ~(m0 & ink));
      const uint8_t keep = static_cast<uint8_t>(~ink);
      for (int16_t b = static_cast<int16_t>(b0 + 1); b < b1; ++b) {
        row[b] = static_cast<uint8_t>(row[b] & keep);
      }
      row[b1] = static_cast<uint8_t>(row[b1] & ~(m1 & ink));
    }
  }
}

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

void plotBlock(DrawTarget& t, int16_t x, int16_t y, int16_t s, Color c) {
  fastFill(t, Rect{x, y, s, s}, Paint::solid(c));
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

void drawEyes(DrawTarget& t, int16_t cx, int16_t cy, int16_t sep, int16_t scale, PetMood mood, uint8_t animFrame,
              Color fg, Color bg) {
  const int16_t lx = static_cast<int16_t>(cx - sep);
  const int16_t rx = static_cast<int16_t>(cx + sep);

  if (mood == PetMood::Sleepy || mood == PetMood::Dormant) {
    // Closed curved sleeping eyelids "-  -"
    fastFill(t, Rect{static_cast<int16_t>(lx - 3 * scale), cy, static_cast<int16_t>(6 * scale), scale},
             Paint::solid(bg));
    fastFill(t, Rect{static_cast<int16_t>(rx - 3 * scale), cy, static_cast<int16_t>(6 * scale), scale},
             Paint::solid(bg));
    fastFill(t,
             Rect{static_cast<int16_t>(lx - 2 * scale), static_cast<int16_t>(cy + scale),
                  static_cast<int16_t>(4 * scale), scale},
             Paint::solid(bg));
    fastFill(t,
             Rect{static_cast<int16_t>(rx - 2 * scale), static_cast<int16_t>(cy + scale),
                  static_cast<int16_t>(4 * scale), scale},
             Paint::solid(bg));
    return;
  }

  if (mood == PetMood::Radiant && (animFrame & 1) == 1) {
    // Happy joyful "^  ^" eyes!
    for (int side = 0; side < 2; ++side) {
      const int16_t ex = side == 0 ? lx : rx;
      t.line(Point{static_cast<int16_t>(ex - 3 * scale), static_cast<int16_t>(cy + scale)},
             Point{ex, static_cast<int16_t>(cy - 2 * scale)}, static_cast<uint8_t>(scale), Paint::solid(bg));
      t.line(Point{ex, static_cast<int16_t>(cy - 2 * scale)},
             Point{static_cast<int16_t>(ex + 3 * scale), static_cast<int16_t>(cy + scale)}, static_cast<uint8_t>(scale),
             Paint::solid(bg));
    }
    return;
  }

  // Luminous big eyes with catchlight spark!
  const int16_t ew = static_cast<int16_t>(3 * scale);
  fillCircle(t, lx, cy, ew, bg);
  fillCircle(t, rx, cy, ew, bg);
  // Inner pupil + white catchlight
  fillCircle(t, lx, cy, static_cast<int16_t>(scale + 1), fg);
  fillCircle(t, rx, cy, static_cast<int16_t>(scale + 1), fg);
  plotBlock(t, static_cast<int16_t>(lx - scale), static_cast<int16_t>(cy - scale), scale, bg);
  plotBlock(t, static_cast<int16_t>(rx - scale), static_cast<int16_t>(cy - scale), scale, bg);
}

}  // namespace

void bindFramebuffer(uint8_t* fb, int16_t panelWidth, int16_t panelHeight, int16_t panelWidthBytes) {
  g_fb = fb;
  g_pw = panelWidth;
  g_ph = panelHeight;
  g_pwb = panelWidthBytes;
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
  if (y1 > kScreenH) y1 = kScreenH;
  if (y0 >= y1) return;
  for (int16_t x = 0; x < kScreenW; ++x) {
    fillSpanY(x, y0, y1, Color::White);
  }
}

void fastFill(DrawTarget& t, Rect r, Paint paint, uint8_t radius) {
  if (r.empty() || paint.kind == freeink::ui::PaintKind::None || paint.color == Color::Transparent) return;
  if (!g_fb || paint.kind == freeink::ui::PaintKind::Bitmap) {
    t.fill(r, paint, radius);
    return;
  }
  const int16_t xEnd = r.right();
  for (int16_t x = r.x; x < xEnd; ++x) {
    int16_t y0, y1;
    roundedSpanAtX(r, radius, x, y0, y1);
    if (y0 < y1) fillSpanY(x, y0, y1, paint.color);
  }
}

void fastStroke(DrawTarget& t, Rect r, Paint paint, uint8_t width, uint8_t radius) {
  if (r.empty() || width == 0 || paint.kind == freeink::ui::PaintKind::None || paint.color == Color::Transparent) return;
  if (!g_fb) {
    t.stroke(r, paint, width, radius);
    return;
  }
  const Rect inner = r.inset(freeink::ui::Insets{static_cast<int16_t>(width), static_cast<int16_t>(width),
                                                 static_cast<int16_t>(width), static_cast<int16_t>(width)});
  const int16_t xEnd = r.right();
  for (int16_t x = r.x; x < xEnd; ++x) {
    int16_t outY0, outY1;
    roundedSpanAtX(r, radius, x, outY0, outY1);
    if (outY0 >= outY1) continue;
    if (inner.empty() || x < inner.x || x >= inner.right()) {
      fillSpanY(x, outY0, outY1, paint.color);
    } else {
      int16_t inY0, inY1;
      roundedSpanAtX(inner, radius, x, inY0, inY1);
      if (inY0 >= inY1) {
        fillSpanY(x, outY0, outY1, paint.color);
      } else {
        if (outY0 < inY0) fillSpanY(x, outY0, inY0, paint.color);
        if (inY1 < outY1) fillSpanY(x, inY1, outY1, paint.color);
      }
    }
  }
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

void fillDotGrid(DrawTarget& t, Rect r, int16_t step, Color dotColor) {
  if (step < 2) step = 2;
  if (g_fb && dotColor == Color::Black) {
    const int16_t xEnd = static_cast<int16_t>(r.right() - 1);
    const int16_t yEnd = static_cast<int16_t>(r.bottom() - 1);
    for (int16_t x = static_cast<int16_t>(r.x + 2); x < xEnd; x = static_cast<int16_t>(x + step)) {
      if (x < 0 || x >= kScreenW) continue;
      uint8_t* const row = g_fb + static_cast<int32_t>((g_ph - 1) - x) * g_pwb;
      for (int16_t y = static_cast<int16_t>(r.y + 2); y < yEnd; y = static_cast<int16_t>(y + step)) {
        if (y >= 0 && y < kScreenH) {
          row[y >> 3] = static_cast<uint8_t>(row[y >> 3] & ~(0x80u >> (y & 7)));
        }
      }
    }
    return;
  }
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

void drawTopHud(DrawTarget& t, const DeviceStatus& status, const PetEngine& pet, const char* sectionTag) {
  const int16_t w = kScreenW;
  const Rect bar{0, 0, w, 40};
  fastFill(t, bar, Paint::solid(Color::Black));

  // Left brand emblem + active section pill
  drawDiamond(t, 16, 20, 5, Color::White);
  plotPx(t, 16, 20, Color::Black);
  drawText(t, Rect{26, 4, 110, 32}, "INKLING", kFontBody, Color::White, TextAlign::Left, false);

  if (sectionTag && *sectionTag) {
    drawPill(t, 118, 8, 24, sectionTag, kFontMicro, false, 10);
  }

  // Center-right telemetry: Streak & Ink Crystals
  char streakBuf[20];
  snprintf(streakBuf, sizeof(streakBuf), "%ug", static_cast<unsigned>(pet.state().streakDays));
  drawIcon16(t, 260, 12, Icon::Flame, Color::White);
  drawText(t, Rect{278, 6, 44, 28}, streakBuf, kFontMicro, Color::White, TextAlign::Left, false);

  char crysBuf[20];
  snprintf(crysBuf, sizeof(crysBuf), "%u", static_cast<unsigned>(pet.state().inkCrystals));
  drawIcon16(t, 322, 12, Icon::Crystal, Color::White);
  drawText(t, Rect{340, 6, 42, 28}, crysBuf, kFontMicro, Color::White, TextAlign::Left, false);

  // Clock
  char clockBuf[16];
  if (status.clockValid) {
    snprintf(clockBuf, sizeof(clockBuf), "%02u:%02u", status.hour, status.minute);
  } else {
    snprintf(clockBuf, sizeof(clockBuf), "--:--");
  }
  drawText(t, Rect{382, 6, 58, 28}, clockBuf, kFontMicro, Color::White, TextAlign::Right, false);

  // Graphical Battery Capsule
  const int16_t bx = static_cast<int16_t>(w - 74);
  const int16_t by = 11;
  const Rect battRect{bx, by, 34, 18};
  fastStroke(t, battRect, Paint::solid(Color::White), 2, 3);
  fastFill(t, Rect{static_cast<int16_t>(bx + 34), static_cast<int16_t>(by + 5), 3, 8}, Paint::solid(Color::White));

  const int16_t pct = (status.batteryPercent >= 0 && status.batteryPercent <= 100) ? status.batteryPercent : 85;
  const int16_t fillW = static_cast<int16_t>((26 * pct) / 100);
  if (fillW > 0) {
    fastFill(t, Rect{static_cast<int16_t>(bx + 4), static_cast<int16_t>(by + 4), fillW, 10}, Paint::solid(Color::White));
  }
  char battTxt[12];
  snprintf(battTxt, sizeof(battTxt), "%d%%", static_cast<int>(pct));
  drawText(t, Rect{static_cast<int16_t>(bx + 39), 6, 34, 28}, battTxt, kFontMicro, Color::White, TextAlign::Left,
           false);
}

void drawBottomKeyBar(DrawTarget& t, const char* k1, const char* l1, const char* k2, const char* l2, const char* k3,
                      const char* l3) {
  const int16_t w = kScreenW;
  const int16_t h = kScreenH;
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

void drawInklingSprite(DrawTarget& t, int16_t cx, int16_t cy, int16_t scale, PetStage stage, PetMood mood,
                       uint8_t animFrame, bool onDark) {
  if (scale < 1) scale = 1;
  const Color fg = onDark ? Color::White : Color::Black;
  const Color bg = onDark ? Color::Black : Color::White;
  const int16_t bob = (animFrame & 1) ? -scale : 0;
  cy = static_cast<int16_t>(cy + bob);

  // Pedestal shadow underneath the creature
  const int16_t shadowY = static_cast<int16_t>(cy + 18 * scale - bob);
  fastFill(t,
           Rect{static_cast<int16_t>(cx - 14 * scale), shadowY, static_cast<int16_t>(28 * scale),
                static_cast<int16_t>(3 * scale)},
           Paint::dither(Color::DarkGray), static_cast<uint8_t>(scale * 2));

  switch (stage) {
    case PetStage::Spore: {
      // Orbiting rune halo + geometric egg core
      strokeCircle(t, cx, cy, static_cast<int16_t>(16 * scale), scale, fg);
      fillCircle(t, cx, cy, static_cast<int16_t>(11 * scale), fg);
      // Inner rune band
      fastFill(t,
               Rect{static_cast<int16_t>(cx - 9 * scale), static_cast<int16_t>(cy + 3 * scale),
                    static_cast<int16_t>(18 * scale), static_cast<int16_t>(2 * scale)},
               Paint::solid(bg));
      drawDiamond(t, cx, static_cast<int16_t>(cy - 6 * scale), static_cast<int16_t>(2 * scale), bg);
      drawEyes(t, cx, static_cast<int16_t>(cy - scale), static_cast<int16_t>(4 * scale), scale, mood, animFrame, fg,
               bg);
      // Orbiting sparks
      drawStar4(t, static_cast<int16_t>(cx - 18 * scale), static_cast<int16_t>(cy - 10 * scale),
                static_cast<int16_t>(2 * scale), fg);
      drawStar4(t, static_cast<int16_t>(cx + 18 * scale), static_cast<int16_t>(cy + 6 * scale),
                static_cast<int16_t>(2 * scale), fg);
      break;
    }

    case PetStage::Drop: {
      // Cute Gocciolino with twin ink-drop antennae, round body, little feet
      // Antennae
      t.line(Point{static_cast<int16_t>(cx - 5 * scale), static_cast<int16_t>(cy - 10 * scale)},
             Point{static_cast<int16_t>(cx - 9 * scale), static_cast<int16_t>(cy - 18 * scale)},
             static_cast<uint8_t>(scale * 2), Paint::solid(fg));
      t.line(Point{static_cast<int16_t>(cx + 5 * scale), static_cast<int16_t>(cy - 10 * scale)},
             Point{static_cast<int16_t>(cx + 9 * scale), static_cast<int16_t>(cy - 18 * scale)},
             static_cast<uint8_t>(scale * 2), Paint::solid(fg));
      fillCircle(t, static_cast<int16_t>(cx - 9 * scale), static_cast<int16_t>(cy - 18 * scale),
                 static_cast<int16_t>(2 * scale + 1), fg);
      fillCircle(t, static_cast<int16_t>(cx + 9 * scale), static_cast<int16_t>(cy - 18 * scale),
                 static_cast<int16_t>(2 * scale + 1), fg);

      // Feet
      fillCircle(t, static_cast<int16_t>(cx - 7 * scale), static_cast<int16_t>(cy + 13 * scale),
                 static_cast<int16_t>(3 * scale), fg);
      fillCircle(t, static_cast<int16_t>(cx + 7 * scale), static_cast<int16_t>(cy + 13 * scale),
                 static_cast<int16_t>(3 * scale), fg);

      // Main body
      fillCircle(t, cx, cy, static_cast<int16_t>(14 * scale), fg);
      // Belly glyph patch
      fillCircle(t, cx, static_cast<int16_t>(cy + 7 * scale), static_cast<int16_t>(6 * scale), bg);
      drawDiamond(t, cx, static_cast<int16_t>(cy + 7 * scale), static_cast<int16_t>(2 * scale), fg);

      drawEyes(t, cx, static_cast<int16_t>(cy - 2 * scale), static_cast<int16_t>(5 * scale), scale, mood, animFrame, fg,
               bg);
      break;
    }

    case PetStage::Glyph: {
      // Glifo Vivente: Kitsune/Owl spirit with tall pointed ears, scarf & ink-brush tail
      // Tail on the right
      fillCircle(t, static_cast<int16_t>(cx + 15 * scale), static_cast<int16_t>(cy + 6 * scale),
                 static_cast<int16_t>(6 * scale), fg);
      fillCircle(t, static_cast<int16_t>(cx + 18 * scale), static_cast<int16_t>(cy + 3 * scale),
                 static_cast<int16_t>(3 * scale), bg);

      // Tall pointed ears
      t.triangle(Point{static_cast<int16_t>(cx - 12 * scale), static_cast<int16_t>(cy - 6 * scale)},
                 Point{static_cast<int16_t>(cx - 3 * scale), static_cast<int16_t>(cy - 10 * scale)},
                 Point{static_cast<int16_t>(cx - 11 * scale), static_cast<int16_t>(cy - 22 * scale)}, Paint::solid(fg));
      t.triangle(Point{static_cast<int16_t>(cx + 12 * scale), static_cast<int16_t>(cy - 6 * scale)},
                 Point{static_cast<int16_t>(cx + 3 * scale), static_cast<int16_t>(cy - 10 * scale)},
                 Point{static_cast<int16_t>(cx + 11 * scale), static_cast<int16_t>(cy - 22 * scale)}, Paint::solid(fg));

      // Head & Torso
      fillCircle(t, cx, static_cast<int16_t>(cy - 3 * scale), static_cast<int16_t>(13 * scale), fg);
      fastFill(t,
               Rect{static_cast<int16_t>(cx - 11 * scale), cy, static_cast<int16_t>(22 * scale),
                    static_cast<int16_t>(15 * scale)},
               Paint::solid(fg), static_cast<uint8_t>(4 * scale));

      // Woven glyph scarf
      fastFill(t,
               Rect{static_cast<int16_t>(cx - 12 * scale), static_cast<int16_t>(cy + 4 * scale),
                    static_cast<int16_t>(24 * scale), static_cast<int16_t>(4 * scale)},
               Paint::solid(bg), static_cast<uint8_t>(scale));
      fastStroke(t,
                 Rect{static_cast<int16_t>(cx - 12 * scale), static_cast<int16_t>(cy + 4 * scale),
                      static_cast<int16_t>(24 * scale), static_cast<int16_t>(4 * scale)},
                 Paint::solid(fg), 1);

      // Forehead diamond crest
      drawDiamond(t, cx, static_cast<int16_t>(cy - 10 * scale), static_cast<int16_t>(2 * scale), bg);
      drawEyes(t, cx, static_cast<int16_t>(cy - 3 * scale), static_cast<int16_t>(5 * scale), scale, mood, animFrame, fg,
               bg);
      break;
    }

    case PetStage::Guardian:
    case PetStage::Archon: {
      // Majestic winged Guardian / Cosmic Archon with crown halo & rune wings
      const bool archon = (stage == PetStage::Archon);
      if (scale >= 2) {
        // Outer astrolabe halo (medium & large habitat scales)
        strokeCircle(t, cx, static_cast<int16_t>(cy - 2 * scale), static_cast<int16_t>((archon ? 24 : 20) * scale),
                     scale, fg);
        if (archon) {
          drawStar4(t, cx, static_cast<int16_t>(cy - 24 * scale), static_cast<int16_t>(4 * scale), fg);
          drawStar4(t, static_cast<int16_t>(cx - 24 * scale), cy, static_cast<int16_t>(3 * scale), fg);
          drawStar4(t, static_cast<int16_t>(cx + 24 * scale), cy, static_cast<int16_t>(3 * scale), fg);
        }
      }

      // Wings
      const int16_t wingSpan = static_cast<int16_t>((scale == 1 ? 17 : 23) * scale);
      t.triangle(Point{static_cast<int16_t>(cx - 10 * scale), cy},
                 Point{static_cast<int16_t>(cx - wingSpan), static_cast<int16_t>(cy - 11 * scale)},
                 Point{static_cast<int16_t>(cx - (wingSpan - 4 * scale)), static_cast<int16_t>(cy + 9 * scale)},
                 Paint::solid(fg));
      t.triangle(Point{static_cast<int16_t>(cx + 10 * scale), cy},
                 Point{static_cast<int16_t>(cx + wingSpan), static_cast<int16_t>(cy - 11 * scale)},
                 Point{static_cast<int16_t>(cx + (wingSpan - 4 * scale)), static_cast<int16_t>(cy + 9 * scale)},
                 Paint::solid(fg));

      // Horns / Crown
      const int16_t hornTipY = static_cast<int16_t>(cy - (scale == 1 ? 16 : 23) * scale);
      t.triangle(Point{static_cast<int16_t>(cx - 11 * scale), static_cast<int16_t>(cy - 8 * scale)},
                 Point{static_cast<int16_t>(cx - 3 * scale), static_cast<int16_t>(cy - 12 * scale)},
                 Point{static_cast<int16_t>(cx - 9 * scale), hornTipY}, Paint::solid(fg));
      t.triangle(Point{static_cast<int16_t>(cx + 11 * scale), static_cast<int16_t>(cy - 8 * scale)},
                 Point{static_cast<int16_t>(cx + 3 * scale), static_cast<int16_t>(cy - 12 * scale)},
                 Point{static_cast<int16_t>(cx + 9 * scale), hornTipY}, Paint::solid(fg));

      // Body & Cape
      fillCircle(t, cx, static_cast<int16_t>(cy - 3 * scale), static_cast<int16_t>(13 * scale), fg);
      fastFill(t,
               Rect{static_cast<int16_t>(cx - 12 * scale), cy, static_cast<int16_t>(24 * scale),
                    static_cast<int16_t>(16 * scale)},
               Paint::solid(fg), static_cast<uint8_t>(5 * scale));

      // Chest core
      fillCircle(t, cx, static_cast<int16_t>(cy + 8 * scale), static_cast<int16_t>(6 * scale), bg);
      drawDiamond(t, cx, static_cast<int16_t>(cy + 8 * scale), static_cast<int16_t>(3 * scale), fg);
      drawDiamond(t, cx, static_cast<int16_t>(cy - 11 * scale), static_cast<int16_t>(2 * scale), bg);

      drawEyes(t, cx, static_cast<int16_t>(cy - 3 * scale), static_cast<int16_t>(5 * scale), scale, mood, animFrame, fg,
               bg);
      break;
    }
  }

  // Contextual mood overlays (Zzz when sleepy, sparkles when radiant, ink drop when hungry) on habitat scales
  if (scale < 2) return;
  if (mood == PetMood::Sleepy) {
    drawText(t,
             Rect{static_cast<int16_t>(cx + 12 * scale), static_cast<int16_t>(cy - 22 * scale),
                  static_cast<int16_t>(22 * scale), static_cast<int16_t>(12 * scale)},
             "Zzz", kFontSmall, fg, TextAlign::Left, false);
  } else if (mood == PetMood::Radiant) {
    drawStar4(t, static_cast<int16_t>(cx - 19 * scale), static_cast<int16_t>(cy - 15 * scale),
              static_cast<int16_t>(3 * scale), fg);
    drawStar4(t, static_cast<int16_t>(cx + 19 * scale), static_cast<int16_t>(cy - 13 * scale),
              static_cast<int16_t>(2 * scale), fg);
  } else if (mood == PetMood::Hungry) {
    drawIcon16(t, static_cast<int16_t>(cx + 14 * scale), static_cast<int16_t>(cy - 18 * scale), Icon::Drop, fg);
  }
}

void drawHabitatScene(DrawTarget& t, Rect r, const PetEngine& pet, const DeviceStatus& status, bool largeMode) {
  LocalTime now{};
  now.valid = status.clockValid;
  now.year = status.year;
  now.month = status.month;
  now.day = status.day;
  now.hour = status.hour;
  now.minute = status.minute;

  const PetMood mood = pet.moodState(now);
  const bool night = pet.state().nightLamp || (status.clockValid && (status.hour >= 21 || status.hour < 6));

  fastFill(t, r, Paint::solid(Color::White), 8);
  fastStroke(t, r, Paint::solid(Color::Black), 2, 8);

  // Sky backdrop
  const int16_t horizonY = static_cast<int16_t>(r.bottom() - (largeMode ? 76 : 36));
  const Rect skyRect{static_cast<int16_t>(r.x + 2), static_cast<int16_t>(r.y + 2),
                     static_cast<int16_t>(r.width - 4), static_cast<int16_t>(horizonY - r.y - 2)};

  if (night) {
    // Dark dithered night skyband at the top + crescent moon + constellation stars
    const Rect topBand{skyRect.x, skyRect.y, skyRect.width, static_cast<int16_t>(skyRect.height / 2)};
    fastFill(t, topBand, Paint::dither(Color::LightGray));
    // Crescent moon
    const int16_t mx = static_cast<int16_t>(r.right() - 36);
    const int16_t my = static_cast<int16_t>(r.y + 28);
    fillCircle(t, mx, my, 12, Color::Black);
    fillCircle(t, static_cast<int16_t>(mx + 5), static_cast<int16_t>(my - 3), 10, Color::White);
    // Constellation stars
    drawStar4(t, static_cast<int16_t>(r.x + 28), static_cast<int16_t>(r.y + 22), 3, Color::Black);
    drawStar4(t, static_cast<int16_t>(r.x + 62), static_cast<int16_t>(r.y + 38), 2, Color::Black);
    drawStar4(t, static_cast<int16_t>(r.x + 98), static_cast<int16_t>(r.y + 18), 3, Color::Black);
    t.line(Point{static_cast<int16_t>(r.x + 28), static_cast<int16_t>(r.y + 22)},
           Point{static_cast<int16_t>(r.x + 62), static_cast<int16_t>(r.y + 38)}, 1, Paint::solid(Color::Black));
    t.line(Point{static_cast<int16_t>(r.x + 62), static_cast<int16_t>(r.y + 38)},
           Point{static_cast<int16_t>(r.x + 98), static_cast<int16_t>(r.y + 18)}, 1, Paint::solid(Color::Black));
  } else {
    // Cyber-editorial dot-matrix sky + geometric sun/radar rings
    fillDotGrid(t, skyRect, 8, Color::Black);
    const int16_t sx = static_cast<int16_t>(r.right() - 36);
    const int16_t sy = static_cast<int16_t>(r.y + 30);
    strokeCircle(t, sx, sy, 14, 1, Color::Black);
    strokeCircle(t, sx, sy, 9, 1, Color::Black);
    fillCircle(t, sx, sy, 4, Color::Black);
  }

  // Isometric perspective floor grid below horizonY
  fastFill(t, Rect{static_cast<int16_t>(r.x + 2), horizonY, static_cast<int16_t>(r.width - 4), 2},
           Paint::solid(Color::Black));
  const int16_t floorBot = static_cast<int16_t>(r.bottom() - 2);
  for (int16_t hy = static_cast<int16_t>(horizonY + 10); hy < floorBot; hy = static_cast<int16_t>(hy + 12)) {
    fastFill(t, Rect{static_cast<int16_t>(r.x + 2), hy, static_cast<int16_t>(r.width - 4), 1},
             Paint::solid(Color::Black));
  }
  const int16_t vanishX = static_cast<int16_t>(r.x + r.width / 2);
  const int16_t vanishY = static_cast<int16_t>(horizonY - 24);
  for (int i = -3; i <= 3; ++i) {
    const int16_t bx = static_cast<int16_t>(vanishX + i * (r.width / 5));
    const int16_t hx = static_cast<int16_t>(vanishX + (bx - vanishX) * (horizonY - vanishY) / (floorBot - vanishY));
    if (hx > r.x + 2 && hx < r.right() - 2 && bx > r.x + 2 && bx < r.right() - 2) {
      t.line(Point{hx, horizonY}, Point{bx, floorBot}, 1, Paint::solid(Color::Black));
    }
  }

  // Subtle IMU tilt shift for the creature (+/- 10px)
  int16_t imuShiftX = 0;
  if (status.imuReady) {
    const int shift = static_cast<int>(status.ax * 14.0f);
    if (shift < -12) imuShiftX = -12;
    else if (shift > 12) imuShiftX = 12;
    else imuShiftX = static_cast<int16_t>(shift);
  }

  const int16_t petCx = static_cast<int16_t>(r.x + r.width / 2 + imuShiftX);
  const int16_t petCy = static_cast<int16_t>(horizonY - (largeMode ? 22 : 16));
  drawInklingSprite(t, petCx, petCy, largeMode ? 3 : 2, pet.stage(), mood, pet.animFrame(), false);
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
    // Render real cached 1-bit cover thumbnail inside the frame
    freeink::ui::BitmapRef ref{};
    ref.data = coverBits;
    ref.width = kCoverW;
    ref.height = kCoverH;
    ref.format = freeink::ui::BitmapFormat::BW1;
    t.bitmap(r, ref, freeink::ui::BitmapMode::Cover, Paint::solid(Color::Black));
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
