// Stage hazards (lost-footing's src/hazards.js): what makes sturdy slabs
// dangerous on a rhythm. Every hazard is announced before it acts. Hazards
// have no owner; a player who steers one (a wall or bumper that reflects a
// comet, a gust that turns it) is credited with what it does next.
#include "internal.hpp"

namespace ledgefall::detail {
namespace {

using fp::Fixed;

// Lost-footing's numbers at 0.32 units a pixel (0.26 for heights on the stage).
const Fixed kCometRadius = fx100(832);
const Fixed kCometKnockX = fx100(480), kCometKnockY = fx100(200);
constexpr int kCometHitCool = 30;
const Fixed kRockRadius = fx100(960), kRockStartVy = fx100(192), kRockAccel = fx100(16);
const Fixed kRockPush = fx100(350), kRockSide = fx100(128);
const Fixed kTideMargin = fx100(200);  // feet this far under the surface drown (8 px)

Fixed neg(Fixed v) { return Fixed::zero() - v; }

Fixed rand_range(State& s, Fixed lo, Fixed hi) {
  const std::int64_t span = static_cast<std::int64_t>(hi.raw()) - lo.raw();
  if (span <= 0) return lo;
  return Fixed::from_raw(static_cast<std::int32_t>(lo.raw() + static_cast<std::int64_t>(next_random(s)) % span));
}

HazardItem* free_item(State& s) {
  for (HazardItem& h : s.hazards.list) {
    if (h.kind == HazardKind::None) return &h;
  }
  return nullptr;
}

bool circle_box(Fixed cx, Fixed cy, Fixed r, const fp::Rect& b) {
  const Fixed nx = cx < b.left ? b.left : cx > b.right ? b.right : cx;
  const Fixed ny = cy < b.bottom ? b.bottom : cy > b.top ? b.top : cy;
  const Fixed dx = cx - nx, dy = cy - ny;
  return dx * dx + dy * dy < r * r;
}

fp::Rect body_of(const Rules& rules, const State& state, std::size_t i) {
  const fp::EntityState& e = state.world.entities[i];
  const fp::MovementParams& m = rules.engine.params[i];
  return {e.pos.x - m.body_half_width, e.pos.x + m.body_half_width, e.pos.y, e.pos.y + m.body_height};
}

void fire(const Rules& rules, State& state, const HazardSpec& spec) {
  Hazards& hz = state.hazards;
  switch (spec.type) {
    case HazardType::Comet: {
      HazardItem* h = free_item(state);
      if (h == nullptr) return;
      const Fixed lane = spec.lanes[static_cast<std::size_t>(rand_int(state, 0, static_cast<int>(spec.lanes.size()) - 1))];
      const int dir = rand_int(state, 0, 1) == 0 ? 1 : -1;
      // The comet skims just above the lane's surface.
      *h = HazardItem{HazardKind::CometWarn, static_cast<std::int8_t>(dir), -1, {Fixed::zero(), lane + kCometRadius - fx100(50)},
                      {spec.speed * Fixed::from_int(dir), Fixed::zero()}, static_cast<std::int16_t>(spec.warn), {}};
      break;
    }
    case HazardType::Rockfall: {
      HazardItem* h = free_item(state);
      if (h == nullptr) return;
      std::vector<std::size_t> slabs;
      for (std::size_t i = 0; i < rules.slab_hp.size(); ++i) {
        if (rules.slab_hp[i] != 0 && in_play(state.world.platforms[i].phase)) slabs.push_back(i);
      }
      std::vector<std::size_t> targets;
      for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (state.world.entities[i].eliminated == 0 && state.players[i].invuln == 0) targets.push_back(i);
      }
      const Fixed bias = spec.sturdy_bias;
      const Fixed roll = Fixed::from_raw(static_cast<std::int32_t>(next_random(state) % static_cast<std::uint32_t>(Fixed::kOneRaw)));
      Fixed x;
      if (roll < bias && !slabs.empty()) {
        const fp::Rect& r = rules.engine.stage.platforms[slabs[static_cast<std::size_t>(rand_int(state, 0, static_cast<int>(slabs.size()) - 1))]].rect;
        x = rand_range(state, r.left + fx100(640), r.right - fx100(640));
      } else if (roll < bias + fx100(25) && !targets.empty()) {
        x = state.world.entities[targets[static_cast<std::size_t>(rand_int(state, 0, static_cast<int>(targets.size()) - 1))]].pos.x;
      } else {
        const fp::Rect& b = rules.engine.stage.blast;
        x = rand_range(state, b.left + (b.right - b.left) / Fixed::from_int(7), b.right - (b.right - b.left) / Fixed::from_int(7));
      }
      *h = HazardItem{HazardKind::RockWarn, 0, -1, {x, rules.engine.stage.blast.top}, {}, static_cast<std::int16_t>(spec.warn), {}};
      break;
    }
    case HazardType::Tide:
      hz.tide = TidePhase::Warn;
      hz.tide_timer = static_cast<std::int16_t>(spec.warn);
      break;
    case HazardType::Gale:
      hz.gale = GalePhase::Warn;
      hz.gale_dir = rand_int(state, 0, 1) == 0 ? 1 : -1;
      hz.gale_timer = static_cast<std::int16_t>(spec.warn);
      break;
  }
}

void update_comet(const Rules& rules, State& state, HazardItem& c) {
  // Gusts push comets; one that turns a comet around steers it.
  for (const Object& g : state.objects) {
    if (g.kind != ObjectKind::Gust || g.arm > 0) continue;
    const Fixed d = (c.pos.x - g.pos.x) * g.vel.x;
    if (d < Fixed::zero() || d > fx100(10880) || fp::abs(c.pos.y - g.pos.y) > fx100(1920)) continue;
    const bool before = c.vel.x > Fixed::zero();
    c.vel.x = c.vel.x + g.vel.x * fx100(5);
    if ((c.vel.x > Fixed::zero()) != before) c.redirected_by = g.owner;
  }
  const Fixed floor = fx100(64);  // never slower than 2 px/tick
  if (fp::abs(c.vel.x) < floor) c.vel.x = c.vel.x < Fixed::zero() ? Fixed::zero() - floor : floor;
  c.pos.x = c.pos.x + c.vel.x;
  // Walls and bumpers reflect it, and whoever placed them now steers it.
  for (std::size_t slot = 0; slot < state.world.placeables.size(); ++slot) {
    const fp::Placeable& w = state.world.placeables[slot];
    if (w.active == 0 || w.kind != fp::PlaceableKind::Wall || !circle_box(c.pos.x, c.pos.y, kCometRadius, w.rect)) continue;
    const bool left = c.pos.x < (w.rect.left + w.rect.right) / Fixed::from_int(2);
    c.vel.x = left ? Fixed::zero() - fp::abs(c.vel.x) : fp::abs(c.vel.x);
    c.redirected_by = state.meta[slot].owner;
    fp::Events scratch;
    fp::remove_placeable(rules.engine, state.world, static_cast<int>(slot), scratch);  // the wall is spent
    state.meta[slot] = {};
  }
  for (Object& b : state.objects) {
    if (b.kind != ObjectKind::Bumper || b.arm > 0) continue;
    const Fixed dx = c.pos.x - b.pos.x, dy = c.pos.y - b.pos.y, rr = kCometRadius + fx100(704);
    if (dx * dx + dy * dy >= rr * rr) continue;
    c.vel.x = dx < Fixed::zero() ? neg(fp::abs(c.vel.x)) : fp::abs(c.vel.x);
    c.redirected_by = b.owner;
    if (--b.uses <= 0) b = {};
  }
  for (std::size_t i = 0; i < state.players.size(); ++i) {
    fp::EntityState& e = state.world.entities[i];
    if (e.eliminated != 0 || state.players[i].invuln > 0 || c.cool[i] > 0) continue;
    if (!circle_box(c.pos.x, c.pos.y, kCometRadius, body_of(rules, state, i))) continue;
    fp::start_fall(e);
    e.vel.x = c.vel.x < Fixed::zero() ? neg(kCometKnockX) : kCometKnockX;
    e.vel.y = kCometKnockY;
    e.fast_falling = 0;
    state.players[i].drill = 0;
    c.cool[i] = kCometHitCool;
    if (c.redirected_by >= 0) blame(state, static_cast<int>(i), c.redirected_by);
  }
  for (auto& cool : c.cool) cool = static_cast<std::int16_t>(cool > 0 ? cool - 1 : 0);
  const fp::Rect& b = rules.engine.stage.blast;
  const Fixed margin = (b.right - b.left) / Fixed::from_int(20);
  if (c.pos.x < b.left - margin || c.pos.x > b.right + margin) c = {};
}

void update_rock(const Rules& rules, State& state, HazardItem& r, fp::Events& scratch) {
  r.vel.y = r.vel.y - kRockAccel;
  r.pos.y = r.pos.y + r.vel.y;
  bool smashed = false;
  for (std::size_t i = 0; i < state.players.size(); ++i) {
    fp::EntityState& e = state.world.entities[i];
    if (e.eliminated != 0 || state.players[i].invuln > 0) continue;
    if (!circle_box(r.pos.x, r.pos.y, kRockRadius, body_of(rules, state, i))) continue;
    fp::start_fall(e);
    if (neg(kRockPush) > e.vel.y) e.vel.y = neg(kRockPush);
    e.vel.x = e.vel.x + (e.pos.x < r.pos.x ? neg(kRockSide) : kRockSide);
    state.players[i].drill = 0;
    state.players[i].no_contact = 6;
  }
  for (std::size_t slot = 0; slot < state.world.placeables.size(); ++slot) {
    const fp::Placeable& p = state.world.placeables[slot];
    if (p.active == 0 || (p.kind != fp::PlaceableKind::Wall && p.kind != fp::PlaceableKind::Spring)) continue;
    if (circle_box(r.pos.x, r.pos.y, kRockRadius, p.rect)) {
      fp::remove_placeable(rules.engine, state.world, static_cast<int>(slot), scratch);
      state.meta[slot] = {};
      smashed = true;
    }
  }
  for (Object& o : state.objects) {
    if (o.kind == ObjectKind::Anvil && circle_box(r.pos.x, r.pos.y, kRockRadius, object_rect(o))) {
      o = {};
      smashed = true;
    }
  }
  if (!smashed) {
    const auto& platforms = rules.engine.stage.platforms;
    for (std::size_t t = 0; t < platforms.size(); ++t) {
      if (!in_play(state.world.platforms[t].phase)) continue;
      const fp::Rect box{platforms[t].rect.left, platforms[t].rect.right, platforms[t].rect.top - fx100(400), platforms[t].rect.top};
      if (!circle_box(r.pos.x, r.pos.y, kRockRadius, box)) continue;
      damage_tile(rules, state, static_cast<int>(t), 2, -1, scratch);
      smashed = true;
    }
  }
  if (smashed || r.pos.y < rules.engine.stage.blast.bottom) r = {};
}

void update_tide(State& state, const HazardSpec& spec) {
  Hazards& hz = state.hazards;
  switch (hz.tide) {
    case TidePhase::Calm:
      break;
    case TidePhase::Warn:
      if (--hz.tide_timer <= 0) {
        hz.tide = TidePhase::Rise;
        hz.tide_timer = static_cast<std::int16_t>(spec.rise);
      }
      break;
    case TidePhase::Rise:
      --hz.tide_timer;
      hz.tide_level = spec.high + (spec.low - spec.high) * Fixed::from_int(hz.tide_timer) / Fixed::from_int(spec.rise);
      if (hz.tide_timer <= 0) {
        hz.tide = TidePhase::Hold;
        hz.tide_timer = static_cast<std::int16_t>(spec.hold);
      }
      break;
    case TidePhase::Hold:
      hz.tide_level = spec.high;
      if (--hz.tide_timer <= 0) {
        hz.tide = TidePhase::Fall;
        hz.tide_timer = static_cast<std::int16_t>(spec.fall);
      }
      break;
    case TidePhase::Fall:
      --hz.tide_timer;
      hz.tide_level = spec.low - (spec.low - spec.high) * Fixed::from_int(hz.tide_timer) / Fixed::from_int(spec.fall);
      if (hz.tide_timer <= 0) {
        hz.tide = TidePhase::Calm;
        hz.tide_level = spec.low;
      }
      break;
  }
}

}  // namespace

