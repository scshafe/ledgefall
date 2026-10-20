// Conveyor Sky: the belts carry whoever stands still on them, and stop, then run the other way.
#include <vector>

#include "check.hpp"
#include "halos.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

Rules belts_rules() {
  Setup s;
  s.players = 2;
  s.stage = StageId::Belts;
  s.characters = {Character::Mason, Character::Glim, Character::Pip, Character::Pip};
  return *make_rules(s);
}

void idle(const Rules& r, State& s, int n = 1) {
  for (int t = 0; t < n; ++t) {
    // Keep the tiles from crumbling under a test that stands still.
    for (std::size_t i = 0; i < s.world.platforms.size(); ++i) {
      fp::PlatformState& p = s.world.platforms[i];
      if (r.slab_hp[i] == 0 && (p.phase == fp::PlatformPhase::Solid || p.phase == fp::PlatformPhase::Shaking)) {
        p.phase = fp::PlatformPhase::Solid;
        p.stress = Fixed::zero();
      }
    }
    const std::vector<Input> none(r.setup.players);
    step(r, s, none);
  }
}

void test_the_stage_has_belts_and_a_rhythm() {
  const Rules r = belts_rules();
  int belted = 0;
  for (const Fixed& b : r.belts) belted += b != Fixed::zero() ? 1 : 0;
  CHECK(belted == 7 + 7 + 5 + 5 + 10 + 4 + 4);  // every tile of the seven rows; the slabs have none
  CHECK(r.belts[0] > Fixed::zero() && r.belts[7] < Fixed::zero());  // the first two rows run opposite ways
  bool has_belts = false;
  for (const HazardSpec& h : r.hazards) has_belts = has_belts || h.type == HazardType::Belts;
  CHECK(has_belts);
}

void test_standing_still_on_a_belt_carries_you() {
  const Rules r = belts_rules();
  State s = make_state(r);
  clear_halos(r, s);
  s.world.entities[1].pos.x = Fixed::from_int(300);
  s.world.entities[0] = fp::spawn_on(r.engine, 0, r.engine.stage.platforms[0].rect.left + Fixed::from_int(4));
  for (PlayerState& p : s.players) p.invuln = 0;
  const Fixed x0 = s.world.entities[0].pos.x;
  idle(r, s, 30);
  CHECK(s.world.entities[0].pos.x > x0 + Fixed::from_int(8));  // 1.2 px/tick x 0.32 = 0.38 a tick, to the right
  // The second row runs the other way.
  State t = make_state(r);
  clear_halos(r, t);
  t.world.entities[1].pos.x = Fixed::from_int(-300);
  const fp::Rect& second = r.engine.stage.platforms[7].rect;
  t.world.entities[0] = fp::spawn_on(r.engine, 7, second.right - Fixed::from_int(4));
  for (PlayerState& p : t.players) p.invuln = 0;
  const Fixed y0 = t.world.entities[0].pos.x;
  idle(r, t, 30);
  CHECK(t.world.entities[0].pos.x < y0 - Fixed::from_int(8));
}

void test_the_belts_stop_then_run_the_other_way() {
  const Rules r = belts_rules();
  State s = make_state(r);
  CHECK(s.hazards.belts == BeltPhase::Run && s.hazards.belts_sign == 1);
  bool stopped = false, flipped = false;
  int stopped_at = -1, ran_again = -1;
  for (int t = 0; t < 1500 && !flipped; ++t) {
    idle(r, s);
    if (!stopped && s.hazards.belts == BeltPhase::Stop) {
      stopped = true;
      stopped_at = t;
      idle(r, s);
      CHECK(s.world.platforms[0].belt == Fixed::zero());  // a stopped belt carries nobody
      ++t;
    }
    if (stopped && s.hazards.belts == BeltPhase::Run) {
      flipped = true;
      ran_again = t;
    }
  }
  CHECK(stopped && flipped);
  CHECK(ran_again - stopped_at >= 70 && ran_again - stopped_at <= 80);  // the warning lasts 75 ticks
  CHECK(stopped_at >= 420 - 5 && stopped_at <= 600 + 5);
  CHECK(s.hazards.belts_sign == -1);
  idle(r, s);
  CHECK(s.world.platforms[0].belt < Fixed::zero());  // the first row now runs left
}

void test_belt_state_round_trips() {
  const Rules r = belts_rules();
  State s = make_state(r);
  idle(r, s, 700);  // past the first stop
  std::vector<std::uint8_t> bytes;
  snapshot(s, bytes);
  State copy = make_state(r);
  CHECK(restore(r, copy, bytes) && hash(copy) == hash(s));
  idle(r, s, 200);
  idle(r, copy, 200);
  CHECK(hash(copy) == hash(s));
}

}  // namespace

int main() {
  test_the_stage_has_belts_and_a_rhythm();
  test_standing_still_on_a_belt_carries_you();
  test_the_belts_stop_then_run_the_other_way();
  test_belt_state_round_trips();
  return test::finish();
}
