// Host tests for the flashcard deck parser and study session (src/quizlet).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>

#include "quizlet/QuizletDeckParser.h"
#include "quizlet/QuizletProgress.h"
#include "quizlet/QuizletSession.h"
#include "quizlet/QuizletStudy.h"
#include "quizlet/QuizletSync.h"
using namespace quizlet;
static int fails = 0;  // counted so every failing check is reported
#define CHECK(c)                                         \
  do {                                                   \
    if (!(c)) {                                          \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
      ++fails;                                           \
    }                                                    \
  } while (0)
static std::vector<Card> parse(const char* s) {
  std::vector<Card> v;
  parseDeckText(s, strlen(s), v);
  return v;
}
int main() {
  // Quizlet default export: tab + newline, with BOM and CRLF
  auto a = parse("\xEF\xBB\xBFhond\tdog\r\nkat\tcat\r\n\r\n# comment\tx\n  vogel \t bird \n");
  CHECK(a.size() == 3);
  CHECK(a[0].term == "hond" && a[0].definition == "dog");
  CHECK(a[2].term == "vogel" && a[2].definition == "bird");
  // Semicolon
  auto b = parse("de mitochondrie;energiecentrale van de cel\nribosoom;maakt eiwitten");
  CHECK(b.size() == 2);
  CHECK(b[1].definition == "maakt eiwitten");
  // Dash style, and definition containing a dash
  auto c = parse("1648 - Vrede van Munster\n1815 - Koninkrijk der Nederlanden - Willem I");
  CHECK(c.size() == 2);
  CHECK(c[1].definition == "Koninkrijk der Nederlanden - Willem I");
  // Missing parts skipped
  auto d = parse("alleen term\n\tleeg\nx\t\nok\tja");
  CHECK(d.size() == 1 && d[0].term == "ok");
  // UTF-8 truncation never splits code points
  std::string s(158, 'a');
  s += "\xC3\xA9\xC3\xA9";  // 162 bytes
  truncateUtf8(s, kMaxFieldBytes);
  CHECK(s.size() == 160);
  std::string t(159, 'a');
  t += "\xC3\xA9";
  truncateUtf8(t, kMaxFieldBytes);
  CHECK(t.size() == 159);
  // Card limit
  std::string big;
  for (int i = 0; i < 700; ++i) big += "t\td\n";
  CHECK(parse(big.c_str()).size() == kMaxCards);
  // File names
  CHECK(isDeckFileName("Biologie H3.txt"));
  CHECK(isDeckFileName("x.TSV"));
  CHECK(isDeckFileName("a.csv"));
  CHECK(!isDeckFileName(".hidden.txt"));
  CHECK(!isDeckFileName("boek.epub"));
  CHECK(!isDeckFileName("noext"));
  CHECK(!isDeckFileName("a.txtx"));
  CHECK(deckTitleFromFileName("Frans_hfst_2.txt") == "Frans hfst 2");
  // Session: no shuffle
  Session ses;
  ses.start(3, false, 0);
  CHECK(ses.currentCard() == 0 && !ses.isFlipped());
  ses.flip();
  CHECK(ses.isFlipped());
  ses.markAgain();
  CHECK(!ses.isFlipped() && ses.currentCard() == 1);
  ses.markKnown();
  ses.markKnown();  // 1, 2
  CHECK(!ses.isFinished() && ses.currentCard() == 0 && ses.remainingCount() == 1);
  ses.markKnown();
  CHECK(ses.isFinished());
  CHECK(ses.knownCount() == 3 && ses.firstTryCount() == 2);
  // Shuffle is a permutation and deterministic
  Session x, y;
  x.start(40, true, 1234);
  y.start(40, true, 1234);
  std::set<int> seen;
  bool differs = false;
  for (int i = 0; i < 40; ++i) {
    seen.insert(x.currentCard());
    if (x.currentCard() != i) differs = true;
    CHECK(x.currentCard() == y.currentCard());
    x.markKnown();
    y.markKnown();
  }
  CHECK(seen.size() == 40 && differs && x.isFinished());
  // Many "again" presses stay bounded and still finish
  Session z;
  z.start(2, false, 0);
  for (int i = 0; i < 1000; ++i) z.markAgain();
  CHECK(!z.isFinished());
  z.markKnown();
  z.markKnown();
  CHECK(z.isFinished() && z.firstTryCount() == 0);
  // Empty deck
  Session e;
  e.start(0, true, 1);
  CHECK(e.isFinished());

  // Options round-trip and tolerate junk
  StudyOptions o;
  o.mode = StudyMode::Speed;
  o.answerWith = AnswerWith::Both;
  o.order = Order::InOrder;
  const std::string ser = serializeOptions(o);
  const StudyOptions back = parseOptions(ser.c_str(), ser.size());
  CHECK(back.mode == StudyMode::Speed && back.answerWith == AnswerWith::Both && back.order == Order::InOrder);
  const char* junk = "mode=9\r\nanswer=x\nfoo\nshuffle=0";
  const StudyOptions j = parseOptions(junk, strlen(junk));
  CHECK(j.mode == StudyMode::Flip && j.answerWith == AnswerWith::Definition && j.order == Order::InOrder);
  CHECK(parseOptions(nullptr, 0).order == Order::Smart);
  // Answer direction
  uint32_t r = 7;
  CHECK(answerIsDefinition(AnswerWith::Definition, r));
  CHECK(!answerIsDefinition(AnswerWith::Term, r));
  int defs = 0;
  for (int i = 0; i < 200; ++i) defs += answerIsDefinition(AnswerWith::Both, r);
  CHECK(defs > 50 && defs < 150);
  // Multiple choice: signaalwoorden-like deck with repeated categories
  auto sw = parse(
      "ook\topsommend\nbovendien\topsommend\nmaar\ttegenstellend\nechter\ttegenstellend\nwant\tredengevend\ndus\tconclu"
      "derend\nkortom\tsamenvattend\n");
  uint16_t ch[kMaxChoices];
  size_t slot = 0;
  uint32_t rng = 42;
  for (int t = 0; t < 100; ++t) {
    const uint16_t card = static_cast<uint16_t>(t % sw.size());
    const size_t n = buildChoices(sw, card, true, rng, ch, slot);
    CHECK(n == 4);
    CHECK(slot < n);
    CHECK(ch[slot] == card);
    for (size_t a = 0; a < n; ++a)
      for (size_t b = a + 1; b < n; ++b) CHECK(sw[ch[a]].definition != sw[ch[b]].definition);
  }
  // Correct slot varies
  bool slots[4] = {};
  for (int t = 0; t < 50; ++t) {
    buildChoices(sw, 0, true, rng, ch, slot);
    slots[slot] = true;
  }
  CHECK(slots[0] && slots[1] && slots[2] && slots[3]);
  // Answer with term uses terms as options
  CHECK(buildChoices(sw, 2, false, rng, ch, slot) == 4 && sw[ch[slot]].term == "maar");
  // Too few distinct answers
  auto same = parse("a\tx\nb\tX\n");
  CHECK(buildChoices(same, 0, true, rng, ch, slot) == 0);
  auto two = parse("a\tx\nb\ty\n");
  CHECK(buildChoices(two, 1, true, rng, ch, slot) == 2 && ch[slot] == 1);
  // Overlapping answers are never offered together
  auto ov = parse(
      "nadat\ttijdsvolgorde\nten slotte\topsommend / tijdsvolgorde\nzoals\tuitleggend / "
      "vergelijkend\nbijvoorbeeld\tuitleggend\nnet als\tvergelijkend\nmaar\ttegenstellend\nwant\tredengevend\n");
  for (int t = 0; t < 200; ++t) {
    const uint16_t card = static_cast<uint16_t>(t % ov.size());
    const size_t n = buildChoices(ov, card, true, rng, ch, slot);
    CHECK(n >= 2 && ch[slot] == card);
    for (size_t a = 0; a < n; ++a)
      for (size_t b = a + 1; b < n; ++b) {
        const std::string& x = ov[ch[a]].definition;
        const std::string& y = ov[ch[b]].definition;
        const bool clash = x.find(y) != std::string::npos || y.find(x) != std::string::npos;
        CHECK(!clash);
      }
  }
  // Answer with term: other cards in the same category are valid answers, never distractors
  for (int t = 0; t < 200; ++t) {
    const uint16_t card = static_cast<uint16_t>(t % sw.size());
    const size_t n = buildChoices(sw, card, false, rng, ch, slot);
    CHECK(n >= 2 && ch[slot] == card);
    for (size_t a = 0; a < n; ++a)
      if (a != slot) CHECK(sw[ch[a]].definition != sw[card].definition);
  }
  // Records
  std::string rec;
  CHECK(recordFor(rec, "/quizlet/a.txt") == 0);
  rec = withRecord(rec, "/quizlet/a.txt", 12);
  rec = withRecord(rec, "/quizlet/b b.txt", 5);
  rec = withRecord(rec, "/quizlet/a.txt", 15);
  CHECK(recordFor(rec, "/quizlet/a.txt") == 15 && recordFor(rec, "/quizlet/b b.txt") == 5);
  CHECK(rec.find("\t12") == std::string::npos);

  // Sync config
  SyncConfig cfg;
  const char* cfgText = "url=https://dashboard.example.nl/\r\ntoken=abc123\n";
  CHECK(parseSyncConfig(cfgText, strlen(cfgText), cfg));
  CHECK(cfg.url == "https://dashboard.example.nl" && cfg.token == "abc123");
  const char* badCfg = "url=ftp://x\ntoken=a\n";
  CHECK(!parseSyncConfig(badCfg, strlen(badCfg), cfg));
  CHECK(!parseSyncConfig("url=https://x\n", 14, cfg));
  // Safe file names
  CHECK(safeDeckFile("Frans H3.txt") == "Frans H3.txt");
  CHECK(safeDeckFile("../../etc/passwd") == "etc passwd.txt");
  CHECK(safeDeckFile(".hidden") == "hidden.txt");
  CHECK(safeDeckFile("") == "set.txt");
  CHECK(safeDeckFile(std::string(100, 'x')).size() == 64);
  // Remote list with duplicate names
  std::vector<RemoteDeck> remote;
  const char* list =
      "1\t2026-10-18T10:00:00Z\tFrans.txt\n2\t2026-10-18T11:00:00Z\tBio.txt\r\n3\t2026-10-18T12:00:00Z\tFrans."
      "txt\nkapot\n";
  CHECK(parseRemoteList(list, strlen(list), remote) == 3);
  CHECK(remote[2].file == "Frans (2).txt" && remote[1].updatedAt == "2026-10-18T11:00:00Z");
  // Synced state round trip
  std::vector<SyncedEntry> synced = {{"Frans.txt", "2026-10-18T10:00:00Z"}, {"Oud.txt", "x"}, {"Bio.txt", "older"}};
  const std::string state = serializeSyncedState(synced);
  CHECK(parseSyncedState(state.c_str(), state.size()).size() == 3);
  // Plan: Frans unchanged, Bio changed, Frans (2) new, Oud removed, Eigen.txt (manual) untouched
  std::vector<std::string> local = {"Frans.txt", "Bio.txt", "Oud.txt", "Eigen.txt"};
  SyncPlan plan = planSync(remote, synced, local);
  CHECK(plan.unchanged == 1);
  CHECK(plan.download.size() == 2 && plan.download[0].file == "Bio.txt" && plan.download[1].file == "Frans (2).txt");
  CHECK(plan.remove.size() == 1 && plan.remove[0] == "Oud.txt");
  // A synced file the user deleted locally is downloaded again
  plan = planSync(remote, synced, {"Bio.txt"});
  CHECK(plan.download.size() == 3 && plan.unchanged == 0 && plan.remove.empty());
  // CJK detection for the SD font swap
  CHECK(containsCjk("\xE4\xBD\xA0\xE5\xA5\xBD"));         // 你好
  CHECK(!containsCjk("n\xC7\x90 h\xC7\x8Eo \xC2\xB7 hello"));  // nǐ hǎo · hello
  CHECK(!containsCjk("caf\xC3\xA9 \xE2\x80\x94"));          // café —
  CHECK(containsCjk("x\xEF\xBC\x8C"));                        // fullwidth comma
  CHECK(isCjkFamilyName("NotoSansCJK") && isCjkFamilyName("my-cjk-font") && !isCjkFamilyName("Literata"));
  // Leitner progress
  {
    CHECK(cardKey(Card{"hond", "dog"}) == 0x6a722bc5u);  // same FNV-1a as the dashboard
    CardProgress p;
    recordAnswer(p, true, true);
    CHECK(p.box == 2 && p.right == 1);
    recordAnswer(p, true, true);
    recordAnswer(p, true, true);
    recordAnswer(p, true, true);
    CHECK(p.box == kMaxBox);
    recordAnswer(p, false, true);
    CHECK(p.box == 1 && p.wrong == 1);
    recordAnswer(p, true, false);  // known after "again": stays in box 1
    CHECK(p.box == 1 && p.right == 4);
    CHECK(isDue(0, 3) && isDue(1, 3) && isDue(2, 4) && !isDue(2, 3) && isDue(5, 32) && !isDue(5, 8));

    std::vector<CardProgress> deck(30);
    for (size_t i = 0; i < deck.size(); ++i) deck[i].box = static_cast<uint8_t>(i < 3 ? 1 : i < 10 ? 0 : 4);
    uint32_t rng = 42;
    auto round = pickSmartRound(deck, 3, rng);  // box 4 not due in round 3
    CHECK(round.size() == 10);
    CHECK(deck[round[0]].box == 1 && deck[round[2]].box == 1 && deck[round[3]].box == 0);
    for (auto& c : deck) c.box = 5;
    round = pickSmartRound(deck, 3, rng);  // nothing due: top up to 5
    CHECK(round.size() == 5);
    deck.resize(60, CardProgress{});
    round = pickSmartRound(deck, 3, rng);
    CHECK(round.size() == kSmartRoundSize);

    std::vector<Card> cards = {{"a", "1"}, {"b", "2"}, {"c", "3"}};
    DeckProgress dp = parseProgress(nullptr, 0, cards);
    dp.rounds = 7;
    dp.cards[1].box = 3;
    dp.cards[1].wrong = 2;
    dp.cards[2].right = 1;
    const std::string text = serializeProgress(dp);
    std::vector<Card> reordered = {{"c", "3"}, {"x", "9"}, {"b", "2"}};
    DeckProgress back = parseProgress(text.c_str(), text.size(), reordered);
    CHECK(back.rounds == 7 && back.cards[2].box == 3 && back.cards[2].wrong == 2 && back.cards[0].right == 1);
    CHECK(back.cards[1].box == 0 && back.cards[1].wrong == 0);
    // Mastery: box 3 = 50% of one card out of three
    CHECK(masteryPercent(dp.cards) == 16);
    CHECK(masteryPercentFromText(text.c_str(), text.size()) == 16);
    CHECK(masteryPercentFromText("rounds=2\n00000001 5 0 1\n", 23) == -1);  // no deck size
    std::vector<CardProgress> all(4);
    for (auto& c : all) c.box = 5;
    CHECK(masteryPercent(all) == 100);

    SavedRound sr;
    sr.options.mode = StudyMode::MultipleChoice;
    sr.total = 5;
    sr.known = 2;
    sr.firstTry = 1;
    sr.pending = {{0xabcdef01u, 0}, {0x12u, 2}, {0x3u, 1}};
    const std::string rt = serializeRound(sr);
    SavedRound rb;
    CHECK(parseRound(rt.c_str(), rt.size(), rb));
    CHECK(rb.options.mode == StudyMode::MultipleChoice && rb.known == 2 && rb.pending.size() == 3 &&
          rb.pending[1].first == 0x12u && rb.pending[1].second == 2);
    CHECK(!parseRound("", 0, rb));

    Session s;
    s.restore({{4, 0}, {1, 2}}, 6, 5, 3, 2);
    CHECK(s.currentCard() == 4 && s.knownCount() == 3 && s.remainingCount() == 2 && s.isFirstAttempt());
    s.markKnown();
    CHECK(!s.isFirstAttempt());
    s.markAgain();
    CHECK(s.pendingCards().size() == 1 && s.pendingCards()[0].second == 3);
    s.startWith({2, 0}, 6);
    CHECK(s.cardCount() == 2 && s.currentCard() == 2);
  }
  printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
  return fails != 0;
}
