#include "QuizletStudyActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
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
constexpr int kOptionGap = 8;
constexpr unsigned long kStopHoldMs = 800;
constexpr unsigned kSpeedRefreshSeconds = 10;
// Physical front-button slots as reported by getPressed/ReleasedFrontButton().
constexpr int kFrontSlotCount = 4;
const char* const kSlotLetters[kFrontSlotCount] = {"A", "B", "C", "D"};

bool wasAnyReleased(const MappedInputManager& input, const MappedInputManager::Button a,
                    const MappedInputManager::Button b, const MappedInputManager::Button c) {
  return input.wasReleased(a) || input.wasReleased(b) || input.wasReleased(c);
}
}  // namespace

QuizletStudyActivity::QuizletStudyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckPath,
                                           std::string deckTitle, const quizlet::StudyOptions options,
                                           const bool resume)
    : Activity("QuizletStudy", renderer, mappedInput),
      deckPath(std::move(deckPath)),
      deckTitle(std::move(deckTitle)),
      options(options),
      resume(resume) {}

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

void QuizletStudyActivity::activateCjkFontIfNeeded() {
  if (!std::any_of(cards.begin(), cards.end(), [](const quizlet::Card& card) {
        return quizlet::containsCjk(card.term) || quizlet::containsCjk(card.definition);
      })) {
    return;
  }
  // Any installed SD family with "CJK" in its name, e.g. /.fonts/NotoSansCJK.
  std::string familyName;
  sdFontSystem.ensureRegistry();
  for (const auto& family : sdFontSystem.registry().getFamilies()) {
    if (quizlet::isCjkFamilyName(family.name)) {
      familyName = family.name;
      break;
    }
  }
  sdFontSystem.releaseRegistry();
  if (familyName.empty()) {
    LOG_INF("QZL", "Deck has CJK text but no CJK font is installed");
    cjkIssue = CjkFontIssue::Missing;
    return;
  }
  // Same swap the dictionary uses: one SD family stays resident, and the
  // reader font comes back in onExit().
  RenderLock lock;
  const DictionaryFontActivation activation = sdFontSystem.activateDictionaryFont(renderer, familyName.c_str(), 16);
  if (!activation.usingDictionaryFont || activation.fontId == 0) {
    LOG_ERR("QZL", "Could not load CJK font %s (free=%u maxAlloc=%u)", familyName.c_str(),
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    cjkIssue = CjkFontIssue::LowMemory;
    return;
  }
  usingCjkFont = true;
  frontFont = backFont = optionFont = contextFont = activation.fontId;
  LOG_INF("QZL", "Using CJK font %s", familyName.c_str());
}

void QuizletStudyActivity::onEnter() {
  Activity::onEnter();
  resolveCardFonts();
  loadResult = quizlet::loadDeck(deckPath, cards);
  if (loadResult == quizlet::LoadResult::Ok) {
    activateCjkFontIfNeeded();
    progress = quizlet::loadProgress(deckPath, cards);
  }
  rng = static_cast<uint32_t>(millis()) | 1u;
  if (!(resume && resumeSavedRound())) {
    if (options.mode == quizlet::StudyMode::Speed) speedRecord = quizlet::loadSpeedRecord(deckPath);
    restart();
  }
  requestUpdate();
}

bool QuizletStudyActivity::resumeSavedRound() {
  quizlet::SavedRound saved;
  if (loadResult != quizlet::LoadResult::Ok || !quizlet::loadRound(deckPath, saved)) return false;
  // Map saved card keys back to deck indices; cards removed by a sync are skipped.
  std::vector<std::pair<uint16_t, uint8_t>> pending;
  pending.reserve(saved.pending.size());
  for (const auto& [key, again] : saved.pending) {
    for (size_t i = 0; i < progress.cards.size(); ++i) {
      if (progress.cards[i].key == key) {
        pending.emplace_back(static_cast<uint16_t>(i), again);
        break;
      }
    }
  }
  if (pending.empty()) return false;
  options = saved.options;
  session.restore(pending, cards.size(), saved.total, saved.known, saved.firstTry);
  answeredInRound = 1;  // keep saving the continued round
  choiceUnavailable = false;
  prepareCard();
  return true;
}

void QuizletStudyActivity::saveRoundState() {
  // Speed rounds are timed and finished rounds have nothing left to continue.
  if (loadResult != quizlet::LoadResult::Ok || options.mode == quizlet::StudyMode::Speed || session.isFinished() ||
      answeredInRound == 0) {
    quizlet::removeRound(deckPath);
    return;
  }
  quizlet::SavedRound round;
  round.options = options;
  round.total = session.cardCount();
  round.known = session.knownCount();
  round.firstTry = session.firstTryCount();
  for (const auto& [index, again] : session.pendingCards()) round.pending.emplace_back(progress.cards[index].key, again);
  quizlet::saveRound(deckPath, round);
}

void QuizletStudyActivity::onExit() {
  saveRoundState();
  if (progressDirty) {
    quizlet::saveProgress(deckPath, progress);
    progressDirty = false;
  }
  // Release the deck right away; it can be tens of KB on the ESP32-C3.
  cards.clear();
  cards.shrink_to_fit();
  if (usingCjkFont) {
    RenderLock lock;
    sdFontSystem.restoreReaderFont(renderer);
    usingCjkFont = false;
  }
  Activity::onExit();
}

// ---------- state ----------

const std::string& QuizletStudyActivity::promptText() const {
  const quizlet::Card& card = cards[session.currentCard()];
  return answerIsDefinition ? card.term : card.definition;
}

const std::string& QuizletStudyActivity::answerText() const {
  const quizlet::Card& card = cards[session.currentCard()];
  return answerIsDefinition ? card.definition : card.term;
}

unsigned QuizletStudyActivity::speedSecondsLeft() const {
  const unsigned long elapsed = millis() - speedStartMs;
  if (elapsed >= quizlet::kSpeedRoundMs) return 0;
  return static_cast<unsigned>((quizlet::kSpeedRoundMs - elapsed + 999) / 1000);
}

void QuizletStudyActivity::restart() {
  answeredInRound = 0;
  if (options.order == quizlet::Order::Smart && options.mode != quizlet::StudyMode::Speed) {
    ++progress.rounds;
    progressDirty = true;
    session.startWith(quizlet::pickSmartRound(progress.cards, progress.rounds, rng), cards.size());
  } else {
    session.start(cards.size(), options.order != quizlet::Order::InOrder, rng);
  }
  quizlet::nextRandom(rng);
  speedStartMs = millis();
  speedCorrect = 0;
  speedFinished = false;
  newRecord = false;
  lastShownSecondsBucket = quizlet::kSpeedRoundMs / 1000 / kSpeedRefreshSeconds;
  choiceUnavailable = false;
  prepareCard();
}

void QuizletStudyActivity::prepareCard() {
  chosenSlot = -1;
  choiceCount = 0;
  if (loadResult != quizlet::LoadResult::Ok) return;

  // The speed round keeps going until the clock runs out: start a new pass.
  if (session.isFinished() && options.mode == quizlet::StudyMode::Speed && !speedFinished) {
    session.start(cards.size(), true, rng);
    quizlet::nextRandom(rng);
  }
  if (session.isFinished()) return;

  answerIsDefinition = quizlet::answerIsDefinition(options.answerWith, rng);
  if (!usesChoices()) return;

  choiceCount = quizlet::buildChoices(cards, session.currentCard(), answerIsDefinition, rng, choices, correctSlot);
  if (choiceCount == 0 && options.answerWith == quizlet::AnswerWith::Both) {
    // This direction has no distinct alternatives; try the other one.
    answerIsDefinition = !answerIsDefinition;
    choiceCount = quizlet::buildChoices(cards, session.currentCard(), answerIsDefinition, rng, choices, correctSlot);
  }
  choiceUnavailable = choiceCount == 0;
}

void QuizletStudyActivity::advance(const bool knewIt) {
  if (!session.isFinished() && session.currentCard() < progress.cards.size()) {
    quizlet::recordAnswer(progress.cards[session.currentCard()], knewIt, session.isFirstAttempt());
    progressDirty = true;
    ++answeredInRound;
  }
  if (knewIt) {
    session.markKnown();
  } else {
    session.markAgain();
  }
  prepareCard();
  requestUpdate();
}

void QuizletStudyActivity::answerChoice(const int slot) {
  if (slot < 0 || static_cast<size_t>(slot) >= choiceCount || chosenSlot >= 0) return;
  const bool right = static_cast<size_t>(slot) == correctSlot;
  if (options.mode == quizlet::StudyMode::Speed && right) {
    // Keep the pace: no feedback screen for a right answer.
    ++speedCorrect;
    advance(true);
    return;
  }
  chosenSlot = slot;
  requestUpdate();
}

void QuizletStudyActivity::finishSpeedRound() {
  speedFinished = true;
  if (speedCorrect > speedRecord) {
    newRecord = speedRecord > 0 || speedCorrect > 0;
    speedRecord = speedCorrect;
    quizlet::saveSpeedRecord(deckPath, speedRecord);
  }
  requestUpdate();
}

// Returns true when the first front button was held long enough to stop.
bool QuizletStudyActivity::handleStopLongPress() {
  if (mappedInput.getPressedFrontButton() == 0) {
    stopPressStartMs = millis();
    stopHandled = false;
  }
  if (stopPressStartMs != 0 && !stopHandled && mappedInput.isFrontButtonPressed(0) &&
      millis() - stopPressStartMs >= kStopHoldMs) {
    stopHandled = true;
    finish();
    return true;
  }
  return false;
}

// ---------- input ----------

void QuizletStudyActivity::loop() {
  using Button = MappedInputManager::Button;

  if (TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer))) {
    finish();
    return;
  }

  if (loadResult != quizlet::LoadResult::Ok || choiceUnavailable) {
    if (mappedInput.wasReleased(Button::Back)) finish();
    return;
  }

  int tapX = 0;
  int tapY = 0;
  const bool tapped = mappedInput.wasScreenTapped(tapX, tapY);

  if (options.mode == quizlet::StudyMode::Speed && !speedFinished) {
    const unsigned left = speedSecondsLeft();
    if (left == 0) {
      finishSpeedRound();
      return;
    }
    // Refresh the countdown now and then; e-ink should not redraw every second.
    const unsigned bucket = (left + kSpeedRefreshSeconds - 1) / kSpeedRefreshSeconds;
    if (bucket != lastShownSecondsBucket) {
      lastShownSecondsBucket = bucket;
      requestUpdate();
    }
  }

  const bool roundOver = options.mode == quizlet::StudyMode::Speed ? speedFinished : session.isFinished();
  if (roundOver) {
    if (mappedInput.wasReleased(Button::Back)) {
      finish();
    } else if (mappedInput.wasReleased(Button::Confirm) || tapped) {
      restart();
      requestUpdate();
    }
    return;
  }

  if (usesChoices()) {
    loopChoice(tapped, tapX, tapY);
  } else {
    if (mappedInput.wasReleased(Button::Back)) {
      finish();
      return;
    }
    loopFlip(tapped, mappedInput.wasSwipe());
  }
}

