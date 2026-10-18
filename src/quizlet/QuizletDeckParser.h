#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Flashcard decks exported from Quizlet ("Export" -> term/definition
// separated by Tab, rows separated by new lines). Pure parsing code without
// Arduino or SD dependencies so it can be unit tested on the host.
namespace quizlet {

struct Card {
  std::string term;
  std::string definition;
};

// Hard limits keep a deck safe for the ESP32-C3 heap (~380 KB usable, often
// under 50 KB in one contiguous block). The raw file is capped at 32 KB, so
// the parsed cards can never take much more than that plus string overhead.
// Typical school decks are 20-80 short cards and use a few KB.
constexpr size_t kMaxCards = 500;
constexpr size_t kMaxFieldBytes = 160;
constexpr size_t kMaxDeckFileBytes = 32 * 1024;

enum class Separator : uint8_t { Tab, Semicolon, Dash };

// Parses UTF-8 deck text into cards. Lines without a separator, empty lines
// and lines starting with '#' are skipped. Returns the number of cards added.
size_t parseDeckText(const char* text, size_t length, std::vector<Card>& out);

// Detects the separator used by most lines (Tab is preferred, then ';',
// then " - "). Exposed for tests.
Separator detectSeparator(const char* text, size_t length);

// Truncates a UTF-8 string to at most maxBytes without splitting a code point.
void truncateUtf8(std::string& value, size_t maxBytes);

// True for file names that look like a deck (.txt, .tsv, .csv; not hidden).
bool isDeckFileName(const char* name);

// "Biologie H3.txt" -> "Biologie H3"
std::string deckTitleFromFileName(const char* name);

}  // namespace quizlet
