#pragma once
// Shared between the core's sources; not part of its interface.

#include "ledgefall/match.hpp"

namespace ledgefall::detail {

// A value in hundredths: fx100(350) is 3.5. Lost-footing's pixels become this
// world's units at about 0.32 (its 38 px body is the engine's 12-unit body; its
// 140 px jump the engine's 35).
constexpr fp::Fixed fx100(int hundredths) {
  return fp::Fixed::from_raw(static_cast<std::int32_t>(static_cast<std::int64_t>(hundredths) * fp::Fixed::kOneRaw / 100));
}

// The movement table of a character: fighter-physics' coefficients as data.
fp::MovementParams movement_for(Character c);

// The ticks a move waits after it has been cast (lost-footing's cooldowns).
int cooldown_ticks(Character c, int slot);

// Record that `by` just did something indirect to `victim` (its spring launched
// them, its stomp landed on them). If the victim is eliminated within
// kBlameWindowTicks, `by` is credited.
void blame(State& state, int victim, int by);

// Try to cast move `slot` of the seat's character. False if the move cannot
// happen now (then no cooldown is spent).
bool cast(const Rules& rules, State& state, int seat, int slot, const Input& in, fp::Events& events);

// Put stress on a tile on behalf of a player: the tile remembers who, so
// whoever it drops is credited to them.
void stress_tile(const Rules& rules, State& state, int tile, fp::Fixed amount, int by, fp::Events& scratch);

// Keep at most `max` placeables of a kind per owner: the oldest goes.
void limit_owned(const Rules& rules, State& state, int seat, fp::PlaceableKind kind, int max, fp::Events& events);

// Register a placeable the engine has given a slot.
void register_placed(State& state, int slot, int seat, std::uint8_t uses, std::int16_t support);

// Glim's lobs. Throw one (payload carried), and per tick fly them and run puddles.
bool throw_lob(State& state, const Rules& rules, int seat, Payload payload, const Input& in);
void update_objects(const Rules& rules, State& state, fp::Events& scratch);

// Per tick, before the engine steps: pending placements arm and appear.
void arm_pending(const Rules& rules, State& state, fp::Events& events);

// Per tick, after the engine has stepped: spring uses, expired and unsupported placeables.
void after_step(const Rules& rules, State& state, const fp::Events& events,
                const std::vector<std::int16_t>& support_before, fp::Events& scratch);

// Contact between players (stomps and bumps), from the engine's events and the
// velocities before the step.
void vertical_contacts(const Rules& rules, State& state, const fp::Events& events,
                       const std::vector<fp::Vec2>& vel_before, fp::Events& scratch);

}  // namespace ledgefall::detail
