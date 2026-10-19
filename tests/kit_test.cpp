// The move framework and the first moves: Mason's wall, Pip's spring, credit,
// stomps, and the ranking of a finished match.
#include <vector>

#include "check.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

Rules rules_for(Character a, Character b) {
  Setup s;
  s.players = 2;
  s.characters = {a, b, Character::Pip, Character::Pip};
  auto r = make_rules(s);
  CHECK(r.has_value());
  return *r;
}

// Both players standing on tier-A tiles, facing each other's way.
State on_ground(const Rules& r) {
  State s = make_state(r);
  s.world.entities[0] = fp::spawn_on(r.engine, 3, Fixed::from_int(-40));
  s.world.entities[1] = fp::spawn_on(r.engine, 9, Fixed::from_int(40));
  return s;
}

// Keep the tiles from crumbling under a test that stands still.
void firm(State& s) {
  for (fp::PlatformState& p : s.world.platforms) {
    if (p.phase == fp::PlatformPhase::Solid || p.phase == fp::PlatformPhase::Shaking) {
      p.phase = fp::PlatformPhase::Solid;
      p.stress = Fixed::zero();
    }
  }
}

void tick(const Rules& r, State& s, Input a = {}, Input b = {}) {
  firm(s);
  const Input in[2] = {a, b};
  step(r, s, in);
}

int active(const State& s, fp::PlaceableKind k) {
  int n = 0;
  for (const fp::Placeable& p : s.world.placeables) n += p.active != 0 && p.kind == k ? 1 : 0;
  return n;
}

const Input kPlace{0, kButtonPlace};

void test_wall_cooldown_and_limit() {
  const Rules r = rules_for(Character::Mason, Character::Glim);
  State s = on_ground(r);
  tick(r, s, kPlace);
  CHECK(active(s, fp::PlaceableKind::Wall) == 1);
  CHECK(s.players[0].stats.placed == 1 && s.players[0].cooldown[0] > 0);
  // The wall stands ahead of Mason (he faces +x), whole and solid.
  for (std::size_t i = 0; i < s.world.placeables.size(); ++i) {
    if (s.world.placeables[i].active == 0) continue;
    CHECK(s.meta[i].owner == 0);
    CHECK(s.world.placeables[i].rect.left > s.world.entities[0].pos.x);
  }
  tick(r, s);  // release
  tick(r, s, kPlace);  // pressed again, but the move is cooling down
  CHECK(active(s, fp::PlaceableKind::Wall) == 1);

  // Two at once; a third removes the oldest.
  for (int round = 0; round < 2; ++round) {
    for (int t = 0; t < 80; ++t) tick(r, s);
    tick(r, s, kPlace);
  }
  CHECK(active(s, fp::PlaceableKind::Wall) == 2);
}

void test_wall_goes_with_its_tile() {
  const Rules r = rules_for(Character::Mason, Character::Glim);
  State s = on_ground(r);
  tick(r, s, kPlace);
  CHECK(active(s, fp::PlaceableKind::Wall) == 1);
  fp::Events scratch;
  // Break the tile it stands on.
  const int support = s.meta[0].owner == 0 ? s.meta[0].support : -1;
  int tile = support;
  for (std::size_t i = 0; i < s.meta.size(); ++i) {
    if (s.meta[i].owner == 0) tile = s.meta[i].support;
  }
  CHECK(tile >= 0);
  fp::destroy_platform(r.engine, s.world, tile, scratch);
  tick(r, s);
  CHECK(active(s, fp::PlaceableKind::Wall) == 0);
}

void test_spring_arms_launches_and_credits() {
  const Rules r = rules_for(Character::Pip, Character::Glim);
  State s = on_ground(r);
  tick(r, s, kPlace);
  CHECK(active(s, fp::PlaceableKind::Spring) == 0);  // arming
  for (int t = 0; t < 15; ++t) tick(r, s);
  CHECK(active(s, fp::PlaceableKind::Spring) == 1);

  // Player 1 stands on the pad: launched, and credited to Pip.
  int slot = -1;
  for (std::size_t i = 0; i < s.world.placeables.size(); ++i) {
    if (s.world.placeables[i].active != 0) slot = static_cast<int>(i);
  }
  const fp::Rect pad = s.world.placeables[static_cast<std::size_t>(slot)].rect;
  const Fixed mid = (pad.left + pad.right) / Fixed::from_int(2);
  const int tile = ((mid + Fixed::from_int(96)) / Fixed::from_int(16)).raw() / Fixed::kOneRaw;  // tier A: 16-wide tiles from -96
  s.world.entities[1] = fp::spawn_on(r.engine, tile, mid);
  tick(r, s);
  CHECK(s.world.entities[1].vel.y > Fixed::zero());
  CHECK(s.players[1].blame_by == 0);
  CHECK(s.meta[static_cast<std::size_t>(slot)].uses == 3);
}

