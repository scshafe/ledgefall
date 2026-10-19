// The stage: three tiers of crumbling one-way tiles. This is the one place
// that knows how a platform crumbles, so a richer model (stress, see
// scshafe/fighter-physics#16) replaces it here and nowhere else.
#include "ledgefall/match.hpp"

namespace ledgefall {
namespace {

using fp::Fixed;

constexpr int kTileWidth = 16;
constexpr int kTileThickness = 4;
// Ticks from first touch to the tile falling, and from falling to its return
// (lost-footing: about 0.9 s standing, back 4.75 s after it broke).
constexpr int kShakeTicks = 50;
constexpr int kRespawnTicks = 285;

void add_row(fp::Stage& stage, int left, int tiles, int top) {
  for (int i = 0; i < tiles; ++i) {
    fp::PlatformDef p;
    p.kind = fp::PlatformKind::PassThrough;
    const int l = left + i * kTileWidth;
    p.rect = {Fixed::from_int(l), Fixed::from_int(l + kTileWidth), Fixed::from_int(top - kTileThickness),
              Fixed::from_int(top)};
    p.shake_frames = kShakeTicks;
    p.respawn_frames = kRespawnTicks;
    stage.platforms.push_back(p);
  }
}

}  // namespace

bool valid(const Setup& s) noexcept {
  return s.players >= kMinPlayers && s.players <= kMaxPlayers && s.stocks >= kMinStocks &&
         s.stocks <= kMaxStocks;
}

std::optional<Rules> make_rules(const Setup& setup) {
  if (!valid(setup)) return std::nullopt;
  Rules r;
  r.setup = setup;
  add_row(r.engine.stage, -96, 12, 0);
  add_row(r.engine.stage, -64, 3, 34);
  add_row(r.engine.stage, 16, 3, 34);
  add_row(r.engine.stage, -32, 4, 68);
  r.engine.stage.blast = {Fixed::from_int(-130), Fixed::from_int(130), Fixed::from_int(-60),
                          Fixed::from_int(160)};
  r.engine.params.assign(static_cast<std::size_t>(setup.players), fp::MovementParams{});
  for (int x : {-60, -20, 20, 60}) r.spawns.push_back({Fixed::from_int(x), Fixed::from_int(100)});
  return r;
}

}  // namespace ledgefall
