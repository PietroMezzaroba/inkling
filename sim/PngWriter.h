#pragma once

#include <cstdint>
#include <vector>

namespace sim {

// Writes an 8-bit grayscale PNG. Uses stored (uncompressed) deflate blocks, so
// it needs no zlib; files are larger but the simulator only writes a few.
bool writeGrayPng(const char* path, int width, int height, const std::vector<uint8_t>& gray);

}  // namespace sim
