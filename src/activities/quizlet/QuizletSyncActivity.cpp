#include "QuizletSyncActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/CompactHeader.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "network/WifiUtils.h"
#include "quizlet/QuizletDeckStore.h"

namespace {
constexpr char kListTmpPath[] = "/quizlet/.sync_list.tmp";
// The deck list is one short line per deck; 16 KB is hundreds of decks.
constexpr size_t kMaxListBytes = 16 * 1024;
constexpr int kAttempts = 2;

// Tokens are hex from the dashboard, but encode anything unusual anyway.
std::string urlEncode(const std::string& value) {
  static const char hex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size());
  for (const unsigned char c : value) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}
}  // namespace

QuizletSyncActivity::QuizletSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         const quizlet::Library library)
    : Activity("QuizletSync", renderer, mappedInput), library(library) {}

void QuizletSyncActivity::setState(const State next) {
  {
    RenderLock lock(*this);
    state = next;
  }
  requestUpdateAndWait();
}

std::string QuizletSyncActivity::deviceUrl(const char* path) const {
  return config.url + path + (strchr(path, '?') ? "&token=" : "?token=") + urlEncode(config.token);
}

void QuizletSyncActivity::onEnter() {
  Activity::onEnter();
  if (!quizlet::loadSyncConfig(config)) {
    LOG_ERR("QZL", "No valid %s", quizlet::kSyncConfigPath);
    state = State::NoConfig;
    requestUpdate();
    return;
  }
  state = State::Connecting;
  wifiActivated = true;
  if (hasActiveStationWifiConnection()) {
    onWifiReady(true);
    return;
  }
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiReady(!result.isCancelled); });
}

void QuizletSyncActivity::onExit() {
  Activity::onExit();
  if (wifiActivated && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
  }
}

void QuizletSyncActivity::onWifiReady(const bool connected) {
  if (!connected) {
    finish();
    return;
  }
  setState(State::Syncing);
  const bool ok = runSync();
  Storage.remove(kListTmpPath);
  if (cancelRequested) {
    finishAfterBackPress();
    return;
  }
  setState(ok ? State::Done : State::Failed);
}

bool QuizletSyncActivity::pollCancel() {
  if (cancelRequested) return true;
  mappedInput.update();
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.isPressed(MappedInputManager::Button::Back) ||
      TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer))) {
    cancelRequested = true;
  }
  return cancelRequested;
}

