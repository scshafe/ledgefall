// Fern's thorns and roots, Dab's rub out.
#include <vector>

#include "check.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

Rules rules(Character a, StageId stage = StageId::Practice) {
  Setup s;
  s.players = 2;
  s.stage = stage;
  s.characters = {a, Character::Mason, Character::Pip, Character::Pip};
  return *make_rules(s);
}

int tile_at(Fixed x) { return ((x + Fixed::from_int(96)) / Fixed::from_int(16)).raw() / Fixed::kOneRaw; }

State on_ground(const Rules& r, int x0 = -60) {
  State s = make_state(r);
  s.world.entities[0] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(x0)), Fixed::from_int(x0));
  s.world.entities[1] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(80)), Fixed::from_int(80));
  return s;
}

void keep(State& s, int from, int to) {  // keep the tiles from..to firm (the players stand still)
  for (int i = from; i <= to; ++i) {
    fp::PlatformState& p = s.world.platforms[static_cast<std::size_t>(i)];
    if (p.phase == fp::PlatformPhase::Solid || p.phase == fp::PlatformPhase::Shaking) {
      p.phase = fp::PlatformPhase::Solid;
      p.stress = Fixed::zero();
    }
  }
}

// Only the tiles the two players stand on are kept firm.
void tick(const Rules& r, State& s, Input a = {}, Input b = {}) {
  keep(s, tile_at(s.world.entities[0].pos.x), tile_at(s.world.entities[0].pos.x));
  keep(s, tile_at(s.world.entities[1].pos.x), tile_at(s.world.entities[1].pos.x));
  const Input in[2] = {a, b};
  step(r, s, in);
}

int sticky(const State& s) {
  int n = 0;
  for (const fp::Placeable& p : s.world.placeables) n += p.active != 0 && p.kind == fp::PlaceableKind::Sticky ? 1 : 0;
  return n;
}

void test_thorns_grow_slow_and_chew() {
  const Rules r = rules(Character::Fern);
  State s = on_ground(r);
  tick(r, s, {0, kButtonTrick});
  CHECK(sticky(s) == 0);  // growing
  for (int t = 0; t < 30; ++t) tick(r, s);
  CHECK(sticky(s) == 1);
  std::size_t slot = 0;
  for (std::size_t i = 0; i < s.world.placeables.size(); ++i) {
    if (s.world.placeables[i].active != 0) slot = i;
  }
  CHECK(s.meta[slot].tag == 1 && s.meta[slot].owner == 0);
  const fp::Rect rect = s.world.placeables[slot].rect;
  CHECK(rect.left > s.world.entities[0].pos.x && rect.right - rect.left <= Fixed::from_int(49));  // 30 px ahead, 150 px long

  // Mason in the brambles is slowed to two fifths and credited to Fern; the tiles under the patch wear away.
  const Fixed mid = (rect.left + rect.right) / Fixed::from_int(2);
  s.world.entities[1] = fp::spawn_on(r.engine, tile_at(mid), mid);
  for (int t = 0; t < 40; ++t) tick(r, s, {}, {60, 0});
  CHECK(fp::abs(s.world.entities[1].vel.x) < Fixed::from_double(0.5));
  CHECK(s.players[1].blame_by == 0);
  for (int t = 0; t < 240; ++t) tick(r, s);
  int gone = 0;
  for (int i = 0; i <= 11; ++i) gone += s.world.platforms[static_cast<std::size_t>(i)].phase == fp::PlatformPhase::Gone ? 1 : 0;
  CHECK(gone >= 1);
  CHECK(s.players[0].stats.tiles_broken >= 1);
}

void test_thorns_need_ground_and_are_limited() {
  const Rules r = rules(Character::Fern);
  State air = make_state(r);
  const Input in[2] = {{0, kButtonTrick}, {}};
  step(r, air, in);
  CHECK(air.players[0].cooldown[1] == 0);  // fizzled: no cooldown spent

  State s = on_ground(r);
  for (int round = 0; round < 3; ++round) {
    tick(r, s, {0, kButtonTrick});
    tick(r, s);
    for (int t = 0; t < 250; ++t) tick(r, s);  // the cooldown (4 s) runs out
    s.players[0].cooldown = {};
  }
  CHECK(sticky(s) <= 2);
}

