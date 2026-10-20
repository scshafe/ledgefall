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
  s.straddle_share = Fixed::from_double(0.75);   // on the seam between two tiles: 75% of the load on each (lost-footing)
  return s;
}

// A slab never crumbles from standing (no rates); it is on the stress model so
// that breaking it (stress to 1) sends it Gone and back after its respawn time.
fp::PlatformStress slab_stress() {
  fp::PlatformStress s;
  s.shake_at = Fixed::from_double(0.45);
  s.doom_at = Fixed::from_double(0.75);
  s.straddle_share = Fixed::from_double(0.75);  // the supporting platform's setting counts, so a slab's seam with a tile too
  return s;
}

struct Builder {
  Rules& r;

  // A row that heaves: it rides between `amp` below and `amp` above its height (lost-footing's
  // sine, as the engine's there-and-back linear motion), `period` ticks a cycle, `phase_percent`
  // of a cycle along.
  struct Bob {
    int amp_px = 0, period = 0, phase_percent = 0;
  };
  Bob bob{};
  Fixed belt{};  // the conveyor speed of the rows being added (units a tick, + right)

  void tile(Fixed left, Fixed right, Fixed top) {
    fp::PlatformDef p;
    if (bob.period > 0) {
      const Fixed amp = fx100(bob.amp_px * 26);
      p.rect = {left, right, top - amp - Fixed::from_int(kTileThickness), top - amp};
      p.travel = {Fixed::zero(), amp * Fixed::from_int(2)};
      p.move_period = bob.period;
      r.start_clock.resize(r.engine.stage.platforms.size(), 0);
      r.start_clock.push_back(bob.period * bob.phase_percent / 100 % bob.period);
    }
    p.kind = fp::PlatformKind::PassThrough;
    if (bob.period == 0) p.rect = {left, right, top - Fixed::from_int(kTileThickness), top};
    p.stress = tile_stress();
    p.respawn_frames = kTileRespawnTicks;
    r.engine.stage.platforms.push_back(p);
    r.slab_hp.push_back(0);
    r.belts.resize(r.engine.stage.platforms.size() - 1, Fixed::zero());
    r.belts.push_back(belt);
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
  // Hazards, in lost-footing's units (pixels; ticks).
  void comet(std::initializer_list<int> lane_px, int lo, int hi, int warn, int speed_hundredths_px) {
    HazardSpec h;
    h.type = HazardType::Comet;
    h.every_lo = lo, h.every_hi = hi, h.warn = warn;
    for (int l : lane_px) h.lanes.push_back(y(l));
    h.speed = fx100(speed_hundredths_px * 32 / 100);
    r.hazards.push_back(h);
  }
  void rockfall(int lo, int hi, int warn, int bias_percent) {
    HazardSpec h;
    h.type = HazardType::Rockfall;
    h.every_lo = lo, h.every_hi = hi, h.warn = warn;
    h.sturdy_bias = fx100(bias_percent);
    r.hazards.push_back(h);
  }
  void tide(int lo, int hi, int warn, int rise, int hold, int fall, int low_px, int high_px) {
    HazardSpec h;
    h.type = HazardType::Tide;
    h.every_lo = lo, h.every_hi = hi, h.warn = warn;
    h.rise = rise, h.hold = hold, h.fall = fall;
    h.low = y(low_px), h.high = y(high_px);
    r.hazards.push_back(h);
  }
  void belts(int lo, int hi, int warn) {
    HazardSpec h;
    h.type = HazardType::Belts;
    h.every_lo = lo, h.every_hi = hi, h.warn = warn;
    r.hazards.push_back(h);
  }
  void gale(int lo, int hi, int warn, int duration, int force_thousandths_px) {
    HazardSpec h;
    h.type = HazardType::Gale;
    h.every_lo = lo, h.every_hi = hi, h.warn = warn;
    h.duration = duration;
    h.force = fx100(force_thousandths_px * 32 / 1000);
    r.hazards.push_back(h);
  }

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
  b.comet({640, 470}, 360, 520, 90, 850);
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
  b.rockfall(200, 300, 80, 60);
}

void tide(Rules& r) {  // "Tide Pools": low stone islands, until the tide comes in
  Builder b{r};
  for (auto [x, y, n] : {std::array{380, 640, 5}, {980, 640, 5}, {680, 530, 5}, {220, 480, 4}, {1188, 480, 4},
                         {500, 390, 3}, {956, 390, 3}, {704, 270, 4}}) {
    b.row(x, y, n);
  }
  b.slab_px(140, 770, 200);
  b.slab_px(700, 770, 200);
  b.slab_px(1260, 770, 200);
  b.blast();
  b.spawn(470, 320), b.spawn(1130, 320), b.spawn(800, 450), b.spawn(800, 190);
  b.tide(720, 900, 120, 150, 240, 150, 960, 735);
  b.comet({530}, 540, 840, 90, 750);
}

void belts(Rules& r) {  // "Conveyor Sky": moving walkways; they stop, then run the other way
  Builder b{r};
  struct Row {
    int x, y, n, belt_hundredths_px;  // lost-footing px/tick x 100, + right
  };
  for (const Row& row : {Row{240, 650, 7, 120}, {1024, 650, 7, -120}, {380, 520, 5, -140}, {980, 520, 5, 140}, {560, 390, 10, 100},
                         {300, 260, 4, -120}, {1108, 260, 4, 120}}) {
    b.belt = fx100(row.belt_hundredths_px * 32 / 100);
    b.row(row.x, row.y, row.n);
  }
  b.belt = Fixed::zero();
  b.slab_px(700, 650, 200);
  b.slab_px(130, 480, 140);
  b.slab_px(1330, 480, 140);
  b.blast();
  b.spawn(470, 440), b.spawn(1130, 440), b.spawn(800, 310), b.spawn(800, 190);
  b.belts(420, 600, 75);
  b.comet({650, 480}, 540, 780, 90, 800);
}

void swell(Rules& r) {  // "Sinking Ship": decks heave on the swell, halves out of step
  Builder b{r};
  struct Row {
    int x, y, n, amp, period, phase_percent;
  };
  // Lost-footing's phases are radians: 0, pi, 1, 1 + pi, 0.5, 2 (as shares of a cycle).
  for (const Row& row : {Row{220, 640, 12, 34, 540, 0}, {820, 640, 12, 34, 540, 50}, {380, 510, 5, 28, 420, 16},
                         {980, 510, 5, 28, 420, 66}, {620, 380, 7, 36, 600, 8}, {700, 250, 4, 22, 360, 32}}) {
    b.bob = {row.amp, row.period, row.phase_percent};
    b.row(row.x, row.y, row.n);
  }
  b.bob = {};
  b.slab_px(700, 780, 200);
  b.slab_px(130, 420, 130);
  b.slab_px(1340, 420, 130);
  b.blast();
  b.spawn(470, 440), b.spawn(1130, 440), b.spawn(800, 300), b.spawn(800, 180);
  b.tide(720, 900, 120, 150, 240, 150, 960, 735);
  b.comet({420}, 540, 840, 90, 750);
}

void gale(Rules& r) {  // "Gale Bridge": one long bridge over nothing
  Builder b{r};
  for (auto [x, y, n] : {std::array{200, 640, 25}, {360, 500, 4}, {1048, 500, 4}, {640, 420, 7}, {420, 280, 4},
                         {988, 280, 4}, {728, 170, 3}}) {
    b.row(x, y, n);
  }
  b.slab_px(180, 380, 130);
  b.slab_px(1290, 380, 130);
  b.slab_px(740, 760, 120);
  b.blast();
  b.spawn(480, 560), b.spawn(1120, 560), b.spawn(300, 300), b.spawn(1300, 300);
  b.gale(600, 780, 100, 200, 340);
  b.comet({380}, 540, 780, 90, 800);
}

}  // namespace

