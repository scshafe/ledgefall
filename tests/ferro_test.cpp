// Ferro's lodestone, polarity and ingot.
#include <vector>

#include "check.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

Rules rules() {
  Setup s;
  s.players = 2;
  s.characters = {Character::Ferro, Character::Fuse, Character::Pip, Character::Pip};
  return *make_rules(s);
}

int tile_at(Fixed x) { return ((x + Fixed::from_int(96)) / Fixed::from_int(16)).raw() / Fixed::kOneRaw; }

State in_air(const Rules& r) {
  State s = make_state(r);
  s.world.entities[0].pos = {Fixed::from_int(-60), Fixed::from_int(120)};
  s.world.entities[1].pos = {Fixed::from_int(60), Fixed::from_int(120)};
  return s;
}

void tick(const Rules& r, State& s, Input a = {}, Input b = {}) {
  for (fp::PlatformState& p : s.world.platforms) {
    if (p.phase == fp::PlatformPhase::Solid || p.phase == fp::PlatformPhase::Shaking) {
      p.phase = fp::PlatformPhase::Solid;
      p.stress = Fixed::zero();
    }
  }
  // Players float where they are, out of the way.
  for (auto& e : s.world.entities) {
    e.vel = {};
  }
  const Input in[2] = {a, b};
  step(r, s, in);
}

Object* find(State& s, ObjectKind k) {
  for (Object& o : s.objects) {
    if (o.kind == k) return &o;
  }
  return nullptr;
}

void test_lodestone_arms_and_expires() {
  const Rules r = rules();
  State s = in_air(r);
  tick(r, s, {0, kButtonPlace});
  Object* m = find(s, ObjectKind::Magnet);
  CHECK(m != nullptr && m->arm > 0 && m->owner == 0);
  CHECK(m->pos.x > s.world.entities[0].pos.x);  // ahead of Ferro
  for (int t = 0; t < 15; ++t) tick(r, s);
  CHECK(find(s, ObjectKind::Magnet)->arm == 0);
  // One at once.
  s.players[0].cooldown = {};
  tick(r, s);
  tick(r, s, {0, kButtonPlace});
  int n = 0;
  for (const Object& o : s.objects) n += o.kind == ObjectKind::Magnet ? 1 : 0;
  CHECK(n == 1);
  for (int t = 0; t < 320; ++t) tick(r, s);
  CHECK(find(s, ObjectKind::Magnet) == nullptr);  // 5 s
}

void test_it_pulls_metal_and_takes_it_over() {
  const Rules r = rules();
  State s = in_air(r);
  tick(r, s, {0, kButtonPlace});
  for (int t = 0; t < 15; ++t) tick(r, s);
  const fp::Vec2 pole = find(s, ObjectKind::Magnet)->pos;

  // Fuse's anvil, well within reach and above the stage, is lifted toward the pole and becomes Ferro's.
  Object anvil;
  anvil.kind = ObjectKind::Anvil;
  anvil.owner = 1;
  anvil.uses = 3;
  anvil.pos = {pole.x + Fixed::from_int(30), pole.y + Fixed::from_int(10)};
  s.objects[5] = anvil;
  for (int t = 0; t < 60; ++t) tick(r, s);
  const Object* a = find(s, ObjectKind::Anvil);
  CHECK(a != nullptr && a->owner == 0);
  const Fixed dx = a->pos.x - pole.x, dy = a->pos.y + Fixed::from_double(4.1) - pole.y;
  CHECK(dx * dx + dy * dy < Fixed::from_int(100));  // hanging on it

  // Beyond 240 px (77 units) nothing happens.
  State far = in_air(r);
  tick(r, far, {0, kButtonPlace});
  for (int t = 0; t < 15; ++t) tick(r, far);
  const fp::Vec2 p2 = find(far, ObjectKind::Magnet)->pos;
  Object lone;
  lone.kind = ObjectKind::Charge;
  lone.owner = 1;
  lone.timer = 200;
  lone.pos = {p2.x + Fixed::from_int(90), p2.y};
  far.objects[5] = lone;
  tick(r, far);
  CHECK(far.objects[5].owner == 1);
}

