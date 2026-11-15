#include "Dictionary.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace inkling {

namespace {

constexpr const char* kDictDirs[] = {"/dizionari", "/dict", "/dictionaries"};
constexpr const char* kSidePath = "/.inkling/dict.six";
constexpr uint32_t kSideMagic = 0x31584953u;  // "SIX1"
constexpr uint32_t kSideStep = 32;            // one side entry every 32 headwords

// File: header, one u32 .idx offset per slot, then kSideEnd. The end marker
// is what tells a finished build from one cut short by a power-off.
struct SideHeader {
  uint32_t magic;
  uint32_t idxSize;
  uint32_t wordCount;
};
constexpr uint32_t kSideEnd = 0xFFFFFFFFu;

bool endsWith(const char* s, const char* suffix) {
  const size_t n = strlen(s), m = strlen(suffix);
  if (n < m) return false;
  for (size_t i = 0; i < m; ++i) {
    char a = s[n - m + i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (a != suffix[i]) return false;
  }
  return true;
}

uint8_t lowerAscii(uint8_t c) { return (c >= 'A' && c <= 'Z') ? static_cast<uint8_t>(c - 'A' + 'a') : c; }

// StarDict's index order: ASCII case-insensitive, then byte order.
int compareWords(const char* a, const char* b) {
  const uint8_t* x = reinterpret_cast<const uint8_t*>(a);
  const uint8_t* y = reinterpret_cast<const uint8_t*>(b);
  for (;; ++x, ++y) {
    const uint8_t cx = lowerAscii(*x), cy = lowerAscii(*y);
    if (cx != cy) return cx < cy ? -1 : 1;
    if (cx == 0) break;
  }
  return strcmp(a, b);
}

bool sameWord(const char* a, const char* b) {
  for (;; ++a, ++b) {
    if (lowerAscii(static_cast<uint8_t>(*a)) != lowerAscii(static_cast<uint8_t>(*b))) return false;
    if (*a == '\0') return true;
  }
}

size_t commonPrefix(const char* a, const char* b) {
  size_t n = 0;
  while (a[n] && lowerAscii(static_cast<uint8_t>(a[n])) == lowerAscii(static_cast<uint8_t>(b[n]))) ++n;
  return n;
}

bool isLetterByte(uint8_t c) { return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

// The word as a dictionary lists it: no surrounding punctuation, lower case
// (ASCII and the accented capitals of Latin-1), the part after an elision
// ("dell'aria" -> "aria").
void normalizeWord(const char* in, char* out, size_t cap) {
  const uint8_t* s = reinterpret_cast<const uint8_t*>(in);
  size_t len = strlen(in);
  // Typographic punctuation (U+2010..U+203F: quotes, dashes) counts as edges.
  auto punctAt = [&](size_t i) { return i + 2 < len && s[i] == 0xE2 && s[i + 1] == 0x80; };
  size_t start = 0;
  while (start < len) {
    if (punctAt(start)) {
      start += 3;
    } else if (s[start] == 0xC2 && start + 1 < len && (s[start + 1] == 0xAB || s[start + 1] == 0xBB)) {
      start += 2;  // « »
    } else if (!isLetterByte(s[start])) {
      ++start;
    } else {
      break;
    }
  }
  size_t end = len;
  while (end > start) {
    if (end >= start + 3 && s[end - 3] == 0xE2 && s[end - 2] == 0x80) {
      end -= 3;
    } else if (end >= start + 2 && s[end - 2] == 0xC2 && (s[end - 1] == 0xAB || s[end - 1] == 0xBB)) {
      end -= 2;
    } else if (!isLetterByte(s[end - 1])) {
      --end;
    } else {
      break;
    }
  }
  // Elision: keep what follows the last apostrophe when something does.
  for (size_t i = start; i < end; ++i) {
    size_t skip = 0;
    if (s[i] == '\'') skip = 1;
    if (i + 2 < end && s[i] == 0xE2 && s[i + 1] == 0x80 && s[i + 2] == 0x99) skip = 3;  // ’
    if (skip && i + skip < end && i - start <= 6) start = i + skip;
  }
  size_t n = 0;
  for (size_t i = start; i < end && n + 1 < cap; ++i) {
    uint8_t c = s[i];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<uint8_t>(c - 'A' + 'a');
    } else if (c == 0xC3 && i + 1 < end && s[i + 1] >= 0x80 && s[i + 1] <= 0x9E && s[i + 1] != 0x97) {
      // À..Þ -> à..þ
      if (n + 2 < cap) {
        out[n++] = static_cast<char>(c);
        out[n++] = static_cast<char>(s[++i] + 0x20);
      }
      continue;
    }
    out[n++] = static_cast<char>(c);
  }
  out[n] = '\0';
}

uint32_t be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// Named entities dictionaries actually use, beyond the five XML ones.
struct NamedEntity {
  const char* name;
  uint16_t codepoint;
};
constexpr NamedEntity kEntities[] = {
    {"nbsp", ' '},      {"agrave", 0xE0},  {"aacute", 0xE1},  {"acirc", 0xE2},   {"auml", 0xE4},   {"egrave", 0xE8},
    {"eacute", 0xE9},   {"ecirc", 0xEA},   {"euml", 0xEB},    {"igrave", 0xEC},  {"iacute", 0xED}, {"icirc", 0xEE},
    {"iuml", 0xEF},     {"ograve", 0xF2},  {"oacute", 0xF3},  {"ocirc", 0xF4},   {"ouml", 0xF6},   {"ugrave", 0xF9},
    {"uacute", 0xFA},   {"ucirc", 0xFB},   {"uuml", 0xFC},    {"ccedil", 0xE7},  {"ntilde", 0xF1}, {"szlig", 0xDF},
    {"Agrave", 0xC0},   {"Egrave", 0xC8},  {"Eacute", 0xC9},  {"Igrave", 0xCC},  {"Ograve", 0xD2}, {"Ugrave", 0xD9},
    {"laquo", 0xAB},    {"raquo", 0xBB},   {"deg", 0xB0},     {"middot", 0xB7},  {"hellip", 0x2026},
    {"ndash", 0x2013},  {"mdash", 0x2014}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C},
    {"rdquo", 0x201D},  {"bull", 0x2022},  {"euro", 0x20AC},  {"rarr", 0x2192},
};

size_t putUtf8(uint32_t cp, char* out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  out[0] = static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
  out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[2] = static_cast<char>(0x80 | (cp & 0x3F));
  return 3;
}

// Dictionary markup (HTML, XDXF, Pango) to plain text, in place: tags go,
// block ends become line breaks, entities are decoded, blank runs collapse.
void plainText(char* text) {
  char* out = text;
  const char* in = text;
  bool lastSpace = true, lastBreak = true;
  auto put = [&](char c) {
    if (c == '\n') {
      if (lastBreak) return;
      while (out > text && out[-1] == ' ') --out;
      *out++ = '\n';
      lastBreak = lastSpace = true;
    } else if (c == ' ' || c == '\t' || c == '\r') {
      if (lastSpace) return;
      *out++ = ' ';
      lastSpace = true;
    } else {
      *out++ = c;
      lastSpace = lastBreak = false;
    }
  };
  while (*in) {
    if (*in == '<') {
      const char* close = strchr(in, '>');
      if (!close) break;
      const char* tag = in + 1;
      const bool closing = *tag == '/';
      if (closing) ++tag;
      const bool lineBreak = strncmp(tag, "br", 2) == 0 || (closing && (strncmp(tag, "p>", 2) == 0 ||
                             strncmp(tag, "div", 3) == 0 || strncmp(tag, "li", 2) == 0 || strncmp(tag, "dd", 2) == 0 ||
                             strncmp(tag, "dt", 2) == 0 || strncmp(tag, "tr", 2) == 0 || strncmp(tag, "blockquote", 10) == 0));
      if (lineBreak) put('\n');
      in = close + 1;
    } else if (*in == '&') {
      const char* semi = strchr(in, ';');
      if (semi && semi - in <= 9) {
        const size_t n = static_cast<size_t>(semi - in - 1);
        const char* name = in + 1;
        uint32_t cp = 0;
        if (name[0] == '#') {
          cp = (name[1] == 'x' || name[1] == 'X') ? static_cast<uint32_t>(strtoul(name + 2, nullptr, 16))
                                                  : static_cast<uint32_t>(strtoul(name + 1, nullptr, 10));
        } else if (n == 3 && strncmp(name, "amp", 3) == 0) {
          cp = '&';
        } else if (n == 2 && strncmp(name, "lt", 2) == 0) {
          cp = '<';
        } else if (n == 2 && strncmp(name, "gt", 2) == 0) {
          cp = '>';
        } else if (n == 4 && strncmp(name, "quot", 4) == 0) {
          cp = '"';
        } else if (n == 4 && strncmp(name, "apos", 4) == 0) {
          cp = '\'';
        } else {
          for (const auto& e : kEntities) {
            if (strlen(e.name) == n && strncmp(name, e.name, n) == 0) {
              cp = e.codepoint;
              break;
            }
          }
        }
        if (cp >= 0x20 && cp < 0x10000) {
          char buf[4];
          const size_t len = putUtf8(cp, buf);  // never longer than the entity it replaces
          if (len == 1) {
            put(buf[0]);
          } else {
            for (size_t i = 0; i < len; ++i) *out++ = buf[i];
            lastSpace = lastBreak = false;
          }
          in = semi + 1;
          continue;
        }
      }
      put(*in++);
    } else if (*in == '\n') {
      put('\n');
      ++in;
    } else if (static_cast<uint8_t>(*in) < 0x20) {
      put(' ');
      ++in;
    } else {
      put(*in++);
    }
  }
  while (out > text && (out[-1] == ' ' || out[-1] == '\n')) --out;
  *out = '\0';
}

struct IfoScan {
  const char* dir;
  char found[112];
  char subdirs[6][48];
  int subdirCount;
};

void onDictEntry(const char* name, bool isDir, void* user) {
  auto& scan = *static_cast<IfoScan*>(user);
  if (isDir) {
    if (name[0] != '.' && scan.subdirCount < 6) {
      snprintf(scan.subdirs[scan.subdirCount++], sizeof(scan.subdirs[0]), "%s", name);
    }
    return;
  }
  if (scan.found[0] || name[0] == '.' || !endsWith(name, ".ifo")) return;
  snprintf(scan.found, sizeof(scan.found), "%s/%s", scan.dir, name);
  scan.found[strlen(scan.found) - 4] = '\0';  // drop ".ifo"
}

}  // namespace

