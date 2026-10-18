#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Pull-only sync with the YH dashboard: decks managed on the web are copied to
// /quizlet on the SD card. Pure logic (config, remote list, plan) so it can be
// unit tested on the host; the network and SD work lives in QuizletSyncActivity.
namespace quizlet {

struct SyncConfig {
  std::string url;    // e.g. "https://dashboard.yuhengcheung.com", no trailing slash
  std::string token;  // device token, sent as ?token=
};

// "url=...\ntoken=...\n" as downloaded from the dashboard. Returns false when
// either value is missing or the URL is not http(s).
bool parseSyncConfig(const char* text, size_t length, SyncConfig& out);

struct RemoteDeck {
  std::string id;
  std::string updatedAt;
  std::string file;  // safe file name inside /quizlet, always ending in .txt
};

// One "id<TAB>updatedAt<TAB>file" line per deck. Unsafe file names are cleaned
// and duplicates get a " (2)", " (3)" suffix so two decks never share a file.
size_t parseRemoteList(const char* text, size_t length, std::vector<RemoteDeck>& out);

// Cleans a server-provided name: no path separators, no leading dot, ends in .txt.
std::string safeDeckFile(const std::string& name);

struct SyncedEntry {
  std::string file;
  std::string updatedAt;
};

// /quizlet/.synced: "file<TAB>updatedAt" per deck that came from the dashboard.
std::vector<SyncedEntry> parseSyncedState(const char* text, size_t length);
std::string serializeSyncedState(const std::vector<SyncedEntry>& entries);

struct SyncPlan {
  std::vector<RemoteDeck> download;  // new or changed on the dashboard, or missing locally
  std::vector<std::string> remove;   // synced before, deleted on the dashboard since
  size_t unchanged = 0;
};

// Files the user put on the card by hand are never removed: only files listed
// in the synced state can be deleted.
SyncPlan planSync(const std::vector<RemoteDeck>& remote, const std::vector<SyncedEntry>& synced,
                  const std::vector<std::string>& localFiles);

}  // namespace quizlet
