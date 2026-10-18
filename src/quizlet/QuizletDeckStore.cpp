#include "QuizletDeckStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace quizlet {
namespace {

constexpr int kMaxListedFiles = 200;
constexpr char kStateDir[] = "/.crosspoint";
constexpr char kOptionsPath[] = "/.crosspoint/quizlet_options.txt";
constexpr char kRecordsPath[] = "/.crosspoint/quizlet_records.txt";
constexpr char kSyncedPath[] = "/quizlet/.synced";
// Both files are tiny; the records file holds one short line per deck.
constexpr size_t kSmallFileBytes = 4096;

std::string readSmallFile(const char* path, const size_t maxBytes = kSmallFileBytes) {
  if (!Storage.exists(path)) return {};
  // Heap, not stack: 4 KB would eat half of the ESP32 main-loop stack.
  auto buffer = makeUniqueNoThrow<char[]>(maxBytes);
  if (!buffer) {
    LOG_ERR("QZL", "OOM: reading %s", path);
    return {};
  }
  const size_t n = Storage.readFileToBuffer(path, buffer.get(), maxBytes);
  return std::string(buffer.get(), n);
}

bool writeSmallFile(const char* path, const std::string& content, const char* dir = kStateDir) {
  if (!Storage.ensureDirectoryExists(dir) || !Storage.writeFile(path, String(content.c_str()))) {
    LOG_ERR("QZL", "Could not write %s", path);
    return false;
  }
  return true;
}

constexpr char kProgressDir[] = "/quizlet/.progress";
// Up to 500 cards of about 20 bytes each.
constexpr size_t kProgressFileBytes = 16 * 1024;

std::string baseName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string progressPathFor(const std::string& deckFileName) {
  return std::string(kProgressDir) + "/" + deckFileName;
}

// The synced list holds one short line per dashboard deck.
constexpr size_t kSyncedFileBytes = 16 * 1024;

bool lessCaseInsensitive(const DeckEntry& a, const DeckEntry& b) {
  return std::lexicographical_compare(
      a.title.begin(), a.title.end(), b.title.begin(), b.title.end(), [](const char x, const char y) {
        return tolower(static_cast<unsigned char>(x)) < tolower(static_cast<unsigned char>(y));
      });
}

}  // namespace

bool hasDecks() {
  if (!Storage.exists(kDeckDir)) return false;
  if (Storage.exists(kSyncConfigPath)) return true;
  for (const auto& name : Storage.listFiles(kDeckDir, kMaxListedFiles)) {
    if (isDeckFileName(name.c_str())) return true;
  }
  return false;
}

void listDecks(std::vector<DeckEntry>& out) {
  out.clear();
  if (!Storage.exists(kDeckDir)) return;
  const auto names = Storage.listFiles(kDeckDir, kMaxListedFiles);
  out.reserve(names.size());
  for (const auto& name : names) {
    if (!isDeckFileName(name.c_str())) continue;
    DeckEntry entry;
    entry.title = deckTitleFromFileName(name.c_str());
    entry.path = std::string(kDeckDir) + "/" + name.c_str();
    out.push_back(std::move(entry));
  }
  std::sort(out.begin(), out.end(), lessCaseInsensitive);
}

LoadResult loadDeck(const std::string& path, std::vector<Card>& out) {
  out.clear();

  // One temporary buffer for the raw file, freed as soon as parsing is done.
  // Heap rather than stack: the file can be tens of KB.
  auto buffer = makeUniqueNoThrow<char[]>(kMaxDeckFileBytes + 1);
  if (!buffer) {
    LOG_ERR("QZL", "OOM: deck buffer (%u bytes)", static_cast<unsigned>(kMaxDeckFileBytes + 1));
    return LoadResult::OutOfMemory;
  }

  const size_t bytesRead = Storage.readFileToBuffer(path.c_str(), buffer.get(), kMaxDeckFileBytes + 1);
  if (bytesRead == 0) {
    LOG_ERR("QZL", "Could not read deck %s", path.c_str());
    return LoadResult::ReadFailed;
  }

  size_t length = bytesRead;
  if (length >= kMaxDeckFileBytes) {
    // The file was cut off: drop the incomplete last line.
    // (memrchr is a GNU extension, not available on every toolchain.)
    size_t cut = length;
    while (cut > 0 && buffer[cut - 1] != '\n') --cut;
    if (cut > 0) length = cut - 1;
    LOG_INF("QZL", "Deck %s truncated to %u bytes", path.c_str(), static_cast<unsigned>(length));
  }

  out.reserve(std::min<size_t>(kMaxCards, 64));
  parseDeckText(buffer.get(), length, out);
  LOG_INF("QZL", "Loaded %u cards from %s", static_cast<unsigned>(out.size()), path.c_str());
  return out.empty() ? LoadResult::Empty : LoadResult::Ok;
}

