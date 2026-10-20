// Glim's lobs: glue and oil become patches on the tile they land on, acid
// eats the tiles under it, and what they do is credited to Glim.
#include <vector>

#include "check.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

Rules rules() {
  Setup s;
  s.players = 2;
  s.characters = {Character::Glim, Character::Mason, Character::Pip, Character::Pip};
  return *make_rules(s);
}

// The tier-A tile (16 wide from -96) holding x.
int tile_at(Fixed x) { return ((x + Fixed::from_int(96)) / Fixed::from_int(16)).raw() / Fixed::kOneRaw; }

State on_ground(const Rules& r) {
  State s = make_state(r);
  s.world.entities[0] = fp::spawn_on(r.engine, 2, Fixed::from_int(-70));
  s.world.entities[1] = fp::spawn_on(r.engine, 10, Fixed::from_int(70));
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

int patches(const State& s, fp::PlaceableKind k) {
  int n = 0;
  for (const fp::Placeable& p : s.world.placeables) n += p.active != 0 && p.kind == k ? 1 : 0;
  return n;
}

int lobs(const State& s) {
  int n = 0;
  for (const Object& o : s.objects) n += o.kind == ObjectKind::Lob ? 1 : 0;
  return n;
}

void test_glue_lands_and_sticks() {
  const Rules r = rules();
  State s = on_ground(r);
  tick(r, s, {0, kButtonPlace});
  CHECK(lobs(s) == 1);
  for (int t = 0; t < 80 && lobs(s) > 0; ++t) tick(r, s);
  CHECK(lobs(s) == 0);
  CHECK(patches(s, fp::PlaceableKind::Sticky) == 1);
  std::size_t slot = 0;
  for (std::size_t i = 0; i < s.world.placeables.size(); ++i) {
    if (s.world.placeables[i].active != 0) slot = i;
  }
  CHECK(s.meta[slot].owner == 0);
  const fp::Rect rect = s.world.placeables[slot].rect;
  CHECK(rect.left > Fixed::from_int(-96) && rect.right < Fixed::from_int(96));  // on the bottom row
  CHECK(rect.right - rect.left <= Fixed::from_int(24));                          // about 72 px

  // Mason stands in it and walks: slowed to a tenth, and credited to Glim.
  const Fixed mid = (rect.left + rect.right) / Fixed::from_int(2);
  s.world.entities[1] = fp::spawn_on(r.engine, tile_at(mid), mid);
  for (int t = 0; t < 40; ++t) tick(r, s, {}, {-60, 0});
  const Fixed speed = fp::abs(s.world.entities[1].vel.x);
  CHECK(speed < Fixed::from_double(0.15));  // an ordinary walk is 0.9 at most
  CHECK(s.players[1].blame_by == 0);
}

void test_oil_is_slick_and_fast() {
  const Rules r = rules();
  State s = on_ground(r);
  tick(r, s, {0, kButtonTrick});
  for (int t = 0; t < 80 && lobs(s) > 0; ++t) tick(r, s);
  CHECK(patches(s, fp::PlaceableKind::Slick) == 1);
  for (const fp::Placeable& p : s.world.placeables) {
    if (p.active != 0) CHECK(p.rect.right - p.rect.left <= Fixed::from_int(36));  // about 110 px
  }
}

void test_two_at_once_oldest_goes() {
  const Rules r = rules();
  State s = on_ground(r);
  for (int round = 0; round < 3; ++round) {
    tick(r, s, {0, kButtonPlace});
    tick(r, s);
    for (int t = 0; t < 140; ++t) tick(r, s);  // lands, and the cooldown runs out
  }
  CHECK(patches(s, fp::PlaceableKind::Sticky) == 2);
}

void test_acid_eats_tiles_and_credits() {
  const Rules r = rules();
  State s = on_ground(r);
  tick(r, s, {0, kButtonBreak});
  bool puddle = false;
  Fixed at{};
  for (int t = 0; t < 80 && !puddle; ++t) {
    tick(r, s);
    for (const Object& o : s.objects) {
      if (o.kind == ObjectKind::Acid) {
        puddle = true;
        at = o.pos.x;
      }
    }
  }
  CHECK(puddle);
  // Run the puddle's life out without keeping the tiles firm.
  for (int t = 0; t < 160; ++t) tick(r, s, {}, {}, false);
  CHECK(s.world.platforms[static_cast<std::size_t>(tile_at(at))].phase == fp::PlatformPhase::Gone);  // dissolved, and still down
  CHECK(s.players[0].stats.tiles_broken >= 1);
  for (const Object& o : s.objects) CHECK(o.kind != ObjectKind::Acid);  // 2.5 s, then it is gone
}

void test_off_the_stage_a_lob_is_lost() {
  const Rules r = rules();
  State s = make_state(r);
  s.world.entities[0].pos = {Fixed::from_int(120), Fixed::from_int(40)};  // off to the side, in the air
  tick(r, s, {0, kButtonPlace | kButtonJump});
  for (int t = 0; t < 400; ++t) tick(r, s, {}, {}, false);
  CHECK(lobs(s) == 0 && patches(s, fp::PlaceableKind::Sticky) == 0);
}

// The highest a player gets from a full hop, standing at x on tile `tile`, with or without a patch under their feet.
Fixed hop_height(const Rules& r, bool patch, bool bramble = false) {
  State s = on_ground(r);
  s.world.entities[1] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(-70)), Fixed::from_int(-70));
  s.world.entities[0].pos.x = Fixed::from_int(80);
  if (patch) {
    fp::Placeable p;
    p.kind = fp::PlaceableKind::Sticky;
    p.rect = {Fixed::from_int(-90), Fixed::from_int(-50), Fixed::from_int(-1), Fixed::from_int(1)};
    p.speed_scale = Fixed::from_double(0.1);
    const int slot = fp::place(s.world, p);
    s.meta[static_cast<std::size_t>(slot)].owner = 0;
    s.meta[static_cast<std::size_t>(slot)].tag = bramble ? 1 : 0;
  }
  s.players[1].invuln = 0;
  Fixed top{};
  for (int t = 0; t < 90; ++t) {
    firm(s);
    const Input in[2] = {{}, {0, static_cast<std::uint8_t>(t < 40 ? kButtonJump : 0)}};
    step(r, s, in);
    if (s.world.entities[1].pos.y > top) top = s.world.entities[1].pos.y;
  }
  return top;
}

