#include "QuizletTodayActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "quizlet/QuizletDeckStore.h"

namespace {
constexpr int kTitleFont = UI_12_FONT_ID;
constexpr int kBodyFont = UI_12_FONT_ID;
constexpr int kSmallFont = UI_10_FONT_ID;
}  // namespace

QuizletTodayActivity::QuizletTodayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("QuizletToday", renderer, mappedInput) {}

void QuizletTodayActivity::onEnter() {
  Activity::onEnter();
  loaded = quizlet::loadTodayPlan(plan);
  requestUpdate();
}

void QuizletTodayActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
      TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer))) {
    finish();
  }
}

// Draws text wrapped to the content width; returns the next y.
int QuizletTodayActivity::drawLine(const char* text, int y, const int bottom, const bool bold) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - 2 * side;
  const int lineHeight = renderer.getLineHeight(kBodyFont);
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  for (const auto& line : renderer.wrappedText(kBodyFont, text, width, 2, style)) {
    if (y + lineHeight > bottom) break;
    renderer.drawText(kBodyFont, side, y, line.c_str(), true, style);
    y += lineHeight;
  }
  return y;
}

int QuizletTodayActivity::drawSection(const char* title, int y, const int bottom) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  y += metrics.verticalSpacing;
  if (y + renderer.getLineHeight(kTitleFont) > bottom) return y;
  renderer.drawText(kTitleFont, metrics.contentSidePadding, y, title, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(kTitleFont) + 2;
  renderer.drawLine(metrics.contentSidePadding, y, renderer.getScreenWidth() - metrics.contentSidePadding, y, true);
  return y + metrics.verticalSpacing / 2;
}

void QuizletTodayActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, tr(STR_QUIZLET_TODAY_ROW));
  } else {
    CompactHeader::drawTitle(renderer, tr(STR_QUIZLET_TODAY_ROW));
  }
  const auto& metrics = UITheme::getInstance().getMetrics();
  int y = CompactHeader::contentTop(metrics) + metrics.verticalSpacing / 2;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (!loaded) {
    drawLine(tr(STR_QUIZLET_TODAY_EMPTY), y + metrics.verticalSpacing, bottom);
  } else {
    char line[160];
    snprintf(line, sizeof(line), tr(STR_QUIZLET_TODAY_ASOF_FORMAT), quizlet::shortDate(plan.date).c_str());
    renderer.drawText(kSmallFont, metrics.contentSidePadding, y, line);
    y += renderer.getLineHeight(kSmallFont);

    y = drawSection(tr(STR_QUIZLET_TODAY_TITLE), y, bottom);
    if (plan.today.empty()) y = drawLine(tr(STR_QUIZLET_TODAY_NOTHING), y, bottom);
    for (const auto& item : plan.today) {
      snprintf(line, sizeof(line), "- %s", item.c_str());
      y = drawLine(line, y, bottom);
    }

    y = drawSection(tr(STR_QUIZLET_TOMORROW), y, bottom);
    if (plan.tomorrow.empty()) y = drawLine(tr(STR_QUIZLET_TODAY_NOTHING), y, bottom);
    for (const auto& item : plan.tomorrow) {
      snprintf(line, sizeof(line), "- %s", item.c_str());
      y = drawLine(line, y, bottom);
    }

    y = drawSection(tr(STR_QUIZLET_TESTS), y, bottom);
    if (plan.tests.empty()) y = drawLine(tr(STR_QUIZLET_TODAY_NOTHING), y, bottom);
    for (const auto& test : plan.tests) {
      snprintf(line, sizeof(line), tr(STR_QUIZLET_TEST_FORMAT), quizlet::shortDate(test.date).c_str(), test.daysLeft,
               test.title.c_str());
      if (!test.mastery.empty()) {
        const size_t used = strlen(line);
        snprintf(line + used, sizeof(line) - used, " (%s)", test.mastery.c_str());
      }
      y = drawLine(line, y, bottom, test.daysLeft <= 3) + 4;
    }
  }

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), nullptr, nullptr, nullptr);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
