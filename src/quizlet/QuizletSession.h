#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Study session over a deck of N cards. Cards answered "again" go to the back
// of the queue until every card has been answered "known" once. Pure logic, no
// rendering or input, so it can be unit tested on the host.
namespace quizlet {

class Session {
 public:
  // Starts a new round over cardCount cards. When shuffle is true the order is
  // randomized with the given seed (deterministic for tests).
  void start(size_t cardCount, bool shuffle, uint32_t seed);
  // Starts a round over the given deck indices, in that order (smart rounds).
  void startWith(std::vector<uint16_t> order, size_t deckSize);
  // Continues a saved round: pending cards in queue order with their "again"
  // counts, plus the totals reached so far.
  void restore(const std::vector<std::pair<uint16_t, uint8_t>>& pending, size_t deckSize, size_t total, size_t known,
               size_t firstTry);

  bool isFinished() const { return position >= queue.size(); }
  bool isFlipped() const { return flipped; }

  // Index into the deck of the card currently shown. Only valid while !isFinished().
  uint16_t currentCard() const { return queue[position]; }

  void flip() { flipped = !flipped; }
  // True when the current card has not been answered "again" in this round.
  bool isFirstAttempt() const { return attempts[queue[position]] == 0; }
  // Cards still to answer, in queue order, with their "again" counts.
  std::vector<std::pair<uint16_t, uint8_t>> pendingCards() const;
  // Card was known: it leaves the queue.
  void markKnown();
  // Card was not known yet: it comes back at the end of the queue.
  void markAgain();

  size_t cardCount() const { return totalCards; }
  // Cards answered "known" so far in this session.
  size_t knownCount() const { return known; }
  // Cards known on the very first attempt.
  size_t firstTryCount() const { return firstTry; }
  // Cards still waiting to be answered "known" (including the current card).
  size_t remainingCount() const { return totalCards - known; }

 private:
  std::vector<uint16_t> queue;
  std::vector<uint8_t> attempts;  // per deck card: times answered "again"
  size_t position = 0;
  size_t totalCards = 0;
  size_t known = 0;
  size_t firstTry = 0;
  bool flipped = false;
};

}  // namespace quizlet
