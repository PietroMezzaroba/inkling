#pragma once

namespace inkling {

// Composes "base letter + combining accent" into the precomposed letter, in
// place, for the Latin-1 accented letters (à è é ì ò ù ç ñ ä ö ü ...). File
// names copied from macOS or Android are often decomposed (NFD): drawn as is,
// the accent becomes a separate glyph after its letter. Anything else is left
// untouched; the result is never longer than the input.
void composeLatinNfc(char* utf8);

}  // namespace inkling