bool hasSyncConfig() { return Storage.exists(kSyncConfigPath); }

bool loadSyncConfig(SyncConfig& out) {
  const std::string text = readSmallFile(kSyncConfigPath);
  return parseSyncConfig(text.c_str(), text.size(), out);
}

std::vector<SyncedEntry> loadSyncedState() {
  const std::string text = readSmallFile(kSyncedPath, kSyncedFileBytes);
  return parseSyncedState(text.c_str(), text.size());
}

bool saveSyncedState(const std::vector<SyncedEntry>& entries) {
  const std::string text = serializeSyncedState(entries);
  if (text.size() >= kSyncedFileBytes) {
    LOG_ERR("QZL", "Synced list too large (%u bytes)", static_cast<unsigned>(text.size()));
    return false;
  }
  return writeSmallFile(kSyncedPath, text, kDeckDir);
}

void listDeckFiles(std::vector<std::string>& out) {
  out.clear();
  if (!Storage.exists(kDeckDir)) return;
  for (const auto& name : Storage.listFiles(kDeckDir, kMaxListedFiles)) {
    if (isDeckFileName(name.c_str())) out.emplace_back(name.c_str());
  }
}

StudyOptions loadOptions() {
  const std::string text = readSmallFile(kOptionsPath);
  return parseOptions(text.c_str(), text.size());
}

void saveOptions(const StudyOptions& options) { (void)writeSmallFile(kOptionsPath, serializeOptions(options)); }

unsigned loadSpeedRecord(const std::string& deckPath) { return recordFor(readSmallFile(kRecordsPath), deckPath); }

void saveSpeedRecord(const std::string& deckPath, const unsigned best) {
  const std::string updated = withRecord(readSmallFile(kRecordsPath), deckPath, best);
  if (updated.size() >= kSmallFileBytes) {
    LOG_ERR("QZL", "Records file full, not saving");
    return;
  }
  (void)writeSmallFile(kRecordsPath, updated);
}

DeckProgress loadProgress(const std::string& deckPath, const std::vector<Card>& deck) {
  const std::string text = readSmallFile(progressPathFor(baseName(deckPath)).c_str(), kProgressFileBytes);
  return parseProgress(text.c_str(), text.size(), deck);
}

bool saveProgress(const std::string& deckPath, const DeckProgress& progress) {
  return writeSmallFile(progressPathFor(baseName(deckPath)).c_str(), serializeProgress(progress), kProgressDir);
}

std::string loadProgressText(const std::string& deckFileName) {
  return readSmallFile(progressPathFor(deckFileName).c_str(), kProgressFileBytes);
}

bool loadRound(const std::string& deckPath, SavedRound& out) {
  const std::string text = readSmallFile((progressPathFor(baseName(deckPath)) + ".round").c_str(), kProgressFileBytes);
  return parseRound(text.c_str(), text.size(), out);
}

bool saveRound(const std::string& deckPath, const SavedRound& round) {
  return writeSmallFile((progressPathFor(baseName(deckPath)) + ".round").c_str(), serializeRound(round), kProgressDir);
}

void removeRound(const std::string& deckPath) {
  const std::string path = progressPathFor(baseName(deckPath)) + ".round";
  if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
}

}  // namespace quizlet
