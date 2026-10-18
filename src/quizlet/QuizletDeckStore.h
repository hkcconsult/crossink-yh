#pragma once

#include <cstdint>
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
constexpr char kChineseDir[] = "/chinese";
// One sync.cfg serves both libraries.
constexpr char kSyncConfigPath[] = "/quizlet/sync.cfg";

// A library is a deck folder with its own Home entry, synced state and progress.
enum class Library : uint8_t { Flashcards, Chinese };
const char* libraryDir(Library library);
// Dashboard folder name for the deck list ("" for the default flashcards).
const char* librarySyncFolder(Library library);

// Home entries to show (bit flags): Flashcards when /quizlet has decks or a
// sync.cfg, Chinese when the /chinese folder exists.
constexpr uint8_t kMenuFlashcards = 1;
constexpr uint8_t kMenuChinese = 2;
uint8_t studyMenus();

struct DeckEntry {
  std::string title;  // file name without extension
  std::string path;   // full SD path
  int mastery = -1;   // 0..100 from the deck's progress file, -1 when never studied
  char masteryText[6] = {};  // "100%"
};

enum class LoadResult { Ok, Empty, ReadFailed, OutOfMemory };

// Lists the library's deck files, sorted by title (case-insensitive).
void listDecks(Library library, std::vector<DeckEntry>& out);

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
std::vector<SyncedEntry> loadSyncedState(Library library);
bool saveSyncedState(Library library, const std::vector<SyncedEntry>& entries);
// File names (not paths) of the deck files currently in the library folder.
void listDeckFiles(Library library, std::vector<std::string>& out);

// Leitner progress per deck, in <deck folder>/.progress/<deck file name>.
DeckProgress loadProgress(const std::string& deckPath, const std::vector<Card>& deck);
bool saveProgress(const std::string& deckPath, const DeckProgress& progress);
// Raw progress file for a deck file name (for the dashboard upload); empty if none.
std::string loadProgressText(Library library, const std::string& deckFileName);

// Unfinished round per deck, in <deck folder>/.progress/<deck file name>.round.
bool loadRound(const std::string& deckPath, SavedRound& out);
bool saveRound(const std::string& deckPath, const SavedRound& round);
void removeRound(const std::string& deckPath);

}  // namespace quizlet
