#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "fontIds.h"
#include "quizlet/QuizletDeckParser.h"
#include "quizlet/QuizletDeckStore.h"
#include "quizlet/QuizletSession.h"
#include "quizlet/QuizletStudy.h"

// Studies one deck in the mode chosen on the options screen.
//
// Flip: Confirm flips the card; afterwards Right/Down/page-forward means "got
// it" and Left/Up/page-back means "again". Touch: tap flips, swipe right/left.
//
// Multiple choice: options A-D sit on the four front buttons in physical
// order (independent of button remapping). Holding A stops. Touch: tap an option.
//
// Speed round: multiple choice against a 60 second clock; a right answer moves
// on immediately, a wrong one shows the correct answer first. Best score per
// deck is stored on the SD card.
class QuizletStudyActivity final : public Activity {
 public:
  QuizletStudyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckPath,
                       std::string deckTitle, quizlet::StudyOptions options);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Box {
    int x = 0, y = 0, w = 0, h = 0;
  };

  std::string deckPath;
  std::string deckTitle;
  quizlet::StudyOptions options;
  std::vector<quizlet::Card> cards;
  quizlet::Session session;
  quizlet::LoadResult loadResult = quizlet::LoadResult::Ok;
  int frontFont = 0;
  int backFont = 0;
  int optionFont = UI_12_FONT_ID;   // multiple-choice options
  int contextFont = UI_12_FONT_ID;  // prompt repeated above the answer
  // True while an SD-card CJK family replaces the reader font for this deck.
  bool usingCjkFont = false;
  uint32_t rng = 1;

  // Current card presentation.
  bool answerIsDefinition = true;
  uint16_t choices[quizlet::kMaxChoices] = {};
  size_t choiceCount = 0;
  size_t correctSlot = 0;
  int chosenSlot = -1;  // -1 while the question is open
  bool choiceUnavailable = false;
  Box optionRects[quizlet::kMaxChoices];  // written by render(), read for touch hit tests

  // Long-press on the first front button stops multiple choice and speed rounds.
  unsigned long stopPressStartMs = 0;
  bool stopHandled = false;

  // Speed round.
  unsigned long speedStartMs = 0;
  unsigned speedCorrect = 0;
  unsigned speedRecord = 0;
  bool speedFinished = false;
  bool newRecord = false;
  unsigned lastShownSecondsBucket = 0;

  bool usesChoices() const { return options.mode != quizlet::StudyMode::Flip; }
  const std::string& promptText() const;
  const std::string& answerText() const;
  unsigned speedSecondsLeft() const;

  void resolveCardFonts();
  void activateCjkFontIfNeeded();
  void drawScreen();
  void restart();
  void prepareCard();
  void answerChoice(int slot);
  void advance(bool knewIt);
  void finishSpeedRound();
  bool handleStopLongPress();

  void loopFlip(bool tapped, MappedInputManager::SwipeDir swipe);
  void loopChoice(bool tapped, int tapX, int tapY);

  void renderMessage(const char* message) const;
  void renderStatus(int top) const;
  void renderFlipCard(int top, int bottom) const;
  void renderChoiceCard(int top, int bottom);
  void renderDone(int top, int bottom) const;
  void renderSpeedDone(int top, int bottom) const;
  // Draws word-wrapped, horizontally centered text inside [top, bottom).
  // Returns the y below the last line.
  int drawWrappedCentered(int fontId, const char* text, int x, int width, int top, int bottom, int maxLines, bool bold,
                          bool verticallyCenter, bool black = true) const;
};
