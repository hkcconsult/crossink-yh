#pragma once

#include <string>

#include "activities/Activity.h"
#include "quizlet/QuizletDeckStore.h"
#include "quizlet/QuizletSync.h"

// Connects to Wi-Fi and copies decks from the YH dashboard into /quizlet.
// Only new or changed decks are downloaded; decks deleted on the dashboard are
// removed again. Decks put on the card by hand are left alone.
class QuizletSyncActivity final : public Activity {
 public:
  QuizletSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, quizlet::Library library);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Syncing; }
  bool skipLoopDelay() override { return state == State::Syncing; }

 private:
  quizlet::Library library;
  enum class State { Connecting, Syncing, Done, Failed, NoConfig };

  State state = State::Connecting;
  quizlet::SyncConfig config;
  bool wifiActivated = false;
  bool cancelRequested = false;
  unsigned progressIndex = 0;
  unsigned progressTotal = 0;
  unsigned updated = 0;
  unsigned removed = 0;
  unsigned unchanged = 0;
  unsigned uploaded = 0;  // decks whose progress reached the dashboard

  void onWifiReady(bool connected);
  bool runSync();
  bool pollCancel();
  void setState(State next);
  std::string deviceUrl(const char* path) const;
};
