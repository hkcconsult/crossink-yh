#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "quizlet/QuizletDeckParser.h"
#include "quizlet/QuizletDeckStore.h"
#include "quizlet/QuizletSession.h"

// Shows one flashcard at a time.
// Buttons: Confirm flips the card. After flipping, Right/Down/page-forward
// means "got it" and Left/Up/page-back means "again" (the card comes back
// later). Touch (X4 Pro): tap flips, swipe right = got it, swipe left = again.
class QuizletStudyActivity final : public Activity {
 public:
  QuizletStudyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckPath,
                       std::string deckTitle);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::string deckPath;
  std::string deckTitle;
  std::vector<quizlet::Card> cards;
  quizlet::Session session;
  quizlet::LoadResult loadResult = quizlet::LoadResult::Ok;
  int frontFont = 0;
  int backFont = 0;

  void resolveCardFonts();
  void restart();
  void onFlip();
  void onAnswer(bool knewIt);

  void renderMessage(const char* message) const;
  void renderCard(int top, int bottom) const;
  void renderDone(int top, int bottom) const;
  // Draws word-wrapped, horizontally centered text inside [top, bottom).
  // Returns the y below the last line.
  int drawWrappedCentered(int fontId, const char* text, int x, int width, int top, int bottom, int maxLines, bool bold,
                          bool verticallyCenter) const;
};
