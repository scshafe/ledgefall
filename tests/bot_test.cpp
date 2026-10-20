// Bots: a match of bots ends with a winner, is reproducible, and bots outlast
// someone standing still.
#include <cstdio>
#include <vector>

#include "check.hpp"
#include "halos.hpp"
#include "ledgefall/bot.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

struct Run {
  bool over = false;
  int winner = -1;
  int ticks = 0;
  std::uint64_t hash = 0;
};

// Seats 0.. are bots of the given level; `idle` seats (the last ones) send nothing.
Run play(int players, int idle, BotLevel level, int stocks, int max_ticks) {
  const auto rules = make_rules({players, stocks});
  State s = make_state(*rules);
  std::vector<Bot> bots(static_cast<std::size_t>(players));
  for (Bot& b : bots) b.level = level;
  std::vector<Input> in(static_cast<std::size_t>(players));
  Run r;
  while (!s.over && r.ticks < max_ticks) {
    for (int i = 0; i < players - idle; ++i) in[static_cast<std::size_t>(i)] = bot_input(*rules, s, i, bots[static_cast<std::size_t>(i)]);
    step(*rules, s, in);
    ++r.ticks;
  }
  r.over = s.over;
  r.winner = s.winner;
  r.hash = hash(s);
  return r;
}

void test_bots_beat_standing_still() {
  for (BotLevel level : {BotLevel::Easy, BotLevel::Normal, BotLevel::Hard}) {
    const Run r = play(2, 1, level, 3, 3 * 60 * 60);
    CHECK(r.over && r.winner == 0);
  }
}

void test_bot_matches_end_with_a_winner() {
  for (BotLevel level : {BotLevel::Easy, BotLevel::Normal, BotLevel::Hard}) {
    const Run r = play(4, 0, level, 3, 6 * 60 * 60);  // lost-footing: within six minutes
    CHECK(r.over && r.winner >= 0);
  }
}

// A bot with each character, against someone who stands still, uses its kit.
void test_every_kit_is_used() {
  for (int c = 0; c < kCharacterCount; ++c) {
    Setup su;
    su.players = 2;
    su.stocks = 5;
    su.characters = {static_cast<Character>(c), Character::Mason};
    const auto rules = make_rules(su);
    State s = make_state(*rules);
    Bot bot;
    bot.level = BotLevel::Hard;
    std::vector<Input> in(2);
    for (int t = 0; t < 4 * 3600 && !s.over; ++t) {
      in[0] = bot_input(*rules, s, 0, bot);
      step(*rules, s, in);
    }
    CHECK(s.players[0].stats.placed > 0);
    if (s.players[0].stats.placed == 0) std::fprintf(stderr, "character %d never cast\n", c);
  }
}

// A State with a lone bot at `seat` 0 and an idle opponent far away, tiles kept firm.
struct Arena {
  Rules rules;
  State state;
  Bot bot;
  std::vector<Input> in{2};
  explicit Arena(Setup su, BotLevel level = BotLevel::Hard) : rules(*make_rules(su)), state(make_state(rules)) {
    bot.level = level;
    for (PlayerState& p : state.players) p.invuln = 0;
    state.world.entities[1].pos = {Fixed::from_int(3000), Fixed::from_int(3000)};  // out of the way (frozen: eliminated)
    state.world.entities[1].eliminated = 1;
    state.players[1].respawn_timer = 30000;
  }
  void tick(bool firm = true) {
    if (firm) {
      for (fp::PlatformState& p : state.world.platforms) {
        if (p.phase == fp::PlatformPhase::Solid || p.phase == fp::PlatformPhase::Shaking) {
          p.phase = fp::PlatformPhase::Solid;
          p.stress = Fixed::zero();
        }
      }
    }
    in[0] = bot_input(rules, state, 0, bot);
    step(rules, state, in);
  }
};

Setup yard_for(Character c) {
  Setup su;
  su.stage = StageId::Yard;
  su.characters = {c, Character::Mason};
  su.stocks = 5;
  return su;
}

