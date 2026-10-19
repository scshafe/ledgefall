// The core's rules: setup limits, input bytes, stocks, respawn, the winner,
// and snapshot/restore round trips.
#include <vector>

#include "check.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;

namespace {

Rules rules_for(int players, int stocks) {
  auto r = make_rules({players, stocks});
  CHECK(r.has_value());
  return *r;
}

void kill(State& s, int seat) { s.world.entities[static_cast<std::size_t>(seat)].pos.y = fp::Fixed::from_int(-1000); }

void test_setup() {
  CHECK(Setup{}.players == 2 && Setup{}.stocks == 3);
  for (int p = 0; p <= 6; ++p) {
    for (int st = 0; st <= 7; ++st) {
      const bool ok = p >= 2 && p <= 4 && st >= 1 && st <= 5;
      CHECK(valid({p, st}) == ok);
      CHECK(make_rules({p, st}).has_value() == ok);
    }
  }
  const State s = make_state(rules_for(4, 5));
  CHECK(s.players.size() == 4 && s.world.entities.size() == 4);
  for (const PlayerState& p : s.players) CHECK(p.stocks == 5);
}

void test_input_bytes() {
  std::uint8_t b[kInputBytes];
  const Input in{-100, kButtonJump | kButtonDown};
  encode(in, b);
  Input out;
  CHECK(decode(b, out) && out == in);
  const std::uint8_t bad_axis[] = {101, 0}, bad_button[] = {0, 4}, short_bytes[] = {0};
  CHECK(!decode(bad_axis, out) && !decode(bad_button, out) && !decode(short_bytes, out));
  CHECK(out == in);
}

void test_stock_lost_and_respawn() {
  const Rules r = rules_for(3, 3);
  State s = make_state(r);
  const std::vector<Input> idle(3);
  kill(s, 0);
  step(r, s, idle);
  CHECK(s.players[0].stocks == 2 && s.players[0].respawn_timer == kRespawnDelayTicks);
  CHECK(s.players[1].stocks == 3 && !s.over);
  for (int i = 0; i < kRespawnDelayTicks - 1; ++i) step(r, s, idle);
  CHECK(s.players[0].respawn_timer == 1 && s.world.entities[0].eliminated != 0);
  step(r, s, idle);
  CHECK(s.players[0].respawn_timer == 0 && s.world.entities[0].eliminated == 0);
  CHECK(s.players[0].stocks == 2);
}

void test_last_with_stocks_wins() {
  const Rules r = rules_for(3, 1);
  State s = make_state(r);
  const std::vector<Input> idle(3);
  kill(s, 2);
  step(r, s, idle);
  CHECK(s.players[2].stocks == 0 && !s.over);
  kill(s, 0);
  step(r, s, idle);
  CHECK(s.over && s.winner == 1);
  const auto h = hash(s);
  step(r, s, idle);  // a finished match no longer changes
  CHECK(hash(s) == h);
}

void test_same_tick_tie_goes_to_lowest_seat() {
  const Rules r = rules_for(2, 1);
  State s = make_state(r);
  kill(s, 0);
  kill(s, 1);
  step(r, s, std::vector<Input>(2));
  CHECK(s.over && s.winner == 0);
}

void test_standing_still_loses_every_stock() {
  const Rules r = rules_for(2, 2);
  State s = make_state(r);
  const std::vector<Input> idle(2);
  for (int t = 0; t < 6000 && !s.over; ++t) step(r, s, idle);
  CHECK(s.over);
}

// A tile one step from breaking, nobody near it.
void doom_first_tile(State& s) {
  fp::PlatformState& p = s.world.platforms[0];
  p.phase = fp::PlatformPhase::Doomed;
  p.stress = fp::Fixed::from_double(0.999);
}

void test_overtime_slows_respawn() {
  const Rules r = rules_for(2, 3);
  const std::vector<Input> idle(2);
  State normal = make_state(r);
  doom_first_tile(normal);
  step(r, normal, idle);
  CHECK(normal.world.platforms[0].phase == fp::PlatformPhase::Gone);
  const int base = normal.world.platforms[0].timer;
  CHECK(base > 0);

  State late = make_state(r);
  late.world.frame = kOvertimeStartTick;
  doom_first_tile(late);
  step(r, late, idle);
  CHECK(late.world.platforms[0].phase == fp::PlatformPhase::Gone);
  CHECK(late.world.platforms[0].timer == base * 3 / 2);  // 50% longer in the first overtime step

  State later = make_state(r);
  later.world.frame = kOvertimeStartTick + 2 * kOvertimeStepTicks;
  doom_first_tile(later);
  step(r, later, idle);
  CHECK(later.world.platforms[0].timer == base * 5 / 2);  // 3 steps in: 50% more each
}

void test_collapse_destroys_and_dooms() {
  const Rules r = rules_for(2, 3);
  const std::vector<Input> idle(2);
  State s = make_state(r);
  s.world.frame = kCollapseStartTick;
  doom_first_tile(s);
  step(r, s, idle);
  CHECK(s.world.platforms[0].phase == fp::PlatformPhase::Destroyed);  // nothing broken comes back

  // On a tick that is a multiple of half a second a random tile is doomed, the same one for the same seed.
  auto doomed_after = [&](std::uint32_t seed) {
    auto rules = make_rules({2, 3, seed});
    State t = make_state(*rules);
    t.world.frame = kCollapseStartTick - 1;
    step(*rules, t, idle);
    int n = 0, first = -1;
    for (std::size_t i = 0; i < t.world.platforms.size(); ++i) {
      if (t.world.platforms[i].phase == fp::PlatformPhase::Doomed) {
        ++n;
        if (first < 0) first = static_cast<int>(i);
      }
    }
    CHECK(n == 1);
    return first;
  };
  CHECK(doomed_after(7) == doomed_after(7));
  bool differs = false;
  for (std::uint32_t seed = 1; seed < 20; ++seed) differs = differs || doomed_after(seed) != doomed_after(7);
  CHECK(differs);
}

void test_snapshot_round_trip() {
  const Rules r = rules_for(2, 3);
  State s = make_state(r);
  const std::vector<Input> idle(2);
  for (int t = 0; t < 200; ++t) step(r, s, idle);
  std::vector<std::uint8_t> bytes;
  snapshot(s, bytes);
  State copy = make_state(r);
  CHECK(restore(r, copy, bytes) && hash(copy) == hash(s));
  for (int t = 0; t < 50; ++t) {
    step(r, s, idle);
    step(r, copy, idle);
  }
  CHECK(hash(copy) == hash(s));

  const auto before = hash(copy);
  std::vector<std::uint8_t> bad = bytes;
  bad[0] ^= 1;
  CHECK(!restore(r, copy, bad));
  bad = bytes;
  bad.pop_back();
  CHECK(!restore(r, copy, bad));
  bad = bytes;
  bad[3] = 99;  // stocks out of range
  CHECK(!restore(r, copy, bad));
  CHECK(!restore(rules_for(3, 3), copy, bytes));  // another player count
  CHECK(hash(copy) == before);
}

}  // namespace

int main() {
  test_setup();
  test_input_bytes();
  test_stock_lost_and_respawn();
  test_last_with_stocks_wins();
  test_same_tick_tie_goes_to_lowest_seat();
  test_standing_still_loses_every_stock();
  test_overtime_slows_respawn();
  test_collapse_destroys_and_dooms();
  test_snapshot_round_trip();
  return test::finish();
}
