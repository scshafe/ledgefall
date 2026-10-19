#pragma once
// The game's rules on top of fighter-physics: a match of 2 to 4 players on
// crumbling platforms, each with a number of stocks, the last with stocks
// winning (docs/lost-footing-brief.md; lost-footing's docs/design.md).
//
// The shape is netplay-kit's Simulation: `Rules` is what a match is set up
// with and never changes, `State` is plain data that is all the simulation
// state, and `step(rules, state, inputs)` is a pure fixed-tick function of the
// state and one input per seat. `hash`, `snapshot` and `restore` cover exactly
// the State. Nothing here depends on time, rounding or container order.

#include <array>
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
// An indirect effect on a player is credited to its maker if the player is
// eliminated within 5 s of it (lost-footing, "Blast zones, stocks and credit").
constexpr int kBlameWindowTicks = 5 * kTicksPerSecond;
// Every match ends (lost-footing, "Overtime"). From 1:30 of play, every 30 s,
// platforms take 50% longer to come back. From 3:30 nothing broken comes back
// and a random tile is doomed every 0.5 s.
constexpr int kOvertimeStartTick = 90 * kTicksPerSecond;
constexpr int kOvertimeStepTicks = 30 * kTicksPerSecond;
constexpr int kCollapseStartTick = 210 * kTicksPerSecond;
constexpr int kCollapseDoomTicks = kTicksPerSecond / 2;

// The characters (lost-footing's, "Characters"). Each has three moves on the
// Place, Trick and Break buttons; the kit is built up in `moves.cpp`.
enum class Character : std::uint8_t { Mason, Glim, Pip, Fuse, Dab, Fern, Ferro };
constexpr int kCharacterCount = 7;
constexpr int kMoveSlots = 3;

// What a match is set up with (the lobby's choices).
struct Setup {
  int players = 2;
  int stocks = kDefaultStocks;
  std::uint32_t seed = 1;  // the match's one random stream (the lobby picks it)
  std::array<Character, kMaxPlayers> characters{Character::Mason, Character::Glim, Character::Pip, Character::Fuse};
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

// What the results screen shows, and how ties are broken.
struct Stats {
  std::uint16_t kos = 0;           // eliminations credited
  std::uint16_t falls = 0;         // self-eliminations
  std::uint16_t deaths = 0;
  std::uint16_t tiles_broken = 0;
  std::uint16_t slabs_broken = 0;
  std::uint16_t placed = 0;        // objects placed
  std::uint16_t stomps = 0;

  friend bool operator==(const Stats&, const Stats&) = default;
};

struct PlayerState {
  std::int8_t stocks = 0;            // stocks left, the current life included; 0 is out
  std::int16_t respawn_timer = 0;    // ticks until an eliminated player with stocks comes back
  std::array<std::int16_t, kMoveSlots> cooldown{};  // ticks until each move can be cast again
  std::uint8_t prev_buttons = 0;     // a move casts on the tick its button goes down
  std::int16_t slow = 0;             // ticks left of a slowdown (a stomp)
  std::int8_t blame_by = -1;         // who last did something indirect to this player, -1 for nobody
  std::int32_t blame_tick = 0;
  std::int32_t ko_tick = -1;         // when this player last lost a stock
  std::int8_t place = 0;             // final placing once the match is over, 1 is the winner
  Stats stats;

  friend bool operator==(const PlayerState&, const PlayerState&) = default;
};

// An object a move has placed that the engine has a slot for. The engine's
// slot knows its shape and lifetime; this knows whose it is.
struct PlaceableMeta {
  std::int8_t owner = -1;     // seat; -1 for a free slot
  std::uint8_t uses = 0;      // uses left (a spring), 0 for unlimited
  std::int16_t support = -1;  // the platform it stands on; it goes when that does
  std::int32_t born = 0;      // the tick it was placed, to find a player's oldest

  friend bool operator==(const PlaceableMeta&, const PlaceableMeta&) = default;
};

// A placement that waits to arm (a spring takes a quarter second) before it appears.
struct Pending {
  bool used = false;
  std::int8_t owner = -1;
  std::int16_t delay = 0;
  fp::Placeable what;
  std::int16_t support = -1;
  std::uint8_t uses = 0;
};

constexpr int kMaxPending = 16;

// Things in flight or at work that the engine has no slot for: a lob on its
// way down, an acid puddle eating tiles. Fixed-point plain data like the rest.
enum class ObjectKind : std::uint8_t { None, Lob, Acid };
enum class Payload : std::uint8_t { Glue, Oil, Acid };  // what a lob carries

struct Object {
  ObjectKind kind = ObjectKind::None;
  std::int8_t owner = -1;
  Payload payload = Payload::Glue;
  fp::Vec2 pos{};           // a lob's position; an acid puddle's x is the middle, y the surface
  fp::Vec2 vel{};
  fp::Fixed x0{}, x1{};     // an acid puddle's extent
  std::int16_t timer = 0;   // ticks left (an acid puddle)
};

constexpr int kMaxObjects = 16;
constexpr int kMaxTiles = 64;  // platforms the stage may have (side tables are this long)

// Who last put stress on a tile, so a player it drops is credited to them.
struct TileBlame {
  std::int8_t by = -1;
  std::int32_t tick = 0;

  friend bool operator==(const TileBlame&, const TileBlame&) = default;
};

// All the simulation state. fp::World's entities are indexed by seat.
struct State {
  fp::World world;
  std::vector<PlayerState> players;
  std::int8_t winner = -1;  // the winning seat once `over`; -1 if nobody
  bool over = false;
  std::uint32_t rng = 1;  // mulberry32 state; all randomness in the rules comes from here
  std::array<PlaceableMeta, fp::kMaxPlaceables> meta{};  // by engine placeable slot
  std::array<Pending, kMaxPending> pending{};
  std::array<Object, kMaxObjects> objects{};
  std::array<TileBlame, kMaxTiles> tile_blame{};  // by stage platform
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
