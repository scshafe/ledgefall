// The stages, sturdy slabs, and Mason's anvil.
#include <vector>

#include "check.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

Rules yard() {
  Setup s;
  s.players = 2;
  s.stage = StageId::Yard;
  s.characters = {Character::Mason, Character::Glim, Character::Pip, Character::Pip};
  return *make_rules(s);
}

constexpr int kSlab = 42;  // the first slab of the Yard comes after its 42 tiles

void firm_tiles(const Rules& r, State& s) {
  for (std::size_t i = 0; i < s.world.platforms.size(); ++i) {
    fp::PlatformState& p = s.world.platforms[i];
    if (r.slab_hp[i] == 0 && (p.phase == fp::PlatformPhase::Solid || p.phase == fp::PlatformPhase::Shaking)) {
      p.phase = fp::PlatformPhase::Solid;
      p.stress = Fixed::zero();
    }
  }
}

void tick(const Rules& r, State& s, Input a = {}, bool firm = true) {
  if (firm) firm_tiles(r, s);
  const Input in[2] = {a, {}};
  step(r, s, in);
}

void test_every_stage_is_sound() {
  for (int id = 0; id < kStageCount; ++id) {
    Setup su;
    su.stage = static_cast<StageId>(id);
    su.players = 4;
    const auto r = make_rules(su);
    CHECK(r.has_value());
    CHECK(r->spawns.size() >= 4);
    CHECK(r->slab_hp.size() == r->engine.stage.platforms.size());
    const fp::Rect& b = r->engine.stage.blast;
    CHECK(b.left < b.right && b.bottom < b.top);
    for (const fp::Vec2& sp : r->spawns) CHECK(sp.x > b.left && sp.x < b.right && sp.y > b.bottom && sp.y < b.top);
    State s = make_state(*r);
    for (int t = 0; t < 600; ++t) {
      const std::vector<Input> idle(4);
      step(*r, s, idle);
    }
    CHECK(s.players.size() == 4);
  }
  Setup bad;
  bad.stage = static_cast<StageId>(kStageCount);
  CHECK(!valid(bad));
}

void test_slabs_exist_and_never_crumble_from_standing() {
  const Rules r = yard();
  CHECK(r.slab_hp[kSlab] == 4 && r.slab_hp[0] == 0);
  State s = make_state(r);
  s.world.entities[0] = fp::spawn_on(r.engine, kSlab, Fixed::zero());
  for (int t = 0; t < 600; ++t) tick(r, s);
  CHECK(s.world.platforms[kSlab].phase == fp::PlatformPhase::Solid);
  CHECK(s.world.entities[0].support == kSlab);
  Setup q;
  q.stage = StageId::Quarry;
  CHECK(make_rules(q)->slab_hp[28] == 8);  // the great ledge
}

Object anvil_at(Fixed x, Fixed y) {
  Object o;
  o.kind = ObjectKind::Anvil;
  o.owner = 0;
  o.pos = {x, y};
  return o;
}

void test_anvil_punches_tiles_and_credits() {
  const Rules r = yard();
  State s = make_state(r);
  // Over the second tier-1 tile of the lower-left row of three (x 392..536 px).
  const Fixed x = Fixed::from_int(-110);
  s.objects[0] = anvil_at(x, Fixed::from_int(40));
  s.world.entities[0].pos.x = Fixed::from_int(200);
  s.world.entities[1].pos.x = Fixed::from_int(-200);
  for (int t = 0; t < 120; ++t) tick(r, s, {}, false);
  CHECK(s.players[0].stats.tiles_broken >= 1);
  int gone = 0;
  for (std::size_t i = 0; i < s.world.platforms.size(); ++i) gone += s.world.platforms[i].phase == fp::PlatformPhase::Gone ? 1 : 0;
  CHECK(gone >= 1 && gone <= 6);  // up to three tiers' worth
}