std::uint32_t next_random(State& s) {
  s.rng += 0x6D2B79F5u;
  std::uint32_t t = s.rng;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return t ^ (t >> 14);
}

int rand_int(State& s, int lo, int hi) {
  if (hi <= lo) return lo;
  return lo + static_cast<int>(next_random(s) % static_cast<std::uint32_t>(hi - lo + 1));
}

int hazard_percent(std::uint32_t tick) {
  if (tick < static_cast<std::uint32_t>(kOvertimeStartTick)) return 100;
  const int level = static_cast<int>((tick - kOvertimeStartTick) / kOvertimeStepTicks) + 1;
  const int pct = 100 - 15 * level;
  return pct < 40 ? 40 : pct;
}

bool drowned(const State& state, const fp::EntityState& e) {
  const Hazards& hz = state.hazards;
  const bool rising = hz.tide == TidePhase::Rise || hz.tide == TidePhase::Hold || hz.tide == TidePhase::Fall;
  return rising && e.pos.y < hz.tide_level - kTideMargin;
}

void apply_gale(const Rules& rules, State& state) {
  const Hazards& hz = state.hazards;
  if (hz.gale != GalePhase::Blow) return;
  Fixed force = fx100(11);  // 0.34 px/tick^2
  for (const HazardSpec& h : rules.hazards) {
    if (h.type == HazardType::Gale) force = h.force;
  }
  // A gale pushes grounded players a little less.
  for (std::size_t i = 0; i < state.players.size(); ++i) {
    fp::EntityState& e = state.world.entities[i];
    if (e.eliminated != 0 || state.players[i].invuln > 0) continue;
    e.vel.x = e.vel.x + force * Fixed::from_int(hz.gale_dir) * (e.mode == fp::Mode::Airborne ? Fixed::one() : fx100(80));
  }
}

