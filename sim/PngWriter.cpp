#include "PngWriter.h"

#include <cstdio>

namespace sim {

namespace {

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0) {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[n] = c;
    }
    ready = true;
  }
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void putBe32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

void putChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data) {
  putBe32(out, static_cast<uint32_t>(data.size()));
  const size_t typeAt = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  putBe32(out, crc32(out.data() + typeAt, data.size() + 4));
}

}  // namespace

bool writeGrayPng(const char* path, int width, int height, const std::vector<uint8_t>& gray) {
  // Raw scanlines, each prefixed with filter type 0.
  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(height) * (width + 1));
  for (int y = 0; y < height; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), gray.begin() + static_cast<long>(y) * width, gray.begin() + static_cast<long>(y + 1) * width);
  }

  // zlib stream of stored blocks (max 65535 bytes each) + Adler-32.
  std::vector<uint8_t> z = {0x78, 0x01};
  size_t pos = 0;
  do {
    const size_t len = raw.size() - pos > 65535 ? 65535 : raw.size() - pos;
    const bool last = pos + len == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(static_cast<uint8_t>(len));
    z.push_back(static_cast<uint8_t>(len >> 8));
    z.push_back(static_cast<uint8_t>(~len));
    z.push_back(static_cast<uint8_t>(~len >> 8));
    z.insert(z.end(), raw.begin() + static_cast<long>(pos), raw.begin() + static_cast<long>(pos + len));
    pos += len;
  } while (pos < raw.size());
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  putBe32(z, (b << 16) | a);

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> ihdr;
  putBe32(ihdr, static_cast<uint32_t>(width));
  putBe32(ihdr, static_cast<uint32_t>(height));
  ihdr.insert(ihdr.end(), {8, 0, 0, 0, 0});  // 8-bit grayscale, no interlace
  putChunk(png, "IHDR", ihdr);
  putChunk(png, "IDAT", z);
  putChunk(png, "IEND", {});

  FILE* f = std::fopen(path, "wb");
  if (!f) return false;
  const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
  return std::fclose(f) == 0 && ok;
}

}  // namespace sim
