#pragma once

#include <string>
#include <vector>

#include "QuizletDeckParser.h"
#include "QuizletProgress.h"
#include "QuizletStudy.h"
#include "QuizletSync.h"

// SD-card side of the flashcard feature. Decks live as plain text files in
// /quizlet on the SD card (Quizlet export: "term<TAB>definition" per line).
namespace quizlet {

constexpr char kDeckDir[] = "/quizlet";
constexpr char kSyncConfigPath[] = "/quizlet/sync.cfg";

struct DeckEntry {
  std::string title;  // file name without extension
  std::string path;   // full SD path
};

enum class LoadResult { Ok, Empty, ReadFailed, OutOfMemory };

// Cheap check used by Home to decide whether to show the menu entry: true when
// there is at least one deck or a dashboard sync configuration.
bool hasDecks();

// Lists deck files in kDeckDir, sorted by title (case-insensitive).
void listDecks(std::vector<DeckEntry>& out);

// Reads and parses a deck. Files larger than kMaxDeckFileBytes are read up to
// that size (the last, partial line is dropped).
LoadResult loadDeck(const std::string& path, std::vector<Card>& out);

// Last used study options (mode, answer with, order); defaults when missing.
StudyOptions loadOptions();
void saveOptions(const StudyOptions& options);

// Best speed-round score for a deck, and storing a new best.
unsigned loadSpeedRecord(const std::string& deckPath);
void saveSpeedRecord(const std::string& deckPath, unsigned best);

// Dashboard sync configuration (/quizlet/sync.cfg) and synced-deck bookkeeping.
bool hasSyncConfig();
bool loadSyncConfig(SyncConfig& out);
std::vector<SyncedEntry> loadSyncedState();
bool saveSyncedState(const std::vector<SyncedEntry>& entries);
// File names (not paths) of the deck files currently in /quizlet.
void listDeckFiles(std::vector<std::string>& out);

// Leitner progress per deck, in /quizlet/.progress/<deck file name>.
DeckProgress loadProgress(const std::string& deckPath, const std::vector<Card>& deck);
bool saveProgress(const std::string& deckPath, const DeckProgress& progress);
// Raw progress file for a deck file name (for the dashboard upload); empty if none.
std::string loadProgressText(const std::string& deckFileName);

// Unfinished round per deck, in /quizlet/.progress/<deck file name>.round.
bool loadRound(const std::string& deckPath, SavedRound& out);
bool saveRound(const std::string& deckPath, const SavedRound& round);
void removeRound(const std::string& deckPath);

}  // namespace quizlet