void Dictionary::pathFor(const char* extension, char* out, size_t cap) const {
  snprintf(out, cap, "%s%s", base_, extension);
}

Dictionary::Result Dictionary::open(Storage& storage) {
  if (searched_) return state_;
  searched_ = true;
  state_ = Result::NoDictionary;

  IfoScan scan{};
  for (const char* dir : kDictDirs) {
    scan.dir = dir;
    scan.subdirCount = 0;
    if (!storage.listDir(dir, onDictEntry, &scan)) continue;
    // StarDict archives unpack into a folder of their own: look one level down.
    char subs[6][48];
    const int subdirs = scan.subdirCount;
    memcpy(subs, scan.subdirs, sizeof(subs));
    for (int i = 0; i < subdirs && !scan.found[0]; ++i) {
      char sub[100];
      snprintf(sub, sizeof(sub), "%s/%s", dir, subs[i]);
      scan.dir = sub;
      storage.listDir(sub, onDictEntry, &scan);
    }
    if (scan.found[0]) break;
  }
  if (!scan.found[0]) return state_;
  snprintf(base_, sizeof(base_), "%s", scan.found);

  char path[128];
  char ifo[768];
  pathFor(".ifo", path, sizeof(path));
  const int32_t n = storage.readFile(path, ifo, sizeof(ifo) - 1);
  if (n <= 0) return state_ = Result::Error;
  ifo[n] = '\0';
  bool offsets64 = false;
  for (char* line = strtok(ifo, "\r\n"); line; line = strtok(nullptr, "\r\n")) {
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    const char* value = eq + 1;
    if (strcmp(line, "bookname") == 0) snprintf(name_, sizeof(name_), "%s", value);
    if (strcmp(line, "wordcount") == 0) wordCount_ = static_cast<uint32_t>(strtoul(value, nullptr, 10));
    if (strcmp(line, "idxfilesize") == 0) idxSize_ = static_cast<uint32_t>(strtoul(value, nullptr, 10));
    if (strcmp(line, "sametypesequence") == 0) snprintf(typeSequence_, sizeof(typeSequence_), "%s", value);
    if (strcmp(line, "idxoffsetbits") == 0 && atoi(value) == 64) offsets64 = true;
  }
  if (!name_[0]) snprintf(name_, sizeof(name_), "Dizionario");
  if (offsets64) return state_ = Result::Error;

  pathFor(".idx", path, sizeof(path));
  const int64_t idx = storage.fileSize(path);
  pathFor(".dict", path, sizeof(path));
  const int64_t dict = storage.fileSize(path);
  if (idx < 0 || dict < 0) return state_ = Result::Compressed;  // only .idx.gz / .dict.dz present
  idxSize_ = static_cast<uint32_t>(idx);
  INKLING_LOG("dictionary: %s (%lu words)", name_, static_cast<unsigned long>(wordCount_));
  return state_ = Result::Found;
}

