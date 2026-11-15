#include "Flashcards.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace inkling {

namespace {

constexpr const char* kDeckDir = "/flashcards";
constexpr uint32_t kSideMagic = 0x31534346u;  // "FCS1"
constexpr int32_t kEpochDay = 18262;          // 2020-01-01 in days since 1970

struct SideHeader {
  uint32_t magic;
  uint32_t count;
};

uint32_t fnv1a(const char* s, size_t n, uint32_t h = 2166136261u) {
  for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<uint8_t>(s[i])) * 16777619u;
  return h;
}

bool endsWithNoCase(const char* name, const char* suffix) {
  const size_t n = strlen(name), m = strlen(suffix);
  if (n <= m) return false;
  for (size_t i = 0; i < m; ++i) {
    char c = name[n - m + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != suffix[i]) return false;
  }
  return true;
}

// In /flashcards any .csv/.tsv/.txt is a deck; in the card root only files
// named "<deck>.flashcards.txt", which is what the Wi-Fi portal can upload.
constexpr const char* kRootDeckSuffix = ".flashcards.txt";

// Parses one CSV row starting at `row` (not necessarily NUL-terminated at the
// row end). Copies up to `maxFields` fields, unquoted, "/n" -> newline.
// Returns the number of bytes the row occupies, including its line ending;
// `fieldsOut` receives how many fields the row has.
size_t parseRow(const char* row, size_t len, char separator, char* fields[], const size_t caps[], int maxFields,
                int* fieldsOut) {
  int field = 0;
  size_t out = 0;
  bool quoted = false, fieldStart = true;
  size_t i = 0;
  auto put = [&](char c) {
    if (field < maxFields && out + 1 < caps[field]) fields[field][out++] = c;
  };
  auto endField = [&] {
    if (field < maxFields) {
      // Trim trailing spaces of unquoted fields.
      while (out > 0 && fields[field][out - 1] == ' ') --out;
      fields[field][out] = '\0';
    }
    ++field;
    out = 0;
    fieldStart = true;
  };
  for (; i < len; ++i) {
    const char c = row[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < len && row[i + 1] == '"') {
          put('"');
          ++i;
        } else {
          quoted = false;
        }
      } else if (c == '/' && i + 1 < len && row[i + 1] == 'n') {
        put('\n');
        ++i;
      } else if (c != '\\') {  // a backslash is one of CrossPet's hint marks
        put(c == '\r' ? ' ' : c);
      }
      continue;
    }
    if (c == '"' && fieldStart) {
      quoted = true;
      fieldStart = false;
    } else if (c == separator) {
      endField();
    } else if (c == '\n' || c == '\r') {
      break;
    } else if (c == '/' && i + 1 < len && row[i + 1] == 'n') {
      put('\n');
      ++i;
      fieldStart = false;
    } else if (c == '\\') {
      // CrossPet wraps a hint in backslashes: keep the text, drop the marks.
    } else if (c == ' ' && fieldStart) {
      // leading space
    } else {
      put(c);
      fieldStart = false;
    }
  }
  endField();
  if (fieldsOut) *fieldsOut = field;
  while (i < len && (row[i] == '\r' || row[i] == '\n')) {
    const char c = row[i++];
    if (c == '\n') break;
  }
  return i;
}

struct DeckScan {
  Flashcards::Deck* decks;
  int count;
  const char* dir;
  bool root;
};

