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
constexpr int kMaxSeatStocks = 9;  // a handicap can go this high
constexpr int kTicksPerSecond = 60;
constexpr int kRespawnDelayTicks = kTicksPerSecond * 3 / 2;  // 1.5 s after an elimination
constexpr int kInvulnTicks = 2 * kTicksPerSecond;            // hazards and contact ignore a respawned player this long
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

// The stages (lost-footing's, "Stages and hazards"; Practice is a plain three-tier test floor).
enum class StageId : std::uint8_t { Practice, Yard, Quarry, Tide, Gale, Swell };
constexpr int kStageCount = 6;

// What a match is set up with (the lobby's choices).
struct Setup {
  int players = 2;
  int stocks = kDefaultStocks;
  std::uint32_t seed = 1;  // the match's one random stream (the lobby picks it)
  StageId stage = StageId::Practice;
  // Per seat: a team (0 or 1; -1 plays alone) and a handicap (this many stocks instead of `stocks`; 0 for none, 1 to 9).
  // The match ends when one side has anyone left and the whole side wins. Teams need two sides (A and B, or a
  // team and a solo player); with only one, `make_rules` has everyone play alone.
  std::array<std::int8_t, kMaxPlayers> teams{-1, -1, -1, -1};
  std::array<std::int8_t, kMaxPlayers> seat_stocks{0, 0, 0, 0};
  std::array<Character, kMaxPlayers> characters{Character::Mason, Character::Glim, Character::Pip, Character::Fuse};
};

bool valid(const Setup& s) noexcept;

// What a stage does to the players on a rhythm (lost-footing, "Stages and
// hazards"): comets skim the lanes, rocks fall, the tide rises, a gale blows.
// Every hazard is announced before it acts.
enum class HazardType : std::uint8_t { Comet, Rockfall, Tide, Gale };

struct HazardSpec {
  HazardType type = HazardType::Comet;
  int every_lo = 0, every_hi = 0;  // ticks between firings, drawn from the match's random stream
  int warn = 0;                    // ticks of warning
  std::vector<fp::Fixed> lanes;    // comet: the surface heights it skims
  fp::Fixed speed{};               // comet: units a tick
  fp::Fixed force{};               // gale: push a tick
  int duration = 0;                // gale: ticks it blows
  fp::Fixed sturdy_bias{};         // rockfall: share of rocks aimed at slabs
  int rise = 0, hold = 0, fall = 0;  // tide: ticks of each leg
  fp::Fixed low{}, high{};         // tide: its levels
};
constexpr int kMaxHazardSpecs = 3;

// Setup plus the fighter-physics config built from it. Never changes in a match.
struct Rules {
  Setup setup;
  fp::Config engine;  // the stage and each seat's movement parameters
  std::vector<fp::Vec2> spawns;  // where eliminated players come back, by index; seat i starts at spawns[i]
  std::vector<std::uint8_t> slab_hp;  // per stage platform: hit points of a sturdy slab, 0 for a crumble tile
  std::vector<HazardSpec> hazards;
  std::vector<std::int32_t> start_clock;  // per stage platform: where its motion cycle starts
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
  std::int16_t invuln = 0;           // ticks left of the immunity after a respawn (hazards and contact ignore the player)
  std::int16_t no_contact = 0;       // ticks without body contact (after a rock)
  std::int8_t drill = 0;             // Pip's drill: >0 hopping, -1 diving, 0 not
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
  std::uint8_t tag = 0;       // 0 an ordinary placeable, 1 a bramble (thorns): a sticky patch that chews tiles

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
  std::uint8_t tag = 0;
};

constexpr int kMaxPending = 16;

// Things in flight or at work that the engine has no slot for: a lob on its
// way down, an acid puddle eating tiles, a gust blowing. Fixed-point plain data like the rest.
enum class ObjectKind : std::uint8_t { None, Lob, Acid, Gust, Anvil, Bumper, Charge, Saw, Roots, Rub, Magnet };
enum class Payload : std::uint8_t { Glue, Oil, Acid, Charge };  // what a lob carries

struct Object {
  ObjectKind kind = ObjectKind::None;
  std::int8_t owner = -1;
  Payload payload = Payload::Glue;
  fp::Vec2 pos{};           // a lob's position; an acid puddle's x is the middle, y the surface
  fp::Vec2 vel{};
  fp::Fixed x0{}, x1{};     // an acid puddle's extent
  std::int16_t timer = 0;   // ticks left (an acid puddle, a gust)
  std::int16_t arm = 0;     // ticks until it acts (a gust spins up first)
  std::int8_t breaks = 0;   // an anvil: tiles it has broken so far
  std::int16_t rest = 0;    // an anvil: ticks left lying on a slab, 0 while it falls
  std::int8_t uses = 0;     // a bumper: bounces left
  std::array<std::int16_t, 4> cool{};  // a bumper: per seat, ticks before it bounces them again
  std::array<std::int16_t, 4> hit{};   // a saw: the slabs it has already chipped (platform + 1; 0 for none)
};

constexpr int kMaxObjects = 24;
constexpr int kMaxTiles = 96;  // platforms the stage may have (side tables are this long)

enum class HazardKind : std::uint8_t { None, CometWarn, Comet, RockWarn, Rock };

struct HazardItem {
  HazardKind kind = HazardKind::None;
  std::int8_t dir = 0;              // a comet's direction
  std::int8_t redirected_by = -1;   // whoever steered a comet (a wall, a bumper, a gust)
  fp::Vec2 pos{};
  fp::Vec2 vel{};
  std::int16_t timer = 0;           // ticks of warning left
  std::array<std::int16_t, 4> cool{};  // a comet: ticks before it hits a seat again
};
constexpr int kMaxHazards = 12;

enum class TidePhase : std::uint8_t { Calm, Warn, Rise, Hold, Fall };
enum class GalePhase : std::uint8_t { Off, Warn, Blow };

struct Hazards {
  std::array<std::int16_t, kMaxHazardSpecs> timers{};  // ticks until each spec next fires
  std::array<HazardItem, kMaxHazards> list{};
  TidePhase tide = TidePhase::Calm;
  std::int16_t tide_timer = 0;
  fp::Fixed tide_level{};
  GalePhase gale = GalePhase::Off;
  std::int8_t gale_dir = 0;
  std::int16_t gale_timer = 0;
};

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
  std::array<std::int8_t, kMaxTiles> slab_hp{};   // by stage platform: hit points left of a sturdy slab
  Hazards hazards;
};

// Whether two seats are on the same side (a team, or the same player).
bool same_side(const Rules& rules, int a, int b);

// The team that won, once the match is over and was played in teams; -1 otherwise.
int winning_team(const Rules& rules, const State& state);

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