void update_hazards(const Rules& rules, State& state, fp::Events& scratch) {
  Hazards& hz = state.hazards;
  for (std::size_t i = 0; i < rules.hazards.size(); ++i) {
    const HazardSpec& spec = rules.hazards[i];
    if (spec.type == HazardType::Tide && hz.tide != TidePhase::Calm) continue;
    if (spec.type == HazardType::Gale && hz.gale != GalePhase::Off) continue;
    if (--hz.timers[i] > 0) continue;
    const int gap = rand_int(state, spec.every_lo, spec.every_hi) * hazard_percent(state.world.frame) / 100;
    hz.timers[i] = static_cast<std::int16_t>(gap);
    fire(rules, state, spec);
  }
  for (const HazardSpec& spec : rules.hazards) {
    if (spec.type == HazardType::Tide) update_tide(state, spec);
    if (spec.type == HazardType::Gale) {
      if (hz.gale == GalePhase::Warn && --hz.gale_timer <= 0) {
        hz.gale = GalePhase::Blow;
        hz.gale_timer = static_cast<std::int16_t>(spec.duration);
      } else if (hz.gale == GalePhase::Blow && --hz.gale_timer <= 0) {
        hz.gale = GalePhase::Off;
      }
    }
  }
  for (HazardItem& h : hz.list) {
    switch (h.kind) {
      case HazardKind::CometWarn:
        if (--h.timer <= 0) {
          const fp::Rect& b = rules.engine.stage.blast;
          const Fixed margin = (b.right - b.left) / Fixed::from_int(40);
          const HazardItem warn = h;
          h = HazardItem{HazardKind::Comet, warn.dir, -1, {warn.dir > 0 ? b.left + margin : b.right - margin, warn.pos.y}, warn.vel, 0, {}};
        }
        break;
      case HazardKind::Comet:
        update_comet(rules, state, h);
        break;
      case HazardKind::RockWarn:
        if (--h.timer <= 0) {
          const HazardItem warn = h;
          h = HazardItem{HazardKind::Rock, 0, -1, warn.pos, {Fixed::zero(), neg(kRockStartVy)}, 0, {}};
        }
        break;
      case HazardKind::Rock:
        update_rock(rules, state, h, scratch);
        break;
      case HazardKind::None:
        break;
    }
  }
}

}  // namespace ledgefall::detail