bool valid(const Setup& s) noexcept {
  for (Character c : s.characters) {
    if (static_cast<int>(c) >= kCharacterCount) return false;
  }
  for (int i = 0; i < kMaxPlayers; ++i) {
    const auto t = s.teams[static_cast<std::size_t>(i)];
    const auto k = s.seat_stocks[static_cast<std::size_t>(i)];
    if (t < -1 || t > 1 || k < 0 || k > kMaxSeatStocks) return false;
  }
  return static_cast<int>(s.stage) < kStageCount && s.players >= kMinPlayers && s.players <= kMaxPlayers &&
         s.stocks >= kMinStocks && s.stocks <= kMaxStocks;
}

std::optional<Rules> make_rules(const Setup& setup) {
  if (!valid(setup)) return std::nullopt;
  Rules r;
  r.setup = setup;
  // Teams need two sides; with one, everyone plays alone.
  {
    std::array<bool, 2 + kMaxPlayers> sides{};
    int count = 0;
    for (int i = 0; i < setup.players; ++i) {
      const auto t = setup.teams[static_cast<std::size_t>(i)];
      const std::size_t side = t >= 0 ? static_cast<std::size_t>(t) : static_cast<std::size_t>(2 + i);
      if (!sides[side]) ++count;
      sides[side] = true;
    }
    if (count < 2) r.setup.teams = {-1, -1, -1, -1};
  }
  switch (setup.stage) {
    case StageId::Practice: practice(r); break;
    case StageId::Yard: yard(r); break;
    case StageId::Quarry: quarry(r); break;
    case StageId::Tide: tide(r); break;
    case StageId::Gale: gale(r); break;
    case StageId::Swell: swell(r); break;
    case StageId::Belts: belts(r); break;
  }
  for (int i = 0; i < setup.players; ++i) r.engine.params.push_back(detail::movement_for(setup.characters[static_cast<std::size_t>(i)]));
  r.start_clock.resize(r.engine.stage.platforms.size(), 0);
  r.belts.resize(r.engine.stage.platforms.size(), Fixed::zero());
  if (r.engine.stage.platforms.size() > static_cast<std::size_t>(kMaxTiles) || r.hazards.size() > static_cast<std::size_t>(kMaxHazardSpecs)) return std::nullopt;
  return r;
}

}  // namespace ledgefall
