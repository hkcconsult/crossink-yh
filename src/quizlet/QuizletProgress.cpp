#include "QuizletProgress.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace quizlet {
namespace {

// Calls fn(line) for each non-empty line without the trailing '\r'.
template <typename Fn>
void forEachLine(const char* text, const size_t length, Fn fn) {
  size_t start = 0;
  while (start < length) {
    size_t end = start;
    while (end < length && text[end] != '\n') ++end;
    size_t stop = end;
    if (stop > start && text[stop - 1] == '\r') --stop;
    if (stop > start) fn(std::string(text + start, stop - start));
    start = end + 1;
  }
}

bool startsWith(const std::string& line, const char* prefix) {
  return line.compare(0, strlen(prefix), prefix) == 0;
}

unsigned long valueAfter(const std::string& line, const size_t prefixLength) {
  return strtoul(line.c_str() + prefixLength, nullptr, 10);
}

// Box priority for a smart round: struggling cards first, then new ones.
int priority(const uint8_t box) { return box == 1 ? 0 : box == 0 ? 1 : box; }

}  // namespace

uint32_t cardKey(const Card& card) {
  uint32_t hash = 2166136261u;
  auto mix = [&hash](const std::string& text) {
    for (const unsigned char c : text) {
      hash ^= c;
      hash *= 16777619u;
    }
  };
  mix(card.term);
  hash ^= static_cast<unsigned char>('\t');
  hash *= 16777619u;
  mix(card.definition);
  return hash;
}

void recordAnswer(CardProgress& progress, const bool knewIt, const bool firstAttempt) {
  if (!knewIt) {
    progress.box = 1;
    if (progress.wrong < UINT16_MAX) ++progress.wrong;
    return;
  }
  if (!firstAttempt) return;  // learned in this round, not yet known
  if (progress.right < UINT16_MAX) ++progress.right;
  // A new card known straight away skips the learning box.
  progress.box = progress.box == 0 ? 2 : static_cast<uint8_t>(std::min<int>(progress.box + 1, kMaxBox));
}

void markHard(CardProgress& progress) { progress.box = 1; }

bool isDue(const uint8_t box, const uint32_t round) {
  if (box <= 1) return true;
  const uint32_t every = 1u << (box - 1);  // 2, 4, 8, 16
  return round % every == 0;
}

std::vector<uint16_t> pickSmartRound(const std::vector<CardProgress>& cards, const uint32_t round, uint32_t& rng) {
  std::vector<uint16_t> order;
  order.reserve(cards.size());
  for (size_t i = 0; i < cards.size(); ++i) order.push_back(static_cast<uint16_t>(i));
  // Shuffle first, then a stable sort by priority keeps the shuffle within a box.
  for (size_t i = order.size(); i > 1; --i) {
    const size_t j = nextRandom(rng) % i;
    std::swap(order[i - 1], order[j]);
  }
  std::stable_sort(order.begin(), order.end(), [&cards](const uint16_t a, const uint16_t b) {
    return priority(cards[a].box) < priority(cards[b].box);
  });

  const size_t minimum = std::min<size_t>(5, cards.size());
  std::vector<uint16_t> picked;
  picked.reserve(std::min(kSmartRoundSize, cards.size()));
  for (const uint16_t index : order) {
    if (picked.size() >= kSmartRoundSize) break;
    if (isDue(cards[index].box, round)) picked.push_back(index);
  }
  for (const uint16_t index : order) {
    if (picked.size() >= minimum) break;
    if (std::find(picked.begin(), picked.end(), index) == picked.end()) picked.push_back(index);
  }
  return picked;
}

namespace {
int boxWeight(const unsigned box) { return box <= 1 ? 0 : static_cast<int>((std::min<unsigned>(box, kMaxBox) - 1) * 25); }
}  // namespace

int masteryPercent(const std::vector<CardProgress>& cards) {
  if (cards.empty()) return 0;
  long total = 0;
  for (const auto& card : cards) total += boxWeight(card.box);
  return static_cast<int>(total / static_cast<long>(cards.size()));
}

int masteryPercentFromText(const char* text, const size_t length) {
  if (text == nullptr) return -1;
  unsigned long deckSize = 0;
  long total = 0;
  forEachLine(text, length, [&](const std::string& line) {
    if (startsWith(line, "cards=")) {
      deckSize = valueAfter(line, 6);
      return;
    }
    if (startsWith(line, "rounds=")) return;
    char* end = nullptr;
    strtoul(line.c_str(), &end, 16);
    if (end == line.c_str()) return;
    total += boxWeight(static_cast<unsigned>(strtoul(end, nullptr, 10)));
  });
  if (deckSize == 0) return -1;
  return static_cast<int>(std::min<long>(100, total / static_cast<long>(deckSize)));
}

