#include "QuizletDeckParser.h"

#include <cctype>
#include <cstring>

namespace quizlet {
namespace {

struct Line {
  const char* begin;
  const char* end;
};

bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

void trim(const char*& begin, const char*& end) {
  while (begin < end && isSpace(*begin)) ++begin;
  while (end > begin && isSpace(*(end - 1))) --end;
}

// Returns the next line in [cursor, limit) and advances cursor past its newline.
bool nextLine(const char*& cursor, const char* limit, Line& line) {
  if (cursor >= limit) return false;
  const char* start = cursor;
  while (cursor < limit && *cursor != '\n') ++cursor;
  line.begin = start;
  line.end = cursor;
  if (line.end > line.begin && *(line.end - 1) == '\r') --line.end;
  if (cursor < limit) ++cursor;  // skip '\n'
  return true;
}

const char* findSeparator(const Line& line, const Separator separator, size_t& separatorLength) {
  switch (separator) {
    case Separator::Tab:
      separatorLength = 1;
      return static_cast<const char*>(memchr(line.begin, '\t', static_cast<size_t>(line.end - line.begin)));
    case Separator::Semicolon:
      separatorLength = 1;
      return static_cast<const char*>(memchr(line.begin, ';', static_cast<size_t>(line.end - line.begin)));
    case Separator::Dash:
      separatorLength = 3;
      for (const char* p = line.begin; p + 3 <= line.end; ++p) {
        if (p[0] == ' ' && p[1] == '-' && p[2] == ' ') return p;
      }
      return nullptr;
  }
  return nullptr;
}

void skipBom(const char*& text, size_t& length) {
  if (length >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF) {
    text += 3;
    length -= 3;
  }
}

}  // namespace

void truncateUtf8(std::string& value, const size_t maxBytes) {
  if (value.size() <= maxBytes) return;
  size_t cut = maxBytes;
  // Step back over UTF-8 continuation bytes (10xxxxxx) so we never split a code point.
  while (cut > 0 && (static_cast<unsigned char>(value[cut]) & 0xC0) == 0x80) --cut;
  value.resize(cut);
}

Separator detectSeparator(const char* text, size_t length) {
  skipBom(text, length);
  size_t tabs = 0;
  size_t semicolons = 0;
  size_t dashes = 0;
  const char* cursor = text;
  const char* limit = text + length;
  Line line{};
  size_t checked = 0;
  while (checked < 50 && nextLine(cursor, limit, line)) {
    if (line.begin == line.end) continue;
    ++checked;
    size_t len = 0;
    if (findSeparator(line, Separator::Tab, len)) ++tabs;
    if (findSeparator(line, Separator::Semicolon, len)) ++semicolons;
    if (findSeparator(line, Separator::Dash, len)) ++dashes;
  }
  if (tabs > 0 && tabs >= semicolons && tabs >= dashes) return Separator::Tab;
  if (semicolons > 0 && semicolons >= dashes) return Separator::Semicolon;
  if (dashes > 0) return Separator::Dash;
  return Separator::Tab;
}

size_t parseDeckText(const char* text, size_t length, std::vector<Card>& out) {
  if (text == nullptr || length == 0) return 0;
  skipBom(text, length);
  const Separator separator = detectSeparator(text, length);

  const size_t before = out.size();
  const char* cursor = text;
  const char* limit = text + length;
  Line line{};
  while (out.size() < kMaxCards && nextLine(cursor, limit, line)) {
    const char* begin = line.begin;
    const char* end = line.end;
    trim(begin, end);
    if (begin == end || *begin == '#') continue;

    const Line trimmed{begin, end};
    size_t separatorLength = 0;
    const char* split = findSeparator(trimmed, separator, separatorLength);
    if (split == nullptr) continue;

    const char* termBegin = begin;
    const char* termEnd = split;
    const char* defBegin = split + separatorLength;
    const char* defEnd = end;
    trim(termBegin, termEnd);
    trim(defBegin, defEnd);
    if (termBegin == termEnd || defBegin == defEnd) continue;

    Card card;
    card.term.assign(termBegin, termEnd);
    card.definition.assign(defBegin, defEnd);
    truncateUtf8(card.term, kMaxFieldBytes);
    truncateUtf8(card.definition, kMaxFieldBytes);
    out.push_back(std::move(card));
  }
  return out.size() - before;
}

bool isDeckFileName(const char* name) {
  if (name == nullptr || name[0] == '\0' || name[0] == '.') return false;
  const char* dot = strrchr(name, '.');
  if (dot == nullptr) return false;
  char ext[5] = {};
  size_t i = 0;
  for (const char* p = dot + 1; *p != '\0' && i < sizeof(ext) - 1; ++p, ++i) {
    ext[i] = static_cast<char>(tolower(static_cast<unsigned char>(*p)));
  }
  if (dot[1 + i] != '\0') return false;  // extension longer than 4 characters
  return strcmp(ext, "txt") == 0 || strcmp(ext, "tsv") == 0 || strcmp(ext, "csv") == 0;
}

std::string deckTitleFromFileName(const char* name) {
  if (name == nullptr) return {};
  std::string title(name);
  const size_t dot = title.rfind('.');
  if (dot != std::string::npos && dot > 0) title.resize(dot);
  for (char& c : title) {
    if (c == '_') c = ' ';
  }
  return title;
}

}  // namespace quizlet