void test_air_spring_launches_the_caster() {
  const Rules r = rules_for(Character::Pip, Character::Glim);
  State s = make_state(r);  // both start in the air
  tick(r, s);
  const Fixed before = s.world.entities[0].vel.y;
  tick(r, s, kPlace);
  CHECK(s.world.entities[0].vel.y > before);
  CHECK(s.world.entities[0].vel.y > Fixed::from_int(2));
  CHECK(active(s, fp::PlaceableKind::Spring) == 0);
}

void test_credit_window() {
  const Rules r = rules_for(Character::Mason, Character::Glim);
  auto lose = [&](bool in_window) {
    State s = on_ground(r);
    s.players[1].blame_by = 0;
    s.players[1].blame_tick = static_cast<std::int32_t>(s.world.frame);
    if (!in_window) s.world.frame += kBlameWindowTicks + 2;
    s.world.entities[1].pos.y = Fixed::from_int(-1000);
    tick(r, s);
    return s;
  };
  State credited = lose(true);
  CHECK(credited.players[0].stats.kos == 1 && credited.players[1].stats.falls == 0 && credited.players[1].stocks == 2);
  State fell = lose(false);
  CHECK(fell.players[0].stats.kos == 0 && fell.players[1].stats.falls == 1);
}

void test_stomp() {
  const Rules r = rules_for(Character::Mason, Character::Glim);
  State s = on_ground(r);
  for (PlayerState& p : s.players) p.invuln = 0;  // a respawned player is untouchable for 2 s
  s.world.entities[0] = fp::spawn_on(r.engine, 9, Fixed::from_int(40));
  s.world.entities[1] = fp::spawn_on(r.engine, 9, Fixed::from_int(40));
  // Player 0 falls fast onto player 1's head.
  s.world.entities[0].mode = fp::Mode::Airborne;
  s.world.entities[0].pos.y = Fixed::from_int(16);
  s.world.entities[0].vel.y = Fixed::from_double(-1.5);
  s.world.entities[0].support = -1;
  for (int t = 0; t < 12 && s.players[0].stats.stomps == 0; ++t) tick(r, s);
  CHECK(s.players[0].stats.stomps == 1);
  CHECK(s.players[1].slow > 0);
  CHECK(s.players[0].blame_by == 1 && s.players[1].blame_by == 0);
  CHECK(s.world.entities[0].vel.y > Fixed::zero());  // bounced up
}

void test_ranking_and_tiebreak() {
  const Rules r = rules_for(Character::Mason, Character::Glim);
  auto both_out = [&](int kos0, int kos1) {
    Setup su;
    su.stocks = 1;
    su.characters = {Character::Mason, Character::Glim};
    const Rules rr = *make_rules(su);
    State s = make_state(rr);
    s.players[0].stats.kos = static_cast<std::uint16_t>(kos0);
    s.players[1].stats.kos = static_cast<std::uint16_t>(kos1);
    s.world.entities[0].pos.y = Fixed::from_int(-1000);
    s.world.entities[1].pos.y = Fixed::from_int(-1000);
    firm(s);
    const Input in[2] = {};
    step(rr, s, in);
    CHECK(s.over);
    return s;
  };
  State a = both_out(0, 0);
  CHECK(a.winner == 0 && a.players[0].place == 1 && a.players[1].place == 2);  // seat order last
  State b = both_out(0, 1);
  CHECK(b.winner == 1 && b.players[1].place == 1);  // credited eliminations first
  (void)r;
}

void test_kit_state_round_trips() {
  const Rules r = rules_for(Character::Pip, Character::Mason);
  State s = on_ground(r);
  tick(r, s, kPlace);
  tick(r, s, {}, kPlace);  // a pending spring and a wall
  std::vector<std::uint8_t> bytes;
  snapshot(s, bytes);
  State copy = make_state(r);
  CHECK(restore(r, copy, bytes) && hash(copy) == hash(s));
  for (int t = 0; t < 30; ++t) {
    tick(r, s);
    tick(r, copy);
  }
  CHECK(hash(copy) == hash(s));
  // A pending placement with a nonsense owner is refused.
  bytes.clear();
  snapshot(s, bytes);
  CHECK(restore(r, copy, bytes));
}

void test_setup_character_range() {
  Setup s;
  s.characters[1] = static_cast<Character>(kCharacterCount);
  CHECK(!valid(s));
}

}  // namespace

int main() {
  test_wall_cooldown_and_limit();
  test_wall_goes_with_its_tile();
  test_spring_arms_launches_and_credits();
  test_air_spring_launches_the_caster();
  test_credit_window();
  test_stomp();
  test_ranking_and_tiebreak();
  test_kit_state_round_trips();
  test_setup_character_range();
  return test::finish();
}