// Falling out of the stage, a bot uses its character's recovery move and gets back on.
void test_bots_recover() {
  for (Character c : {Character::Pip, Character::Fuse, Character::Mason, Character::Dab}) {
    Arena a(yard_for(c));
    clear_halos(a.rules, a.state);
    a.state.world.entities[0].pos = {Fixed::from_int(-120), Fixed::from_int(-12)};  // below the bottom row's end, falling
    a.state.world.entities[0].vel = {Fixed::zero(), Fixed::from_double(-1.4)};
    a.state.world.entities[0].mode = fp::Mode::Airborne;
    a.state.world.entities[0].support = -1;
    for (int t = 0; t < 40; ++t) a.tick();
    CHECK(a.state.players[0].stats.placed >= 1);
    if (a.state.players[0].stats.placed == 0) std::fprintf(stderr, "character %d did not recover with a move\n", static_cast<int>(c));
  }
  // A character without a recovery move still flies back towards the stage and uses its jumps (Glim has two).
  Arena g(yard_for(Character::Glim));
  clear_halos(g.rules, g.state);
  g.state.world.entities[0].pos = {Fixed::from_int(-120), Fixed::from_int(-10)};
  g.state.world.entities[0].vel = {Fixed::zero(), Fixed::from_double(-1.0)};
  g.state.world.entities[0].mode = fp::Mode::Airborne;
  g.state.world.entities[0].support = -1;
  Fixed best = g.state.world.entities[0].pos.y;
  for (int t = 0; t < 30; ++t) {
    g.tick();
    if (g.state.world.entities[0].pos.y > best) best = g.state.world.entities[0].pos.y;
  }
  CHECK(best > Fixed::from_int(-4));  // it climbed
}

// A comet in the bot's row: a bot that sees it jumps; one that does nothing is knocked back.
void test_bots_jump_comets() {
  auto knocked = [&](bool bot_plays) {
    Arena a(yard_for(Character::Mason));
    clear_halos(a.rules, a.state);
    a.state.world.entities[0] = fp::spawn_on(a.rules.engine, 0, a.rules.engine.stage.platforms[0].rect.left + Fixed::from_int(10));
    a.bot.wait = 30000;  // an int16: no new decisions, so no footing changes: stand and watch
    a.bot.target = 0;
    HazardItem c;
    c.kind = HazardKind::Comet;
    c.dir = 1;
    c.pos = {a.state.world.entities[0].pos.x - Fixed::from_int(60), a.state.world.entities[0].pos.y + Fixed::from_int(6)};
    c.vel = {Fixed::from_double(2.7), Fixed::zero()};
    a.state.hazards.list[0] = c;
    bool hit = false;
    for (int t = 0; t < 60; ++t) {
      if (bot_plays) a.tick();
      else {
        a.in[0] = {};
        step(a.rules, a.state, a.in);
      }
      hit = hit || fp::abs(a.state.world.entities[0].vel.x) > Fixed::from_int(3);
    }
    return hit;
  };
  CHECK(knocked(false));   // the control: standing still, it is hit
  CHECK(!knocked(true));   // the bot jumped it
}

// A rock is announced above the bot: it leaves the column.
void test_bots_leave_a_rocks_column() {
  Setup su = yard_for(Character::Mason);
  su.stage = StageId::Quarry;
  Arena a(su);
  clear_halos(a.rules, a.state);
  const std::size_t slab = 28;  // the great ledge
  a.state.world.entities[0] = fp::spawn_on(a.rules.engine, static_cast<int>(slab), Fixed::zero());
  a.bot.wait = 30000;  // an int16: no new decisions for the test
  a.bot.target = static_cast<std::int16_t>(slab);
  HazardItem warn;
  warn.kind = HazardKind::RockWarn;
  warn.pos = {Fixed::zero(), a.rules.engine.stage.blast.top};
  warn.timer = 70;
  a.state.hazards.list[0] = warn;
  for (int t = 0; t < 40; ++t) a.tick();
  CHECK(fp::abs(a.state.world.entities[0].pos.x) > Fixed::from_int(14));
  for (int t = 0; t < 120; ++t) a.tick();
  CHECK(a.state.players[0].stocks == 5);
}

