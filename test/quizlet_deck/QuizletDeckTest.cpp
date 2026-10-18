// Host tests for the flashcard deck parser and study session (src/quizlet).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>

#include "quizlet/QuizletDeckParser.h"
#include "quizlet/QuizletSession.h"
#include "quizlet/QuizletStudy.h"
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
  o.shuffle = false;
  const std::string ser = serializeOptions(o);
  const StudyOptions back = parseOptions(ser.c_str(), ser.size());
  CHECK(back.mode == StudyMode::Speed && back.answerWith == AnswerWith::Both && !back.shuffle);
  const char* junk = "mode=9\r\nanswer=x\nfoo\nshuffle=0";
  const StudyOptions j = parseOptions(junk, strlen(junk));
  CHECK(j.mode == StudyMode::Flip && j.answerWith == AnswerWith::Definition && !j.shuffle);
  CHECK(parseOptions(nullptr, 0).shuffle);
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
  printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
  return fails != 0;
}