std::string serializeProgress(const DeckProgress& progress) {
  std::string out = "rounds=" + std::to_string(progress.rounds) + "\n";
  out += "cards=" + std::to_string(progress.cards.size()) + "\n";
  char line[48];
  for (const auto& card : progress.cards) {
    if (card.box == 0 && card.wrong == 0 && card.right == 0) continue;
    snprintf(line, sizeof(line), "%08lx %u %u %u\n", static_cast<unsigned long>(card.key),
             static_cast<unsigned>(card.box), static_cast<unsigned>(card.wrong), static_cast<unsigned>(card.right));
    out += line;
  }
  return out;
}

DeckProgress parseProgress(const char* text, const size_t length, const std::vector<Card>& deck) {
  DeckProgress progress;
  progress.cards.resize(deck.size());
  // Sorted (key, index) pairs for lookups; decks hold up to 500 cards.
  std::vector<std::pair<uint32_t, uint16_t>> index;
  index.reserve(deck.size());
  for (size_t i = 0; i < deck.size(); ++i) {
    progress.cards[i].key = cardKey(deck[i]);
    index.emplace_back(progress.cards[i].key, static_cast<uint16_t>(i));
  }
  std::sort(index.begin(), index.end());
  if (text == nullptr) return progress;

  forEachLine(text, length, [&](const std::string& line) {
    if (startsWith(line, "rounds=")) {
      progress.rounds = static_cast<uint32_t>(valueAfter(line, 7));
      return;
    }
    if (startsWith(line, "cards=")) return;  // deck size for the overview only
    char* end = nullptr;
    const uint32_t key = static_cast<uint32_t>(strtoul(line.c_str(), &end, 16));
    if (end == line.c_str()) return;
    const unsigned long box = strtoul(end, &end, 10);
    const unsigned long wrong = strtoul(end, &end, 10);
    const unsigned long right = strtoul(end, &end, 10);
    auto it = std::lower_bound(index.begin(), index.end(), std::make_pair(key, static_cast<uint16_t>(0)));
    // Duplicate cards share a key; give all of them the stored values.
    for (; it != index.end() && it->first == key; ++it) {
      CardProgress& card = progress.cards[it->second];
      card.box = static_cast<uint8_t>(std::min<unsigned long>(box, kMaxBox));
      card.wrong = static_cast<uint16_t>(std::min<unsigned long>(wrong, UINT16_MAX));
      card.right = static_cast<uint16_t>(std::min<unsigned long>(right, UINT16_MAX));
    }
  });
  return progress;
}

std::string serializeRound(const SavedRound& round) {
  std::string out = serializeOptions(round.options);
  out += "total=" + std::to_string(round.total) + "\n";
  out += "known=" + std::to_string(round.known) + "\n";
  out += "first=" + std::to_string(round.firstTry) + "\n";
  char line[32];
  for (const auto& [key, attempts] : round.pending) {
    snprintf(line, sizeof(line), "c=%08lx %u\n", static_cast<unsigned long>(key), static_cast<unsigned>(attempts));
    out += line;
  }
  return out;
}

bool parseRound(const char* text, const size_t length, SavedRound& out) {
  out = SavedRound{};
  if (text == nullptr || length == 0) return false;
  out.options = parseOptions(text, length);
  forEachLine(text, length, [&out](const std::string& line) {
    if (startsWith(line, "total=")) {
      out.total = valueAfter(line, 6);
    } else if (startsWith(line, "known=")) {
      out.known = valueAfter(line, 6);
    } else if (startsWith(line, "first=")) {
      out.firstTry = valueAfter(line, 6);
    } else if (startsWith(line, "c=")) {
      char* end = nullptr;
      const uint32_t key = static_cast<uint32_t>(strtoul(line.c_str() + 2, &end, 16));
      if (end == line.c_str() + 2) return;
      const unsigned long attempts = strtoul(end, nullptr, 10);
      out.pending.emplace_back(key, static_cast<uint8_t>(std::min<unsigned long>(attempts, UINT8_MAX)));
    }
  });
  return !out.pending.empty() && out.total >= out.known + out.pending.size();
}

}  // namespace quizlet