// With the tide in, a bot does not go for a surface under water.
void test_bots_keep_above_the_tide() {
  Setup su = yard_for(Character::Mason);
  su.stage = StageId::Tide;
  Arena a(su);
  clear_halos(a.rules, a.state);
  const HazardSpec* tide = nullptr;
  for (const HazardSpec& h : a.rules.hazards) tide = h.type == HazardType::Tide ? &h : tide;
  a.state.hazards.timers.fill(100000);
  a.state.hazards.tide = TidePhase::Hold;
  a.state.hazards.tide_timer = 400;
  a.state.hazards.tide_level = tide->high;
  const int island = 33;  // a low stone island, under the high tide
  a.state.world.entities[0] = fp::spawn_on(a.rules.engine, island, Fixed::from_int(-180));
  a.state.players[0].invuln = 100000;  // see where it goes, not whether it drowns
  for (int t = 0; t < 120; ++t) a.tick();
  CHECK(a.state.world.entities[0].pos.y > tide->high);  // it climbed out of the water
}

// With a gale blowing a bot on the downwind side leans into it; with none, it stays put.
void test_bots_lean_into_the_gale() {
  Setup su = yard_for(Character::Mason);
  su.stage = StageId::Gale;
  Arena a(su);
  clear_halos(a.rules, a.state);
  const auto& platforms = a.rules.engine.stage.platforms;
  std::size_t far = 0;
  for (std::size_t i = 0; i < platforms.size(); ++i) {
    if (platforms[i].rect.right > platforms[far].rect.right) far = i;
  }
  a.state.world.entities[0] = fp::spawn_on(a.rules.engine, static_cast<int>(far), (platforms[far].rect.left + platforms[far].rect.right) / Fixed::from_int(2));
  a.bot.wait = 30000;  // an int16: no new decisions for the test
  a.bot.target = static_cast<std::int16_t>(far);  // content to stay where it is
  a.state.hazards.timers.fill(10000);
  CHECK(bot_input(a.rules, a.state, 0, a.bot).move_x == 0);  // no gale: no reason to move
  a.state.hazards.gale = GalePhase::Blow;
  a.state.hazards.gale_dir = 1;
  a.state.hazards.gale_timer = 100;
  CHECK(bot_input(a.rules, a.state, 0, a.bot).move_x < 0);  // blowing right: lean left
  a.state.hazards.gale_dir = -1;
  CHECK(bot_input(a.rules, a.state, 0, a.bot).move_x >= 0);  // it blows away from this side: nothing to resist
}

// On a belt that runs off the end of its row, a bot at the end walks against it instead of going with it.
void test_bots_hold_against_a_belt() {
  Setup su = yard_for(Character::Mason);
  su.stage = StageId::Belts;
  Arena a(su);
  clear_halos(a.rules, a.state);
  const std::size_t end = 6;  // the right end of the first row, whose belt runs right
  CHECK(a.rules.belts[end] > Fixed::zero());
  const fp::Rect& r = a.rules.engine.stage.platforms[end].rect;
  a.state.world.entities[0] = fp::spawn_on(a.rules.engine, static_cast<int>(end), r.right - Fixed::from_int(4));
  a.state.hazards.timers.fill(10000);
  a.bot.wait = 30000;  // an int16: no new decisions for the test
  a.bot.target = 7;  // a tile on the far side of the stage: it wants to go right
  CHECK(bot_input(a.rules, a.state, 0, a.bot).move_x < 0);
  // A bot that misses hazards (blind) just goes where it was heading.
  a.bot.blind = true;
  CHECK(bot_input(a.rules, a.state, 0, a.bot).move_x > 0);
}

// Easy bots miss hazards sometimes; hard ones never do.
void test_miss_rates() {
  CHECK(hazard_miss_percent(BotLevel::Easy) == 45 && hazard_miss_percent(BotLevel::Normal) == 15 && hazard_miss_percent(BotLevel::Hard) == 0);
}

void test_bot_matches_are_reproducible() {
  const Run a = play(4, 0, BotLevel::Normal, 3, 6 * 60 * 60);
  const Run b = play(4, 0, BotLevel::Normal, 3, 6 * 60 * 60);
  CHECK(a.hash == b.hash && a.ticks == b.ticks && a.winner == b.winner);
}

}  // namespace

int main() {
  test_bots_beat_standing_still();
  test_bot_matches_end_with_a_winner();
  test_every_kit_is_used();
  test_bots_recover();
  test_bots_jump_comets();
  test_bots_leave_a_rocks_column();
  test_bots_keep_above_the_tide();
  test_bots_lean_into_the_gale();
  test_bots_hold_against_a_belt();
  test_miss_rates();
  test_bot_matches_are_reproducible();
  return test::finish();
}