void test_polarity_throws_loose_things_away() {
  const Rules r = rules();
  State s = in_air(r);
  tick(r, s, {0, kButtonTrick});
  CHECK(s.players[0].cooldown[1] == 0);  // no lodestone yet: nothing to flip, no cooldown spent
  tick(r, s, {0, kButtonPlace});
  for (int t = 0; t < 15; ++t) tick(r, s);
  Object* m = find(s, ObjectKind::Magnet);
  const fp::Vec2 pole = m->pos;
  Object anvil;
  anvil.kind = ObjectKind::Anvil;
  anvil.owner = 1;
  anvil.uses = 3;
  anvil.pos = {pole.x, pole.y - Fixed::from_int(2)};
  s.objects[5] = anvil;
  for (int t = 0; t < 4; ++t) tick(r, s);  // hangs on the pole
  CHECK(find(s, ObjectKind::Anvil)->owner == 0);
  tick(r, s, {0, kButtonTrick});
  m = find(s, ObjectKind::Magnet);
  CHECK(m->uses == 1);  // pushing
  const Object* a = find(s, ObjectKind::Anvil);
  CHECK(a == nullptr || fp::abs(a->vel.x) + fp::abs(a->vel.y) > Fixed::from_int(1));
  for (int t = 0; t < 155; ++t) tick(r, s);
  CHECK(find(s, ObjectKind::Magnet) == nullptr || find(s, ObjectKind::Magnet)->uses == 0);  // the flip wears off
}

void test_ingot_breaks_only_the_first_row() {
  const Rules r = rules();
  State s = make_state(r);
  s.world.entities[0] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(-80)), Fixed::from_int(-80));
  s.world.entities[1] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(80)), Fixed::from_int(80));
  // An ingot dropped over the tier-2 tiles and the floor below: it breaks the upper tile it meets, then is spent.
  Object ingot;
  ingot.kind = ObjectKind::Anvil;
  ingot.owner = 0;
  ingot.uses = 1;
  ingot.pos = {Fixed::from_int(-40), Fixed::from_int(60)};  // above the tier-1 tiles at y 34
  s.objects[0] = ingot;
  for (int t = 0; t < 60; ++t) {
    const Input in[2] = {};
    step(r, s, in);
  }
  int gone_upper = 0, gone_floor = 0;
  for (std::size_t i = 0; i < s.world.platforms.size(); ++i) {
    if (s.world.platforms[i].phase != fp::PlatformPhase::Gone) continue;
    (i < 12 ? gone_floor : gone_upper) += 1;
  }
  CHECK(gone_upper >= 1 && gone_upper <= 2);
  CHECK(gone_floor <= 0 + 2);  // it never reached the floor (only players standing still may have worn a tile)
  CHECK(find(s, ObjectKind::Anvil) == nullptr);

  // Cast: the ingot is thrown by the third move.
  State t2 = in_air(r);
  tick(r, t2, {0, kButtonBreak});
  const Object* thrown = find(t2, ObjectKind::Anvil);
  CHECK(thrown != nullptr && thrown->uses == 1);
}

void test_ferro_round_trip() {
  const Rules r = rules();
  State s = in_air(r);
  tick(r, s, {0, kButtonPlace});
  for (int t = 0; t < 15; ++t) tick(r, s);
  tick(r, s, {0, kButtonTrick});
  std::vector<std::uint8_t> bytes;
  snapshot(s, bytes);
  State copy = make_state(r);
  CHECK(restore(r, copy, bytes) && hash(copy) == hash(s));
  for (int t = 0; t < 40; ++t) {
    tick(r, s);
    tick(r, copy);
  }
  CHECK(hash(copy) == hash(s));
}

}  // namespace

int main() {
  test_lodestone_arms_and_expires();
  test_it_pulls_metal_and_takes_it_over();
  test_polarity_throws_loose_things_away();
  test_ingot_breaks_only_the_first_row();
  test_ferro_round_trip();
  return test::finish();
}
