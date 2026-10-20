// Team mode and stock handicaps.
#include <vector>

#include "check.hpp"
#include "ledgefall/bot.hpp"
#include "ledgefall/capi.h"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

Setup three(std::array<std::int8_t, 4> teams, std::array<std::int8_t, 4> seat_stocks = {0, 0, 0, 0}) {
  Setup s;
  s.players = 3;
  s.stocks = 2;
  s.teams = teams;
  s.seat_stocks = seat_stocks;
  return s;
}

void kill(State& s, int seat) { s.world.entities[static_cast<std::size_t>(seat)].pos.y = Fixed::from_int(-1000); }

void idle(const Rules& r, State& s, int n = 1) {
  for (int t = 0; t < n; ++t) {
    const std::vector<Input> in(r.setup.players);
    step(r, s, in);
  }
}

void test_one_side_means_everyone_plays_alone() {
  const Rules all_a = *make_rules(three({0, 0, 0, -1}));
  CHECK(all_a.setup.teams[0] == -1 && all_a.setup.teams[1] == -1);
  const Rules two = *make_rules(three({0, 0, 1, -1}));
  CHECK(two.setup.teams[0] == 0 && two.setup.teams[2] == 1);
  const Rules team_vs_solo = *make_rules(three({0, 0, -1, -1}));
  CHECK(team_vs_solo.setup.teams[0] == 0);  // a team and a solo player are two sides
  CHECK(!valid(three({2, 0, 0, 0})));
}

void test_a_side_wins_together() {
  const Rules r = *make_rules(three({0, 0, 1, -1}));
  State s = make_state(r);
  CHECK(same_side(r, 0, 1) && !same_side(r, 0, 2));
  // Seats 0 and 1 on team A, seat 2 on B. Lose all of B's stocks.
  for (PlayerState& p : s.players) p.invuln = 0;
  s.players[1].stats.kos = 3;  // the teammates are ranked by credited eliminations
  kill(s, 2);
  idle(r, s);
  CHECK(!s.over);
  s.players[2].respawn_timer = 0;
  s.world.entities[2].eliminated = 0;
  s.world.entities[2].pos = {Fixed::zero(), Fixed::from_int(-1000)};
  idle(r, s);
  CHECK(s.over);
  CHECK(s.winner == 1 && winning_team(r, s) == 0);
  CHECK(s.players[1].place == 1 && s.players[0].place == 2 && s.players[2].place == 3);  // the whole side places first
}

void test_the_match_goes_on_while_a_side_has_anyone() {
  const Rules r = *make_rules(three({0, 0, 1, -1}));
  State s = make_state(r);
  kill(s, 0);
  kill(s, 1);
  idle(r, s);
  idle(r, s, 200);
  CHECK(!s.over);  // seat 2's side is still in, and so is A (they have a stock left each)
}

void test_friendly_fire_earns_no_credit() {
  const Rules r = *make_rules(three({0, 0, 1, -1}));
  State s = make_state(r);
  s.players[1].blame_by = 0;
  s.players[1].blame_tick = static_cast<std::int32_t>(s.world.frame);
  kill(s, 1);
  idle(r, s);
  CHECK(s.players[0].stats.kos == 0 && s.players[1].stats.falls == 1);
  // An opponent's credit stands.
  State t = make_state(r);
  t.players[1].blame_by = 2;
  t.players[1].blame_tick = static_cast<std::int32_t>(t.world.frame);
  kill(t, 1);
  idle(r, t);
  CHECK(t.players[2].stats.kos == 1);
}

void test_handicaps() {
  const Rules r = *make_rules(three({-1, -1, -1, -1}, {1, 0, 9, 0}));
  const State s = make_state(r);
  CHECK(s.players[0].stocks == 1 && s.players[1].stocks == 2 && s.players[2].stocks == 9);
  Setup bad = three({-1, -1, -1, -1}, {10, 0, 0, 0});
  CHECK(!valid(bad));
}

void test_the_c_face_takes_teams_and_handicaps() {
  lf_setup su{};
  su.players = 3;
  su.stocks = 3;
  su.stage = LF_STAGE_PRACTICE;
  for (int i = 0; i < 4; ++i) su.teams[i] = -1;
  su.teams[0] = 0;
  su.teams[1] = 0;
  su.teams[2] = 1;
  su.seat_stocks[2] = 5;
  lf_match* m = lf_match_create_setup(&su);
  CHECK(m != nullptr);
  lf_player p;
  CHECK(lf_match_player(m, 2, &p) && p.team == 1 && p.stocks == 5);
  CHECK(lf_match_player(m, 0, &p) && p.team == 0 && p.stocks == 3);
  CHECK(lf_match_winning_team(m) == -1);
  lf_match_destroy(m);
  CHECK(lf_match_create_setup(nullptr) == nullptr);
}

void test_bots_do_not_target_teammates() {
  // Glim's bot has only a teammate on the stage to throw at: it never throws.
  Setup su = three({0, 0, 1, -1});
  su.characters = {Character::Glim, Character::Mason, Character::Mason, Character::Mason};
  const Rules r = *make_rules(su);
  State s = make_state(r);
  s.world.entities[2].eliminated = 1;  // the only opponent is out of play
  s.players[2].respawn_timer = kRespawnDelayTicks * 100;
  s.world.entities[0] = fp::spawn_on(r.engine, 3, Fixed::from_int(-40));
  s.world.entities[1] = fp::spawn_on(r.engine, 5, Fixed::from_int(-20));
  Bot bot;
  bot.level = BotLevel::Hard;
  std::vector<Input> in(3);
  int placed_before = s.players[0].stats.placed;
  for (int t = 0; t < 300; ++t) {
    for (fp::PlatformState& p : s.world.platforms) {
      p.phase = fp::PlatformPhase::Solid;
      p.stress = Fixed::zero();
    }
    in[0] = bot_input(r, s, 0, bot);
    step(r, s, in);
  }
  CHECK(s.players[0].stats.placed == placed_before);
}

}  // namespace

int main() {
  test_one_side_means_everyone_plays_alone();
  test_a_side_wins_together();
  test_the_match_goes_on_while_a_side_has_anyone();
  test_friendly_fire_earns_no_credit();
  test_handicaps();
  test_the_c_face_takes_teams_and_handicaps();
  test_bots_do_not_target_teammates();
  return test::finish();
}
