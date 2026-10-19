// Vertical contact between players (lost-footing, "Vertical contact"). The
// engine decides who lands on whom and lets a gentle landing ride on a head; a
// fast one is a stomp (or, from below, a bump): the upper body bounces up and
// gets its air jump back, a grounded lower body is slowed and its tile takes
// stress, an airborne one is pushed down. Both are credited contact.
#include "internal.hpp"

namespace ledgefall::detail {
namespace {

using fp::Fixed;

// Closing speed from which contact is a stomp: 30% of the fall speed cap
// (lost-footing: 4 of a 13 px/tick cap).
const Fixed kStompSpeed = fx100(45);
const Fixed kBounceBase = fx100(60);
const Fixed kBounceScale = fx100(40);
const Fixed kBounceMax = fx100(190);
const Fixed kPushShare = fx100(75);
const Fixed kStompStress = fx100(25);
constexpr int kSlowTicks = 30;

}  // namespace

void blame(State& state, int victim, int by) {
  if (by < 0 || by == victim) return;
  PlayerState& p = state.players[static_cast<std::size_t>(victim)];
  p.blame_by = static_cast<std::int8_t>(by);
  p.blame_tick = static_cast<std::int32_t>(state.world.frame);
}

void vertical_contacts(const Rules& rules, State& state, const fp::Events& events,
                       const std::vector<fp::Vec2>& vel_before, fp::Events& scratch) {
  for (const fp::Event& ev : events) {
    int upper, lower;
    if (ev.type == fp::EventType::LandedOnEntity) {
      upper = ev.id;
      lower = ev.other;
    } else if (ev.type == fp::EventType::BumpedEntity) {
      upper = ev.other;
      lower = ev.id;
    } else {
      continue;
    }
    const auto u = static_cast<std::size_t>(upper), l = static_cast<std::size_t>(lower);
    // y is up: falling is negative. The closing speed is how much faster the upper body goes down.
    const Fixed closing = vel_before[l].y - vel_before[u].y;
    if (closing < kStompSpeed) continue;  // gentle: the engine lets it ride

    fp::EntityState& up = state.world.entities[u];
    fp::EntityState& low = state.world.entities[l];
    Fixed bounce = kBounceBase + kBounceScale * closing;
    if (bounce > kBounceMax) bounce = kBounceMax;

    const bool low_grounded = low.mode != fp::Mode::Airborne;
    if (up.mode != fp::Mode::Airborne) fp::start_fall(up);
    up.vel.y = low.vel.y + bounce;
    up.jumps_used = 0;
    up.fast_falling = 0;
    if (low_grounded) {
      state.players[l].slow = kSlowTicks;
      if (low.support >= 0 && low.support < static_cast<int>(rules.engine.stage.platforms.size())) {
        fp::add_stress(rules.engine, state.world, low.support, kStompStress, scratch);
      }
    } else {
      const Fixed pushed = vel_before[u].y * kPushShare;
      if (pushed < low.vel.y) low.vel.y = pushed;
    }
    const bool stomp = vel_before[u].y < Fixed::zero();
    if (stomp) ++state.players[u].stats.stomps;
    blame(state, lower, upper);
    blame(state, upper, lower);
  }
}

}  // namespace ledgefall::detail