void test_roots_shake_then_break() {
  const Rules r = rules(Character::Fern);
  State s = on_ground(r, -80);
  tick(r, s, {0, kButtonBreak});
  const Object* roots = nullptr;
  for (const Object& o : s.objects) roots = o.kind == ObjectKind::Roots ? &o : roots;
  CHECK(roots != nullptr);
  const int seed_tile = tile_at(roots->pos.x);
  for (int t = 0; t < 50; ++t) tick(r, s);
  // Shaking while it swells (stress held near 0.48), nothing broken yet.
  const fp::PlatformState& near = s.world.platforms[static_cast<std::size_t>(seed_tile)];
  CHECK(near.phase == fp::PlatformPhase::Shaking && near.stress >= Fixed::from_double(0.45));
  for (int t = 0; t < 60; ++t) tick(r, s);
  CHECK(s.world.platforms[static_cast<std::size_t>(seed_tile)].phase == fp::PlatformPhase::Gone);
  CHECK(s.players[0].stats.tiles_broken >= 3);
}

void test_roots_hurt_slabs() {
  const Rules r = rules(Character::Fern, StageId::Yard);
  State s = make_state(r);
  s.world.entities[1].pos.x = Fixed::from_int(200);
  s.world.entities[0].pos.x = Fixed::from_int(-200);
  const int slab = 42;
  Object seed;
  seed.kind = ObjectKind::Roots;
  seed.owner = 0;
  seed.pos = {Fixed::zero(), r.engine.stage.platforms[static_cast<std::size_t>(slab)].rect.top};
  seed.timer = 1;  // bursts on the next tick
  s.objects[0] = seed;
  const Input idle[2] = {};
  step(r, s, idle);
  CHECK(s.slab_hp[slab] == 3);
  CHECK(s.objects[0].kind == ObjectKind::None);
}

void test_rub_out_erases_the_span_but_not_her_own_tile() {
  const Rules r = rules(Character::Dab);
  State s = on_ground(r, -70);
  const int own = tile_at(Fixed::from_int(-70));
  tick(r, s, {0, kButtonBreak});
  const Object* stroke = nullptr;
  for (const Object& o : s.objects) stroke = o.kind == ObjectKind::Rub ? &o : stroke;
  CHECK(stroke != nullptr && stroke->x0 >= s.world.entities[0].pos.x);
  for (int t = 0; t < 40; ++t) tick(r, s);
  CHECK(s.world.platforms[static_cast<std::size_t>(own)].phase == fp::PlatformPhase::Solid);
  int gone = 0;
  for (int i = own + 1; i <= 11; ++i) gone += s.world.platforms[static_cast<std::size_t>(i)].phase == fp::PlatformPhase::Gone ? 1 : 0;
  CHECK(gone >= 3 && gone <= 5);  // 170 px is about three and a half tiles
  CHECK(s.players[0].stats.tiles_broken >= 3);
}

int leaves(const State& s) {
  int n = 0;
  for (std::size_t i = 0; i < s.world.placeables.size(); ++i) n += s.world.placeables[i].active != 0 && s.meta[i].tag == kTagLeaf ? 1 : 0;
  return n;
}

