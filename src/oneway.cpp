// The one-way placeables (fighter-physics#21): Mason's ledge, Dab's decoy and hollow tiles, the
// respawn halo. A one-way is stood on from above only; with `only_for` it is solid for one player and
// air for everyone else.
#include "internal.hpp"

namespace ledgefall::detail {
namespace {

using fp::Fixed;

constexpr int kLedgeLife = 240, kLedgeHp = 2;
const Fixed kLedgeHalfWidth = fx100(1440), kLedgeAhead = fx100(3520);
constexpr int kDecoyLife = 300, kDecoyMax = 2;
const Fixed kDecoyHalfWidth = fx100(1536), kDecoyNear = fx100(2880), kDecoyFar = fx100(4800);
constexpr int kHollowArm = 24, kHollowTicks = 150;
const Fixed kHollowNear = fx100(960), kHollowReach = fx100(4160);
const Fixed kHaloHalfWidth = fx100(1344);
constexpr int kHaloLife = 180, kHaloMinAge = 6;
const Fixed kThickness = fx100(400);

int place_oneway(State& state, const fp::Rect& rect, int only_for, int life, int owner, std::uint8_t tag, std::uint8_t uses) {
  fp::Placeable p;
  p.kind = fp::PlaceableKind::OneWay;
  p.rect = rect;
  p.life = life;
  p.only_for = only_for;
  const int slot = fp::place(state.world, p);
  if (slot >= 0) {
    state.meta[static_cast<std::size_t>(slot)] = {static_cast<std::int8_t>(owner), uses, -1, static_cast<std::int32_t>(state.world.frame), tag};
  }
  return slot;
}

// Keep at most `max` one-ways of a tag per owner: the oldest goes.
void limit_tag(const Rules& rules, State& state, int owner, std::uint8_t tag, int max, fp::Events& events) {
  for (;;) {
    int count = 0, oldest = -1;
    for (std::size_t slot = 0; slot < state.world.placeables.size(); ++slot) {
      const PlaceableMeta& m = state.meta[slot];
      if (state.world.placeables[slot].active == 0 || m.owner != owner || m.tag != tag) continue;
      ++count;
      if (oldest < 0 || m.born < state.meta[static_cast<std::size_t>(oldest)].born) oldest = static_cast<int>(slot);
    }
    if (count < max) return;
    fp::remove_placeable(rules.engine, state.world, oldest, events);
    state.meta[static_cast<std::size_t>(oldest)] = {};
  }
}

}  // namespace

// Ledge: a temporary sturdy slab under you in the air, or ahead of you on the ground. 4 s, 2 hit points.
bool cast_ledge(State& state, const Rules& rules, int seat, fp::Events& events) {
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  Fixed cx, top;
  if (e.mode != fp::Mode::Airborne) {
    cx = e.pos.x + kLedgeAhead * Fixed::from_int(e.facing);
    top = e.pos.y;
  } else {
    cx = e.pos.x;
    top = e.pos.y - fx100(64);
  }
  limit_tag(rules, state, seat, kTagLedge, 1, events);
  return place_oneway(state, {cx - kLedgeHalfWidth, cx + kLedgeHalfWidth, top - kThickness, top}, -1, kLedgeLife, seat, kTagLedge, kLedgeHp) >= 0;
}

// Decoy: two tiles of fake floor at your feet's height, ahead (further with forward held), solid for the
// caster only. It fizzles where real floor already is. In the air it appears under you.
bool cast_decoy(State& state, const Rules& rules, int seat, const Input& in, fp::Events& events) {
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  Fixed cx, top;
  if (e.mode != fp::Mode::Airborne) {
    const bool forward = (in.move_x > 25 && e.facing > 0) || (in.move_x < -25 && e.facing < 0);
    cx = e.pos.x + (forward ? kDecoyFar : kDecoyNear) * Fixed::from_int(e.facing);
    top = e.pos.y;
    // Painted over real floor it would be pointless.
    Fixed covered = Fixed::zero();
    const Fixed lo = cx - kDecoyHalfWidth, hi = cx + kDecoyHalfWidth;
    const auto& platforms = rules.engine.stage.platforms;
    for (std::size_t t = 0; t < platforms.size(); ++t) {
      if (!in_play(state.world.platforms[t].phase) || platforms[t].rect.top != top) continue;
      const Fixed a = platforms[t].rect.left > lo ? platforms[t].rect.left : lo;
      const Fixed b = platforms[t].rect.right < hi ? platforms[t].rect.right : hi;
      if (b > a) covered = covered + (b - a);
    }
    if (covered >= kDecoyHalfWidth) return false;
  } else {
    cx = e.pos.x;
    top = e.pos.y - fx100(64);
  }
  limit_tag(rules, state, seat, kTagDecoy, kDecoyMax, events);
  return place_oneway(state, {cx - kDecoyHalfWidth, cx + kDecoyHalfWidth, top - kThickness, top}, seat, kDecoyLife, seat, kTagDecoy, 0) >= 0;
}

// Hollow: the tiles 30 to 160 px ahead on your tier shimmer for 0.4 s, then for 2.5 s hold only you.
bool cast_hollow(State& state, const Rules& rules, int seat) {
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const int under = tile_under_feet(rules, e);
  if (under < 0) return false;
  const Fixed top = rules.engine.stage.platforms[static_cast<std::size_t>(under)].rect.top;
  const Fixed a = e.pos.x + kHollowNear * Fixed::from_int(e.facing), b = e.pos.x + (kHollowNear + kHollowReach) * Fixed::from_int(e.facing);
  const Fixed lo = a < b ? a : b, hi = a < b ? b : a;
  bool any = false;
  const auto& platforms = rules.engine.stage.platforms;
  for (std::size_t t = 0; t < platforms.size(); ++t) {
    any = any || (rules.slab_hp[t] == 0 && in_play(state.world.platforms[t].phase) && platforms[t].rect.top == top &&
                  platforms[t].rect.left < hi && platforms[t].rect.right > lo);
  }
  if (!any) return false;
  for (Pending& p : state.pending) {
    if (p.used) continue;
    fp::Placeable span;
    span.rect = {lo, hi, top - kThickness, top};
    p = {true, static_cast<std::int8_t>(seat), kHollowArm, span, -1, 0, kPendingHollow};
    return true;
  }
  return false;
}

// The shimmer is over: each tile of the span goes, and a floor only the caster holds takes its place.
void arm_hollow(const Rules& rules, State& state, const Pending& p, fp::Events& scratch) {
  const auto& platforms = rules.engine.stage.platforms;
  for (std::size_t t = 0; t < platforms.size(); ++t) {
    const fp::Rect& r = platforms[t].rect;
    if (rules.slab_hp[t] != 0 || !in_play(state.world.platforms[t].phase)) continue;
    if (r.top != p.what.rect.top || r.left >= p.what.rect.right || r.right <= p.what.rect.left) continue;
    stress_tile(rules, state, static_cast<int>(t), Fixed::one(), p.owner, scratch);  // whoever stands there drops, credited to the caster
    state.world.platforms[t].timer = kHollowTicks;                                   // and it is back when the floor is gone
    place_oneway(state, {r.left, r.right, r.top - kThickness, r.top}, p.owner, kHollowTicks, p.owner, kTagHollow, 0);
  }
}

void place_halo(const Rules& rules, State& state, int seat, const fp::Vec2& at) {
  fp::Events scratch;
  limit_tag(rules, state, seat, kTagHalo, 1, scratch);
  (void)rules;
  place_oneway(state, {at.x - kHaloHalfWidth, at.x + kHaloHalfWidth, at.y - kThickness, at.y}, seat, kHaloLife, seat, kTagHalo, 0);
}

void damage_ledge(const Rules& rules, State& state, int slot, int amount, fp::Events& scratch) {
  PlaceableMeta& m = state.meta[static_cast<std::size_t>(slot)];
  if (m.tag != kTagLedge && m.tag != kTagLeaf) return;
  if (m.uses > amount) {
    m.uses = static_cast<std::uint8_t>(m.uses - amount);
    return;
  }
  fp::remove_placeable(rules.engine, state.world, slot, scratch);
  m = {};
}

void oneway_upkeep(const Rules& rules, State& state, const std::vector<fp::Vec2>& pos_before, fp::Events& scratch) {
  const int platforms = static_cast<int>(rules.engine.stage.platforms.size());
  for (std::size_t slot = 0; slot < state.world.placeables.size(); ++slot) {
    const fp::Placeable& p = state.world.placeables[slot];
    const PlaceableMeta& m = state.meta[slot];
    if (p.active == 0 || m.owner < 0) continue;
    if (m.tag == kTagHalo) {
      // A halo vanishes as soon as its owner steps off it (or after 3 s), or when the owner is out of play.
      const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(m.owner)];
      const bool on_it = e.support == platforms + static_cast<int>(slot);
      const bool old_enough = static_cast<std::int32_t>(state.world.frame) - m.born > kHaloMinAge;
      if (e.eliminated != 0 || (!on_it && old_enough)) {
        fp::remove_placeable(rules.engine, state.world, static_cast<int>(slot), scratch);
        state.meta[slot] = {};
      }
    } else if (m.tag == kTagDecoy) {
      // A decoy drops whoever steps onto it (anyone but its painter), who is credited to the painter.
      for (std::size_t i = 0; i < state.players.size(); ++i) {
        if (static_cast<int>(i) == m.owner) continue;
        const fp::EntityState& e = state.world.entities[i];
        if (e.eliminated != 0 || state.players[i].invuln > 0) continue;
        if (e.pos.x >= p.rect.left && e.pos.x <= p.rect.right && pos_before[i].y >= p.rect.top && e.pos.y < p.rect.top) {
          blame(state, static_cast<int>(i), m.owner);
        }
      }
    }
  }
}

}  // namespace ledgefall::detail