void QuizletStudyActivity::loopFlip(const bool tapped, const MappedInputManager::SwipeDir swipe) {
  using Button = MappedInputManager::Button;
  const bool next = wasAnyReleased(mappedInput, Button::Right, Button::Down, Button::PageForward);
  const bool previous = wasAnyReleased(mappedInput, Button::Left, Button::Up, Button::PageBack);

  if (!session.isFlipped()) {
    // Any key reveals the answer first, so a stray press never skips a card.
    if (mappedInput.wasReleased(Button::Confirm) || next || previous || tapped) {
      session.flip();
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm) || tapped) {
    session.flip();
    requestUpdate();
  } else if (next || swipe == MappedInputManager::SwipeDir::Right) {
    advance(true);
  } else if (previous || swipe == MappedInputManager::SwipeDir::Left) {
    advance(false);
  }
}

void QuizletStudyActivity::loopChoice(const bool tapped, const int tapX, const int tapY) {
  using Button = MappedInputManager::Button;

  if (chosenSlot >= 0) {
    // Feedback is showing: Back stops, anything else continues.
    if (mappedInput.wasReleased(Button::Back)) {
      finish();
      return;
    }
    const bool any = tapped || mappedInput.wasReleased(Button::Confirm) ||
                     wasAnyReleased(mappedInput, Button::Left, Button::Right, Button::Up) ||
                     wasAnyReleased(mappedInput, Button::Down, Button::PageBack, Button::PageForward);
    if (any) advance(static_cast<size_t>(chosenSlot) == correctSlot);
    return;
  }

  if (handleStopLongPress()) return;

  if (tapped) {
    for (size_t i = 0; i < choiceCount; ++i) {
      const Box& r = optionRects[i];
      if (tapX >= r.x && tapX < r.x + r.w && tapY >= r.y && tapY < r.y + r.h) {
        answerChoice(static_cast<int>(i));
        return;
      }
    }
    return;
  }

  const int released = mappedInput.getReleasedFrontButton();
  if (released < 0 || released >= kFrontSlotCount) return;
  if (released == 0 && stopHandled) return;  // release after the stop long-press
  stopPressStartMs = 0;
  answerChoice(released);
}

