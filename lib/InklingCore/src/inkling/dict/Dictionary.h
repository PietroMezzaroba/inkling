#pragma once

#include <cstddef>
#include <cstdint>

#include "inkling/Platform.h"

namespace inkling {

// A StarDict dictionary read straight from the card: the .ifo/.idx/.dict
// triple in /dizionari (or a folder inside it). Nothing is loaded in RAM: a
// small side index (/.inkling/dict.six, built on first use) turns a lookup
// into a dozen short reads.
//
// The .dict must be uncompressed (.dict, not .dict.dz) and the .idx too.
class Dictionary {
 public:
  enum class Result : uint8_t {
    Found,         // the word itself
    Nearest,       // not the word: the closest headword (inflected forms)
    NotFound,
    NoDictionary,  // nothing in /dizionari
    Compressed,    // .dict.dz / .idx.gz: must be unpacked on a computer
    Error,
  };

  // Looks for a dictionary. Cheap when called again.
  Result open(Storage& storage);
  bool ready() const { return state_ == Result::Found; }
  const char* name() const { return name_; }
  uint32_t wordCount() const { return wordCount_; }

  // Looks `word` up as it appears in a book (case, punctuation and elisions
  // are handled). `headword` and `definition` receive plain UTF-8 text.
  Result lookup(Storage& storage, const char* word, char* headword, size_t headCap, char* definition, size_t defCap);

 private:
  struct Entry {
    char word[64];
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t next = 0;  // .idx offset of the following entry
  };

  bool readEntry(Storage& storage, uint32_t idxOffset, Entry* out);
  bool ensureSideIndex(Storage& storage);
  bool sideOffset(Storage& storage, uint32_t slot, uint32_t* out);
  void pathFor(const char* extension, char* out, size_t cap) const;

  Result state_ = Result::NoDictionary;
  bool searched_ = false;
  bool sideReady_ = false;
  char base_[112] = {};  // path without extension
  char name_[40] = {};
  char typeSequence_[8] = {};
  uint32_t wordCount_ = 0;
  uint32_t idxSize_ = 0;
  uint32_t sideSlots_ = 0;
};

}  // namespace inkling