bool Dictionary::readEntry(Storage& storage, uint32_t idxOffset, Entry* out) {
  if (idxOffset >= idxSize_) return false;
  char path[128];
  pathFor(".idx", path, sizeof(path));
  uint8_t buf[sizeof(out->word) + 200];
  const int32_t n = storage.readFileAt(path, idxOffset, buf, sizeof(buf));
  if (n <= 8) return false;
  const uint8_t* nul = static_cast<const uint8_t*>(memchr(buf, 0, static_cast<size_t>(n)));
  if (!nul || nul + 9 > buf + n) return false;  // headword longer than 255 bytes: not valid StarDict
  const size_t len = static_cast<size_t>(nul - buf);
  const size_t copy = len < sizeof(out->word) - 1 ? len : sizeof(out->word) - 1;
  memcpy(out->word, buf, copy);
  out->word[copy] = '\0';
  out->offset = be32(nul + 1);
  out->size = be32(nul + 5);
  out->next = idxOffset + static_cast<uint32_t>(len) + 9;
  return true;
}

bool Dictionary::sideOffset(Storage& storage, uint32_t slot, uint32_t* out) {
  return storage.readFileAt(kSidePath, static_cast<uint32_t>(sizeof(SideHeader)) + slot * 4, out, 4) == 4;
}

