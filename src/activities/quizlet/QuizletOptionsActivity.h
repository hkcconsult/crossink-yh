#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <string>

#include "activities/Activity.h"
#include "quizlet/QuizletStudy.h"
#include "util/ButtonNavigator.h"

// Shown after picking a deck: choose mode, "answer with" and order, then start.
// Confirm (or a tap) on an option row cycles its value; the choice is remembered.
class QuizletOptionsActivity final : public Activity {
 public:
  QuizletOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckPath,
                         std::string deckTitle);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // RowContinue is only shown while an unfinished round is saved for the deck.
  enum Row : int16_t { RowContinue = 0, RowStart, RowMode, RowAnswer, RowOrder, RowTotal };

  std::string deckPath;
  std::string deckTitle;
  quizlet::StudyOptions options;
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  bool hasRound = false;
  unsigned roundLeft = 0;
  char continueLabel[48] = {};
  int rowCount() const { return hasRound ? RowTotal : RowTotal - 1; }
  Row rowAt(int index) const { return static_cast<Row>(hasRound ? index : index + 1); }
  void refreshRound();
  using UiApp = freeink::ui::FreeInkApp<8, 4>;
  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;
  std::atomic<bool> uiReady{false};
  // Only a Back press that starts on this screen counts. Leaving a study round
  // by holding the first button must not also close this screen on release.
  bool backArmed = false;

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
  void activate(int index);
  void startStudy(bool resume);
};
