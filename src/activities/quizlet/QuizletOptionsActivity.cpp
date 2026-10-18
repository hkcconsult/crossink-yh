#include "QuizletOptionsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "QuizletStudyActivity.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "quizlet/QuizletDeckStore.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_ROW = 1;

const char* modeLabel(const quizlet::StudyMode mode) {
  switch (mode) {
    case quizlet::StudyMode::Flip:
      return tr(STR_QUIZLET_MODE_FLIP);
    case quizlet::StudyMode::MultipleChoice:
      return tr(STR_QUIZLET_MODE_CHOICE);
    case quizlet::StudyMode::Speed:
      return tr(STR_QUIZLET_MODE_SPEED);
  }
  return "";
}

const char* answerLabel(const quizlet::AnswerWith answer) {
  switch (answer) {
    case quizlet::AnswerWith::Definition:
      return tr(STR_QUIZLET_DEFINITION);
    case quizlet::AnswerWith::Term:
      return tr(STR_QUIZLET_TERM);
    case quizlet::AnswerWith::Both:
      return tr(STR_QUIZLET_BOTH);
  }
  return "";
}

const char* orderLabel(const quizlet::Order order) {
  switch (order) {
    case quizlet::Order::InOrder:
      return tr(STR_QUIZLET_IN_ORDER);
    case quizlet::Order::Shuffled:
      return tr(STR_QUIZLET_SHUFFLED);
    case quizlet::Order::Smart:
      return tr(STR_QUIZLET_SMART);
  }
  return "";
}
}  // namespace

QuizletOptionsActivity::QuizletOptionsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::string deckPath, std::string deckTitle)
    : Activity("QuizletOptions", renderer, mappedInput),
      deckPath(std::move(deckPath)),
      deckTitle(std::move(deckTitle)),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void QuizletOptionsActivity::onEnter() {
  Activity::onEnter();
  options = quizlet::loadOptions();
  refreshRound();
  selectedIndex = 0;
  backArmed = false;
  uiReady = false;
  applySharedUiTheme(app, uiTarget);
  app.on(ACTION_ROW, &QuizletOptionsActivity::onRowEvent, this);
  app.setScreen(&QuizletOptionsActivity::listScreen, this);
  requestUpdate();
}

void QuizletOptionsActivity::refreshRound() {
  quizlet::SavedRound round;
  hasRound = quizlet::loadRound(deckPath, round);
  roundLeft = static_cast<unsigned>(round.pending.size());
  snprintf(continueLabel, sizeof(continueLabel), tr(STR_QUIZLET_CONTINUE_FORMAT), roundLeft);
}

void QuizletOptionsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) backArmed = true;
  if (TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer)) ||
      (backArmed && mappedInput.wasReleased(MappedInputManager::Button::Back))) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate(selectedIndex);
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
  buttonNavigator.onNextRelease([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, rowCount());
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, rowCount());
    requestUpdate();
  });
}

void QuizletOptionsActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<QuizletOptionsActivity*>(user);
  if (event.value < 0 || event.value >= self->rowCount()) return;
  self->selectedIndex = event.value;
  self->app.clearTapFlash();
  self->activate(event.value);
}

void QuizletOptionsActivity::activate(const int index) {
  switch (rowAt(index)) {
    case RowContinue:
      startStudy(true);
      return;
    case RowStart:
      startStudy(false);
      return;
    case RowMode:
      options.mode = static_cast<quizlet::StudyMode>((static_cast<int>(options.mode) + 1) % 3);
      break;
    case RowAnswer:
      options.answerWith = static_cast<quizlet::AnswerWith>((static_cast<int>(options.answerWith) + 1) % 3);
      break;
    case RowOrder:
      options.order = static_cast<quizlet::Order>((static_cast<int>(options.order) + 1) % 3);
      break;
    case RowTotal:
      return;
  }
  quizlet::saveOptions(options);
  requestUpdate();
}

void QuizletOptionsActivity::startStudy(const bool resume) {
  auto study = makeUniqueNoThrow<QuizletStudyActivity>(renderer, mappedInput, deckPath, deckTitle, options, resume);
  if (!study) {
    LOG_ERR("QZL", "OOM: QuizletStudyActivity");
    return;
  }
  startActivityForResult(std::move(study), [this](const ActivityResult&) {
    backArmed = false;
    refreshRound();
    // Back on top: Continue when the round was left unfinished, else Start.
    selectedIndex = 0;
    requestUpdate();
  });
}

void QuizletOptionsActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<QuizletOptionsActivity*>(user)->buildListScreen(screen);
}

void QuizletOptionsActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(CompactHeader::contentTop(metrics)), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListItem items[RowTotal];
  const int count = rowCount();
  for (int i = 0; i < count; ++i) {
    fui::ListItem& item = items[i];
    item.actionValue = static_cast<int16_t>(i);
    switch (rowAt(i)) {
      case RowContinue:
        item.label = continueLabel;
        break;
      case RowStart:
        item.label = hasRound ? tr(STR_QUIZLET_START_NEW) : tr(STR_QUIZLET_START);
        break;
      case RowMode:
        item.label = tr(STR_QUIZLET_MODE);
        item.value = modeLabel(options.mode);
        break;
      case RowAnswer:
        item.label = tr(STR_QUIZLET_ANSWER_WITH);
        item.value = answerLabel(options.answerWith);
        break;
      case RowOrder:
        item.label = tr(STR_QUIZLET_ORDER);
        item.value = orderLabel(options.order);
        break;
      case RowTotal:
        break;
    }
  }

  fui::ListProps props;
  props.items = items;
  props.count = static_cast<int16_t>(count);
  props.selectedIndex = static_cast<int16_t>(selectedIndex);
  props.action = ACTION_ROW;
  props.inputMask = static_cast<uint16_t>(fui::InputTouch);
  props.labelText = screen.theme().bodyText;
  props.labelText.bold = true;
  configureUiList(props, screen.theme(), screen.body(), UiListRowType::SingleLine);
  props.topIndex = 0;
  screen.list(props);
}

void QuizletOptionsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, deckTitle.c_str());
  } else {
    CompactHeader::drawTitle(renderer, deckTitle.c_str());
  }
  uiReady = false;
  app.render();
  uiReady = true;

  const Row row = rowAt(selectedIndex);
  const char* confirm = row == RowStart || row == RowContinue ? tr(STR_QUIZLET_START) : tr(STR_SELECT);
  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
