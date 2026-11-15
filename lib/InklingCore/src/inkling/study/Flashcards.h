#pragma once

#include <cstdint>

#include "inkling/Platform.h"

namespace inkling {

// Spaced-repetition flashcards (SuperMemo-2) from CSV decks in /flashcards.
//
// Decks are the .csv/.tsv/.txt files of /flashcards and, in the card root, the
// files named "<deck>.flashcards.txt" (the Wi-Fi portal only takes books, and
// a .txt passes as one).
//
// A deck is a text file with one card per line: "front,back", or
// "id,front,back[,...]" (the layout CrossPet used; its scheduling columns are
// ignored). Comma, semicolon and tab separators are recognised, fields may be
// quoted, "/n" inside a field is a line break. The file is never written:
// what has been learnt lives in /.inkling/fc_<deck>.bin.
class Flashcards {
 public:
  static constexpr int kMaxDecks = 8;
  static constexpr int kMaxCards = 500;      // per deck
  static constexpr int kSessionReviews = 20; // cards due again, per session
  static constexpr int kSessionNew = 8;      // cards never seen, per session

  struct Deck {
    char path[80];
    char name[36];
  };

  enum class Grade : uint8_t { Again = 1, Hard = 3, Good = 4, Easy = 5 };

  void scan(Storage& storage);
  int deckCount() const { return deckCount_; }
  const Deck& deck(int i) const { return decks_[i]; }

  // Starts a session on a deck: the cards due today, then a few new ones.
  // False when the deck cannot be read or there is no memory for it.
  bool begin(Storage& storage, int deckIndex, int32_t today);
  // Ends the session and frees its memory (progress is saved card by card).
  void end();

  bool active() const { return srs_ != nullptr; }
  bool finished() const { return active() && queueLen_ == 0; }
  const char* deckName() const { return deckIndex_ >= 0 ? decks_[deckIndex_].name : ""; }
  const char* front() const { return front_; }
  const char* back() const { return back_; }
  bool showingBack() const { return showingBack_; }
  void reveal() { showingBack_ = true; }
  // Days until the card would come back for each grade (for the button hints).
  uint16_t intervalFor(Grade grade) const;
  void grade(Storage& storage, Grade grade);

  int remaining() const { return queueLen_; }
  int reviewed() const { return reviewed_; }
  int deckCards() const { return cardCount_; }
  int learnedCards() const;   // seen at least once
  int dueTomorrow() const;

 private:
  struct Srs {
    uint32_t idHash;
    uint16_t dueDay;     // days since 2020-01-01; 0 = never seen
    uint16_t interval;   // days
    uint16_t ease;       // x1000
    uint8_t reps;
    uint8_t lapses;
  };
  struct QueueItem {
    uint32_t offset;  // row start in the CSV
    uint16_t srs;
  };

  void loadCurrent(Storage& storage);
  void save(Storage& storage);
  static void schedule(Srs& card, Grade grade, uint16_t today);

  Deck decks_[kMaxDecks];
  int deckCount_ = 0;
  int deckIndex_ = -1;

  // Session state. `block_` is one allocation: the file header + kMaxCards
  // records, saved as it is.
  uint8_t* block_ = nullptr;
  Srs* srs_ = nullptr;
  int cardCount_ = 0;
  QueueItem queue_[kSessionReviews + kSessionNew];
  int queueLen_ = 0;
  int reviewed_ = 0;
  uint16_t today_ = 0;
  char separator_ = ',';
  bool idColumn_ = false;
  bool showingBack_ = false;
  char sidePath_[48] = {};
  char front_[200] = {};
  char back_[360] = {};
};

}  // namespace inkling
