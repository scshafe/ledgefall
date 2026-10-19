#pragma once
// The game's rules on top of fighter-physics: a match of 2 to 4 players on
// crumbling platforms, each with a number of stocks, the last with stocks
// winning (docs/lost-footing-brief.md; lost-footing's docs/design.md, "Blast
// zones, stocks and credit").
//
// The shape is netplay-kit's Simulation: `Rules` is what a match is set up
// with and never changes, `State` is plain data that is all the simulation
// state, and `step(rules, state, inputs)` is a pure fixed-tick function of the
// state and one input per seat. `hash`, `snapshot` and `restore` cover exactly
// the State. Nothing here depends on time, rounding or container order.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "fp/world.hpp"
#include "ledgefall/input.hpp"

namespace ledgefall {

constexpr int kMinPlayers = 2;
constexpr int kMaxPlayers = 4;
constexpr int kMinStocks = 1;
constexpr int kMaxStocks = 5;
constexpr int kDefaultStocks = 3;
constexpr int kTicksPerSecond = 60;
constexpr int kRespawnDelayTicks = kTicksPerSecond * 3 / 2;  // 1.5 s after an elimination

// What a match is set up with (the lobby's choices).
struct Setup {
  int players = 2;
  int stocks = kDefaultStocks;
};

bool valid(const Setup& s) noexcept;

// Setup plus the fighter-physics config built from it. Never changes in a match.
struct Rules {
  Setup setup;
  fp::Config engine;  // the stage and each seat's movement parameters
  std::vector<fp::Vec2> spawns;  // where eliminated players come back, by index
};

// Nullopt if the setup is not valid.
std::optional<Rules> make_rules(const Setup& setup);

struct PlayerState {
  std::int8_t stocks = 0;            // stocks left, the current life included; 0 is out
  std::int16_t respawn_timer = 0;    // ticks until an eliminated player with stocks comes back

  friend bool operator==(const PlayerState&, const PlayerState&) = default;
};

// All the simulation state. fp::World's entities are indexed by seat.
struct State {
  fp::World world;
  std::vector<PlayerState> players;
  std::int8_t winner = -1;  // the winning seat once `over`; -1 if nobody
  bool over = false;
};

State make_state(const Rules& rules);

// Advance one tick. `inputs` has one entry per seat. After the match is over
// the state no longer changes.
void step(const Rules& rules, State& state, std::span<const Input> inputs);

// A 64-bit hash of the whole State, the same on every platform.
std::uint64_t hash(const State& state);

// The State as platform-independent bytes (replacing `out`), and back. `restore`
// returns false, leaving `state` untouched, for bytes that are not a snapshot
// of a State for these rules.
void snapshot(const State& state, std::vector<std::uint8_t>& out);
bool restore(const Rules& rules, State& state, std::span<const std::uint8_t> bytes);

}  // namespace ledgefall
