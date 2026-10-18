#pragma once

#include <string>
#include <vector>

#include "QuizletDeckParser.h"

// SD-card side of the flashcard feature. Decks live as plain text files in
// /quizlet on the SD card (Quizlet export: "term<TAB>definition" per line).
namespace quizlet {

constexpr char kDeckDir[] = "/quizlet";

struct DeckEntry {
  std::string title;  // file name without extension
  std::string path;   // full SD path
};

enum class LoadResult { Ok, Empty, ReadFailed, OutOfMemory };

// Cheap check used by Home to decide whether to show the menu entry.
bool hasDecks();

// Lists deck files in kDeckDir, sorted by title (case-insensitive).
void listDecks(std::vector<DeckEntry>& out);

// Reads and parses a deck. Files larger than kMaxDeckFileBytes are read up to
// that size (the last, partial line is dropped).
LoadResult loadDeck(const std::string& path, std::vector<Card>& out);

}  // namespace quizlet
