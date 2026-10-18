#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "QuizletDeckParser.h"
#include "QuizletStudy.h"

// Per-card learning progress (Leitner boxes) and saving an unfinished round.
// Pure logic without SD or rendering so it can be unit tested on the host.
//
// Cards are identified by a hash of "term<TAB>definition", so progress
// survives a re-sync that reorders or adds cards. The dashboard computes the
// same key to show which cards are hard.
namespace quizlet {

constexpr uint8_t kMaxBox = 5;
// A smart round asks at most this many cards, the ones that are due first.
constexpr size_t kSmartRoundSize = 20;

struct CardProgress {
  uint32_t key = 0;
  uint8_t box = 0;  // 0 = never answered, 1 = learning ... kMaxBox = known well
  uint16_t wrong = 0;
  uint16_t right = 0;
};

struct DeckProgress {
  uint32_t rounds = 0;               // smart rounds started; drives the Leitner schedule
  std::vector<CardProgress> cards;   // aligned with the deck's cards
};

// FNV-1a over "term\tdefinition" (UTF-8 bytes).
uint32_t cardKey(const Card& card);

// First answer for a card in a round moves it up a box when right. Any wrong
// answer sends it back to box 1.
void recordAnswer(CardProgress& progress, bool knewIt, bool firstAttempt);

// Box 0 and 1 are asked every round, box 2 every 2nd, box 3 every 4th, ...
bool isDue(uint8_t box, uint32_t round);

// Card indices for a smart round: due cards, hardest first (box 1, then new
// cards, then higher boxes), shuffled within a box, at most kSmartRoundSize.
// When few cards are due the round is topped up with the lowest boxes so a
// round never has fewer than min(5, deck size) cards.
std::vector<uint16_t> pickSmartRound(const std::vector<CardProgress>& cards, uint32_t round, uint32_t& rng);

// How well a deck is known, 0..100: box 2 counts 25%, box 3 50%, box 4 75%,
// box 5 100%; new and struggling cards (box 0, 1) count 0.
int masteryPercent(const std::vector<CardProgress>& cards);
// Same from a saved progress file without loading the deck (the file records
// the deck size). -1 when the file has no deck size, e.g. never studied.
int masteryPercentFromText(const char* text, size_t length);

// "rounds=N\ncards=N\n" then "KEYHEX box wrong right" per card that has any history.
std::string serializeProgress(const DeckProgress& progress);
// Fills progress for the given deck: known keys get their stored values, other
// cards start fresh. Unknown keys (cards removed from the deck) are dropped.
DeckProgress parseProgress(const char* text, size_t length, const std::vector<Card>& deck);

// An unfinished flip or multiple-choice round, saved when leaving it.
struct SavedRound {
  StudyOptions options;
  size_t total = 0;
  size_t known = 0;
  size_t firstTry = 0;
  std::vector<std::pair<uint32_t, uint8_t>> pending;  // card key, "again" count, in queue order
};

std::string serializeRound(const SavedRound& round);
// Returns false for an empty or malformed file.
bool parseRound(const char* text, size_t length, SavedRound& out);

}  // namespace quizlet
