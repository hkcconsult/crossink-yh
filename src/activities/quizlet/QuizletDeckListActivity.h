#pragma once

#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <vector>

#include <I18n.h>

#include "activities/Activity.h"
#include "quizlet/QuizletDeckStore.h"
#include "util/ButtonNavigator.h"

// Lists flashcard decks from /quizlet on the SD card and opens one to study.
class QuizletDeckListActivity final : public Activity {
 public:
  QuizletDeckListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                          quizlet::Library library = quizlet::Library::Flashcards);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  quizlet::Library library;
  std::vector<quizlet::DeckEntry> decks;
  bool hasSync = false;  // first row is "Sync with dashboard" when /quizlet/sync.cfg exists
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  using UiApp = freeink::ui::FreeInkApp<20, 4>;
  freeink::ui::GfxRendererTarget uiTarget;
  UiApp app;
  std::atomic<bool> uiReady{false};
  int visibleRows = 1;
  int topIndex = 0;

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
  int rowCount() const { return static_cast<int>(decks.size()) + (hasSync ? 1 : 0); }
  void reload();
  const char* title() const {
    return library == quizlet::Library::Chinese ? tr(STR_CHINESE_TITLE) : tr(STR_QUIZLET_TITLE);
  }
  void activateRow(int row);
  void openSync();
  void openDeck(int index);
};
