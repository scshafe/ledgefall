// The one-way placeables: the respawn halo, Mason's ledge, Dab's decoy and hollow tiles.
#include <vector>

#include "check.hpp"
#include "halos.hpp"
#include "../src/internal.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using ledgefall::detail::damage_ledge;
using fp::Fixed;

namespace {

Rules rules_for(Character a, Character b = Character::Mason) {
  Setup s;
  s.players = 2;
  s.characters = {a, b, Character::Pip, Character::Pip};
  return *make_rules(s);
}

int tile_at(Fixed x) { return ((x + Fixed::from_int(96)) / Fixed::from_int(16)).raw() / Fixed::kOneRaw; }

void keep_firm(State& s, int from, int to) {
  for (int i = from; i <= to; ++i) {
    fp::PlatformState& p = s.world.platforms[static_cast<std::size_t>(i)];
    if (p.phase == fp::PlatformPhase::Solid || p.phase == fp::PlatformPhase::Shaking) {
      p.phase = fp::PlatformPhase::Solid;
      p.stress = Fixed::zero();
    }
  }
}

void tick(const Rules& r, State& s, Input a = {}, Input b = {}, bool firm = true) {
  if (firm) keep_firm(s, 0, 21);
  const Input in[2] = {a, b};
  step(r, s, in);
}

int count_tag(const State& s, std::uint8_t tag) {
  int n = 0;
  for (std::size_t i = 0; i < s.world.placeables.size(); ++i) n += s.world.placeables[i].active != 0 && s.meta[i].tag == tag ? 1 : 0;
  return n;
}

int slot_of(const State& s, std::uint8_t tag) {
  for (std::size_t i = 0; i < s.world.placeables.size(); ++i) {
    if (s.world.placeables[i].active != 0 && s.meta[i].tag == tag) return static_cast<int>(i);
  }
  return -1;
}

State on_ground(const Rules& r) {
  State s = make_state(r);
  clear_halos(r, s);
  s.world.entities[0] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(-40)), Fixed::from_int(-40));
  s.world.entities[1] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(60)), Fixed::from_int(60));
  for (PlayerState& p : s.players) p.invuln = 0;
  return s;
}

void test_a_halo_holds_its_owner_until_they_step_off() {
  const Rules r = rules_for(Character::Mason);
  State s = make_state(r);
  CHECK(count_tag(s, kTagHalo) == 2);
  // Standing on it, a player is on the halo and it stays (3 s), however still they are.
  for (int t = 0; t < 60; ++t) tick(r, s, {}, {}, false);
  CHECK(count_tag(s, kTagHalo) == 2);
  CHECK(s.world.entities[0].support >= static_cast<int>(r.engine.stage.platforms.size()));
  // Stepping off takes it away.
  for (int t = 0; t < 30; ++t) tick(r, s, {100, 0}, {}, false);
  CHECK(count_tag(s, kTagHalo) == 1);  // seat 1's still there
  // And it times out after 3 s.
  for (int t = 0; t < 130; ++t) tick(r, s, {}, {}, false);
  CHECK(count_tag(s, kTagHalo) == 0 || s.world.entities[1].eliminated != 0);
}

void test_a_respawned_player_comes_back_on_a_halo() {
  const Rules r = rules_for(Character::Mason);
  State s = make_state(r);
  clear_halos(r, s);
  s.world.entities[0].pos.y = Fixed::from_int(-1000);
  tick(r, s, {}, {}, false);
  CHECK(s.players[0].respawn_timer > 0);
  for (int t = 0; t < kRespawnDelayTicks + 2; ++t) tick(r, s, {}, {}, false);
  CHECK(s.world.entities[0].eliminated == 0 && count_tag(s, kTagHalo) >= 1);
  for (int t = 0; t < 10; ++t) tick(r, s, {}, {}, false);
  CHECK(s.world.entities[0].support >= static_cast<int>(r.engine.stage.platforms.size()));  // standing on it
}