// ---------- rendering ----------

int QuizletStudyActivity::drawWrappedCentered(const int fontId, const char* text, const int x, const int width,
                                              const int top, const int bottom, const int maxLines, const bool bold,
                                              const bool verticallyCenter, const bool black) const {
  const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int lineHeight = renderer.getLineHeight(fontId);
  const int fitLines = lineHeight > 0 ? std::max(1, (bottom - top) / lineHeight) : 1;
  const auto lines = renderer.wrappedText(fontId, text, width, std::min(maxLines, fitLines), style);
  const int blockHeight = static_cast<int>(lines.size()) * lineHeight;
  int y = verticallyCenter ? top + std::max(0, (bottom - top - blockHeight) / 2) : top;
  for (const auto& line : lines) {
    const int lineWidth = renderer.getTextWidth(fontId, line.c_str(), style);
    renderer.drawText(fontId, x + std::max(0, (width - lineWidth) / 2), y, line.c_str(), black, style);
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

void QuizletStudyActivity::renderStatus(const int top) const {
  const char* issue = nullptr;
  switch (cjkIssue) {
    case CjkFontIssue::None:
      break;
    case CjkFontIssue::Missing:
      issue = tr(STR_QUIZLET_CJK_MISSING);
      break;
    case CjkFontIssue::LowMemory:
      issue = tr(STR_QUIZLET_CJK_LOW_MEMORY);
      break;
    case CjkFontIssue::GlyphsFailed:
      issue = tr(STR_QUIZLET_CJK_GLYPHS_FAILED);
      break;
  }
  if (issue) {
    renderer.drawCenteredText(kSmallFont, top, issue);
    return;
  }
  char status[64];
  if (options.mode == quizlet::StudyMode::Speed) {
    snprintf(status, sizeof(status), tr(STR_QUIZLET_SPEED_STATUS_FORMAT), speedSecondsLeft(), speedCorrect);
  } else {
    snprintf(status, sizeof(status), tr(STR_QUIZLET_PROGRESS_FORMAT), static_cast<unsigned>(session.remainingCount()),
             static_cast<unsigned>(session.knownCount()));
  }
  renderer.drawCenteredText(kSmallFont, top, status);
}

void QuizletStudyActivity::renderFlipCard(const int top, const int bottom) const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - 2 * side;

  renderStatus(top);
  const int cardTop = top + renderer.getLineHeight(kSmallFont) + metrics.verticalSpacing;
  const int cardHeight = bottom - cardTop;
  if (cardHeight <= 2 * kCardPadding) return;
  renderer.drawRect(side, cardTop, width, cardHeight, kCardBorder, true);

  const int innerX = side + kCardPadding;
  const int innerWidth = width - 2 * kCardPadding;
  const int innerTop = cardTop + kCardPadding;
  const int innerBottom = cardTop + cardHeight - kCardPadding;

  if (!session.isFlipped()) {
    drawWrappedCentered(frontFont, promptText().c_str(), innerX, innerWidth, innerTop, innerBottom, kMaxCardLines, true,
                        true);
    return;
  }
  // Back side: the prompt stays visible at the top for context, the answer below.
  const int promptBottom = drawWrappedCentered(contextFont, promptText().c_str(), innerX, innerWidth, innerTop,
                                               innerBottom, 2, true, false);
  const int dividerY = promptBottom + metrics.verticalSpacing / 2;
  renderer.drawLine(innerX, dividerY, innerX + innerWidth, dividerY, true);
  drawWrappedCentered(backFont, answerText().c_str(), innerX, innerWidth, dividerY + metrics.verticalSpacing,
                      innerBottom, kMaxCardLines, false, true);
}

void QuizletStudyActivity::renderChoiceCard(const int top, const int bottom) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - 2 * side;

  renderStatus(top);
  const int areaTop = top + renderer.getLineHeight(kSmallFont) + metrics.verticalSpacing;
  const int areaHeight = bottom - areaTop;
  if (areaHeight <= 4 * kCardPadding) return;

  // Prompt card takes a bit over a third; options share the rest.
  const int promptHeight = areaHeight * 36 / 100;
  renderer.drawRect(side, areaTop, width, promptHeight, kCardBorder, true);
  drawWrappedCentered(frontFont, promptText().c_str(), side + kCardPadding, width - 2 * kCardPadding,
                      areaTop + kCardPadding / 2, areaTop + promptHeight - kCardPadding / 2, 4, true, true);

  const int optionsTop = areaTop + promptHeight + metrics.verticalSpacing;
  const int count = static_cast<int>(choiceCount);
  const int optionHeight = (bottom - optionsTop - (count - 1) * kOptionGap) / std::max(1, count);
  const int letterWidth = renderer.getTextWidth(UI_12_FONT_ID, "W", EpdFontFamily::BOLD) + 2 * kCardPadding;

  for (int i = 0; i < count; ++i) {
    const int y = optionsTop + i * (optionHeight + kOptionGap);
    optionRects[i] = {side, y, width, optionHeight};
    const bool answered = chosenSlot >= 0;
    const bool isCorrect = static_cast<size_t>(i) == correctSlot;
    const bool isChosenWrong = answered && i == chosenSlot && !isCorrect;
    const bool inverted = answered && isCorrect;

    if (inverted) {
      renderer.fillRect(side, y, width, optionHeight, true);
    } else {
      renderer.drawRect(side, y, width, optionHeight, isChosenWrong ? 5 : 1, true);
    }
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    renderer.drawText(UI_12_FONT_ID, side + kCardPadding, y + (optionHeight - lineHeight) / 2, kSlotLetters[i],
                      !inverted, EpdFontFamily::BOLD);
    const auto& card = cards[choices[i]];
    const std::string& text = answerIsDefinition ? card.definition : card.term;
    const int textX = side + letterWidth;
    drawWrappedCentered(optionFont, text.c_str(), textX, width - letterWidth - kCardPadding, y + 4,
                        y + optionHeight - 4, 3, isCorrect && answered, true, !inverted);
    if (isChosenWrong) {
      // Strike through the wrong pick so it reads as wrong without color.
      const int midY = y + optionHeight / 2;
      renderer.drawLine(textX, midY, side + width - kCardPadding, midY, 3, true);
    }
  }
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

void QuizletStudyActivity::renderSpeedDone(const int top, const int bottom) const {
  const int lineHeight = renderer.getLineHeight(frontFont);
  const int smallHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int blockTop = top + std::max(0, (bottom - top - 2 * lineHeight - smallHeight) / 2);
  renderer.drawCenteredText(frontFont, blockTop, tr(STR_QUIZLET_TIME_UP), true, EpdFontFamily::BOLD);

  char line[64];
  snprintf(line, sizeof(line), tr(STR_QUIZLET_SPEED_RESULT_FORMAT), speedCorrect);
  renderer.drawCenteredText(frontFont, blockTop + lineHeight, line);
  if (newRecord) {
    snprintf(line, sizeof(line), "%s", tr(STR_QUIZLET_NEW_RECORD));
  } else {
    snprintf(line, sizeof(line), tr(STR_QUIZLET_RECORD_FORMAT), speedRecord);
  }
  renderer.drawCenteredText(UI_12_FONT_ID, blockTop + 2 * lineHeight + smallHeight / 2, line, true,
                            newRecord ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

void QuizletStudyActivity::render(RenderLock&&) {
  if (!usingCjkFont) {
    drawScreen();
    renderer.displayBuffer();
    return;
  }
  // SD-card fonts only draw glyphs that were loaded beforehand: a scan pass
  // collects the text, the glyphs are read from the card, then the real pass
  // draws. The scope frees the glyph cache again after the refresh.
  auto* fontCache = renderer.getFontCacheManager();
  // Lean like the dictionary: skip the kerning tables (tens of KB for a CJK
  // font) that CJK text does not use, so the C3 heap has room for the glyphs.
  auto scope = fontCache->createPrewarmScope(FontCacheManager::PreparationPolicy::DictionaryLean);
  drawScreen();
  // The scan pass measures text before its glyphs exist, so wrapping can cut a
  // line short and skip characters. Record the full card texts as well.
  if (loadResult == quizlet::LoadResult::Ok && !cards.empty() && !session.isFinished()) {
    for (const auto style : {EpdFontFamily::REGULAR, EpdFontFamily::BOLD}) {
      fontCache->recordText(promptText().c_str(), frontFont, style);
      fontCache->recordText(answerText().c_str(), frontFont, style);
      for (size_t i = 0; i < choiceCount; ++i) {
        const auto& card = cards[choices[i]];
        fontCache->recordText((answerIsDefinition ? card.definition : card.term).c_str(), frontFont, style);
      }
    }
  }
  if (!scope.endScanAndPrewarm()) {
    LOG_ERR("QZL", "CJK glyph prewarm failed (free=%u maxAlloc=%u)", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    cjkIssue = CjkFontIssue::GlyphsFailed;
  } else if (cjkIssue == CjkFontIssue::GlyphsFailed) {
    cjkIssue = CjkFontIssue::None;
  }
  drawScreen();
  renderer.displayBuffer();
}

void QuizletStudyActivity::drawScreen() {
  renderer.clearScreen();
  const bool touch = mappedInput.hasTouchHardware();
  if (touch) {
    TouchHeaderBackButton::drawCompact(renderer, deckTitle.c_str());
  } else {
    CompactHeader::drawTitle(renderer, deckTitle.c_str());
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = CompactHeader::contentTop(metrics) + metrics.verticalSpacing;
  int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const auto back = mappedInput.withBackArrow(tr(STR_BACK));
  MappedInputManager::Labels labels{};
  bool slotHints = false;

  if (loadResult == quizlet::LoadResult::Empty) {
    renderMessage(tr(STR_QUIZLET_DECK_EMPTY));
    labels = mappedInput.mapLabels(back, nullptr, nullptr, nullptr);
  } else if (loadResult != quizlet::LoadResult::Ok) {
    renderMessage(tr(STR_QUIZLET_LOAD_FAILED));
    labels = mappedInput.mapLabels(back, nullptr, nullptr, nullptr);
  } else if (choiceUnavailable) {
    renderMessage(tr(STR_QUIZLET_CHOICE_UNAVAILABLE));
    labels = mappedInput.mapLabels(back, nullptr, nullptr, nullptr);
  } else if (options.mode == quizlet::StudyMode::Speed && speedFinished) {
    renderSpeedDone(top, bottom);
    labels = mappedInput.mapLabels(back, tr(STR_QUIZLET_RESTART), nullptr, nullptr);
  } else if (options.mode != quizlet::StudyMode::Speed && session.isFinished()) {
    renderDone(top, bottom);
    labels = mappedInput.mapLabels(back, tr(STR_QUIZLET_RESTART), nullptr, nullptr);
  } else if (usesChoices()) {
    const bool open = chosenSlot < 0;
    if (open && !touch) {
      // Leave room for the stop hint above the button labels.
      bottom -= renderer.getLineHeight(kSmallFont) + metrics.verticalSpacing / 2;
      renderer.drawCenteredText(kSmallFont, bottom + metrics.verticalSpacing / 2, tr(STR_QUIZLET_HOLD_TO_STOP));
    }
    renderChoiceCard(top, bottom);
    if (open) {
      slotHints = true;
    } else {
      labels = mappedInput.mapLabels(back, tr(STR_QUIZLET_NEXT), nullptr, tr(STR_QUIZLET_NEXT));
    }
  } else {
    renderFlipCard(top, bottom);
    labels = session.isFlipped()
                 ? mappedInput.mapLabels(back, tr(STR_QUIZLET_FLIP), tr(STR_QUIZLET_AGAIN), tr(STR_QUIZLET_KNOWN))
                 : mappedInput.mapLabels(back, tr(STR_QUIZLET_FLIP), nullptr, nullptr);
  }

  if (slotHints) {
    // Physical slots, deliberately not remapped: A is always the first button.
    const char* hints[kFrontSlotCount] = {};
    for (size_t i = 0; i < choiceCount && i < static_cast<size_t>(kFrontSlotCount); ++i) hints[i] = kSlotLetters[i];
    GUI.drawButtonHints(renderer, hints[0], hints[1], hints[2], hints[3]);
  } else {
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
}
