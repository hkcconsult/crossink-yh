#include "QuizletDeckListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "MappedInputManager.h"
#include "QuizletOptionsActivity.h"
#include "QuizletSyncActivity.h"
#include "QuizletTodayActivity.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;
}  // namespace

QuizletDeckListActivity::QuizletDeckListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                 const quizlet::Library library)
    : Activity("QuizletDeckList", renderer, mappedInput),
      library(library),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void QuizletDeckListActivity::onEnter() {
  Activity::onEnter();
  reload();
  selectedIndex = 0;
  topIndex = 0;
  visibleRows = 1;
  uiReady = false;
  applySharedUiTheme(app, uiTarget);
  app.on(ACTION_ROW, &QuizletDeckListActivity::onRowEvent, this);
  app.setScreen(&QuizletDeckListActivity::listScreen, this);
  requestUpdate();
}

void QuizletDeckListActivity::onExit() {
  decks.clear();
  decks.shrink_to_fit();
  Activity::onExit();
}

void QuizletDeckListActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer))) {
    onGoHome();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const int listSize = rowCount();
  if (listSize == 0) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateRow(selectedIndex);
    return;
  }

  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;
    }
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int next = scrollListBy(topIndex, swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows,
                                  visibleRows, listSize);
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
    return;
  }

  const auto moveSelection = [this, listSize](const int next) {
    selectedIndex = next;
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, listSize);
    requestUpdate();
  };
  buttonNavigator.onNextRelease(
      [this, listSize, &moveSelection] { moveSelection(ButtonNavigator::nextIndex(selectedIndex, listSize)); });
  buttonNavigator.onPreviousRelease(
      [this, listSize, &moveSelection] { moveSelection(ButtonNavigator::previousIndex(selectedIndex, listSize)); });
  buttonNavigator.onNextContinuous([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::nextPageIndex(selectedIndex, listSize, visibleRows));
  });
  buttonNavigator.onPreviousContinuous([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::previousPageIndex(selectedIndex, listSize, visibleRows));
  });
}

void QuizletDeckListActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<QuizletDeckListActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->rowCount())) return;
  self->selectedIndex = event.value;
  self->app.clearTapFlash();
  self->activateRow(self->selectedIndex);
}

void QuizletDeckListActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<QuizletDeckListActivity*>(user)->buildListScreen(screen);
}

void QuizletDeckListActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(CompactHeader::contentTop(metrics)), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (rowCount() == 0) {
    screen.centeredText(tr(STR_QUIZLET_NO_DECKS), screen.theme().bodyText);
    return;
  }
  std::vector<fui::ListItem> items;
  items.reserve(rowCount());
  if (hasSync) {
    fui::ListItem item;
    item.label = tr(STR_QUIZLET_SYNC);
    item.actionValue = 0;
    items.push_back(item);
  }
  if (hasToday) {
    fui::ListItem item;
    item.label = tr(STR_QUIZLET_TODAY_ROW);
    item.actionValue = static_cast<int16_t>(items.size());
    items.push_back(item);
  }
  for (size_t i = 0; i < decks.size(); ++i) {
    fui::ListItem item;
    item.label = decks[i].title.c_str();
    if (decks[i].mastery >= 0) item.value = decks[i].masteryText;
    item.actionValue = static_cast<int16_t>(items.size());
    items.push_back(item);
  }
  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask = static_cast<uint16_t>(fui::InputTouch);
  props.labelText = screen.theme().bodyText;
  const fui::Rect bounds = screen.body();
  const auto rows = configureUiList(props, screen.theme(), bounds, UiListRowType::SingleLine);
  visibleRows = rows > 0 ? rows : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, rowCount());
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void QuizletDeckListActivity::render(RenderLock&&) {
  renderer.clearScreen();

  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, title());
  } else {
    CompactHeader::drawTitle(renderer, title());
  }
  uiReady = false;
  app.render();
  uiReady = true;

  const char* openLabel = rowCount() == 0 ? nullptr : tr(STR_OPEN);
  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_HOME)), openLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void QuizletDeckListActivity::reload() {
  quizlet::listDecks(library, decks);
  hasSync = quizlet::hasSyncConfig();
  hasToday = library == quizlet::Library::Flashcards && quizlet::hasTodayPlan();
}

void QuizletDeckListActivity::activateRow(const int row) {
  if (hasSync && row == 0) {
    openSync();
    return;
  }
  if (hasToday && row == (hasSync ? 1 : 0)) {
    openToday();
    return;
  }
  openDeck(row - extraRows());
}

void QuizletDeckListActivity::openSync() {
  auto sync = makeUniqueNoThrow<QuizletSyncActivity>(renderer, mappedInput, library);
  if (!sync) {
    LOG_ERR("QZL", "OOM: QuizletSyncActivity");
    return;
  }
  startActivityForResult(std::move(sync), [this](const ActivityResult&) {
    // The deck files may have changed; keep the selection on the sync row.
    reload();
    selectedIndex = 0;
    topIndex = 0;
    requestUpdate();
  });
}

void QuizletDeckListActivity::openToday() {
  auto today = makeUniqueNoThrow<QuizletTodayActivity>(renderer, mappedInput);
  if (!today) {
    LOG_ERR("QZL", "OOM: QuizletTodayActivity");
    return;
  }
  startActivityForResult(std::move(today), [this](const ActivityResult&) { requestUpdate(); });
}

void QuizletDeckListActivity::openDeck(const int index) {
  if (index < 0 || index >= static_cast<int>(decks.size())) return;
  auto study = makeUniqueNoThrow<QuizletOptionsActivity>(renderer, mappedInput, decks[index].path, decks[index].title);
  if (!study) {
    LOG_ERR("QZL", "OOM: QuizletOptionsActivity");
    return;
  }
  startActivityForResult(std::move(study), [this](const ActivityResult&) {
    reload();  // the percentage known changes after studying
    requestUpdate();
  });
}