bool Dictionary::ensureSideIndex(Storage& storage) {
  if (sideReady_) return true;
  SideHeader hdr{};
  const int64_t size = storage.fileSize(kSidePath);
  const int64_t minSize = static_cast<int64_t>(sizeof(hdr)) + 8;
  uint32_t end = 0;
  if (size >= minSize && storage.readFileAt(kSidePath, 0, &hdr, sizeof(hdr)) == static_cast<int32_t>(sizeof(hdr)) &&
      hdr.magic == kSideMagic && hdr.idxSize == idxSize_ && hdr.wordCount == wordCount_ &&
      storage.readFileAt(kSidePath, static_cast<uint32_t>(size - 4), &end, 4) == 4 && end == kSideEnd) {
    sideSlots_ = static_cast<uint32_t>((size - static_cast<int64_t>(sizeof(hdr)) - 4) / 4);
    return sideReady_ = true;
  }

  // One pass over the .idx, noting where every 32nd entry starts.
  INKLING_LOG("dictionary: building side index");
  char path[128];
  pathFor(".idx", path, sizeof(path));
  storage.mkdirs("/.inkling");
  storage.removeFile(kSidePath);
  hdr = SideHeader{kSideMagic, idxSize_, wordCount_};
  if (!storage.appendFile(kSidePath, &hdr, sizeof(hdr))) return false;

  uint8_t buf[512];
  uint32_t pending[64];
  uint32_t pendingCount = 0, slots = 0, entries = 0;
  uint32_t fileOffset = 0, entryStart = 0;
  uint8_t tail = 0;  // bytes of the 8-byte offset/size still to skip
  bool inWord = true;
  while (fileOffset < idxSize_) {
    const int32_t n = storage.readFileAt(path, fileOffset, buf, sizeof(buf));
    if (n <= 0) break;
    for (int32_t i = 0; i < n; ++i) {
      if (inWord) {
        if (buf[i] == 0) {
          inWord = false;
          tail = 8;
        }
      } else if (--tail == 0) {
        if (entries % kSideStep == 0) {
          pending[pendingCount++] = entryStart;
          ++slots;
          if (pendingCount == 64) {
            if (!storage.appendFile(kSidePath, pending, sizeof(pending))) return false;
            pendingCount = 0;
          }
        }
        ++entries;
        entryStart = fileOffset + static_cast<uint32_t>(i) + 1;
        inWord = true;
      }
    }
    fileOffset += static_cast<uint32_t>(n);
  }
  if (slots == 0) return false;
  pending[pendingCount++] = kSideEnd;  // there is always room: the buffer is flushed when full
  if (!storage.appendFile(kSidePath, pending, pendingCount * 4)) return false;
  sideSlots_ = slots;
  return sideReady_ = true;
}

