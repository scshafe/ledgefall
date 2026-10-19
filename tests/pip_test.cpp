// Pip's gust and drill.
#include <vector>

#include "check.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

Rules rules() {
  Setup s;
  s.players = 2;
  s.characters = {Character::Pip, Character::Mason, Character::Pip, Character::Pip};
  return *make_rules(s);
}

int tile_at(Fixed x) { return ((x + Fixed::from_int(96)) / Fixed::from_int(16)).raw() / Fixed::kOneRaw; }

State on_ground(const Rules& r) {
  State s = make_state(r);
  s.world.entities[0] = fp::spawn_on(r.engine, 3, Fixed::from_int(-40));
  s.world.entities[1] = fp::spawn_on(r.engine, 9, Fixed::from_int(60));
  return s;
}

void firm(State& s) {
  for (fp::PlatformState& p : s.world.platforms) {
    if (p.phase == fp::PlatformPhase::Solid || p.phase == fp::PlatformPhase::Shaking) {
      p.phase = fp::PlatformPhase::Solid;
      p.stress = Fixed::zero();
    }
  }
}

void tick(const Rules& r, State& s, Input a = {}, Input b = {}, bool keep = true) {
  if (keep) firm(s);
  const Input in[2] = {a, b};
  step(r, s, in);
}

int gusts(const State& s) {
  int n = 0;
  for (const Object& o : s.objects) n += o.kind == ObjectKind::Gust ? 1 : 0;
  return n;
}

void test_gust_spins_up_then_pushes_and_credits() {
  const Rules r = rules();
  State s = on_ground(r);
  s.world.entities[1] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(0)), Fixed::from_int(0));  // 40 units ahead of Pip
  tick(r, s, {0, kButtonTrick});
  CHECK(gusts(s) == 1);
  for (int t = 0; t < 20; ++t) tick(r, s);
  CHECK(s.players[1].blame_by == -1);  // still spinning up
  Fixed pushed{};
  for (int t = 0; t < 60; ++t) {
    tick(r, s);
    if (s.world.entities[1].vel.x > pushed) pushed = s.world.entities[1].vel.x;
  }
  CHECK(pushed > Fixed::zero());
  CHECK(s.players[1].blame_by == 0);
  // It lasts 3 s, then stops.
  for (int t = 0; t < 200; ++t) tick(r, s);
  CHECK(gusts(s) == 0);
}

void test_one_gust_at_a_time() {
  const Rules r = rules();
  State s = on_ground(r);
  tick(r, s, {0, kButtonTrick});
  for (int t = 0; t < 500; ++t) tick(r, s);  // the cooldown (8 s) runs out; the first gust is long gone
  tick(r, s, {0, kButtonTrick});
  tick(r, s);
  CHECK(gusts(s) == 1);
}

void test_drill_breaks_the_tile_it_lands_on() {
  const Rules r = rules();
  State s = on_ground(r);
  s.world.entities[1] = fp::spawn_on(r.engine, 11, Fixed::from_int(90));
  tick(r, s, {0, kButtonBreak});
  CHECK(s.players[0].drill > 0);  // the hop
  const int ground = 3;
  bool bounced = false;
  for (int t = 0; t < 40 && !bounced; ++t) {
    tick(r, s);
    bounced = s.players[0].drill == 0 && s.world.entities[0].vel.y > Fixed::from_int(1);
  }
  CHECK(bounced);
  CHECK(s.world.platforms[ground].phase == fp::PlatformPhase::Gone);
  CHECK(s.world.platforms[ground + 1].stress > Fixed::from_double(0.3));  // neighbours take 0.4
  CHECK(s.players[0].stats.tiles_broken == 1);
  CHECK(s.world.entities[0].jumps_used == 0);
}

void test_drill_in_the_air_dives_at_once() {
  const Rules r = rules();
  State s = on_ground(r);
  s.world.entities[0].pos.y = Fixed::from_int(30);
  s.world.entities[0].mode = fp::Mode::Airborne;
  s.world.entities[0].support = -1;
  tick(r, s, {0, kButtonBreak});
  CHECK(s.players[0].drill == -1);
  tick(r, s);
  CHECK(s.world.entities[0].vel.y < Fixed::from_int(-3));
  CHECK(s.world.entities[0].vel.x == Fixed::zero());
}

void test_air_jump_cancels_the_dive() {
  const Rules r = rules();
  State s = on_ground(r);
  s.world.entities[0].pos.y = Fixed::from_int(60);
  s.world.entities[0].mode = fp::Mode::Airborne;
  s.world.entities[0].support = -1;
  tick(r, s, {0, kButtonBreak});
  tick(r, s);
  CHECK(s.players[0].drill == -1);
  tick(r, s, {0, kButtonJump});
  CHECK(s.players[0].drill == 0);
  CHECK(s.world.entities[0].vel.y > Fixed::zero());
}

void test_pip_state_round_trips() {
  const Rules r = rules();
  State s = on_ground(r);
  tick(r, s, {0, kButtonTrick | kButtonBreak});
  for (int t = 0; t < 5; ++t) tick(r, s);
  std::vector<std::uint8_t> bytes;
  snapshot(s, bytes);
  State copy = make_state(r);
  CHECK(restore(r, copy, bytes) && hash(copy) == hash(s));
  for (int t = 0; t < 60; ++t) {
    tick(r, s);
    tick(r, copy);
  }
  CHECK(hash(copy) == hash(s));
}

}  // namespace

int main() {
  test_gust_spins_up_then_pushes_and_credits();
  test_one_gust_at_a_time();
  test_drill_breaks_the_tile_it_lands_on();
  test_drill_in_the_air_dives_at_once();
  test_air_jump_cancels_the_dive();
  test_pip_state_round_trips();
  return test::finish();
}