void test_vine_grows_a_zigzag_of_leaves() {
  const Rules r = rules(Character::Fern);
  State s = on_ground(r, -60);
  for (PlayerState& p : s.players) p.invuln = 0;
  tick(r, s, {0, kButtonPlace});
  CHECK(leaves(s) == 0);
  for (int t = 0; t < 25; ++t) tick(r, s);
  CHECK(leaves(s) == 0);  // the seed takes half a second
  for (int t = 0; t < 10; ++t) tick(r, s);
  CHECK(leaves(s) == 1);
  for (int t = 0; t < 60; ++t) tick(r, s);
  CHECK(leaves(s) == 4);

  // Four leaves, alternating sides of the seed, each higher than the last.
  std::vector<std::pair<Fixed, Fixed>> at;  // (centre x, top) in the order they were placed
  for (std::size_t i = 0; i < s.world.placeables.size(); ++i) {
    if (s.world.placeables[i].active == 0 || s.meta[i].tag != kTagLeaf) continue;
    const fp::Rect& rc = s.world.placeables[i].rect;
    at.push_back({(rc.left + rc.right) / Fixed::from_int(2), rc.top});
    CHECK(rc.right - rc.left > Fixed::from_int(17) && rc.right - rc.left < Fixed::from_int(19));  // 56 px
    CHECK(s.meta[i].owner == 0);
  }
  CHECK(at.size() == 4);
  for (std::size_t i = 1; i < at.size(); ++i) {
    CHECK(at[i].second > at[i - 1].second);                       // up
    CHECK(fp::abs(at[i].first - at[i - 1].first) > Fixed::from_int(20));  // and across
  }
  // Anyone can climb it: a player dropped onto the lowest leaf stands on it.
  State climb = s;
  const int slot = [&] {
    int best = -1;
    for (std::size_t i = 0; i < climb.world.placeables.size(); ++i) {
      if (climb.world.placeables[i].active != 0 && climb.meta[i].tag == kTagLeaf &&
          (best < 0 || climb.world.placeables[i].rect.top < climb.world.placeables[static_cast<std::size_t>(best)].rect.top)) {
        best = static_cast<int>(i);
      }
    }
    return best;
  }();
  const fp::Rect leaf = climb.world.placeables[static_cast<std::size_t>(slot)].rect;
  climb.world.entities[1].pos = {(leaf.left + leaf.right) / Fixed::from_int(2), leaf.top + Fixed::from_int(8)};
  climb.world.entities[1].mode = fp::Mode::Airborne;
  climb.world.entities[1].support = -1;
  climb.world.entities[1].vel = {};
  for (int t = 0; t < 20; ++t) tick(r, climb);
  CHECK(climb.world.entities[1].support == static_cast<int>(r.engine.stage.platforms.size()) + slot);

  // The leaves wither (the first after about 7 s) and the seed is gone.
  for (int t = 0; t < 500; ++t) tick(r, s);
  CHECK(leaves(s) == 0);
}

void test_vine_needs_ground_for_its_seed() {
  const Rules r = rules(Character::Fern);
  // At the right end of the floor, the seed 50 px ahead would be over the pit.
  State s = on_ground(r, 90);
  s.world.entities[0].facing = 1;
  tick(r, s, {0, kButtonPlace});
  CHECK(s.players[0].cooldown[0] == 0);  // a fizzle spends nothing
  for (const Object& o : s.objects) CHECK(o.kind != ObjectKind::Vine);
}

void test_a_vine_dies_with_its_ground() {
  const Rules r = rules(Character::Fern);
  State s = on_ground(r, -60);
  tick(r, s, {0, kButtonPlace});
  const Object* seed = nullptr;
  for (const Object& o : s.objects) seed = o.kind == ObjectKind::Vine ? &o : seed;
  CHECK(seed != nullptr);
  const int tile = tile_at(seed->pos.x);
  fp::Events scratch;
  fp::destroy_platform(r.engine, s.world, tile, scratch);
  for (int t = 0; t < 60; ++t) tick(r, s);
  CHECK(leaves(s) == 0);
}

void test_dab_and_fern_round_trip() {
  const Rules r = rules(Character::Fern);
  State s = on_ground(r);
  tick(r, s, {0, kButtonTrick});
  for (int t = 0; t < 40; ++t) tick(r, s);
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
  test_thorns_grow_slow_and_chew();
  test_thorns_need_ground_and_are_limited();
  test_roots_shake_then_break();
  test_roots_hurt_slabs();
  test_rub_out_erases_the_span_but_not_her_own_tile();
  test_vine_grows_a_zigzag_of_leaves();
  test_vine_needs_ground_for_its_seed();
  test_a_vine_dies_with_its_ground();
  test_dab_and_fern_round_trip();
  return test::finish();
}