void onDeckEntry(const char* name, bool isDir, void* user) {
  auto& c = *static_cast<DeckScan*>(user);
  if (isDir || name[0] == '.' || c.count >= Flashcards::kMaxDecks) return;
  const char* suffix = nullptr;
  if (c.root) {
    if (endsWithNoCase(name, kRootDeckSuffix)) suffix = kRootDeckSuffix;
  } else {
    static const char* const kExtensions[] = {".csv", ".tsv", ".txt"};
    for (const char* ext : kExtensions) {
      if (endsWithNoCase(name, ext)) suffix = ext;
    }
    if (endsWithNoCase(name, kRootDeckSuffix)) suffix = kRootDeckSuffix;
  }
  if (!suffix) return;
  Flashcards::Deck& d = c.decks[c.count];
  if (snprintf(d.path, sizeof(d.path), "%s%s%s", c.dir, c.root ? "" : "/", name) >= static_cast<int>(sizeof(d.path))) {
    return;
  }
  snprintf(d.name, sizeof(d.name), "%s", name);
  const size_t n = strlen(d.name), m = strlen(suffix);
  if (n > m && strlen(name) < sizeof(d.name)) d.name[n - m] = '\0';
  for (char* p = d.name; *p; ++p) {
    if (*p == '_') *p = ' ';
  }
  ++c.count;
}

}  // namespace

void Flashcards::scan(Storage& storage) {
  DeckScan ctx{decks_, 0, kDeckDir, false};
  storage.listDir(kDeckDir, onDeckEntry, &ctx);
  ctx.dir = "/";
  ctx.root = true;
  storage.listDir("/", onDeckEntry, &ctx);
  deckCount_ = ctx.count;
}

