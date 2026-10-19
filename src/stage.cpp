// The stage: three tiers of crumbling one-way tiles, with lost-footing's
// stress model (docs/lost-footing-brief.md; its design.md, "Crumble platform").
#include "ledgefall/match.hpp"

namespace ledgefall {
namespace {

using fp::Fixed;

constexpr int kTileWidth = 16;
constexpr int kTileThickness = 4;
// Lost-footing's per-tick numbers (60 ticks a second, as ours), with its pixel
// speeds scaled to this world (lost-footing run speed 5.4 px, ours 1.76 units).
// A tile is back 4.75 s after it breaks.
constexpr int kRespawnTicks = 285;

fp::PlatformStress tile_stress() {
  fp::PlatformStress s;
  s.shake_at = Fixed::from_double(0.45);
  s.doom_at = Fixed::from_double(0.75);
  s.still_rate = Fixed::from_double(1.0 / 56);   // a player standing still: about 0.9 s
  s.moving_rate = Fixed::from_double(1.0 / 110); // a player moving on it: about 3 s of pacing
  s.still_speed = Fixed::from_double(0.36);      // lost-footing: under 1.1 px/tick
  s.rider_share = Fixed::from_double(0.6);       // someone riding on your head: 60% of their load
  s.doom_rate = Fixed::from_double(1.0 / 80);    // doomed: it keeps going by itself
  s.decay = Fixed::from_double(1.0 / 260);       // nobody on it
  return s;
}

void add_row(fp::Stage& stage, int left, int tiles, int top) {
  for (int i = 0; i < tiles; ++i) {
    fp::PlatformDef p;
    p.kind = fp::PlatformKind::PassThrough;
    const int l = left + i * kTileWidth;
    p.rect = {Fixed::from_int(l), Fixed::from_int(l + kTileWidth), Fixed::from_int(top - kTileThickness),
              Fixed::from_int(top)};
    p.stress = tile_stress();
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
