// The characters' moves. A move is cast on the tick its button goes down, if
// its cooldown is spent and the character has it; casting puts something into
// the engine (a wall, a spring pad) or into the entity (a launch), and the
// engine and the rest of the rules take it from there.
#include "internal.hpp"

namespace ledgefall::detail {
namespace {

using fp::Fixed;

constexpr int kWallLifeTicks = 330;     // 5.5 s
constexpr int kWallMax = 2;
constexpr int kSpringLifeTicks = 480;   // 8 s
constexpr int kSpringArmTicks = 15;     // a quarter second before it launches
constexpr int kSpringMax = 2;
constexpr int kSpringUses = 4;
constexpr int kThornMax = 2;

bool grounded(const fp::EntityState& e) { return e.mode != fp::Mode::Airborne; }

bool gone(fp::PlatformPhase p) { return p == fp::PlatformPhase::Gone || p == fp::PlatformPhase::Destroyed; }

bool overlaps(const fp::Rect& a, const fp::Rect& b) {
  return a.left < b.right && a.right > b.left && a.bottom < b.top && a.top > b.bottom;
}

fp::Rect body(const Rules& rules, const State& state, int seat) {
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const fp::MovementParams& p = rules.engine.params[static_cast<std::size_t>(seat)];
  return {e.pos.x - p.body_half_width, e.pos.x + p.body_half_width, e.pos.y, e.pos.y + p.body_height};
}

bool blocked(const Rules& rules, const State& state, const fp::Rect& r) {
  for (std::size_t i = 0; i < state.players.size(); ++i) {
    if (state.world.entities[i].eliminated == 0 && overlaps(r, body(rules, state, static_cast<int>(i)))) return true;
  }
  for (const fp::Placeable& p : state.world.placeables) {
    if (p.active != 0 && p.kind == fp::PlaceableKind::Wall && overlaps(r, p.rect)) return true;
  }
  return false;
}

}  // namespace

void limit_owned(const Rules& rules, State& state, int seat, fp::PlaceableKind kind, int max, fp::Events& events,
                 std::uint8_t tag) {
  for (;;) {
    int count = 0, oldest = -1;
    for (std::size_t slot = 0; slot < state.world.placeables.size(); ++slot) {
      const fp::Placeable& p = state.world.placeables[slot];
      const PlaceableMeta& m = state.meta[slot];
      if (p.active == 0 || p.kind != kind || m.owner != seat || m.tag != tag) continue;
      ++count;
      if (oldest < 0 || m.born < state.meta[static_cast<std::size_t>(oldest)].born) oldest = static_cast<int>(slot);
    }
    if (count < max) return;
    fp::remove_placeable(rules.engine, state.world, oldest, events);
    state.meta[static_cast<std::size_t>(oldest)] = {};
  }
}

namespace {

// The tile an entity on the ground stands on, if it stands on one.
std::int16_t tile_under(const Rules& rules, const fp::EntityState& e) {
  return e.support >= 0 && e.support < static_cast<int>(rules.engine.stage.platforms.size()) ? e.support : -1;
}

}  // namespace

void register_placed(State& state, int slot, int seat, std::uint8_t uses, std::int16_t support, std::uint8_t tag) {
  if (slot < 0) return;
  state.meta[static_cast<std::size_t>(slot)] = {static_cast<std::int8_t>(seat), uses, support,
                                                static_cast<std::int32_t>(state.world.frame), tag};
}

namespace {

// ---- Mason --------------------------------------------------------------

// Wall: a 22 x 90 px stone wall ahead, solid on every side, 5.5 s, two at once.
bool cast_wall(const Rules& rules, State& state, int seat, fp::Events& events) {
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const Fixed half = fx100(350), height = fx100(2900);
  for (int dist : {1200, 2000, 2800}) {  // 38, 62 and 86 px ahead
    const Fixed cx = e.pos.x + fx100(dist * e.facing);
    const fp::Rect r{cx - half, cx + half, e.pos.y, e.pos.y + height};
    if (blocked(rules, state, r)) continue;
    limit_owned(rules, state, seat, fp::PlaceableKind::Wall, kWallMax, events);
    fp::Placeable w;
    w.kind = fp::PlaceableKind::Wall;
    w.rect = r;
    w.life = kWallLifeTicks;
    const int slot = fp::place(state.world, w);
    if (slot < 0) return false;
    register_placed(state, slot, seat, 0, grounded(e) ? tile_under(rules, e) : std::int16_t{-1});
    return true;
  }
  return false;
}

// ---- Pip ----------------------------------------------------------------

// Spring: a pad ahead that launches whoever steps on it, straight up or, if a
// direction was held, up and sideways. In the air it appears under you and
// launches you.
bool cast_spring(const Rules& rules, State& state, int seat, const Input& in, fp::Events& events) {
  fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  if (!grounded(e)) {
    e.vel.y = fx100(305);  // lost-footing: -13.5 px/tick
    e.fast_falling = 0;
    e.jumps_used = static_cast<std::int8_t>(rules.engine.params[static_cast<std::size_t>(seat)].aerial_jumps);  // it replaces the air jump
    return true;
  }
  const int dir = in.move_x > 25 ? 1 : in.move_x < -25 ? -1 : 0;
  const Fixed cx = e.pos.x + fx100(1300 * e.facing);  // 40 px ahead
  const Fixed half = fx100(550);                      // a 34 px pad
  fp::Placeable s;
  s.kind = fp::PlaceableKind::Spring;
  s.rect = {cx - half, cx + half, e.pos.y, e.pos.y + fx100(400)};
  s.life = kSpringLifeTicks - kSpringArmTicks;
  s.launch = dir == 0 ? fp::Vec2{Fixed::zero(), fx100(380)} : fp::Vec2{fx100(160 * dir), fx100(340)};
  if (s.rect.left <= Fixed::from_int(-1000)) return false;

  limit_owned(rules, state, seat, fp::PlaceableKind::Spring, kSpringMax, events);
  for (Pending& p : state.pending) {
    if (p.used) continue;
    p = {true, static_cast<std::int8_t>(seat), kSpringArmTicks, s, tile_under(rules, e), kSpringUses};
    return true;
  }
  return false;
}

}  // namespace

bool cast(const Rules& rules, State& state, int seat, int slot, const Input& in, fp::Events& events) {
  const Character c = rules.setup.characters[static_cast<std::size_t>(seat)];
  bool done = false;
  switch (c) {
    case Character::Mason:
      if (slot == 0) done = cast_wall(rules, state, seat, events);
      else if (slot == 2) done = cast_anvil(state, rules, seat, in);
      break;
    case Character::Pip:
      if (slot == 0) done = cast_spring(rules, state, seat, in, events);
      else if (slot == 1) done = cast_gust(rules, state, seat);
      else done = cast_drill(state, seat);
      break;
    case Character::Fuse:
      if (slot == 0) done = cast_bumper(state, rules, seat, events);
      else if (slot == 1) done = throw_lob(state, rules, seat, Payload::Charge, in);
      else done = cast_saw(state, rules, seat);
      break;
    case Character::Fern:  // the vine waits on fighter-physics#21
      if (slot == 1) done = cast_thorns(state, rules, seat);
      else if (slot == 2) done = cast_roots(state, rules, seat);
      break;
    case Character::Dab:  // the decoy and hollow tile wait on fighter-physics#21
      if (slot == 2) done = cast_rub(state, rules, seat);
      break;
    case Character::Glim:  // glue, oil and acid, each a lob
      done = throw_lob(state, rules, seat, slot == 0 ? Payload::Glue : slot == 1 ? Payload::Oil : Payload::Acid, in);
      break;
    default:
      break;
  }
  if (done) ++state.players[static_cast<std::size_t>(seat)].stats.placed;
  return done;
}

void arm_pending(const Rules& rules, State& state, fp::Events& events) {
  for (Pending& p : state.pending) {
    if (!p.used || --p.delay > 0) continue;
    p.used = false;
    // The tile it was to stand on may have gone while it armed.
    if (p.support >= 0 && gone(state.world.platforms[static_cast<std::size_t>(p.support)].phase)) continue;
    limit_owned(rules, state, p.owner, p.what.kind, p.tag == 1 ? kThornMax : kSpringMax, events, p.tag);
    register_placed(state, fp::place(state.world, p.what), p.owner, p.uses, p.support, p.tag);
  }
}

void after_step(const Rules& rules, State& state, const fp::Events& events, fp::Events& scratch) {
  for (const fp::Event& e : events) {
    if (e.type == fp::EventType::PlaceableRemoved) {
      state.meta[static_cast<std::size_t>(e.id)] = {};
    } else if (e.type == fp::EventType::Sprung) {
      PlaceableMeta& m = state.meta[static_cast<std::size_t>(e.other)];
      if (m.owner >= 0) blame(state, e.id, m.owner);
      if (m.uses > 0 && --m.uses == 0) {
        fp::remove_placeable(rules.engine, state.world, e.other, scratch);
        m = {};
      }
    }
  }
  // Standing in someone's glue or oil is credited to them.
  for (std::size_t slot = 0; slot < state.world.placeables.size(); ++slot) {
    const fp::Placeable& p = state.world.placeables[slot];
    const PlaceableMeta& m = state.meta[slot];
    if (m.owner < 0 || p.active == 0) continue;
    if (p.kind != fp::PlaceableKind::Sticky && p.kind != fp::PlaceableKind::Slick) continue;
    for (std::size_t i = 0; i < state.players.size(); ++i) {
      const fp::EntityState& e = state.world.entities[i];
      if (e.eliminated != 0 || e.mode == fp::Mode::Airborne || static_cast<int>(i) == m.owner) continue;
      if (e.pos.x >= p.rect.left && e.pos.x <= p.rect.right && e.pos.y >= p.rect.bottom && e.pos.y <= p.rect.top) {
        blame(state, static_cast<int>(i), m.owner);
      }
    }
  }

  // Brambles chew the tiles under them.
  for (std::size_t slot = 0; slot < state.world.placeables.size(); ++slot) {
    const fp::Placeable& p = state.world.placeables[slot];
    const PlaceableMeta& m = state.meta[slot];
    if (m.owner < 0 || p.active == 0 || m.tag != 1) continue;
    const auto& platforms = rules.engine.stage.platforms;
    for (std::size_t t = 0; t < platforms.size(); ++t) {
      if (rules.slab_hp[t] != 0 || !in_play(state.world.platforms[t].phase)) continue;
      const fp::Rect& r = platforms[t].rect;
      if (r.top < p.rect.bottom || r.top > p.rect.top || r.right <= p.rect.left || r.left >= p.rect.right) continue;
      stress_tile(rules, state, static_cast<int>(t), Fixed::from_raw(Fixed::kOneRaw / 130), m.owner, scratch);
    }
  }

  // What stood on a tile goes when the tile does.
  for (std::size_t slot = 0; slot < state.meta.size(); ++slot) {
    const PlaceableMeta& m = state.meta[slot];
    if (m.owner < 0 || m.support < 0) continue;
    if (gone(state.world.platforms[static_cast<std::size_t>(m.support)].phase)) {
      fp::remove_placeable(rules.engine, state.world, static_cast<int>(slot), scratch);
      state.meta[slot] = {};
    }
  }
}

void credit_crumbles(const Rules& rules, State& state, const fp::Events& events, const std::vector<std::int16_t>& support_before) {
  for (const fp::Event& e : events) {
    if (e.type == fp::EventType::PlatformRespawned) {
      state.slab_hp[static_cast<std::size_t>(e.id)] = static_cast<std::int8_t>(rules.slab_hp[static_cast<std::size_t>(e.id)]);
      continue;
    }
    if (e.type != fp::EventType::PlatformCrumbled) continue;
    // A tile someone stressed: they are credited with it, and with whoever it drops.
    TileBlame& tb = state.tile_blame[static_cast<std::size_t>(e.id)];
    if (tb.by >= 0 && static_cast<std::int32_t>(state.world.frame) - tb.tick <= kBlameWindowTicks) {
      Stats& st = state.players[static_cast<std::size_t>(tb.by)].stats;
      ++(rules.slab_hp[static_cast<std::size_t>(e.id)] != 0 ? st.slabs_broken : st.tiles_broken);
      for (std::size_t i = 0; i < support_before.size(); ++i) {
        if (support_before[i] == e.id) blame(state, static_cast<int>(i), tb.by);
      }
    }
    tb = {};
  }
}

}  // namespace ledgefall::detail