bool Flashcards::begin(Storage& storage, int deckIndex, int32_t today) {
  end();
  if (deckIndex < 0 || deckIndex >= deckCount_) return false;
  const Deck& deck = decks_[deckIndex];
  const int64_t fileSize = storage.fileSize(deck.path);
  if (fileSize <= 0) return false;

  const size_t blockBytes = sizeof(SideHeader) + sizeof(Srs) * kMaxCards;
  block_ = static_cast<uint8_t*>(malloc(blockBytes));
  if (!block_) return false;
  memset(block_, 0, blockBytes);
  srs_ = reinterpret_cast<Srs*>(block_ + sizeof(SideHeader));
  deckIndex_ = deckIndex;
  snprintf(sidePath_, sizeof(sidePath_), "/.inkling/fc_%08lx.bin",
           static_cast<unsigned long>(fnv1a(deck.path, strlen(deck.path))));

  // What was learnt so far, by card id.
  int known = 0;
  {
    SideHeader hdr{};
    if (storage.readFileAt(sidePath_, 0, &hdr, sizeof(hdr)) == static_cast<int32_t>(sizeof(hdr)) &&
        hdr.magic == kSideMagic && hdr.count <= static_cast<uint32_t>(kMaxCards)) {
      const uint32_t bytes = hdr.count * sizeof(Srs);
      if (bytes == 0 || storage.readFileAt(sidePath_, sizeof(hdr), srs_, bytes) == static_cast<int32_t>(bytes)) {
        known = static_cast<int>(hdr.count);
      }
    }
  }
  const int32_t day = today - kEpochDay;
  today_ = static_cast<uint16_t>(day > 1 ? (day > 65000 ? 65000 : day) : 1);

  // One pass over the deck: match every row with its record (or make one) and
  // pick today's cards. Records are reordered to follow the file.
  Srs* old = nullptr;
  if (known > 0) {
    old = static_cast<Srs*>(malloc(sizeof(Srs) * static_cast<size_t>(known)));
    if (!old) {
      end();
      return false;
    }
    memcpy(old, srs_, sizeof(Srs) * static_cast<size_t>(known));
    memset(srs_, 0, sizeof(Srs) * kMaxCards);
  }

  char chunk[640];
  char f0[80], f1[80], f2[8];
  char* fields[3] = {f0, f1, f2};
  const size_t caps[3] = {sizeof(f0), sizeof(f1), sizeof(f2)};
  uint32_t offset = 0;
  bool first = true;
  int reviews = 0, fresh = 0;
  QueueItem freshItems[kSessionNew];
  cardCount_ = 0;
  queueLen_ = 0;
  separator_ = ',';
  idColumn_ = false;
  while (offset < static_cast<uint32_t>(fileSize) && cardCount_ < kMaxCards) {
    const int32_t n = storage.readFileAt(deck.path, offset, chunk, sizeof(chunk));
    if (n <= 0) break;
    size_t start = 0;
    if (first && n >= 3 && static_cast<uint8_t>(chunk[0]) == 0xEF && static_cast<uint8_t>(chunk[1]) == 0xBB &&
        static_cast<uint8_t>(chunk[2]) == 0xBF) {
      start = 3;  // UTF-8 byte order mark
    }
    if (first) {
      // The separator is whichever of tab / semicolon / comma the first line uses.
      bool tab = false, semi = false, comma = false;
      for (int32_t i = static_cast<int32_t>(start); i < n && chunk[i] != '\n'; ++i) {
        tab = tab || chunk[i] == '\t';
        semi = semi || chunk[i] == ';';
        comma = comma || chunk[i] == ',';
      }
      separator_ = tab ? '\t' : (semi && !comma ? ';' : ',');
    }
    int count = 0;
    const size_t used = parseRow(chunk + start, static_cast<size_t>(n) - start, separator_, fields, caps, 3, &count);
    const uint32_t rowOffset = offset + static_cast<uint32_t>(start);
    offset += static_cast<uint32_t>(start + (used ? used : 1));
    // A row longer than the chunk: its first fields are parsed, skip the rest.
    if (start + used == static_cast<size_t>(n) && chunk[n - 1] != '\n' && chunk[n - 1] != '\r') {
      bool ended = false;
      while (!ended && offset < static_cast<uint32_t>(fileSize)) {
        const int32_t more = storage.readFileAt(deck.path, offset, chunk, sizeof(chunk));
        if (more <= 0) break;
        const char* nl = static_cast<const char*>(memchr(chunk, '\n', static_cast<size_t>(more)));
        offset += nl ? static_cast<uint32_t>(nl - chunk + 1) : static_cast<uint32_t>(more);
        ended = nl != nullptr;
      }
    }
    if (first) {
      first = false;
      idColumn_ = count >= 3;
      // A header row names its columns.
      const char* head = f0;
      if (strncmp(head, "card_id", 7) == 0 || strncmp(head, "id", 3) == 0 || strncmp(head, "front", 5) == 0 ||
          strncmp(head, "domanda", 7) == 0 || strncmp(head, "question", 8) == 0) {
        continue;
      }
    }
    const char* front = idColumn_ ? f1 : f0;
    if (count < 2 || !front[0]) continue;  // blank or broken line
    const uint32_t idHash = (idColumn_ && f0[0]) ? fnv1a(f0, strlen(f0)) : fnv1a(front, strlen(front));

    Srs& card = srs_[cardCount_];
    card = Srs{idHash, 0, 0, 2500, 0, 0};
    for (int k = 0; k < known; ++k) {
      if (old[k].idHash == idHash) {
        card = old[k];
        break;
      }
    }
    if (card.dueDay == 0) {
      if (fresh < kSessionNew) freshItems[fresh++] = QueueItem{rowOffset, static_cast<uint16_t>(cardCount_)};
    } else if (card.dueDay <= today_ && reviews < kSessionReviews) {
      queue_[reviews++] = QueueItem{rowOffset, static_cast<uint16_t>(cardCount_)};
    }
    ++cardCount_;
  }
  free(old);
  queueLen_ = reviews;
  for (int i = 0; i < fresh; ++i) queue_[queueLen_++] = freshItems[i];
  reviewed_ = 0;
  if (cardCount_ == 0) {
    end();
    return false;
  }
  loadCurrent(storage);
  return true;
}

void Flashcards::end() {
  free(block_);
  block_ = nullptr;
  srs_ = nullptr;
  queueLen_ = 0;
  cardCount_ = 0;
  deckIndex_ = -1;
  showingBack_ = false;
  front_[0] = back_[0] = '\0';
}

