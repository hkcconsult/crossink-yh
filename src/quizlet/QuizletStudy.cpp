#include "QuizletStudy.h"

#include <cctype>
#include <cstdlib>
#include <utility>
#include <vector>

namespace quizlet {
namespace {

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

std::string trimmedLower(const std::string& value, size_t begin, size_t end) {
  while (begin < end && isspace(static_cast<unsigned char>(value[begin]))) ++begin;
  while (end > begin && isspace(static_cast<unsigned char>(value[end - 1]))) --end;
  std::string out = value.substr(begin, end - begin);
  for (char& c : out) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return out;
}

// Splits "uitleggend / vergelijkend" or "a, b" into lowercase parts.
std::vector<std::string> answerParts(const std::string& value) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t i = 0; i <= value.size(); ++i) {
    if (i == value.size() || value[i] == '/' || value[i] == ',' || value[i] == ';') {
      std::string part = trimmedLower(value, start, i);
      if (!part.empty()) parts.push_back(std::move(part));
      start = i + 1;
    }
  }
  return parts;
}

// True when two answers are equal or share a part, so one would be partly right
// as an alternative for the other (e.g. "opsommend / tijdsvolgorde" vs "tijdsvolgorde").
bool answersOverlap(const std::string& a, const std::string& b) {
  if (equalsIgnoreCase(a, b)) return true;
  const auto partsA = answerParts(a);
  const auto partsB = answerParts(b);
  for (const auto& x : partsA) {
    for (const auto& y : partsB) {
      if (x == y) return true;
    }
  }
  return false;
}

const std::string& answerText(const Card& card, const bool answerIsDefinition) {
  return answerIsDefinition ? card.definition : card.term;
}

// Calls fn(key, value) for every "key=value" / "key<TAB>value" line.
template <typename Fn>
void forEachLine(const char* text, const size_t length, const char separator, Fn fn) {
  size_t start = 0;
  while (start < length) {
    size_t end = start;
    while (end < length && text[end] != '\n') ++end;
    size_t lineEnd = end;
    if (lineEnd > start && text[lineEnd - 1] == '\r') --lineEnd;
    for (size_t i = start; i < lineEnd; ++i) {
      if (text[i] == separator) {
        fn(std::string(text + start, i - start), std::string(text + i + 1, lineEnd - i - 1));
        break;
      }
    }
    start = end + 1;
  }
}

}  // namespace

std::string serializeOptions(const StudyOptions& options) {
  std::string out;
  out += "mode=" + std::to_string(static_cast<unsigned>(options.mode)) + "\n";
  out += "answer=" + std::to_string(static_cast<unsigned>(options.answerWith)) + "\n";
  out += "order=" + std::to_string(static_cast<unsigned>(options.order)) + "\n";
  return out;
}

StudyOptions parseOptions(const char* text, const size_t length) {
  StudyOptions options;
  if (text == nullptr) return options;
  forEachLine(text, length, '=', [&options](const std::string& key, const std::string& value) {
    const long number = strtol(value.c_str(), nullptr, 10);
    if (key == "mode" && number >= 0 && number <= 2) options.mode = static_cast<StudyMode>(number);
    if (key == "answer" && number >= 0 && number <= 2) options.answerWith = static_cast<AnswerWith>(number);
    if (key == "shuffle") options.order = number != 0 ? Order::Shuffled : Order::InOrder;
    if (key == "order" && number >= 0 && number <= 2) options.order = static_cast<Order>(number);
  });
  return options;
}

bool answerIsDefinition(const AnswerWith answerWith, uint32_t& rng) {
  switch (answerWith) {
    case AnswerWith::Definition:
      return true;
    case AnswerWith::Term:
      return false;
    case AnswerWith::Both:
      return (nextRandom(rng) & 1u) != 0;
  }
  return true;
}

size_t buildChoices(const std::vector<Card>& cards, const uint16_t cardIndex, const bool answerIsDef, uint32_t& rng,
                    uint16_t (&choices)[kMaxChoices], size_t& correctSlot) {
  correctSlot = 0;
  if (cardIndex >= cards.size()) return 0;
  const std::string& correct = answerText(cards[cardIndex], answerIsDef);
  // The question side of the current card. A card whose question side overlaps
  // is just as valid an answer (e.g. "opsommend" -> "ook" and "ten eerste"), so
  // it can never serve as a wrong option.
  const std::string& prompt = answerText(cards[cardIndex], !answerIsDef);

  // Collect candidate distractors with distinct answer texts. Starting at a
  // random offset spreads the picks over the deck without shuffling it.
  uint16_t picked[kMaxChoices - 1];
  size_t pickedCount = 0;
  const size_t n = cards.size();
  const size_t offset = n > 0 ? nextRandom(rng) % n : 0;
  for (size_t step = 0; step < n && pickedCount < kMaxChoices - 1; ++step) {
    const size_t i = (offset + step) % n;
    if (i == cardIndex) continue;
    const std::string& candidate = answerText(cards[i], answerIsDef);
    if (answersOverlap(candidate, correct)) continue;
    if (answersOverlap(answerText(cards[i], !answerIsDef), prompt)) continue;
    bool duplicate = false;
    for (size_t p = 0; p < pickedCount; ++p) {
      if (answersOverlap(candidate, answerText(cards[picked[p]], answerIsDef))) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) picked[pickedCount++] = static_cast<uint16_t>(i);
  }
  if (pickedCount == 0) return 0;

  const size_t count = pickedCount + 1;
  correctSlot = nextRandom(rng) % count;
  size_t next = 0;
  for (size_t slot = 0; slot < count; ++slot) {
    choices[slot] = slot == correctSlot ? cardIndex : picked[next++];
  }
  return count;
}

unsigned recordFor(const std::string& recordsText, const std::string& deckPath) {
  unsigned best = 0;
  forEachLine(recordsText.c_str(), recordsText.size(), '\t', [&](const std::string& path, const std::string& value) {
    if (path == deckPath) best = static_cast<unsigned>(strtoul(value.c_str(), nullptr, 10));
  });
  return best;
}

std::string withRecord(const std::string& recordsText, const std::string& deckPath, const unsigned best) {
  std::string out;
  forEachLine(recordsText.c_str(), recordsText.size(), '\t', [&](const std::string& path, const std::string& value) {
    if (path != deckPath) out += path + "\t" + value + "\n";
  });
  out += deckPath + "\t" + std::to_string(best) + "\n";
  return out;
}

bool containsCjk(const std::string& text) {
  const size_t n = text.size();
  for (size_t i = 0; i + 2 < n; ++i) {
    const auto b0 = static_cast<unsigned char>(text[i]);
    if ((b0 & 0xF0) != 0xE0) continue;  // CJK blocks are all 3-byte UTF-8
    const auto b1 = static_cast<unsigned char>(text[i + 1]);
    const auto b2 = static_cast<unsigned char>(text[i + 2]);
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) continue;
    const uint32_t cp = ((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
    if ((cp >= 0x3000 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF)) {
      return true;
    }
    i += 2;
  }
  return false;
}

bool isCjkFamilyName(const std::string& name) {
  for (size_t i = 0; i + 3 <= name.size(); ++i) {
    if (toupper(static_cast<unsigned char>(name[i])) == 'C' && toupper(static_cast<unsigned char>(name[i + 1])) == 'J' &&
        toupper(static_cast<unsigned char>(name[i + 2])) == 'K') {
      return true;
    }
  }
  return false;
}

}  // namespace quizlet
