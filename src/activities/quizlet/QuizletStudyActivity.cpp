#include "QuizletStudyActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"

#if CROSSINK_SCALABLE_FONTS
#include <ScalableBuiltins.h>
#endif

namespace {
// Card text uses the built-in Lexend Deca reader font (family 0). Bitmap
// devices (X3/X4) register it at boot; scalable-font devices (X4 Pro, Sticky)
// load it on demand, see resolveCardFonts().
constexpr unsigned kLexendFamily = 0;
constexpr int kSmallFont = UI_10_FONT_ID;
constexpr int kCardBorder = 2;
constexpr int kCardPadding = 16;
constexpr int kMaxCardLines = 8;

bool wasAnyReleased(const MappedInputManager& input, const MappedInputManager::Button a,
                    const MappedInputManager::Button b, const MappedInputManager::Button c) {
  return input.wasReleased(a) || input.wasReleased(b) || input.wasReleased(c);
}
}  // namespace

QuizletStudyActivity::QuizletStudyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckPath,
                                           std::string deckTitle)
    : Activity("QuizletStudy", renderer, mappedInput), deckPath(std::move(deckPath)), deckTitle(std::move(deckTitle)) {}

void QuizletStudyActivity::resolveCardFonts() {
#if CROSSINK_SCALABLE_FONTS
  {
    // Inserting fonts mutates the renderer's font map; keep the render task out.
    RenderLock lock;
    ensureScalableBuiltinFamily(renderer, kLexendFamily);
  }
  // Falls back to UI_12 when the built-in family could not be loaded.
  frontFont = scalableBuiltinReaderFontId(kLexendFamily, 18);
  backFont = scalableBuiltinReaderFontId(kLexendFamily, 16);
#else
  frontFont = LEXENDDECA_16_FONT_ID;
  backFont = LEXENDDECA_14_FONT_ID;
#endif
}

void QuizletStudyActivity::onEnter() {
  Activity::onEnter();
  resolveCardFonts();
  loadResult = quizlet::loadDeck(deckPath, cards);
  restart();
  requestUpdate();
}

void QuizletStudyActivity::onExit() {
  // Release the deck right away; it can be tens of KB on the ESP32-C3.
  cards.clear();
  cards.shrink_to_fit();
  Activity::onExit();
}

void QuizletStudyActivity::restart() {
  // Shuffle so the order is not memorized; millis() varies enough between runs.
  session.start(cards.size(), true, static_cast<uint32_t>(millis()) | 1u);
}

void QuizletStudyActivity::onFlip() {
  session.flip();
  requestUpdate();
}

void QuizletStudyActivity::onAnswer(const bool knewIt) {
  if (knewIt) {
    session.markKnown();
  } else {
    session.markAgain();
  }
  requestUpdate();
}

void QuizletStudyActivity::loop() {
  using Button = MappedInputManager::Button;

  if (TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer)) ||
      mappedInput.wasReleased(Button::Back)) {
    finish();
    return;
  }

  if (loadResult != quizlet::LoadResult::Ok) return;

  int tapX = 0;
  int tapY = 0;
  const bool tapped = mappedInput.wasScreenTapped(tapX, tapY);

  if (session.isFinished()) {
    if (mappedInput.wasReleased(Button::Confirm) || tapped) {
      restart();
      requestUpdate();
    }
    return;
  }

  const bool next = wasAnyReleased(mappedInput, Button::Right, Button::Down, Button::PageForward);
  const bool previous = wasAnyReleased(mappedInput, Button::Left, Button::Up, Button::PageBack);
  const auto swipe = mappedInput.wasSwipe();

  if (!session.isFlipped()) {
    // Any key reveals the answer first, so a stray press never skips a card.
    if (mappedInput.wasReleased(Button::Confirm) || next || previous || tapped) onFlip();
    return;
  }

  if (mappedInput.wasReleased(Button::Confirm) || tapped) {
    onFlip();
  } else if (next || swipe == MappedInputManager::SwipeDir::Right) {
    onAnswer(true);
  } else if (previous || swipe == MappedInputManager::SwipeDir::Left) {
    onAnswer(false);
  }
}

int QuizletStudyActivity::drawWrappedCentered(const int fontId, const char* text, const int x, const int width,
                                              const int top, const int bottom, const int maxLines, const bool bold,
                                              const bool verticallyCenter) const {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int lineHeight = renderer.getLineHeight(fontId);
  const int fitLines = lineHeight > 0 ? std::max(1, (bottom - top) / lineHeight) : 1;
  const auto lines = renderer.wrappedText(fontId, text, width, std::min(maxLines, fitLines), style);
  const int blockHeight = static_cast<int>(lines.size()) * lineHeight;
  int y = verticallyCenter ? top + std::max(0, (bottom - top - blockHeight) / 2) : top;
  for (const auto& line : lines) {
    const int lineWidth = renderer.getTextWidth(fontId, line.c_str(), style);
    renderer.drawText(fontId, x + std::max(0, (width - lineWidth) / 2), y, line.c_str(), true, style);
    y += lineHeight;
  }
  return y;
}

