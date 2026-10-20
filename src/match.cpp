#include <algorithm>

#include "internal.hpp"

namespace ledgefall {
namespace {

using fp::Fixed;

fp::EntityState spawn_at(const fp::Vec2& at) {
  fp::EntityState e;
  e.pos = at;
  e.mode = fp::Mode::Airborne;
  e.frames = 1;
  return e;
}

// The spawn point farthest from the others still in play (the largest
// distance to the nearest of them; ties go to the lowest index).
const fp::Vec2& farthest_spawn(const Rules& rules, const State& state, int seat) {
  std::size_t best = 0;
  Fixed best_gap = Fixed::from_int(-1);
  for (std::size_t i = 0; i < rules.spawns.size(); ++i) {
    Fixed gap = Fixed::from_int(1 << 14);
    for (std::size_t s = 0; s < state.players.size(); ++s) {
      if (static_cast<int>(s) == seat || state.world.entities[s].eliminated != 0) continue;
      const Fixed d = fp::abs(state.world.entities[s].pos.x - rules.spawns[i].x);
      if (d < gap) gap = d;
    }
    if (gap > best_gap) {
      best_gap = gap;
      best = i;
    }
  }
  return rules.spawns[best];
}

fp::Intent to_intent(const Input& in) {
  fp::Intent i;
  i.move_x = Fixed::from_raw(static_cast<std::int32_t>(in.move_x) * Fixed::kOneRaw / kAxisMax);
  i.jump = (in.buttons & kButtonJump) != 0;
  i.fast_fall = (in.buttons & kButtonDown) != 0;
  return i;
}

// Overtime and collapse, after the engine has stepped.
void overtime(const Rules& rules, State& state, const fp::Events& events) {
  const std::uint32_t tick = state.world.frame;
  if (tick < static_cast<std::uint32_t>(kOvertimeStartTick)) return;
  const bool collapse = tick >= static_cast<std::uint32_t>(kCollapseStartTick);
  const std::int32_t level = static_cast<std::int32_t>((tick - kOvertimeStartTick) / kOvertimeStepTicks) + 1;

  // A tile that broke this tick comes back later, or never.
  fp::Events scratch;
  for (const fp::Event& e : events) {
    if (e.type != fp::EventType::PlatformCrumbled) continue;
    if (collapse) {
      fp::destroy_platform(rules.engine, state.world, e.id, scratch);
    } else {
      fp::PlatformState& p = state.world.platforms[static_cast<std::size_t>(e.id)];
      p.timer = p.timer * (2 + level) / 2;
    }
  }

  // Collapse: every sturdy slab loses a hit point every 2 s.
  if (collapse && tick % static_cast<std::uint32_t>(2 * kTicksPerSecond) == 0) {
    for (std::size_t i = 0; i < rules.slab_hp.size(); ++i) {
      if (rules.slab_hp[i] != 0) detail::damage_tile(rules, state, static_cast<int>(i), 1, -1, scratch);
    }
  }

  // Collapse: a random tile still standing is doomed every half second.
  if (collapse && tick % static_cast<std::uint32_t>(kCollapseDoomTicks) == 0) {
    const std::size_t n = state.world.platforms.size();
    const std::size_t start = detail::next_random(state) % n;
    for (std::size_t k = 0; k < n; ++k) {
      const std::size_t i = (start + k) % n;
      const fp::PlatformState& p = state.world.platforms[i];
      if (p.phase != fp::PlatformPhase::Solid && p.phase != fp::PlatformPhase::Shaking) continue;
      if (rules.slab_hp[i] != 0) continue;  // slabs are worn down, not doomed
      const fp::PlatformStress& st = rules.engine.stage.platforms[i].stress;
      if (p.stress < st.doom_at) fp::add_stress(rules.engine, state.world, static_cast<int>(i), st.doom_at - p.stress, scratch);
      break;
    }
  }
}
// The order players placed in: the survivors first, then the eliminated, the
// last out first. Ties (the same tick, or survivors together) go to credited
// eliminations, then fewer self-eliminations, then more platforms broken, then
// the lower seat (lost-footing's tieBreak).
int side_of(const Rules& rules, int seat) {
  const auto t = rules.setup.teams[static_cast<std::size_t>(seat)];
  return t >= 0 ? t : 100 + seat;
}

void rank(State& state) {
  const std::size_t n = state.players.size();
  auto better = [&](int a, int b) {
    const Stats& x = state.players[static_cast<std::size_t>(a)].stats;
    const Stats& y = state.players[static_cast<std::size_t>(b)].stats;
    if (x.kos != y.kos) return x.kos > y.kos;
    if (x.falls != y.falls) return x.falls < y.falls;
    const int bx = x.tiles_broken + x.slabs_broken, by = y.tiles_broken + y.slabs_broken;
    if (bx != by) return bx > by;
    return a < b;
  };
  std::vector<int> standing, out;
  for (std::size_t i = 0; i < n; ++i) (state.players[i].stocks > 0 ? standing : out).push_back(static_cast<int>(i));
  std::sort(standing.begin(), standing.end(), better);
  std::sort(out.begin(), out.end(), [&](int a, int b) {
    const auto ka = state.players[static_cast<std::size_t>(a)].ko_tick, kb = state.players[static_cast<std::size_t>(b)].ko_tick;
    return ka != kb ? ka > kb : better(a, b);
  });
  std::int8_t place = 1;
  for (int seat : standing) state.players[static_cast<std::size_t>(seat)].place = place++;
  for (int seat : out) state.players[static_cast<std::size_t>(seat)].place = place++;
  state.winner = static_cast<std::int8_t>(!standing.empty() ? standing.front() : out.front());
}

// A player leaves the stage: a stock lost, to whoever is credited or to nobody.
void eliminate(const Rules& rules, State& state, std::size_t i) {
  PlayerState& p = state.players[i];
  int by = p.blame_by >= 0 && static_cast<std::int32_t>(state.world.frame) - p.blame_tick <= kBlameWindowTicks ? p.blame_by : -1;
  if (by >= 0 && same_side(rules, by, static_cast<int>(i))) by = -1;  // friendly fire earns no credit
  --p.stocks;
  ++p.stats.deaths;
  if (by >= 0) ++state.players[static_cast<std::size_t>(by)].stats.kos;
  else ++p.stats.falls;
  p.blame_by = -1;
  p.ko_tick = static_cast<std::int32_t>(state.world.frame);
  if (p.stocks > 0) p.respawn_timer = kRespawnDelayTicks;
}

}  // namespace

bool same_side(const Rules& rules, int a, int b) { return side_of(rules, a) == side_of(rules, b); }

int winning_team(const Rules& rules, const State& state) {
  if (!state.over || state.winner < 0) return -1;
  return rules.setup.teams[static_cast<std::size_t>(state.winner)];
}

State make_state(const Rules& rules) {
  State s;
  s.rng = rules.setup.seed;
  s.world = fp::make_world(rules.engine);
  const int n = rules.setup.players;
  for (std::size_t t = 0; t < rules.slab_hp.size(); ++t) s.slab_hp[t] = static_cast<std::int8_t>(rules.slab_hp[t]);
  for (std::size_t t = 0; t < rules.start_clock.size(); ++t) s.world.platforms[t].clock = rules.start_clock[t];
  for (std::size_t h = 0; h < rules.hazards.size(); ++h) s.hazards.timers[h] = static_cast<std::int16_t>(300 + 90 * h);
  for (const HazardSpec& h : rules.hazards) {
    if (h.type == HazardType::Tide) s.hazards.tide_level = h.low;
  }
  for (int i = 0; i < n; ++i) {
    s.world.entities.push_back(spawn_at(rules.spawns[static_cast<std::size_t>(i)]));
    detail::place_halo(rules, s, i, rules.spawns[static_cast<std::size_t>(i)]);
    PlayerState p;
    const auto handicap = rules.setup.seat_stocks[static_cast<std::size_t>(i)];
    p.stocks = static_cast<std::int8_t>(handicap > 0 ? handicap : rules.setup.stocks);
    p.invuln = kInvulnTicks;
    s.players.push_back(p);
  }
  return s;
}

void step(const Rules& rules, State& state, std::span<const Input> inputs) {
  if (state.over) return;
  const std::size_t n = state.players.size();
  fp::Events events, scratch;
  // Who stood on what at the start of the tick: what a tile that breaks this tick (a hollow's, a cast's)
  // drops is credited with.
  std::vector<std::int16_t> support_before(n);
  for (std::size_t i = 0; i < n; ++i) support_before[i] = state.world.entities[i].support;

  // Moves: cooldowns run, a button that went down casts.
  for (std::size_t i = 0; i < n; ++i) {
    PlayerState& p = state.players[i];
    const Input in = i < inputs.size() ? inputs[i] : Input{};
    for (auto& c : p.cooldown) c = static_cast<std::int16_t>(std::max(c - 1, 0));
    if (state.world.entities[i].eliminated == 0) {
      for (int slot = 0; slot < kMoveSlots; ++slot) {
        const std::uint8_t bit = static_cast<std::uint8_t>(kButtonPlace << slot);
        if ((in.buttons & bit) == 0 || (p.prev_buttons & bit) != 0 || p.cooldown[static_cast<std::size_t>(slot)] > 0) continue;
        if (detail::cast(rules, state, static_cast<int>(i), slot, in, scratch)) {
          p.cooldown[static_cast<std::size_t>(slot)] = static_cast<std::int16_t>(detail::cooldown_ticks(rules.setup.characters[i], slot));
        }
      }
    }
    p.prev_buttons = in.buttons;
  }
  detail::arm_pending(rules, state, scratch);

  std::vector<fp::Intent> intents(n);
  for (std::size_t i = 0; i < n && i < inputs.size(); ++i) intents[i] = to_intent(inputs[i]);
  detail::apply_modifiers(state);
  detail::apply_gale(rules, state);
  detail::apply_wind_and_drill(rules, state, intents);
  std::vector<fp::Vec2> vel_before(n);
  std::vector<fp::Vec2> pos_before(n);
  for (std::size_t i = 0; i < n; ++i) {
    pos_before[i] = state.world.entities[i].pos;
    vel_before[i] = state.world.entities[i].vel;
  }

  fp::step(rules.engine, state.world, intents, events);
  for (PlayerState& p : state.players) {
    p.slow = static_cast<std::int16_t>(std::max(p.slow - 1, 0));
    p.invuln = static_cast<std::int16_t>(std::max(p.invuln - 1, 0));
    p.no_contact = static_cast<std::int16_t>(std::max(p.no_contact - 1, 0));
  }

  detail::drill_landings(rules, state, events, scratch);
  detail::update_hazards(rules, state, scratch);
  detail::vertical_contacts(rules, state, events, vel_before, scratch);
  detail::after_step(rules, state, events, scratch);
  detail::update_objects(rules, state, scratch);
  detail::oneway_upkeep(rules, state, pos_before, scratch);
  detail::credit_crumbles(rules, state, events, support_before);
  detail::credit_crumbles(rules, state, scratch, support_before);
  overtime(rules, state, events);

  // The tide takes whoever it covers (a respawned player is immune for a while).
  for (std::size_t i = 0; i < n; ++i) {
    fp::EntityState& e = state.world.entities[i];
    if (e.eliminated == 0 && state.players[i].invuln == 0 && detail::drowned(state, e)) e.eliminated = 1;
  }

  // Players eliminated this tick lose a stock; the others' respawn clocks run.
  for (std::size_t i = 0; i < n; ++i) {
    PlayerState& p = state.players[i];
    if (p.respawn_timer > 0) {
      if (--p.respawn_timer == 0) {
        const fp::Vec2& at = farthest_spawn(rules, state, static_cast<int>(i));
        state.world.entities[i] = spawn_at(at);
        p.invuln = kInvulnTicks;
        detail::place_halo(rules, state, static_cast<int>(i), at);
      }
    } else if (state.world.entities[i].eliminated != 0 && p.stocks > 0) {
      eliminate(rules, state, i);
    }
  }

  // The match ends when one side (or nobody) has anyone left.
  int sides[2 + kMaxPlayers + 100] = {};
  int standing_sides = 0;
  for (std::size_t i = 0; i < n; ++i) {
    if (state.players[i].stocks <= 0) continue;
    const int side = side_of(rules, static_cast<int>(i));
    if (sides[side]++ == 0) ++standing_sides;
  }
  if (standing_sides <= 1) {
    state.over = true;
    rank(state);
  }
}

}  // namespace ledgefall