void test_masons_ledge_on_the_ground_and_in_the_air() {
  const Rules r = rules_for(Character::Mason);
  State s = on_ground(r);
  // Mason on the right end of the top tier (tile 21), facing the pit: the ledge is built out over nothing.
  s.world.entities[0] = fp::spawn_on(r.engine, 21, Fixed::from_int(28));
  tick(r, s, {0, kButtonTrick});
  const int slot = slot_of(s, kTagLedge);
  CHECK(slot >= 0 && s.meta[static_cast<std::size_t>(slot)].owner == 0 && s.meta[static_cast<std::size_t>(slot)].uses == 2);
  const fp::Rect ledge = s.world.placeables[static_cast<std::size_t>(slot)].rect;
  CHECK(ledge.left > s.world.entities[0].pos.x && ledge.top == s.world.entities[0].pos.y);  // ahead, at his feet's height
  CHECK(ledge.right - ledge.left > Fixed::from_int(28) && ledge.right - ledge.left < Fixed::from_int(30));  // 90 px

  // Someone who falls onto it lands on it.
  s.world.entities[1].pos = {(ledge.left + ledge.right) / Fixed::from_int(2), ledge.top + Fixed::from_int(10)};
  s.world.entities[1].mode = fp::Mode::Airborne;
  s.world.entities[1].support = -1;
  for (int t = 0; t < 30; ++t) tick(r, s);
  CHECK(s.world.entities[1].support == static_cast<int>(r.engine.stage.platforms.size()) + slot);

  // It lasts 4 s, and there is only one at a time.
  s.players[0].cooldown = {};
  tick(r, s);
  tick(r, s, {0, kButtonTrick});
  CHECK(count_tag(s, kTagLedge) == 1);
  for (int t = 0; t < 250; ++t) tick(r, s);
  CHECK(count_tag(s, kTagLedge) == 0);

  // In the air it appears under his feet.
  State air = on_ground(r);
  air.world.entities[0].pos.y = Fixed::from_int(50);
  air.world.entities[0].mode = fp::Mode::Airborne;
  air.world.entities[0].support = -1;
  tick(r, air, {0, kButtonTrick});
  const int a = slot_of(air, kTagLedge);
  CHECK(a >= 0 && air.world.placeables[static_cast<std::size_t>(a)].rect.top < air.world.entities[0].pos.y + Fixed::from_int(2));
}

void test_a_ledge_takes_damage_and_dies_to_a_charge() {
  const Rules r = rules_for(Character::Mason);
  State s = on_ground(r);
  tick(r, s, {0, kButtonTrick});
  const int slot = slot_of(s, kTagLedge);
  CHECK(slot >= 0);
  fp::Events scratch;
  damage_ledge(r, s, slot, 1, scratch);
  CHECK(s.world.placeables[static_cast<std::size_t>(slot)].active != 0 && s.meta[static_cast<std::size_t>(slot)].uses == 1);
  damage_ledge(r, s, slot, 1, scratch);
  CHECK(s.world.placeables[static_cast<std::size_t>(slot)].active == 0);

  // An anvil dropped on one smashes it.
  State t = on_ground(r);
  tick(r, t, {0, kButtonTrick});
  const fp::Rect ledge = t.world.placeables[static_cast<std::size_t>(slot_of(t, kTagLedge))].rect;
  Object anvil;
  anvil.kind = ObjectKind::Anvil;
  anvil.owner = 1;
  anvil.uses = 3;
  anvil.pos = {(ledge.left + ledge.right) / Fixed::from_int(2), ledge.top + Fixed::from_int(25)};
  t.objects[0] = anvil;
  for (int i = 0; i < 40; ++i) tick(r, t);
  CHECK(count_tag(t, kTagLedge) == 0);
}

