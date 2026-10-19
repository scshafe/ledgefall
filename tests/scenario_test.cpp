// Scripted scenarios: what the rules do with real inputs, and that replaying
// from a snapshot reproduces a run.
#include <vector>

#include "check.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;

namespace {

// Seat 0 paces the bottom tier, running to one end and back; seat 1 stands still.
// The pacer is the one who is still in the match longer.
void test_keeping_moving_outlasts_standing_still() {
  const auto rules = make_rules({2, 1});
  State s = make_state(*rules);
  for (int t = 0; t < 6000 && !s.over; ++t) {
    const int phase = (t / 40) % 2;
    const std::vector<Input> in{{static_cast<std::int8_t>(phase == 0 ? 100 : -100), 0}, {0, 0}};
    step(*rules, s, in);
  }
  CHECK(s.over && s.winner == 0);
}

// A replay from a snapshot taken mid-match reproduces the original run tick for tick.
void test_replay_from_snapshot() {
  const auto rules = make_rules({3, 3});
  State s = make_state(*rules);
  auto inputs_at = [](int t) {
    return std::vector<Input>{{static_cast<std::int8_t>((t / 17 % 3 - 1) * 100), static_cast<std::uint8_t>(t % 29 == 0 ? kButtonJump : 0)},
                              {static_cast<std::int8_t>((t / 23 % 3 - 1) * 100), static_cast<std::uint8_t>(t % 31 == 0 ? kButtonJump : 0)},
                              {0, static_cast<std::uint8_t>(t % 41 == 0 ? kButtonDown : 0)}};
  };
  std::vector<std::uint8_t> bytes;
  std::vector<std::uint64_t> hashes;
  const int from = 300, to = 900;
  for (int t = 0; t < to; ++t) {
    if (t == from) snapshot(s, bytes);
    step(*rules, s, inputs_at(t));
    if (t >= from) hashes.push_back(hash(s));
  }
  State r = make_state(*rules);
  CHECK(restore(*rules, r, bytes));
  for (int t = from; t < to; ++t) {
    step(*rules, r, inputs_at(t));
    CHECK(hash(r) == hashes[static_cast<std::size_t>(t - from)]);
  }
}

// Rolling back repeatedly (restore the snapshot of 8 ticks ago, re-simulate) never changes the outcome.
void test_repeated_rollback() {
  const auto rules = make_rules({2, 3});
  State straight = make_state(*rules), rolled = make_state(*rules);
  auto inputs_at = [](int t) {
    return std::vector<Input>{{static_cast<std::int8_t>((t / 19 % 3 - 1) * 100), static_cast<std::uint8_t>(t % 23 == 0 ? kButtonJump : 0)},
                              {static_cast<std::int8_t>(-(t / 13 % 3 - 1) * 100), 0}};
  };
  std::vector<std::uint8_t> bytes;
  for (int t = 0; t < 600; ++t) {
    step(*rules, straight, inputs_at(t));
    if (t % 8 == 0) snapshot(rolled, bytes);
    step(*rules, rolled, inputs_at(t));
    if (t % 8 == 7) {
      CHECK(restore(*rules, rolled, bytes));
      for (int u = t - 7; u <= t; ++u) step(*rules, rolled, inputs_at(u));
    }
    CHECK(hash(rolled) == hash(straight));
  }
}

}  // namespace

int main() {
  test_keeping_moving_outlasts_standing_still();
  test_replay_from_snapshot();
  test_repeated_rollback();
  return test::finish();
}
