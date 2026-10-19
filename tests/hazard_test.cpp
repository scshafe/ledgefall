// Stage hazards: comets, rockfall, the tide, the gale, and respawn immunity.
#include <vector>

#include "check.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

Rules rules_for(StageId stage) {
  Setup s;
  s.players = 2;
  s.stage = stage;
  s.characters = {Character::Pip, Character::Mason, Character::Pip, Character::Pip};
  return *make_rules(s);
}

void idle(const Rules& r, State& s, int ticks = 1) {
  for (int t = 0; t < ticks; ++t) {
    const Input in[2] = {};
    step(r, s, in);
  }
}

int count(const State& s, HazardKind k) {
  int n = 0;
  for (const HazardItem& h : s.hazards.list) n += h.kind == k ? 1 : 0;
  return n;
}

void test_stages_have_their_hazards() {
  CHECK(rules_for(StageId::Practice).hazards.empty());
  CHECK(rules_for(StageId::Yard).hazards.size() == 1 && rules_for(StageId::Yard).hazards[0].type == HazardType::Comet);
  CHECK(rules_for(StageId::Quarry).hazards[0].type == HazardType::Rockfall);
  CHECK(rules_for(StageId::Tide).hazards.size() == 2);
  CHECK(rules_for(StageId::Gale).hazards[0].type == HazardType::Gale);
}

void test_a_comet_is_announced_then_crosses() {
  const Rules r = rules_for(StageId::Yard);
  State s = make_state(r);
  bool warned = false, flew = false;
  int warn_at = -1, fly_at = -1;
  for (int t = 0; t < 900 && !flew; ++t) {
    idle(r, s);
    if (!warned && count(s, HazardKind::CometWarn) > 0) {
      warned = true;
      warn_at = t;
    }
    if (count(s, HazardKind::Comet) > 0) {
      flew = true;
      fly_at = t;
    }
  }
  CHECK(warned && flew);
  CHECK(fly_at - warn_at >= 85 && fly_at - warn_at <= 95);  // the lane warning lasts 1.5 s
  CHECK(warn_at >= 295 && warn_at <= 305);                   // the first one comes after 5 s
}

void test_a_comet_knocks_back_and_credits_whoever_steered_it() {
  const Rules r = rules_for(StageId::Yard);
  State s = make_state(r);
  for (PlayerState& p : s.players) p.invuln = 0;
  s.world.entities[1] = fp::spawn_on(r.engine, 0, Fixed::from_int(-150));
  HazardItem c;
  c.kind = HazardKind::Comet;
  c.dir = 1;
  c.pos = {Fixed::from_int(-175), s.world.entities[1].pos.y + Fixed::from_int(4)};
  c.vel = {Fixed::from_double(2.7), Fixed::zero()};
  c.redirected_by = 0;
  s.hazards.list[0] = c;
  for (int t = 0; t < 30 && s.players[1].blame_by < 0; ++t) idle(r, s);
  CHECK(s.world.entities[1].vel.x > Fixed::from_int(2));
  CHECK(s.players[1].blame_by == 0);

  // A respawned player is untouchable.
  State imm = make_state(r);
  imm.world.entities[1] = fp::spawn_on(r.engine, 0, Fixed::from_int(-150));
  imm.hazards.list[0] = c;
  const Fixed before = imm.world.entities[1].vel.x;
  for (int t = 0; t < 30; ++t) idle(r, imm);
  CHECK(imm.players[1].blame_by == -1);
  (void)before;
}

void test_a_wall_reflects_a_comet_and_is_spent() {
  const Rules r = rules_for(StageId::Yard);
  State s = make_state(r);
  s.world.entities[0].pos.x = Fixed::from_int(200);
  s.world.entities[1].pos.x = Fixed::from_int(-200);
  fp::Placeable w;
  w.kind = fp::PlaceableKind::Wall;
  w.rect = {Fixed::from_int(20), Fixed::from_int(27), Fixed::from_int(30), Fixed::from_int(60)};
  const int slot = fp::place(s.world, w);
  s.meta[static_cast<std::size_t>(slot)].owner = 0;
  HazardItem c;
  c.kind = HazardKind::Comet;
  c.dir = 1;
  c.pos = {Fixed::from_int(0), Fixed::from_int(45)};
  c.vel = {Fixed::from_double(2.7), Fixed::zero()};
  s.hazards.list[0] = c;
  for (int t = 0; t < 12; ++t) idle(r, s);
  const HazardItem* comet = nullptr;
  for (const HazardItem& h : s.hazards.list) comet = h.kind == HazardKind::Comet ? &h : comet;
  CHECK(comet != nullptr && comet->vel.x < Fixed::zero() && comet->redirected_by == 0);
  CHECK(s.world.placeables[static_cast<std::size_t>(slot)].active == 0);
}