void Flashcards::loadCurrent(Storage& storage) {
  showingBack_ = false;
  front_[0] = back_[0] = '\0';
  if (!active() || queueLen_ == 0) return;
  char chunk[640];
  const int32_t n = storage.readFileAt(decks_[deckIndex_].path, queue_[0].offset, chunk, sizeof(chunk));
  if (n <= 0) return;
  char id[8];
  if (idColumn_) {
    char* fields[3] = {id, front_, back_};
    const size_t caps[3] = {sizeof(id), sizeof(front_), sizeof(back_)};
    parseRow(chunk, static_cast<size_t>(n), separator_, fields, caps, 3, nullptr);
  } else {
    char* fields[2] = {front_, back_};
    const size_t caps[2] = {sizeof(front_), sizeof(back_)};
    parseRow(chunk, static_cast<size_t>(n), separator_, fields, caps, 2, nullptr);
  }
}

void Flashcards::schedule(Srs& card, Grade grade, uint16_t today) {
  const int q = static_cast<int>(grade);
  if (q < 3) {
    // Forgotten: back to the start, and again in this session.
    card.reps = 0;
    card.interval = 0;
    if (card.lapses < 255) ++card.lapses;
    card.dueDay = today;
  } else {
    uint32_t interval;
    if (card.reps == 0) {
      interval = q == 5 ? 3 : 1;
    } else if (card.reps == 1) {
      interval = q == 3 ? 3 : 6;
    } else {
      interval = (static_cast<uint32_t>(card.interval) * card.ease + 500u) / 1000u;
      if (q == 3) interval = (static_cast<uint32_t>(card.interval) * 12u + 5u) / 10u;
      if (q == 5) interval = (interval * 13u + 5u) / 10u;
      if (interval <= card.interval) interval = card.interval + 1u;
    }
    if (interval > 3650) interval = 3650;
    card.interval = static_cast<uint16_t>(interval);
    if (card.reps < 255) ++card.reps;
    card.dueDay = static_cast<uint16_t>(today + interval > 65000u ? 65000u : today + interval);
  }
  // SM-2 ease update, never below 1.3.
  int ease = static_cast<int>(card.ease) + (100 - (5 - q) * (80 + (5 - q) * 20));
  if (ease < 1300) ease = 1300;
  if (ease > 3500) ease = 3500;
  card.ease = static_cast<uint16_t>(ease);
}

uint16_t Flashcards::intervalFor(Grade grade) const {
  if (!active() || queueLen_ == 0) return 0;
  Srs copy = srs_[queue_[0].srs];
  schedule(copy, grade, today_);
  return static_cast<uint16_t>(copy.dueDay - today_);
}

void Flashcards::grade(Storage& storage, Grade grade) {
  if (!active() || queueLen_ == 0) return;
  const QueueItem item = queue_[0];
  schedule(srs_[item.srs], grade, today_);
  for (int i = 0; i + 1 < queueLen_; ++i) queue_[i] = queue_[i + 1];
  if (grade == Grade::Again) {
    queue_[queueLen_ - 1] = item;  // once more before the session ends
  } else {
    --queueLen_;
    ++reviewed_;
  }
  save(storage);
  loadCurrent(storage);
}

void Flashcards::save(Storage& storage) {
  if (!active()) return;
  const SideHeader hdr{kSideMagic, static_cast<uint32_t>(cardCount_)};
  memcpy(block_, &hdr, sizeof(hdr));
  storage.mkdirs("/.inkling");
  storage.writeFile(sidePath_, block_, static_cast<uint32_t>(sizeof(hdr) + sizeof(Srs) * static_cast<size_t>(cardCount_)));
}

int Flashcards::learnedCards() const {
  int n = 0;
  for (int i = 0; i < cardCount_; ++i) n += (srs_ && srs_[i].dueDay != 0) ? 1 : 0;
  return n;
}

int Flashcards::dueTomorrow() const {
  int n = 0;
  for (int i = 0; i < cardCount_; ++i) n += (srs_ && srs_[i].dueDay != 0 && srs_[i].dueDay <= today_ + 1) ? 1 : 0;
  return n;
}

}  // namespace inkling
