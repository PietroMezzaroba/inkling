#include "Nfc.h"

#include <cstdint>

namespace inkling {

namespace {

struct Composition {
  char base;
  uint16_t mark;      // combining code point
  uint16_t composed;  // precomposed code point (Latin-1, 2 bytes in UTF-8)
};

// Grave U+0300, acute U+0301, circumflex U+0302, tilde U+0303,
// diaeresis U+0308, cedilla U+0327.
constexpr Composition kTable[] = {
    {'A', 0x300, 0xC0}, {'A', 0x301, 0xC1}, {'A', 0x302, 0xC2}, {'A', 0x303, 0xC3}, {'A', 0x308, 0xC4},
    {'C', 0x327, 0xC7}, {'E', 0x300, 0xC8}, {'E', 0x301, 0xC9}, {'E', 0x302, 0xCA}, {'E', 0x308, 0xCB},
    {'I', 0x300, 0xCC}, {'I', 0x301, 0xCD}, {'I', 0x302, 0xCE}, {'I', 0x308, 0xCF}, {'N', 0x303, 0xD1},
    {'O', 0x300, 0xD2}, {'O', 0x301, 0xD3}, {'O', 0x302, 0xD4}, {'O', 0x303, 0xD5}, {'O', 0x308, 0xD6},
    {'U', 0x300, 0xD9}, {'U', 0x301, 0xDA}, {'U', 0x302, 0xDB}, {'U', 0x308, 0xDC}, {'Y', 0x301, 0xDD},
    {'a', 0x300, 0xE0}, {'a', 0x301, 0xE1}, {'a', 0x302, 0xE2}, {'a', 0x303, 0xE3}, {'a', 0x308, 0xE4},
    {'c', 0x327, 0xE7}, {'e', 0x300, 0xE8}, {'e', 0x301, 0xE9}, {'e', 0x302, 0xEA}, {'e', 0x308, 0xEB},
    {'i', 0x300, 0xEC}, {'i', 0x301, 0xED}, {'i', 0x302, 0xEE}, {'i', 0x308, 0xEF}, {'n', 0x303, 0xF1},
    {'o', 0x300, 0xF2}, {'o', 0x301, 0xF3}, {'o', 0x302, 0xF4}, {'o', 0x303, 0xF5}, {'o', 0x308, 0xF6},
    {'u', 0x300, 0xF9}, {'u', 0x301, 0xFA}, {'u', 0x302, 0xFB}, {'u', 0x308, 0xFC}, {'y', 0x301, 0xFD},
    {'y', 0x308, 0xFF},
};

}  // namespace

void composeLatinNfc(char* s) {
  char* out = s;
  for (const char* in = s; *in;) {
    // A combining mark in U+0300..U+036F is two bytes: 0xCC/0xCD + continuation.
    const auto b1 = static_cast<uint8_t>(in[1]);
    const auto b2 = b1 ? static_cast<uint8_t>(in[2]) : 0;
    if (b1 >= 0xCC && b1 <= 0xCD && (b2 & 0xC0) == 0x80) {
      const uint16_t mark = static_cast<uint16_t>(((b1 & 0x1F) << 6) | (b2 & 0x3F));
      bool done = false;
      for (const auto& c : kTable) {
        if (c.base == *in && c.mark == mark) {
          *out++ = static_cast<char>(0xC0 | (c.composed >> 6));
          *out++ = static_cast<char>(0x80 | (c.composed & 0x3F));
          in += 3;
          done = true;
          break;
        }
      }
      if (done) continue;
    }
    *out++ = *in++;
  }
  *out = '\0';
}

}  // namespace inkling
