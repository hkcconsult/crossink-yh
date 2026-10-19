#include "QuizletToday.h"

#include <cstdlib>

namespace quizlet {
namespace {

// Splits one line on TABs (at most maxParts, the last part keeps the rest).
std::vector<std::string> splitTabs(const std::string& line, const size_t maxParts) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (parts.size() + 1 < maxParts) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string::npos) break;
    parts.push_back(line.substr(start, tab - start));
    start = tab + 1;
  }
  parts.push_back(line.substr(start));
  return parts;
}

constexpr size_t kMaxItemsPerList = 12;

}  // namespace

TodayPlan parseTodayPlan(const char* text, const size_t length) {
  TodayPlan plan;
  if (text == nullptr) return plan;
  size_t start = 0;
  while (start < length) {
    size_t end = start;
    while (end < length && text[end] != '\n') ++end;
    std::string line(text + start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    start = end + 1;
    if (line.empty()) continue;
    const auto parts = splitTabs(line, 5);
    if (parts.size() < 2) continue;
    const std::string& kind = parts[0];
    if (kind == "date") {
      plan.date = parts[1];
    } else if (kind == "today" && plan.today.size() < kMaxItemsPerList) {
      plan.today.push_back(parts[1]);
    } else if (kind == "tomorrow" && plan.tomorrow.size() < kMaxItemsPerList) {
      plan.tomorrow.push_back(parts[1]);
    } else if (kind == "test" && parts.size() >= 4 && plan.tests.size() < kMaxItemsPerList) {
      TodayTest test;
      test.date = parts[1];
      test.daysLeft = static_cast<int>(strtol(parts[2].c_str(), nullptr, 10));
      test.title = parts[3];
      if (parts.size() >= 5) test.mastery = parts[4];
      plan.tests.push_back(std::move(test));
    }
  }
  return plan;
}

std::string shortDate(const std::string& isoDate) {
  if (isoDate.size() != 10 || isoDate[4] != '-' || isoDate[7] != '-') return isoDate;
  return isoDate.substr(8, 2) + "-" + isoDate.substr(5, 2);
}

}  // namespace quizlet