void test_anvil_damages_a_slab_and_it_returns() {
  const Rules r = yard();
  State s = make_state(r);
  s.world.entities[0].pos.x = Fixed::from_int(200);
  s.world.entities[1].pos.x = Fixed::from_int(-200);
  s.objects[0] = anvil_at(Fixed::zero(), Fixed::from_int(60));
  for (int t = 0; t < 40; ++t) tick(r, s, {}, false);
  CHECK(s.slab_hp[kSlab] == 2);
  CHECK(s.world.platforms[kSlab].phase == fp::PlatformPhase::Solid);
  CHECK(s.objects[0].kind == ObjectKind::Anvil && s.objects[0].rest > 0);  // lying on it
  for (int t = 0; t < 60; ++t) tick(r, s, {}, false);
  CHECK(s.objects[0].kind == ObjectKind::None);

  s.objects[0] = anvil_at(Fixed::zero(), Fixed::from_int(60));
  for (int t = 0; t < 40; ++t) tick(r, s, {}, false);
  CHECK(s.world.platforms[kSlab].phase == fp::PlatformPhase::Gone);
  CHECK(s.players[0].stats.slabs_broken == 1);
  for (int t = 0; t < 500; ++t) tick(r, s, {}, false);  // back after 8 s, whole
  CHECK(s.world.platforms[kSlab].phase == fp::PlatformPhase::Solid);
  CHECK(s.slab_hp[kSlab] == 4);
}

void test_masons_throw_lands_an_anvil_ahead() {
  const Rules r = yard();
  State s = make_state(r);
  s.world.entities[0] = fp::spawn_on(r.engine, 0, Fixed::from_int(-150));
  tick(r, s, {0, kButtonBreak});
  bool seen = false;
  for (const Object& o : s.objects) seen = seen || (o.kind == ObjectKind::Anvil && o.owner == 0 && o.vel.x > Fixed::zero());
  CHECK(seen);
  CHECK(s.players[0].cooldown[2] > 0);
}

void test_collapse_wears_slabs_down() {
  const Rules r = yard();
  State s = make_state(r);
  s.world.frame = kCollapseStartTick - 1;
  s.world.entities[0].pos.x = Fixed::from_int(200);
  const int before = s.slab_hp[kSlab];
  tick(r, s, {}, false);
  CHECK(s.slab_hp[kSlab] == before - 1);
}

void test_sinking_ship_decks_heave_out_of_step() {
  Setup su;
  su.stage = StageId::Swell;
  su.players = 2;
  const Rules r = *make_rules(su);
  State s = make_state(r);
  const std::size_t left = 0, right = 12;  // the two halves of the long deck
  Fixed lo = Fixed::from_int(1000), hi = Fixed::from_int(-1000);
  bool apart = false;
  for (int t = 0; t < 600; ++t) {
    std::vector<Input> none(2);
    step(r, s, none);
    if (t < 3) continue;
    const Fixed y = s.world.platforms[left].offset.y;
    if (y < lo) lo = y;
    if (y > hi) hi = y;
    apart = apart || fp::abs(y - s.world.platforms[right].offset.y) > Fixed::from_int(8);
  }
  CHECK(hi - lo > Fixed::from_int(14) && hi - lo < Fixed::from_int(20));  // 2 x 34 px x 0.26
  CHECK(apart);

  // Whoever stands on a deck rides it.
  State on = make_state(r);
  on.world.entities[0] = fp::spawn_on(r.engine, 5, r.engine.stage.platforms[5].rect.left + Fixed::from_int(4));
  const Fixed y0 = on.world.entities[0].pos.y;
  Fixed top = y0;
  for (int t = 0; t < 120; ++t) {
    for (fp::PlatformState& p : on.world.platforms) {
      p.phase = p.phase == fp::PlatformPhase::Gone ? fp::PlatformPhase::Solid : p.phase;
      p.stress = Fixed::zero();
    }
    std::vector<Input> none(2);
    step(r, on, none);
    if (on.world.entities[0].pos.y > top) top = on.world.entities[0].pos.y;
  }
  CHECK(fp::abs(top - y0) > Fixed::from_int(3));
}

void test_slab_state_round_trips() {
  const Rules r = yard();
  State s = make_state(r);
  s.slab_hp[kSlab] = 2;
  s.objects[0] = anvil_at(Fixed::zero(), Fixed::from_int(60));
  std::vector<std::uint8_t> bytes;
  snapshot(s, bytes);
  State copy = make_state(r);
  CHECK(restore(r, copy, bytes) && hash(copy) == hash(s));
  s.slab_hp[kSlab] = 5;  // more than the slab has
  snapshot(s, bytes);
  CHECK(!restore(r, copy, bytes));
}

}  // namespace

int main() {
  test_every_stage_is_sound();
  test_slabs_exist_and_never_crumble_from_standing();
  test_anvil_punches_tiles_and_credits();
  test_anvil_damages_a_slab_and_it_returns();
  test_masons_throw_lands_an_anvil_ahead();
  test_collapse_wears_slabs_down();
  test_sinking_ship_decks_heave_out_of_step();
  test_slab_state_round_trips();
  return test::finish();
}
