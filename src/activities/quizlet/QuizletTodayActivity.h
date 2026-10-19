#pragma once

#include "activities/Activity.h"
#include "quizlet/QuizletToday.h"

// Shows the planner summary from the last dashboard sync: homework for today
// and tomorrow and the tests of the coming weeks. Read-only; Back returns.
class QuizletTodayActivity final : public Activity {
 public:
  QuizletTodayActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  quizlet::TodayPlan plan;
  bool loaded = false;

  int drawSection(const char* title, int y, int bottom) const;
  int drawLine(const char* text, int y, int bottom, bool bold = false) const;
};
