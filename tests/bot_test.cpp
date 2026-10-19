// Bots: a match of bots ends with a winner, is reproducible, and bots outlast
// someone standing still.
#include <vector>

#include "check.hpp"
#include "ledgefall/bot.hpp"

using namespace ledgefall;

namespace {

struct Run {
  bool over = false;
  int winner = -1;
  int ticks = 0;
  std::uint64_t hash = 0;
};

// Seats 0.. are bots of the given level; `idle` seats (the last ones) send nothing.
Run play(int players, int idle, BotLevel level, int stocks, int max_ticks) {
  const auto rules = make_rules({players, stocks});
  State s = make_state(*rules);
  std::vector<Bot> bots(static_cast<std::size_t>(players));
  for (Bot& b : bots) b.level = level;
  std::vector<Input> in(static_cast<std::size_t>(players));
  Run r;
  while (!s.over && r.ticks < max_ticks) {
    for (int i = 0; i < players - idle; ++i) in[static_cast<std::size_t>(i)] = bot_input(*rules, s, i, bots[static_cast<std::size_t>(i)]);
    step(*rules, s, in);
    ++r.ticks;
  }
  r.over = s.over;
  r.winner = s.winner;
  r.hash = hash(s);
  return r;
}

void test_bots_beat_standing_still() {
  for (BotLevel level : {BotLevel::Easy, BotLevel::Normal, BotLevel::Hard}) {
    const Run r = play(2, 1, level, 3, 3 * 60 * 60);
    CHECK(r.over && r.winner == 0);
  }
}

void test_bot_matches_end_with_a_winner() {
  for (BotLevel level : {BotLevel::Easy, BotLevel::Normal, BotLevel::Hard}) {
    const Run r = play(4, 0, level, 3, 6 * 60 * 60);  // lost-footing: within six minutes
    CHECK(r.over && r.winner >= 0);
  }
}

void test_bot_matches_are_reproducible() {
  const Run a = play(4, 0, BotLevel::Normal, 3, 6 * 60 * 60);
  const Run b = play(4, 0, BotLevel::Normal, 3, 6 * 60 * 60);
  CHECK(a.hash == b.hash && a.ticks == b.ticks && a.winner == b.winner);
}

}  // namespace

int main() {
  test_bots_beat_standing_still();
  test_bot_matches_end_with_a_winner();
  test_bot_matches_are_reproducible();
  return test::finish();
}
