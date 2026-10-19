#pragma once

#include <cstddef>
#include <string>
#include <vector>

// The planner summary the dashboard serves at /api/device/today.txt, saved on
// the card at sync time. Lines are TAB separated:
//   date<TAB>2026-10-19
//   today<TAB>TOETS 08:30 Frans H3          (one line per item)
//   tomorrow<TAB>Engels werkboek blz. 34
//   test<TAB>2026-10-22<TAB>3<TAB>Toets Frans H3<TAB>16%
// Pure parsing so it can be unit tested on the host.
namespace quizlet {

struct TodayTest {
  std::string date;     // YYYY-MM-DD
  int daysLeft = 0;     // as of the sync date
  std::string title;
  std::string mastery;  // "16%" when a flashcard set is linked, else empty
};

struct TodayPlan {
  std::string date;  // sync date, YYYY-MM-DD
  std::vector<std::string> today;
  std::vector<std::string> tomorrow;
  std::vector<TodayTest> tests;
};

constexpr size_t kMaxTodayBytes = 4096;

TodayPlan parseTodayPlan(const char* text, size_t length);

// "2026-10-19" -> "19-10" for compact headers; input returned unchanged when malformed.
std::string shortDate(const std::string& isoDate);

}  // namespace quizlet
