// Fuse's bumper, charge and saw.
#include <vector>

#include "check.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

Rules rules(StageId stage = StageId::Practice) {
  Setup s;
  s.players = 2;
  s.stage = stage;
  s.characters = {Character::Fuse, Character::Mason, Character::Pip, Character::Pip};
  return *make_rules(s);
}

int tile_at(Fixed x) { return ((x + Fixed::from_int(96)) / Fixed::from_int(16)).raw() / Fixed::kOneRaw; }

State on_ground(const Rules& r) {
  State s = make_state(r);
  s.world.entities[0] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(-70)), Fixed::from_int(-70));
  s.world.entities[1] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(70)), Fixed::from_int(70));
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

const Object* find(const State& s, ObjectKind k) {
  for (const Object& o : s.objects) {
    if (o.kind == k) return &o;
  }
  return nullptr;
}

int count(const State& s, ObjectKind k) {
  int n = 0;
  for (const Object& o : s.objects) n += o.kind == k ? 1 : 0;
  return n;
}

void test_bumper_arms_then_bounces_and_credits() {
  const Rules r = rules();
  State s = on_ground(r);
  tick(r, s, {0, kButtonPlace});
  const Object* b = find(s, ObjectKind::Bumper);
  CHECK(b != nullptr && b->arm > 0 && b->pos.x > s.world.entities[0].pos.x);
  const Fixed bx = b->pos.x, by = b->pos.y;
  // Mason stands in it while it arms: nothing happens.
  s.world.entities[1] = fp::spawn_on(r.engine, tile_at(bx), bx);
  for (int t = 0; t < 20; ++t) tick(r, s);
  CHECK(s.players[1].blame_by == -1);
  for (int t = 0; t < 20; ++t) tick(r, s);
  CHECK(s.players[1].blame_by == 0);
  // Thrown away fast, and no longer on the ground.
  CHECK(s.world.entities[1].mode == fp::Mode::Airborne || s.world.entities[1].vel.y > Fixed::zero());
  const fp::Vec2 v = s.world.entities[1].vel;
  CHECK(fp::abs(v.x) + fp::abs(v.y) > Fixed::from_int(2));
  (void)by;
}

void test_bumper_uses_and_limit() {
  const Rules r = rules();
  State s = on_ground(r);
  for (int round = 0; round < 3; ++round) {
    tick(r, s, {0, kButtonPlace});
    tick(r, s);
    for (int t = 0; t < 140; ++t) tick(r, s);
  }
  CHECK(count(s, ObjectKind::Bumper) == 2);
}

void test_falling_bumper_bounces_fuse() {
  const Rules r = rules();
  State s = make_state(r);
  s.world.entities[0].pos = {Fixed::from_int(-40), Fixed::from_int(80)};
  for (int t = 0; t < 10; ++t) tick(r, s);
  CHECK(s.world.entities[0].vel.y < Fixed::zero());
  tick(r, s, {0, kButtonPlace});
  CHECK(s.world.entities[0].vel.y > Fixed::zero());
}

void test_charge_sticks_then_breaks_everything_near() {
  const Rules r = rules();
  State s = on_ground(r);
  tick(r, s, {0, kButtonTrick});
  for (int t = 0; t < 80 && find(s, ObjectKind::Charge) == nullptr; ++t) tick(r, s);
  const Object* c = find(s, ObjectKind::Charge);
  CHECK(c != nullptr);
  const Fixed cx = c->pos.x;
  // A wall right next to it will go too.
  fp::Placeable w;
  w.kind = fp::PlaceableKind::Wall;
  w.rect = {cx + Fixed::from_int(10), cx + Fixed::from_int(17), Fixed::zero(), Fixed::from_int(29)};
  const int slot = fp::place(s.world, w);
  CHECK(slot >= 0);
  s.meta[static_cast<std::size_t>(slot)].owner = 1;
  for (int t = 0; t < 110; ++t) tick(r, s, {}, {}, false);
  CHECK(find(s, ObjectKind::Charge) == nullptr);
  CHECK(s.world.placeables[static_cast<std::size_t>(slot)].active == 0);
  CHECK(s.world.platforms[static_cast<std::size_t>(tile_at(cx))].phase == fp::PlatformPhase::Gone);
  // Tiles far away (more than 115 px = 37 units) are untouched.
  CHECK(s.world.platforms[static_cast<std::size_t>(tile_at(cx + Fixed::from_int(60)))].phase != fp::PlatformPhase::Gone ||
        tile_at(cx + Fixed::from_int(60)) > 11);
  CHECK(s.players[0].stats.tiles_broken >= 3);
}