void test_a_decoy_holds_only_dab_and_credits_her() {
  const Rules r = rules_for(Character::Dab);
  State s = on_ground(r);
  // Painted over real floor it fizzles and costs nothing.
  tick(r, s, {0, kButtonPlace});
  CHECK(count_tag(s, kTagDecoy) == 0 && s.players[0].cooldown[0] == 0);

  // Over a gap it holds: Dab at the right end of the left tier-1 tiles, facing the gap before the right ones.
  State g = make_state(r);
  clear_halos(r, g);
  g.world.entities[0] = fp::spawn_on(r.engine, 14, Fixed::from_int(-20));
  g.world.entities[1] = fp::spawn_on(r.engine, 5, Fixed::from_int(-30));
  for (PlayerState& p : g.players) p.invuln = 0;
  tick(r, g, {0, kButtonPlace});
  const int slot = slot_of(g, kTagDecoy);
  CHECK(slot >= 0 && g.world.placeables[static_cast<std::size_t>(slot)].only_for == 0);
  const fp::Rect decoy = g.world.placeables[static_cast<std::size_t>(slot)].rect;
  CHECK(decoy.left > g.world.entities[0].pos.x);  // out ahead of her, across the gap

  // Dab stands on it; Mason dropped onto it falls through, and is credited to Dab.
  const Fixed mid = (decoy.left + decoy.right) / Fixed::from_int(2);
  g.world.entities[0].pos = {mid + Fixed::from_int(3), decoy.top + Fixed::from_int(4)};
  g.world.entities[0].mode = fp::Mode::Airborne;
  g.world.entities[0].support = -1;
  g.world.entities[0].vel = {};
  g.world.entities[1].pos = {mid - Fixed::from_int(8), decoy.top + Fixed::from_int(30)};  // clear of Dab's head, over the gap
  g.world.entities[1].mode = fp::Mode::Airborne;
  g.world.entities[1].support = -1;
  g.world.entities[1].vel = {};
  for (int t = 0; t < 50; ++t) tick(r, g);
  CHECK(g.world.entities[0].support == static_cast<int>(r.engine.stage.platforms.size()) + slot);
  CHECK(g.world.entities[1].pos.y < decoy.top - Fixed::from_int(4));
  CHECK(g.players[1].blame_by == 0);
}

void test_a_hollow_tile_drops_everyone_but_dab() {
  const Rules r = rules_for(Character::Dab);
  State s = on_ground(r);
  s.world.entities[1] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(-10)), Fixed::from_int(-10));  // standing in the span
  tick(r, s, {0, kButtonTrick});
  CHECK(s.players[0].cooldown[1] > 0);
  for (int t = 0; t < 20; ++t) tick(r, s);
  CHECK(s.world.entities[1].support >= 0 && s.world.entities[1].support < 12);  // still shimmering: nothing changed
  CHECK(count_tag(s, kTagHollow) == 0);
  for (int t = 0; t < 10; ++t) tick(r, s);
  CHECK(count_tag(s, kTagHollow) >= 1);  // the shimmer is over: the floor is Dab's
  CHECK(s.world.entities[1].pos.y < Fixed::from_int(-3) || s.world.entities[1].mode == fp::Mode::Airborne);
  CHECK(s.players[1].blame_by == 0);
  // Dab can stand on it.
  const int slot = slot_of(s, kTagHollow);
  const fp::Rect floor = s.world.placeables[static_cast<std::size_t>(slot)].rect;
  s.world.entities[0] = fp::spawn_on(r.engine, tile_at(Fixed::from_int(-40)), Fixed::from_int(-40));
  s.world.entities[0].pos = {(floor.left + floor.right) / Fixed::from_int(2), floor.top + Fixed::from_int(3)};
  s.world.entities[0].mode = fp::Mode::Airborne;
  s.world.entities[0].support = -1;
  for (int t = 0; t < 20; ++t) tick(r, s, {}, {}, false);
  CHECK(s.world.entities[0].eliminated == 0 && s.world.entities[0].support >= static_cast<int>(r.engine.stage.platforms.size()) - 0);
  // After 2.5 s the floor is gone and the tile is back.
  for (int t = 0; t < 160; ++t) tick(r, s, {}, {}, false);
  CHECK(count_tag(s, kTagHollow) == 0);
}

void test_one_way_state_round_trips() {
  const Rules r = rules_for(Character::Dab);
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
  test_a_halo_holds_its_owner_until_they_step_off();
  test_a_respawned_player_comes_back_on_a_halo();
  test_masons_ledge_on_the_ground_and_in_the_air();
  test_a_ledge_takes_damage_and_dies_to_a_charge();
  test_a_decoy_holds_only_dab_and_credits_her();
  test_a_hollow_tile_drops_everyone_but_dab();
  test_one_way_state_round_trips();
  return test::finish();
}
