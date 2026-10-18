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

bool lessCaseInsensitive(const DeckEntry& a, const DeckEntry& b) {
  return std::lexicographical_compare(
      a.title.begin(), a.title.end(), b.title.begin(), b.title.end(), [](const char x, const char y) {
        return tolower(static_cast<unsigned char>(x)) < tolower(static_cast<unsigned char>(y));
      });
}

}  // namespace

bool hasDecks() {
  if (!Storage.exists(kDeckDir)) return false;
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

}  // namespace quizlet