bool QuizletSyncActivity::runSync() {
  LOG_INF("QZL", "Sync start: free=%u maxAlloc=%u", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
  updated = removed = unchanged = uploaded = 0;
  progressIndex = progressTotal = 0;

  // wolfSSL, like the OPDS book downloads: mbedTLS managed the first request
  // but every following one failed on an X4, before reaching the server.
  HttpDownloader::DownloadOptions options;
  options.shouldCancel = [this]() { return pollCancel(); };
  options.transport = HttpDownloader::Transport::WOLFSSL;

  // 1. Deck list. Saved to the card first so the TLS buffers are freed before parsing.
  // The Chinese library asks only for the decks the dashboard files under "chinese".
  const std::string folder = quizlet::librarySyncFolder(library);
  const std::string listPath = folder.empty() ? "/api/device/decks.txt" : "/api/device/decks.txt?folder=" + folder;
  Storage.remove(kListTmpPath);
  HttpDownloader::DownloadError result = HttpDownloader::HTTP_ERROR;
  for (int attempt = 0; attempt < kAttempts && result != HttpDownloader::OK; ++attempt) {
    if (attempt > 0) delay(800);
    result = HttpDownloader::downloadToFile(deviceUrl(listPath.c_str()), kListTmpPath, nullptr, &cancelRequested,
                                            "", "", options);
    if (result == HttpDownloader::ABORTED) return false;
  }
  if (result != HttpDownloader::OK) {
    LOG_ERR("QZL", "Deck list download failed (%d)", static_cast<int>(result));
    return false;
  }

  auto listText = makeUniqueNoThrow<char[]>(kMaxListBytes);
  if (!listText) {
    LOG_ERR("QZL", "OOM: deck list buffer");
    return false;
  }
  const size_t listLength = Storage.readFileToBuffer(kListTmpPath, listText.get(), kMaxListBytes);
  std::vector<quizlet::RemoteDeck> remote;
  quizlet::parseRemoteList(listText.get(), listLength, remote);
  listText.reset();

  // 2. Send learning progress so the dashboard can show the hard cards. A
  // failed upload does not fail the sync; the next sync sends it again.
  for (const auto& deck : remote) {
    if (pollCancel()) return false;
    const std::string text = quizlet::loadProgressText(library, deck.file);
    if (text.empty()) continue;
    freeink::SecureHttpClient http;
    http.setInsecure();
    http.setTimeout(20000);
    if (!http.begin(deviceUrl(("/api/device/decks/" + deck.id + "/progress").c_str()))) continue;
    http.addHeader("Content-Type", "text/plain; charset=utf-8");
    const int status = http.sendRequest("POST", text);
    http.end();
    if (status == 200) {
      ++uploaded;
    } else {
      LOG_ERR("QZL", "Progress upload for %s failed (%d)", deck.file.c_str(), status);
    }
  }

  // 3. Plan against what is on the card.
  std::vector<std::string> localFiles;
  quizlet::listDeckFiles(library, localFiles);
  std::vector<quizlet::SyncedEntry> synced = quizlet::loadSyncedState(library);
  const quizlet::SyncPlan plan = quizlet::planSync(remote, synced, localFiles);
  unchanged = static_cast<unsigned>(plan.unchanged);
  progressTotal = static_cast<unsigned>(plan.download.size());
  LOG_INF("QZL", "Sync plan: %u to download, %u to remove, %u unchanged, progress sent for %u", progressTotal,
          static_cast<unsigned>(plan.remove.size()), unchanged, uploaded);

  // The new synced state: every remote deck that is (or stays) on the card.
  std::vector<quizlet::SyncedEntry> nextState;
  nextState.reserve(remote.size());
  for (const auto& deck : remote) {
    const bool pending = std::any_of(plan.download.begin(), plan.download.end(),
                                     [&deck](const quizlet::RemoteDeck& d) { return d.file == deck.file; });
    if (!pending) nextState.push_back({deck.file, deck.updatedAt});
  }

  // 4. Download new and changed decks. A failed deck keeps its old file.
  bool allOk = true;
  HttpDownloader::DownloadOptions deckOptions;
  deckOptions.shouldCancel = [this]() { return pollCancel(); };
  deckOptions.stageAsPart = true;
  deckOptions.transport = HttpDownloader::Transport::WOLFSSL;
  for (const auto& deck : plan.download) {
    {
      RenderLock lock(*this);
      ++progressIndex;
    }
    requestUpdateAndWait();
    const std::string path = std::string(quizlet::libraryDir(library)) + "/" + deck.file;
    const std::string url = deviceUrl(("/api/device/decks/" + deck.id + "/export.txt").c_str());
    HttpDownloader::DownloadError deckResult = HttpDownloader::HTTP_ERROR;
    for (int attempt = 0; attempt < kAttempts && deckResult != HttpDownloader::OK; ++attempt) {
      if (attempt > 0) delay(800);
      LOG_INF("QZL", "Deck %s attempt %d: free=%u maxAlloc=%u", deck.file.c_str(), attempt + 1,
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
      deckResult = HttpDownloader::downloadToFile(url, path, nullptr, &cancelRequested, "", "", deckOptions);
      if (deckResult == HttpDownloader::ABORTED) break;
    }
    if (deckResult == HttpDownloader::ABORTED) break;
    if (deckResult == HttpDownloader::OK) {
      ++updated;
      nextState.push_back({deck.file, deck.updatedAt});
    } else {
      LOG_ERR("QZL", "Deck %s download failed (%d)", deck.file.c_str(), static_cast<int>(deckResult));
      allOk = false;
      // Keep the previous entry so a later sync can still remove or update it.
      for (const auto& entry : synced) {
        if (entry.file == deck.file) nextState.push_back({entry.file, ""});
      }
    }
  }

  // 5. Remove decks that were deleted on the dashboard (only ones we synced).
  if (!cancelRequested) {
    for (const auto& file : plan.remove) {
      const std::string path = std::string(quizlet::libraryDir(library)) + "/" + file;
      if (Storage.remove(path.c_str())) ++removed;
    }
  } else {
    // Cancelled: keep tracking decks we did not get to remove yet.
    for (const auto& file : plan.remove) nextState.push_back({file, ""});
  }

  quizlet::saveSyncedState(library, nextState);
  return allOk && !cancelRequested;
}

void QuizletSyncActivity::loop() {
  if (state == State::Syncing || state == State::Connecting) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      TouchHeaderBackButton::wasTapped(mappedInput, TouchHeaderBackButton::compactHeaderRect(renderer))) {
    finish();
  }
}

void QuizletSyncActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, tr(STR_QUIZLET_SYNC));
  } else {
    CompactHeader::drawTitle(renderer, tr(STR_QUIZLET_SYNC));
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - 2 * side;
  const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int centerY = renderer.getScreenHeight() / 2 - lineHeight;

  char line[96] = {};
  const char* title = nullptr;
  switch (state) {
    case State::Connecting:
    case State::Syncing:
      if (progressTotal == 0) {
        title = tr(STR_QUIZLET_SYNC_FETCHING);
      } else {
        snprintf(line, sizeof(line), tr(STR_QUIZLET_SYNC_PROGRESS_FORMAT), progressIndex, progressTotal);
        title = line;
      }
      break;
    case State::Done:
      title = tr(STR_QUIZLET_SYNC_DONE);
      snprintf(line, sizeof(line), tr(STR_QUIZLET_SYNC_RESULT_FORMAT), updated, removed, unchanged);
      break;
    case State::Failed:
      title = tr(STR_QUIZLET_SYNC_FAILED);
      if (updated > 0 || removed > 0) {
        snprintf(line, sizeof(line), tr(STR_QUIZLET_SYNC_RESULT_FORMAT), updated, removed, unchanged);
      }
      break;
    case State::NoConfig:
      title = tr(STR_QUIZLET_SYNC_NO_CONFIG);
      break;
  }

  const auto titleLines = renderer.wrappedText(UI_12_FONT_ID, title, width, 3, EpdFontFamily::BOLD);
  int y = centerY - static_cast<int>(titleLines.size() - 1) * lineHeight / 2;
  for (const auto& text : titleLines) {
    renderer.drawCenteredText(UI_12_FONT_ID, y, text.c_str(), true, EpdFontFamily::BOLD);
    y += lineHeight;
  }
  if ((state == State::Done || state == State::Failed) && line[0] != '\0') {
    renderer.drawCenteredText(UI_10_FONT_ID, y + lineHeight / 2, line);
  }

  // Back cancels while syncing and returns to the deck list afterwards.
  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), nullptr, nullptr, nullptr);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
