#include "QuizletSync.h"

#include <algorithm>
#include <cctype>

namespace quizlet {
namespace {

constexpr size_t kMaxFileNameBytes = 64;

// Splits text into lines (CRLF tolerant) and calls fn(line) for each non-empty one.
template <typename Fn>
void forEachLine(const char* text, const size_t length, Fn fn) {
  if (text == nullptr) return;
  size_t start = 0;
  while (start < length) {
    size_t end = start;
    while (end < length && text[end] != '\n') ++end;
    size_t lineEnd = end;
    if (lineEnd > start && text[lineEnd - 1] == '\r') --lineEnd;
    if (lineEnd > start) fn(std::string(text + start, lineEnd - start));
    start = end + 1;
  }
}

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> parts;
  size_t start = 0;
  for (size_t i = 0; i <= line.size(); ++i) {
    if (i == line.size() || line[i] == '\t') {
      parts.push_back(line.substr(start, i - start));
      start = i + 1;
    }
  }
  return parts;
}

std::string trim(const std::string& value) {
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end && (value[begin] == ' ' || value[begin] == '\t')) ++begin;
  while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t')) --end;
  return value.substr(begin, end - begin);
}

bool endsWithTxt(const std::string& name) {
  if (name.size() < 4) return false;
  std::string ext = name.substr(name.size() - 4);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
  return ext == ".txt";
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
  return std::find(list.begin(), list.end(), value) != list.end();
}

}  // namespace

bool parseSyncConfig(const char* text, const size_t length, SyncConfig& out) {
  SyncConfig config;
  forEachLine(text, length, [&config](const std::string& line) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) return;
    const std::string key = trim(line.substr(0, eq));
    const std::string value = trim(line.substr(eq + 1));
    if (key == "url") config.url = value;
    if (key == "token") config.token = value;
  });
  while (!config.url.empty() && config.url.back() == '/') config.url.pop_back();
  const bool httpUrl = config.url.rfind("https://", 0) == 0 || config.url.rfind("http://", 0) == 0;
  if (!httpUrl || config.token.empty()) return false;
  out = std::move(config);
  return true;
}

std::string safeDeckFile(const std::string& name) {
  std::string out;
  out.reserve(name.size());
  for (const char c : name) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
        c == '\t' || static_cast<unsigned char>(c) < 0x20) {
      out += ' ';
    } else {
      out += c;
    }
  }
  // Leading dots or spaces would hide the file or leave ".." remnants.
  while (!out.empty() && (out.front() == '.' || out.front() == ' ')) out.erase(out.begin());
  out = trim(out);
  if (!endsWithTxt(out)) out += ".txt";
  if (out.size() > kMaxFileNameBytes) {
    // Cut the base name, keep the extension, never split a UTF-8 code point.
    size_t cut = kMaxFileNameBytes - 4;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
    out = out.substr(0, cut) + ".txt";
  }
  if (out == ".txt") out = "set.txt";
  return out;
}

size_t parseRemoteList(const char* text, const size_t length, std::vector<RemoteDeck>& out) {
  const size_t before = out.size();
  std::vector<std::string> used;
  for (const auto& deck : out) used.push_back(deck.file);
  forEachLine(text, length, [&](const std::string& line) {
    const auto parts = splitTabs(line);
    if (parts.size() < 3 || parts[0].empty()) return;
    RemoteDeck deck{parts[0], parts[1], safeDeckFile(parts[2])};
    if (contains(used, deck.file)) {
      const std::string base = deck.file.substr(0, deck.file.size() - 4);
      for (int n = 2; n < 100; ++n) {
        std::string candidate = base + " (" + std::to_string(n) + ").txt";
        if (!contains(used, candidate)) {
          deck.file = std::move(candidate);
          break;
        }
      }
    }
    used.push_back(deck.file);
    out.push_back(std::move(deck));
  });
  return out.size() - before;
}

std::vector<SyncedEntry> parseSyncedState(const char* text, const size_t length) {
  std::vector<SyncedEntry> entries;
  forEachLine(text, length, [&entries](const std::string& line) {
    const auto parts = splitTabs(line);
    if (parts.size() >= 2 && !parts[0].empty()) entries.push_back({parts[0], parts[1]});
  });
  return entries;
}

std::string serializeSyncedState(const std::vector<SyncedEntry>& entries) {
  std::string out;
  for (const auto& entry : entries) out += entry.file + "\t" + entry.updatedAt + "\n";
  return out;
}

SyncPlan planSync(const std::vector<RemoteDeck>& remote, const std::vector<SyncedEntry>& synced,
                  const std::vector<std::string>& localFiles) {
  SyncPlan plan;
  for (const auto& deck : remote) {
    const auto it = std::find_if(synced.begin(), synced.end(),
                                 [&deck](const SyncedEntry& entry) { return entry.file == deck.file; });
    const bool upToDate = it != synced.end() && it->updatedAt == deck.updatedAt && contains(localFiles, deck.file);
    if (upToDate) {
      ++plan.unchanged;
    } else {
      plan.download.push_back(deck);
    }
  }
  for (const auto& entry : synced) {
    const bool stillRemote =
        std::any_of(remote.begin(), remote.end(), [&entry](const RemoteDeck& deck) { return deck.file == entry.file; });
    if (!stillRemote && contains(localFiles, entry.file)) plan.remove.push_back(entry.file);
  }
  return plan;
}

}  // namespace quizlet
