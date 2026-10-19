#include "ledgefall/match.hpp"

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

// mulberry32: the match's random stream, in the state so rollback restores it.
std::uint32_t next_random(State& s) {
  s.rng += 0x6D2B79F5u;
  std::uint32_t t = s.rng;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return t ^ (t >> 14);
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

  // Collapse: a random tile still standing is doomed every half second.
  if (collapse && tick % static_cast<std::uint32_t>(kCollapseDoomTicks) == 0) {
    const std::size_t n = state.world.platforms.size();
    const std::size_t start = next_random(state) % n;
    for (std::size_t k = 0; k < n; ++k) {
      const std::size_t i = (start + k) % n;
      const fp::PlatformState& p = state.world.platforms[i];
      if (p.phase != fp::PlatformPhase::Solid && p.phase != fp::PlatformPhase::Shaking) continue;
      const fp::PlatformStress& st = rules.engine.stage.platforms[i].stress;
      if (p.stress < st.doom_at) fp::add_stress(rules.engine, state.world, static_cast<int>(i), st.doom_at - p.stress, scratch);
      break;
    }
  }
}

}  // namespace

State make_state(const Rules& rules) {
  State s;
  s.rng = rules.setup.seed;
  s.world = fp::make_world(rules.engine);
  const int n = rules.setup.players;
  for (int i = 0; i < n; ++i) {
    // Spread along the spawn line: n = 2 gives the ends, n = 4 the four points.
    const Fixed x = Fixed::from_int(-60 + 120 * i / (n - 1));
    s.world.entities.push_back(spawn_at({x, rules.spawns[0].y}));
    s.players.push_back({static_cast<std::int8_t>(rules.setup.stocks), 0});
  }
  return s;
}

void step(const Rules& rules, State& state, std::span<const Input> inputs) {
  if (state.over) return;
  const std::size_t n = state.players.size();

  std::vector<fp::Intent> intents(n);
  for (std::size_t i = 0; i < n && i < inputs.size(); ++i) intents[i] = to_intent(inputs[i]);
  fp::Events events;
  fp::step(rules.engine, state.world, intents, events);
  overtime(rules, state, events);

  // Players eliminated this tick lose a stock; the others' respawn clocks run.
  std::vector<int> lost_last;
  for (std::size_t i = 0; i < n; ++i) {
    PlayerState& p = state.players[i];
    if (p.respawn_timer > 0) {
      if (--p.respawn_timer == 0) state.world.entities[i] = spawn_at(farthest_spawn(rules, state, static_cast<int>(i)));
    } else if (state.world.entities[i].eliminated != 0 && p.stocks > 0) {
      if (--p.stocks > 0) {
        p.respawn_timer = kRespawnDelayTicks;
      } else {
        lost_last.push_back(static_cast<int>(i));
      }
    }
  }

  int standing = 0;
  int last_seat = -1;
  for (std::size_t i = 0; i < n; ++i) {
    if (state.players[i].stocks > 0) {
      ++standing;
      last_seat = static_cast<int>(i);
    }
  }
  if (standing <= 1) {
    state.over = true;
    // Several players out on the same tick: the tie-break that needs credit and
    // platform counts arrives with placeables; until then the lowest seat wins.
    state.winner = static_cast<std::int8_t>(standing == 1 ? last_seat : (lost_last.empty() ? -1 : lost_last.front()));
  }
}

}  // namespace ledgefall