void test_glue_and_brambles_cut_the_jump() {
  const Rules r = rules();
  const Fixed free = hop_height(r, false);
  const Fixed glued = hop_height(r, true);
  const Fixed thorny = hop_height(r, true, true);
  CHECK(free > Fixed::from_int(20));
  CHECK(glued < free * Fixed::from_double(0.5));    // jump x0.55: a 0.3 share of the height
  CHECK(thorny < free * Fixed::from_double(0.8) && thorny > glued);  // jump x0.8: a 0.64 share
}

void test_objects_round_trip() {
  const Rules r = rules();
  State s = on_ground(r);
  tick(r, s, {0, kButtonBreak});
  for (int t = 0; t < 8; ++t) tick(r, s);
  CHECK(lobs(s) == 1);
  std::vector<std::uint8_t> bytes;
  snapshot(s, bytes);
  State copy = make_state(r);
  CHECK(restore(r, copy, bytes) && hash(copy) == hash(s));
  for (int t = 0; t < 120; ++t) {
    tick(r, s);
    tick(r, copy);
  }
  CHECK(hash(copy) == hash(s));
}

}  // namespace

int main() {
  test_glue_lands_and_sticks();
  test_oil_is_slick_and_fast();
  test_two_at_once_oldest_goes();
  test_acid_eats_tiles_and_credits();
  test_off_the_stage_a_lob_is_lost();
  test_glue_and_brambles_cut_the_jump();
  test_objects_round_trip();
  return test::finish();
}
