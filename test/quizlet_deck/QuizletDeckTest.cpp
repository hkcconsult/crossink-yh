// Host tests for the flashcard deck parser and study session (src/quizlet).
#include <cassert>
#include <cstdio>
#include <cstring>
#include <set>

#include "quizlet/QuizletDeckParser.h"
#include "quizlet/QuizletSession.h"
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
  printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
  return fails != 0;
}
