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

// Up to 500 cards of about 20 bytes each.
constexpr size_t kProgressFileBytes = 16 * 1024;

std::string syncedPath(const Library library) { return std::string(libraryDir(library)) + "/.synced"; }
std::string progressDir(const Library library) { return std::string(libraryDir(library)) + "/.progress"; }

// "/quizlet/Frans.txt" -> "/quizlet/.progress" and "Frans.txt".
std::string progressDirOf(const std::string& deckPath) {
  const size_t slash = deckPath.find_last_of('/');
  return (slash == std::string::npos ? std::string() : deckPath.substr(0, slash)) + "/.progress";
}
std::string progressPathOf(const std::string& deckPath) {
  const size_t slash = deckPath.find_last_of('/');
  return progressDirOf(deckPath) + "/" + (slash == std::string::npos ? deckPath : deckPath.substr(slash + 1));
}

bool hasDeckFiles(const char* dir) {
  if (!Storage.exists(dir)) return false;
  for (const auto& name : Storage.listFiles(dir, kMaxListedFiles)) {
    if (isDeckFileName(name.c_str())) return true;
  }
  return false;
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

const char* libraryDir(const Library library) {
  return library == Library::Chinese ? kChineseDir : kDeckDir;
}

const char* librarySyncFolder(const Library library) { return library == Library::Chinese ? "chinese" : ""; }

uint8_t studyMenus() {
  uint8_t menus = 0;
  if (Storage.exists(kDeckDir) && (Storage.exists(kSyncConfigPath) || hasDeckFiles(kDeckDir))) {
    menus |= kMenuFlashcards;
  }
  // The Chinese folder alone is enough: a dashboard sync can fill it.
  if (Storage.exists(kChineseDir)) menus |= kMenuChinese;
  return menus;
}

void listDecks(const Library library, std::vector<DeckEntry>& out) {
  out.clear();
  const char* dir = libraryDir(library);
  if (!Storage.exists(dir)) return;
  const auto names = Storage.listFiles(dir, kMaxListedFiles);
  out.reserve(names.size());
  for (const auto& name : names) {
    if (!isDeckFileName(name.c_str())) continue;
    DeckEntry entry;
    entry.title = deckTitleFromFileName(name.c_str());
    entry.path = std::string(dir) + "/" + name.c_str();
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

std::vector<SyncedEntry> loadSyncedState(const Library library) {
  const std::string text = readSmallFile(syncedPath(library).c_str(), kSyncedFileBytes);
  return parseSyncedState(text.c_str(), text.size());
}

bool saveSyncedState(const Library library, const std::vector<SyncedEntry>& entries) {
  const std::string text = serializeSyncedState(entries);
  if (text.size() >= kSyncedFileBytes) {
    LOG_ERR("QZL", "Synced list too large (%u bytes)", static_cast<unsigned>(text.size()));
    return false;
  }
  return writeSmallFile(syncedPath(library).c_str(), text, libraryDir(library));
}

void listDeckFiles(const Library library, std::vector<std::string>& out) {
  out.clear();
  const char* dir = libraryDir(library);
  if (!Storage.exists(dir)) return;
  for (const auto& name : Storage.listFiles(dir, kMaxListedFiles)) {
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
  const std::string text = readSmallFile(progressPathOf(deckPath).c_str(), kProgressFileBytes);
  return parseProgress(text.c_str(), text.size(), deck);
}

bool saveProgress(const std::string& deckPath, const DeckProgress& progress) {
  return writeSmallFile(progressPathOf(deckPath).c_str(), serializeProgress(progress), progressDirOf(deckPath).c_str());
}

std::string loadProgressText(const Library library, const std::string& deckFileName) {
  return readSmallFile((progressDir(library) + "/" + deckFileName).c_str(), kProgressFileBytes);
}

bool loadRound(const std::string& deckPath, SavedRound& out) {
  const std::string text = readSmallFile((progressPathOf(deckPath) + ".round").c_str(), kProgressFileBytes);
  return parseRound(text.c_str(), text.size(), out);
}

bool saveRound(const std::string& deckPath, const SavedRound& round) {
  return writeSmallFile((progressPathOf(deckPath) + ".round").c_str(), serializeRound(round),
                        progressDirOf(deckPath).c_str());
}

void removeRound(const std::string& deckPath) {
  const std::string path = progressPathOf(deckPath) + ".round";
  if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
}

}  // namespace quizlet
