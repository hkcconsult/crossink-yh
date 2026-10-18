#include "QuizletSession.h"

#include <utility>

#include "QuizletStudy.h"

namespace quizlet {

void Session::start(const size_t cardCount, const bool shuffle, const uint32_t seed) {
  totalCards = cardCount;
  position = 0;
  known = 0;
  firstTry = 0;
  flipped = false;

  queue.clear();
  attempts.assign(cardCount, 0);
  // Worst case every card is answered "again" a few times; reserve some slack
  // up front to avoid repeated reallocation while studying.
  queue.reserve(cardCount + cardCount / 2 + 1);
  for (size_t i = 0; i < cardCount; ++i) queue.push_back(static_cast<uint16_t>(i));

  if (shuffle && cardCount > 1) {
    uint32_t state = seed;
    for (size_t i = cardCount - 1; i > 0; --i) {
      const size_t j = nextRandom(state) % (i + 1);
      std::swap(queue[i], queue[j]);
    }
  }
}

void Session::startWith(std::vector<uint16_t> order, const size_t deckSize) {
  queue = std::move(order);
  attempts.assign(deckSize, 0);
  totalCards = queue.size();
  position = 0;
  known = 0;
  firstTry = 0;
  flipped = false;
}

void Session::restore(const std::vector<std::pair<uint16_t, uint8_t>>& pending, const size_t deckSize,
                      const size_t total, const size_t known_, const size_t firstTry_) {
  queue.clear();
  queue.reserve(pending.size() + pending.size() / 2 + 1);
  attempts.assign(deckSize, 0);
  for (const auto& [card, again] : pending) {
    if (card >= deckSize) continue;
    queue.push_back(card);
    attempts[card] = again;
  }
  position = 0;
  known = known_;
  firstTry = firstTry_;
  totalCards = known + queue.size();
  (void)total;  // derived from known + pending so a changed deck stays consistent
  flipped = false;
}

std::vector<std::pair<uint16_t, uint8_t>> Session::pendingCards() const {
  std::vector<std::pair<uint16_t, uint8_t>> out;
  if (isFinished()) return out;
  out.reserve(queue.size() - position);
  for (size_t i = position; i < queue.size(); ++i) out.emplace_back(queue[i], attempts[queue[i]]);
  return out;
}

void Session::markKnown() {
  if (isFinished()) return;
  const uint16_t card = queue[position];
  if (attempts[card] == 0) ++firstTry;
  ++known;
  ++position;
  flipped = false;
}

void Session::markAgain() {
  if (isFinished()) return;
  const uint16_t card = queue[position];
  if (attempts[card] < UINT8_MAX) ++attempts[card];
  queue.push_back(card);
  ++position;
  flipped = false;
  // Drop the consumed prefix now and then so the queue does not grow without
  // bound when the same few cards keep coming back.
  if (position > 64 && position * 2 > queue.size()) {
    queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(position));
    position = 0;
  }
}

}  // namespace quizlet