void test_rocks_fall_and_smash() {
  const Rules r = rules_for(StageId::Quarry);
  State s = make_state(r);
  s.world.entities[0].pos.x = Fixed::from_int(250);
  s.world.entities[1].pos.x = Fixed::from_int(-250);
  // Warned, then dropped, over the great ledge (28th platform: 5+5+4+4+4+3+3 tiles come first).
  const int slab = 28;
  HazardItem warn;
  warn.kind = HazardKind::RockWarn;
  warn.pos = {Fixed::zero(), r.engine.stage.blast.top};
  warn.timer = 5;
  s.hazards.list[0] = warn;
  for (int t = 0; t < 8; ++t) idle(r, s);
  CHECK(count(s, HazardKind::Rock) == 1);
  for (int t = 0; t < 120 && count(s, HazardKind::Rock) > 0; ++t) idle(r, s);
  CHECK(count(s, HazardKind::Rock) == 0);

  // A rock already near the great ledge does 2 damage to its 8 hp.
  HazardItem rock;
  rock.kind = HazardKind::Rock;
  rock.pos = {Fixed::zero(), Fixed::from_int(60)};
  rock.vel = {Fixed::zero(), Fixed::from_double(-1.9)};
  s.hazards.list[1] = rock;
  for (int t = 0; t < 40; ++t) idle(r, s);
  CHECK(s.slab_hp[slab] == 6);
}

void test_the_tide_drowns_and_spares_the_immune() {
  const Rules r = rules_for(StageId::Tide);
  State s = make_state(r);
  const int low_island = 33;  // the stage's first slab, at the water line
  s.world.entities[0] = fp::spawn_on(r.engine, low_island, Fixed::from_int(-180));
  s.world.entities[1] = fp::spawn_on(r.engine, low_island, Fixed::from_int(-170));
  for (PlayerState& p : s.players) p.invuln = 0;
  s.players[1].invuln = 100;
  s.hazards.tide = TidePhase::Hold;
  s.hazards.tide_timer = 100;
  s.hazards.tide_level = r.hazards[0].high;
  const Fixed feet = s.world.entities[0].pos.y;
  CHECK(feet < r.hazards[0].high);  // the low islands are under the high tide
  idle(r, s);
  CHECK(s.players[0].stocks == 2 && s.players[0].stats.falls == 1);
  CHECK(s.players[1].stocks == 3);
}

void test_the_gale_pushes() {
  const Rules r = rules_for(StageId::Gale);
  State s = make_state(r);
  for (PlayerState& p : s.players) p.invuln = 0;
  s.hazards.gale = GalePhase::Blow;
  s.hazards.gale_dir = 1;
  s.hazards.gale_timer = 100;
  s.hazards.timers.fill(1000);
  s.world.entities[0].pos = {Fixed::from_int(-100), Fixed::from_int(120)};
  s.world.entities[1].pos = {Fixed::from_int(100), Fixed::from_int(120)};
  idle(r, s, 20);
  CHECK(s.world.entities[0].vel.x > Fixed::from_double(0.5));
  idle(r, s, 100);
  CHECK(s.hazards.gale == GalePhase::Off);
}

void test_hazard_state_round_trips() {
  const Rules r = rules_for(StageId::Tide);
  State s = make_state(r);
  idle(r, s, 340);  // past the first comet warning and into the stage's rhythm
  std::vector<std::uint8_t> bytes;
  snapshot(s, bytes);
  State copy = make_state(r);
  CHECK(restore(r, copy, bytes) && hash(copy) == hash(s));
  idle(r, s, 200);
  idle(r, copy, 200);
  CHECK(hash(copy) == hash(s));
}

void test_every_stage_with_bots_ends() {
  // Not a bot test: players that stand still lose to the stage on every layout, hazards and all.
  for (int id = 1; id < kStageCount; ++id) {
    Setup su;
    su.stage = static_cast<StageId>(id);
    su.players = 3;
    const Rules r = *make_rules(su);
    State s = make_state(r);
    std::vector<Input> none(3);
    for (int t = 0; t < 8000 && !s.over; ++t) step(r, s, none);
    CHECK(s.over);
  }
}

}  // namespace

int main() {
  test_stages_have_their_hazards();
  test_a_comet_is_announced_then_crosses();
  test_a_comet_knocks_back_and_credits_whoever_steered_it();
  test_a_wall_reflects_a_comet_and_is_spent();
  test_rocks_fall_and_smash();
  test_the_tide_drowns_and_spares_the_immune();
  test_the_gale_pushes();
  test_hazard_state_round_trips();
  test_every_stage_with_bots_ends();
  return test::finish();
}