Dictionary::Result Dictionary::lookup(Storage& storage, const char* word, char* headword, size_t headCap,
                                      char* definition, size_t defCap) {
  if (headword && headCap) headword[0] = '\0';
  if (definition && defCap) definition[0] = '\0';
  const Result opened = open(storage);
  if (opened != Result::Found) return opened;
  if (!headword || !definition || headCap < 2 || defCap < 16) return Result::Error;

  char key[64];
  normalizeWord(word, key, sizeof(key));
  if (!key[0]) return Result::NotFound;
  if (!ensureSideIndex(storage)) return Result::Error;

  // Last side slot whose headword is <= key.
  static Entry entry, best, previous;
  uint32_t lo = 0, hi = sideSlots_;
  while (hi - lo > 1) {
    const uint32_t mid = lo + (hi - lo) / 2;
    uint32_t offset = 0;
    if (!sideOffset(storage, mid, &offset) || !readEntry(storage, offset, &entry)) return Result::Error;
    if (compareWords(entry.word, key) <= 0) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  uint32_t offset = 0;
  if (!sideOffset(storage, lo, &offset)) return Result::Error;

  // Walk the block: the word, or the neighbours it would sit between.
  bool found = false, havePrevious = false, haveNext = false;
  for (uint32_t i = 0; i < kSideStep * 2 && readEntry(storage, offset, &entry); ++i) {
    if (sameWord(entry.word, key)) {
      best = entry;
      found = true;
      break;
    }
    if (compareWords(entry.word, key) > 0) {
      haveNext = true;
      break;
    }
    previous = entry;
    havePrevious = true;
    offset = entry.next;
  }
  Result result = Result::Found;
  if (!found) {
    // An inflected form usually sits right next to its headword. Take the
    // neighbour that shares more of the word, if it shares most of it.
    const size_t keyLen = strlen(key);
    const size_t withPrevious = havePrevious ? commonPrefix(key, previous.word) : 0;
    const size_t withNext = haveNext ? commonPrefix(key, entry.word) : 0;
    const size_t shared = withPrevious >= withNext ? withPrevious : withNext;
    if (shared < 4 || shared * 10 < keyLen * 6) return Result::NotFound;
    best = withPrevious >= withNext ? previous : entry;
    result = Result::Nearest;
  }

  snprintf(headword, headCap, "%s", best.word);
  char path[128];
  pathFor(".dict", path, sizeof(path));
  uint32_t want = best.size < defCap - 1 ? best.size : static_cast<uint32_t>(defCap - 1);
  const int32_t n = storage.readFileAt(path, best.offset, definition, want);
  if (n <= 0) return Result::Error;
  definition[n] = '\0';
  // Cut on a character boundary when the entry did not fit.
  if (static_cast<uint32_t>(n) < best.size) {
    int32_t end = n;
    while (end > 0 && (static_cast<uint8_t>(definition[end - 1]) & 0xC0) == 0x80) --end;
    if (end > 0 && static_cast<uint8_t>(definition[end - 1]) >= 0xC0) --end;
    definition[end] = '\0';
  }
  // Without sametypesequence each field starts with its type letter.
  if (typeSequence_[0] == '\0' && definition[0] >= 'a' && definition[0] <= 'z') {
    memmove(definition, definition + 1, strlen(definition));
  }
  plainText(definition);
  return result;
}

}  // namespace inkling