void QuizletStudyActivity::renderMessage(const char* message) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = CompactHeader::contentTop(metrics);
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight;
  const int side = metrics.contentSidePadding;
  drawWrappedCentered(UI_12_FONT_ID, message, side, renderer.getScreenWidth() - 2 * side, top, bottom, 4, false, true);
}

void QuizletStudyActivity::renderCard(const int top, const int bottom) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - 2 * side;

  // Progress line above the card.
  char progress[64];
  snprintf(progress, sizeof(progress), tr(STR_QUIZLET_PROGRESS_FORMAT), static_cast<unsigned>(session.remainingCount()),
           static_cast<unsigned>(session.knownCount()));
  const int progressHeight = renderer.getLineHeight(kSmallFont);
  renderer.drawCenteredText(kSmallFont, top, progress);

  const int cardTop = top + progressHeight + metrics.verticalSpacing;
  const int cardHeight = bottom - cardTop;
  if (cardHeight <= 2 * kCardPadding) return;
  renderer.drawRect(side, cardTop, width, cardHeight, kCardBorder, true);

  const int innerX = side + kCardPadding;
  const int innerWidth = width - 2 * kCardPadding;
  const int innerTop = cardTop + kCardPadding;
  const int innerBottom = cardTop + cardHeight - kCardPadding;
  const quizlet::Card& card = cards[session.currentCard()];

  if (!session.isFlipped()) {
    drawWrappedCentered(frontFont, card.term.c_str(), innerX, innerWidth, innerTop, innerBottom, kMaxCardLines, true,
                        true);
    return;
  }

  // Back side: the term stays visible at the top for context, the answer below.
  const int termBottom =
      drawWrappedCentered(UI_12_FONT_ID, card.term.c_str(), innerX, innerWidth, innerTop, innerBottom, 2, true, false);
  const int dividerY = termBottom + metrics.verticalSpacing / 2;
  renderer.drawLine(innerX, dividerY, innerX + innerWidth, dividerY, true);
  drawWrappedCentered(backFont, card.definition.c_str(), innerX, innerWidth, dividerY + metrics.verticalSpacing,
                      innerBottom, kMaxCardLines, false, true);
}

void QuizletStudyActivity::renderDone(const int top, const int bottom) const {
  const int lineHeight = renderer.getLineHeight(frontFont);
  const int smallHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int blockTop = top + std::max(0, (bottom - top - lineHeight - smallHeight) / 2);
  renderer.drawCenteredText(frontFont, blockTop, tr(STR_QUIZLET_DONE), true, EpdFontFamily::BOLD);

  char result[96];
  snprintf(result, sizeof(result), tr(STR_QUIZLET_DONE_FORMAT), static_cast<unsigned>(session.firstTryCount()),
           static_cast<unsigned>(session.cardCount()));
  renderer.drawCenteredText(UI_12_FONT_ID, blockTop + lineHeight, result);
}

void QuizletStudyActivity::render(RenderLock&&) {
  renderer.clearScreen();

  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, deckTitle.c_str());
  } else {
    CompactHeader::drawTitle(renderer, deckTitle.c_str());
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = CompactHeader::contentTop(metrics) + metrics.verticalSpacing;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const auto back = mappedInput.withBackArrow(tr(STR_BACK));

  MappedInputManager::Labels labels{};
  switch (loadResult) {
    case quizlet::LoadResult::Ok:
      if (session.isFinished()) {
        renderDone(top, bottom);
        labels = mappedInput.mapLabels(back, tr(STR_QUIZLET_RESTART), nullptr, nullptr);
      } else {
        renderCard(top, bottom);
        labels = session.isFlipped()
                     ? mappedInput.mapLabels(back, tr(STR_QUIZLET_FLIP), tr(STR_QUIZLET_AGAIN), tr(STR_QUIZLET_KNOWN))
                     : mappedInput.mapLabels(back, tr(STR_QUIZLET_FLIP), nullptr, nullptr);
      }
      break;
    case quizlet::LoadResult::Empty:
      renderMessage(tr(STR_QUIZLET_DECK_EMPTY));
      labels = mappedInput.mapLabels(back, nullptr, nullptr, nullptr);
      break;
    case quizlet::LoadResult::ReadFailed:
    case quizlet::LoadResult::OutOfMemory:
      renderMessage(tr(STR_QUIZLET_LOAD_FAILED));
      labels = mappedInput.mapLabels(back, nullptr, nullptr, nullptr);
      break;
  }

  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
