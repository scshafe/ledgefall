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

// Hurt a tile: a crumble tile breaks outright, a sturdy slab loses `amount` hit
// points and breaks at 0 (back after its respawn time, whole). `by` is credited
// (-1 for the stage itself).
void damage_tile(const Rules& rules, State& state, int tile, int amount, int by, fp::Events& scratch);

// Whether a stage platform is up (solid, shaking or doomed).
bool in_play(fp::PlatformPhase p);

// The box an object is drawn (and, for a gust, felt) in.
fp::Rect object_rect(const Object& o);

bool cast_bumper(State& state, const Rules& rules, int seat, fp::Events& events);
bool cast_saw(State& state, const Rules& rules, int seat);

bool cast_thorns(State& state, const Rules& rules, int seat);
bool cast_roots(State& state, const Rules& rules, int seat);
bool cast_rub(State& state, const Rules& rules, int seat);

// A tile the entity stands on (a stage platform), or -1.
int tile_under_feet(const Rules& rules, const fp::EntityState& e);

bool cast_anvil(State& state, const Rules& rules, int seat, const Input& in, bool ingot = false);
// Mason's ledge, Dab's decoy and hollow (one-way placeables), and the respawn halo.
bool cast_ledge(State& state, const Rules& rules, int seat, fp::Events& events);
bool cast_decoy(State& state, const Rules& rules, int seat, const Input& in, fp::Events& events);
bool cast_vine(State& state, const Rules& rules, int seat);
bool cast_hollow(State& state, const Rules& rules, int seat);
void arm_hollow(const Rules& rules, State& state, const Pending& p, fp::Events& scratch);
// A personal platform for a player who has just come into play at `at` (the player's feet are put on it).
void place_halo(const Rules& rules, State& state, int seat, const fp::Vec2& at);
// Per tick, after the engine has stepped: halos go when stepped off, decoys fool, a ledge takes damage.
void oneway_upkeep(const Rules& rules, State& state, const std::vector<fp::Vec2>& pos_before, fp::Events& scratch);
// Hit a ledge or leaf (hit points in `uses`); gone at 0.
void damage_ledge(const Rules& rules, State& state, int slot, int amount, fp::Events& scratch);

bool cast_lodestone(State& state, const Rules& rules, int seat);
bool cast_polarity(State& state, const Rules& rules, int seat);

// Keep at most `max` placeables of a kind per owner: the oldest goes.
void limit_owned(const Rules& rules, State& state, int seat, fp::PlaceableKind kind, int max, fp::Events& events,
                 std::uint8_t tag = 0);

// Register a placeable the engine has given a slot.
void register_placed(State& state, int slot, int seat, std::uint8_t uses, std::int16_t support, std::uint8_t tag = 0);

// Glim's lobs. Throw one (payload carried), and per tick fly them and run puddles.
bool throw_lob(State& state, const Rules& rules, int seat, Payload payload, const Input& in);
void update_objects(const Rules& rules, State& state, fp::Events& scratch);

// Where a lob thrown by `seat`, facing `facing` with `in`, starts and how fast it goes.
void lob_launch(const Rules& rules, const State& state, int seat, int facing, const Input& in, fp::Vec2& pos, fp::Vec2& vel);

// Where a lob from `pos` at `vel` lands: the x, and the top of the surface (the tile or wall it lands on).
// False if it falls out of the world. Ignores gusts, lodestones and walls' sides.
bool predict_lob(const Rules& rules, const State& state, fp::Vec2 pos, fp::Vec2 vel, fp::Fixed& land_x, fp::Fixed& land_top);

bool cast_gust(const Rules& rules, State& state, int seat);
bool cast_drill(State& state, int seat);

// Pip's gust and drill, before the engine steps: the wind pushes whoever is in
// it, a dive overrides the dive's own movement. `intents` may be changed.
void apply_wind_and_drill(const Rules& rules, State& state, std::vector<fp::Intent>& intents);

// Pip's drill lands (from the engine's Landed events), after the engine has stepped.
void drill_landings(const Rules& rules, State& state, const fp::Events& events, fp::Events& scratch);

// The match's random stream: an integer in [lo, hi].
std::uint32_t next_random(State& s);
int rand_int(State& s, int lo, int hi);

// Stage hazards: fire on their rhythm, warn, act. After the engine has stepped.
void update_hazards(const Rules& rules, State& state, fp::Events& scratch);

// What slows or hampers a player this tick, as the engine's per-entity modifier: a stomp, standing in
// glue (jump x0.55) or in brambles (accel x0.5, jump x0.8). Before the engine steps.
void apply_modifiers(State& state);

// Stage wind (a gale), before the engine steps.
void apply_gale(const Rules& rules, State& state);

// The conveyors: set each tile's belt speed for this tick (running one way, stopped, or the other way).
void apply_belts(const Rules& rules, State& state);

// Whether the tide has swallowed a player standing at (x, y).
bool drowned(const State& state, const fp::EntityState& e);

// The shortening of hazard gaps in overtime (percent of the normal gap).
int hazard_percent(std::uint32_t tick);

// Per tick, before the engine steps: pending placements arm and appear.
void arm_pending(const Rules& rules, State& state, fp::Events& events);

// Per tick, after the engine has stepped: spring uses, expired and unsupported placeables.
void after_step(const Rules& rules, State& state, const fp::Events& events, fp::Events& scratch);

// Tiles that crumbled (in the engine's events, or in ones our own stress caused)
// count as broken for whoever stressed them last and credit whoever they drop.
void credit_crumbles(const Rules& rules, State& state, const fp::Events& events, const std::vector<std::int16_t>& support_before);

// Contact between players (stomps and bumps), from the engine's events and the
// velocities before the step.
void vertical_contacts(const Rules& rules, State& state, const fp::Events& events,
                       const std::vector<fp::Vec2>& vel_before, fp::Events& scratch);

}  // namespace ledgefall::detail
