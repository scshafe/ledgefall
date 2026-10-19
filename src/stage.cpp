// The stages: rows of crumbling one-way tiles and a few sturdy slabs, with
// lost-footing's stress model (docs/lost-footing-brief.md; its design.md,
// "Crumble platform" and "Sturdy platform"). The layouts are lost-footing's
// (src/stages.js), moved to this world: 0.32 units a pixel across, 0.26 up
// (so one jump still reaches the next tier), y up with the lowest row at 0.
#include "internal.hpp"

namespace ledgefall {
namespace {

using fp::Fixed;
using detail::fx100;

// Lost-footing's per-tick numbers (60 ticks a second, as ours), with its pixel
// speeds scaled to this world (lost-footing run speed 5.4 px, ours 1.76 units).
// A tile is back 4.75 s after it breaks; a slab 8 s.
constexpr int kTileRespawnTicks = 285;
constexpr int kSlabRespawnTicks = 480;
constexpr int kSlabHp = 4;
constexpr int kTileThickness = 4;

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

// A slab never crumbles from standing (no rates); it is on the stress model so
// that breaking it (stress to 1) sends it Gone and back after its respawn time.
fp::PlatformStress slab_stress() {
  fp::PlatformStress s;
  s.shake_at = Fixed::from_double(0.45);
  s.doom_at = Fixed::from_double(0.75);
  return s;
}

struct Builder {
  Rules& r;

  void tile(Fixed left, Fixed right, Fixed top) {
    fp::PlatformDef p;
    p.kind = fp::PlatformKind::PassThrough;
    p.rect = {left, right, top - Fixed::from_int(kTileThickness), top};
    p.stress = tile_stress();
    p.respawn_frames = kTileRespawnTicks;
    r.engine.stage.platforms.push_back(p);
    r.slab_hp.push_back(0);
  }

  void slab(Fixed left, Fixed right, Fixed top, int hp) {
    fp::PlatformDef p;
    p.kind = fp::PlatformKind::PassThrough;
    p.rect = {left, right, top - Fixed::from_int(kTileThickness), top};
    p.stress = slab_stress();
    p.respawn_frames = kSlabRespawnTicks;
    r.engine.stage.platforms.push_back(p);
    r.slab_hp.push_back(static_cast<std::uint8_t>(hp));
  }

  // Lost-footing's pixels: x across 1600, y down 900, the lowest row at y = 770.
  static Fixed x(int px) { return fx100((px - 800) * 32); }
  static Fixed y(int px) { return fx100((770 - px) * 26); }

  void row(int px_x, int px_y, int n) {
    for (int i = 0; i < n; ++i) tile(x(px_x + i * 48), x(px_x + (i + 1) * 48), y(px_y));
  }
  void slab_px(int px_x, int px_y, int w, int hp = kSlabHp) { slab(x(px_x), x(px_x + w), y(px_y), hp); }
  void spawn(int px_x, int px_y) { r.spawns.push_back({x(px_x), y(px_y)}); }
  void blast() { r.engine.stage.blast = {x(-260), x(1860), y(980), y(-420)}; }
};

void practice(Rules& r) {
  // Three tiers of 16-wide tiles on a small floor, for tests and for learning the moves.
  Builder b{r};
  auto row = [&](int left, int n, int top) {
    for (int i = 0; i < n; ++i) b.tile(Fixed::from_int(left + i * 16), Fixed::from_int(left + (i + 1) * 16), Fixed::from_int(top));
  };
  row(-96, 12, 0);
  row(-64, 3, 34);
  row(16, 3, 34);
  row(-32, 4, 68);
  r.engine.stage.blast = {Fixed::from_int(-130), Fixed::from_int(130), Fixed::from_int(-60), Fixed::from_int(160)};
  for (int x : {-60, 60, -20, 20}) r.spawns.push_back({Fixed::from_int(x), Fixed::from_int(100)});
}

void yard(Rules& r) {  // "Crumble Yard": brick tiers over a pit
  Builder b{r};
  for (auto [x, y, n] : {std::array{260, 640, 8}, {956, 640, 8}, {400, 510, 4}, {1008, 510, 4}, {584, 380, 3},
                         {872, 380, 3}, {656, 250, 6}, {392, 770, 3}, {1064, 770, 3}}) {
    b.row(x, y, n);
  }
  b.slab_px(700, 640, 200);
  b.slab_px(150, 470, 130);
  b.slab_px(1320, 470, 130);
  b.blast();
  b.spawn(470, 420), b.spawn(1130, 420), b.spawn(300, 300), b.spawn(1300, 300);
}

void quarry(Rules& r) {  // "The Quarry": a great stone ledge over the pit
  Builder b{r};
  for (auto [x, y, n] : {std::array{150, 610, 5}, {1210, 610, 5}, {380, 480, 4}, {1028, 480, 4}, {704, 350, 4},
                         {520, 220, 3}, {936, 220, 3}}) {
    b.row(x, y, n);
  }
  b.slab_px(600, 620, 400, 8);
  b.slab_px(140, 350, 130);
  b.slab_px(1330, 350, 130);
  b.blast();
  b.spawn(470, 400), b.spawn(1130, 400), b.spawn(300, 270), b.spawn(1300, 270);
}

}  // namespace

bool valid(const Setup& s) noexcept {
  for (Character c : s.characters) {
    if (static_cast<int>(c) >= kCharacterCount) return false;
  }
  return static_cast<int>(s.stage) < kStageCount && s.players >= kMinPlayers && s.players <= kMaxPlayers &&
         s.stocks >= kMinStocks && s.stocks <= kMaxStocks;
}

std::optional<Rules> make_rules(const Setup& setup) {
  if (!valid(setup)) return std::nullopt;
  Rules r;
  r.setup = setup;
  switch (setup.stage) {
    case StageId::Practice: practice(r); break;
    case StageId::Yard: yard(r); break;
    case StageId::Quarry: quarry(r); break;
  }
  for (int i = 0; i < setup.players; ++i) r.engine.params.push_back(detail::movement_for(setup.characters[static_cast<std::size_t>(i)]));
  if (r.engine.stage.platforms.size() > static_cast<std::size_t>(kMaxTiles)) return std::nullopt;
  return r;
}

}  // namespace ledgefall