void test_saw_cuts_what_it_crosses() {
  const Rules r = rules();
  State s = on_ground(r);
  tick(r, s, {0, kButtonBreak});
  CHECK(count(s, ObjectKind::Saw) == 1);
  const Fixed start = find(s, ObjectKind::Saw)->pos.x;
  for (int t = 0; t < 40; ++t) tick(r, s, {}, {}, false);
  const Object* saw = find(s, ObjectKind::Saw);
  CHECK(saw != nullptr && saw->pos.x > start + Fixed::from_int(30));  // rolling along at about 2.2 a tick
  // The tiles it has crossed are doomed and have gone down (a doomed tile falls in a quarter second).
  CHECK(s.world.platforms[static_cast<std::size_t>(tile_at(start + Fixed::from_int(20)))].phase == fp::PlatformPhase::Gone);
  for (int t = 0; t < 100; ++t) tick(r, s, {}, {}, false);
  CHECK(count(s, ObjectKind::Saw) == 0);  // 2.2 s
  CHECK(s.players[0].stats.tiles_broken >= 1);
}

void test_saw_drops_off_an_edge_and_wall_turns_it() {
  const Rules r = rules();
  State s = make_state(r);
  s.world.entities[0] = fp::spawn_on(r.engine, 12, Fixed::from_int(-40));  // tier 1 (the left three)
  s.world.entities[1] = fp::spawn_on(r.engine, 0, Fixed::from_int(-90));
  tick(r, s, {0, kButtonBreak});
  const Object* saw = find(s, ObjectKind::Saw);
  CHECK(saw != nullptr);
  const Fixed top = saw->pos.y - Fixed::from_double(4.48);
  for (int t = 0; t < 40; ++t) tick(r, s);
  saw = find(s, ObjectKind::Saw);
  CHECK(saw != nullptr && saw->pos.y < top - Fixed::from_int(5));  // rolled off the end and dropped to a lower tier

  // A wall in its way turns it back.
  State w = on_ground(r);
  tick(r, w, {0, kButtonBreak});
  const Fixed sx = find(w, ObjectKind::Saw)->pos.x;
  fp::Placeable wall;
  wall.kind = fp::PlaceableKind::Wall;
  wall.rect = {sx + Fixed::from_int(20), sx + Fixed::from_int(27), Fixed::zero(), Fixed::from_int(29)};
  CHECK(fp::place(w.world, wall) >= 0);
  for (int t = 0; t < 30; ++t) tick(r, w);
  const Object* back = find(w, ObjectKind::Saw);
  CHECK(back != nullptr && back->vel.x < Fixed::zero());
}

void test_fuse_round_trip() {
  const Rules r = rules();
  State s = on_ground(r);
  // (No saw here: a snapshot with a doomed tile cannot be restored until fighter-physics#23 is fixed.)
  tick(r, s, {0, kButtonPlace});
  tick(r, s, {0, kButtonTrick});
  for (int t = 0; t < 12; ++t) tick(r, s);
  std::vector<std::uint8_t> bytes;
  snapshot(s, bytes);
  State copy = make_state(r);
  CHECK(restore(r, copy, bytes) && hash(copy) == hash(s));
  for (int t = 0; t < 150; ++t) {
    tick(r, s);
    tick(r, copy);
  }
  CHECK(hash(copy) == hash(s));
}

}  // namespace

int main() {
  test_bumper_arms_then_bounces_and_credits();
  test_bumper_uses_and_limit();
  test_falling_bumper_bounces_fuse();
  test_charge_sticks_then_breaks_everything_near();
  test_saw_cuts_what_it_crosses();
  test_saw_drops_off_an_edge_and_wall_turns_it();
  test_fuse_round_trip();
  return test::finish();
}
