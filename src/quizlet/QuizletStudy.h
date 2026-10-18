#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "QuizletDeckParser.h"

// Study options and helpers for the flashcard modes. Pure logic without SD or
// rendering dependencies so it can be unit tested on the host.
namespace quizlet {

enum class StudyMode : uint8_t { Flip = 0, MultipleChoice = 1, Speed = 2 };
// What the learner has to produce. Definition: the term is shown and the
// definition is the answer (Quizlet's default "Answer with: Definition").
enum class AnswerWith : uint8_t { Definition = 0, Term = 1, Both = 2 };

struct StudyOptions {
  StudyMode mode = StudyMode::Flip;
  AnswerWith answerWith = AnswerWith::Definition;
  bool shuffle = true;
};

constexpr uint32_t kSpeedRoundMs = 60 * 1000;
constexpr size_t kMaxChoices = 4;

// xorshift32: tiny deterministic PRNG, good enough for shuffling flashcards.
inline uint32_t nextRandom(uint32_t& state) {
  if (state == 0) state = 0x9E3779B9u;
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// "mode=1\nanswer=0\nshuffle=1\n". Unknown or invalid lines keep defaults.
std::string serializeOptions(const StudyOptions& options);
StudyOptions parseOptions(const char* text, size_t length);

// Decides per card whether the definition is the answer (true) or the term.
bool answerIsDefinition(AnswerWith answerWith, uint32_t& rng);

// Multiple choice for cards[cardIndex]. Fills `choices` with up to
// kMaxChoices card indices whose answer text is shown as an option. Options
// never overlap: no two are equal (case-insensitive) or share a part split on
// '/', ',' or ';', so "opsommend / tijdsvolgorde" is never offered as a wrong
// answer next to "tijdsvolgorde". The correct card is placed at a
// random slot. Returns the number of choices (0 when the deck has fewer than
// two distinct answers, in which case multiple choice is not possible).
size_t buildChoices(const std::vector<Card>& cards, uint16_t cardIndex, bool answerIsDefinition, uint32_t& rng,
                    uint16_t (&choices)[kMaxChoices], size_t& correctSlot);

// True when text contains CJK ideographs, kana, CJK punctuation or fullwidth
// forms, which the built-in fonts cannot draw.
bool containsCjk(const std::string& text);
// True for an installed SD font family meant for CJK ("NotoSansCJK", ...).
bool isCjkFamilyName(const std::string& name);

// Speed-round records, one "path<TAB>best" line per deck.
unsigned recordFor(const std::string& recordsText, const std::string& deckPath);
// Returns the updated records text with `best` stored for deckPath.
std::string withRecord(const std::string& recordsText, const std::string& deckPath, unsigned best);

}  // namespace quizlet
