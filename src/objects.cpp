// Things in flight or at work that the engine has no slot for: Glim's lobs and
// acid puddles. A lob arcs, lands on a tile and becomes a patch on it (glue and
// oil are the engine's sticky and slick patches) or an acid puddle that eats
// the tiles under it.
#include "internal.hpp"

namespace ledgefall::detail {
namespace {

using fp::Fixed;

// Lost-footing's numbers at 0.32 units a pixel (gravity 0.45 px/tick^2).
const Fixed kLobGravity = fx100(14);
constexpr int kGlueLife = 420, kOilLife = 420, kAcidLife = 150;
const Fixed kGlueWidth = fx100(2300), kOilWidth = fx100(3500), kAcidWidth = fx100(2600);
constexpr int kPatchMax = 2, kAcidMax = 1, kAcidSlabEvery = 48;

// Pip's gust: a fan that blows 340 px ahead for 3 s after a 0.4 s spin-up.
constexpr int kGustLife = 180, kGustArm = 24;
const Fixed kGustLen = fx100(10880), kGustHalfHeight = fx100(1920);
const Fixed kGustForce = fx100(9);      // 0.27 px/tick^2 at the fan, half that at the far end
const Fixed kGustLobPush = fx100(4);    // and what it does to a lob in flight (0.12 px/tick^2)

// Pip's drill: a hop, then a dive that breaks the tiles it lands on.
// Mason's anvil (30 x 26 px): punches through up to three crumble tiles, or
// does 2 damage to a slab and lies on it a while.
constexpr int kAnvilMaxBreaks = 3, kAnvilRestTicks = 45;
const Fixed kAnvilHalfWidth = fx100(480), kAnvilHeight = fx100(830);
const Fixed kAnvilGravity = fx100(19), kAnvilMaxFall = fx100(448);
const Fixed kSpringBounce = fx100(416);  // a spring launches an anvil at 13 px/tick

constexpr int kDrillHopTicks = 10;
const Fixed kDrillHop = fx100(180);     // 8 px/tick up
const Fixed kDrillDive = fx100(350);    // 16 px/tick down
const Fixed kDrillBounce = fx100(200);  // 9 px/tick up
const Fixed kDrillNeighbourStress = fx100(40);
const Fixed kDrillReach = fx100(4500);  // neighbours within 140 px
const Fixed kAcidStress = Fixed::from_raw(Fixed::kOneRaw / 40);

struct Surface {
  Fixed left, right, top;
  int tile;  // stage platform, or -1 for a wall
};

// What a lob can land on, found highest first by the caller.
template <class F>
void each_surface(const Rules& rules, const State& state, F&& f) {
  const auto& platforms = rules.engine.stage.platforms;
  for (std::size_t i = 0; i < platforms.size(); ++i) {
    if (!in_play(state.world.platforms[i].phase)) continue;
    const fp::PlatformState& s = state.world.platforms[i];
    f(Surface{platforms[i].rect.left + s.offset.x, platforms[i].rect.right + s.offset.x,
              platforms[i].rect.top + s.offset.y, static_cast<int>(i)});
  }
  for (const fp::Placeable& p : state.world.placeables) {
    if (p.active != 0 && p.kind == fp::PlaceableKind::Wall) f(Surface{p.rect.left, p.rect.right, p.rect.top, -1});
  }
}

Object* free_object(State& state) {
  for (Object& o : state.objects) {
    if (o.kind == ObjectKind::None) return &o;
  }
  return nullptr;
}

// A lob lands on a tile: it becomes a patch, or an acid puddle.
void land(const Rules& rules, State& state, const Object& lob, const Surface& on, fp::Events& scratch) {
  if (on.tile < 0) return;  // the top of a wall: wasted
  const bool acid = lob.payload == Payload::Acid;
  const Fixed width = acid ? kAcidWidth : lob.payload == Payload::Glue ? kGlueWidth : kOilWidth;
  Fixed x0 = lob.pos.x - width / Fixed::from_int(2), x1 = lob.pos.x + width / Fixed::from_int(2);

  // Clip it to the tiles in the same row it touches.
  Fixed lo = Fixed::from_int(1 << 14), hi = Fixed::from_int(-(1 << 14));
  each_surface(rules, state, [&](const Surface& s) {
    if (s.tile < 0 || s.top != on.top || s.right <= x0 || s.left >= x1) return;
    if (s.left < lo) lo = s.left;
    if (s.right > hi) hi = s.right;
  });
  if (x0 < lo) x0 = lo;
  if (x1 > hi) x1 = hi;

  if (acid) {
    for (;;) {  // one puddle at a time per owner
      int count = 0;
      Object* oldest = nullptr;
      for (Object& o : state.objects) {
        if (o.kind != ObjectKind::Acid || o.owner != lob.owner) continue;
        ++count;
        if (oldest == nullptr || o.timer < oldest->timer) oldest = &o;
      }
      if (count < kAcidMax) break;
      *oldest = {};
    }
    if (Object* o = free_object(state)) {
      *o = Object{ObjectKind::Acid, lob.owner, Payload::Acid, {(x0 + x1) / Fixed::from_int(2), on.top}, {}, x0, x1,
                  static_cast<std::int16_t>(kAcidLife)};
    }
    return;
  }

  const bool glue = lob.payload == Payload::Glue;
  const fp::PlaceableKind kind = glue ? fp::PlaceableKind::Sticky : fp::PlaceableKind::Slick;
  limit_owned(rules, state, lob.owner, kind, kPatchMax, scratch);
  fp::Placeable patch;
  patch.kind = kind;
  patch.rect = {x0, x1, on.top - Fixed::one(), on.top + Fixed::one()};
  patch.life = glue ? kGlueLife : kOilLife;
  // Glue: speed x0.1 and sticky. Oil: almost no traction, top speed x1.25.
  patch.traction_scale = glue ? Fixed::from_int(3) : fx100(5);
  patch.speed_scale = glue ? fx100(10) : fx100(125);
  register_placed(state, fp::place(state.world, patch), lob.owner, 0, static_cast<std::int16_t>(on.tile));
}

// An anvil falls (or lies on a slab for a moment). Crossing a surface it
// breaks what it can; every surface at the highest top crossed is hit.
void update_anvil(const Rules& rules, State& state, Object& o, fp::Events& scratch) {
  if (o.rest > 0) {
    if (--o.rest <= 0) o = {};
    return;
  }
  for (const Object& g : state.objects) {  // gusts push anvils a little
    if (g.kind != ObjectKind::Gust || g.arm > 0) continue;
    const Fixed d = (o.pos.x - g.pos.x) * g.vel.x;
    if (d >= Fixed::zero() && d <= kGustLen && fp::abs(o.pos.y - g.pos.y) < kGustHalfHeight) o.vel.x = o.vel.x + g.vel.x * kGustLobPush / Fixed::from_int(10);
  }
  const Fixed prev_bottom = o.pos.y;
  o.pos.x = o.pos.x + o.vel.x;
  o.vel.y = o.vel.y - kAnvilGravity;
  if (o.vel.y < Fixed::zero() - kAnvilMaxFall) o.vel.y = Fixed::zero() - kAnvilMaxFall;
  const Fixed new_bottom = o.pos.y + o.vel.y;
  const Fixed left = o.pos.x - kAnvilHalfWidth, right = o.pos.x + kAnvilHalfWidth;
  if (o.vel.y < Fixed::zero()) {
    // A spring under it throws it back up.
    for (std::size_t slot = 0; slot < state.world.placeables.size(); ++slot) {
      const fp::Placeable& p = state.world.placeables[slot];
      if (p.active == 0 || p.kind != fp::PlaceableKind::Spring || right <= p.rect.left || left >= p.rect.right) continue;
      if (prev_bottom >= p.rect.bottom && new_bottom <= p.rect.top) {
        o.vel.y = kSpringBounce;
        o.pos.y = p.rect.top;
        PlaceableMeta& m = state.meta[slot];
        if (m.uses > 0 && --m.uses == 0) {
          fp::remove_placeable(rules.engine, state.world, static_cast<int>(slot), scratch);
          m = {};
        }
        return;
      }
    }
    bool found = false;
    Fixed top = Fixed::zero();
    each_surface(rules, state, [&](const Surface& s) {
      if (right <= s.left + fx100(60) || left >= s.right - fx100(60)) return;
      if (prev_bottom >= s.top && new_bottom <= s.top && (!found || s.top > top)) {
        top = s.top;
        found = true;
      }
    });
    if (found) {
      bool stopped = false;
      each_surface(rules, state, [&](const Surface& s) {
        if (s.top != top || right <= s.left + fx100(60) || left >= s.right - fx100(60)) return;
        if (s.tile < 0) {  // a wall under it is smashed
          for (std::size_t slot = 0; slot < state.world.placeables.size(); ++slot) {
            const fp::Placeable& p = state.world.placeables[slot];
            if (p.active != 0 && p.kind == fp::PlaceableKind::Wall && p.rect.top == top && right > p.rect.left && left < p.rect.right) {
              fp::remove_placeable(rules.engine, state.world, static_cast<int>(slot), scratch);
              state.meta[slot] = {};
            }
          }
          return;
        }
        damage_tile(rules, state, s.tile, 2, o.owner, scratch);
        if (rules.slab_hp[static_cast<std::size_t>(s.tile)] != 0 && in_play(state.world.platforms[static_cast<std::size_t>(s.tile)].phase)) stopped = true;
      });
      ++o.breaks;
      if (stopped) {
        o.pos.y = top;
        o.vel = {};
        o.rest = kAnvilRestTicks;
        return;
      }
      if (o.breaks >= kAnvilMaxBreaks) {
        o = {};
        return;
      }
      o.vel.y = o.vel.y * fx100(60);
      o.vel.x = o.vel.x * fx100(50);
    }
  }
  o.pos.y = o.pos.y + o.vel.y;
  const fp::Rect& blast = rules.engine.stage.blast;
  if (o.pos.y < blast.bottom) o = {};
}

}  // namespace

bool in_play(fp::PlatformPhase p) {
  return p == fp::PlatformPhase::Solid || p == fp::PlatformPhase::Shaking || p == fp::PlatformPhase::Doomed;
}

void damage_tile(const Rules& rules, State& state, int tile, int amount, int by, fp::Events& scratch) {
  const auto t = static_cast<std::size_t>(tile);
  if (!in_play(state.world.platforms[t].phase)) return;
  if (rules.slab_hp[t] == 0) {
    stress_tile(rules, state, tile, Fixed::one(), by, scratch);
    return;
  }
  if (by >= 0) state.tile_blame[t] = {static_cast<std::int8_t>(by), static_cast<std::int32_t>(state.world.frame)};
  state.slab_hp[t] = static_cast<std::int8_t>(state.slab_hp[t] - amount);
  if (state.slab_hp[t] <= 0) stress_tile(rules, state, tile, Fixed::one(), by, scratch);
}

void stress_tile(const Rules& rules, State& state, int tile, Fixed amount, int by, fp::Events& scratch) {
  fp::add_stress(rules.engine, state.world, tile, amount, scratch);
  state.tile_blame[static_cast<std::size_t>(tile)] = {static_cast<std::int8_t>(by), static_cast<std::int32_t>(state.world.frame)};
}

bool throw_lob(State& state, const Rules& rules, int seat, Payload payload, const Input& in) {
  Object* o = free_object(state);
  if (o == nullptr) return false;
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const fp::MovementParams& m = rules.engine.params[static_cast<std::size_t>(seat)];
  // Lost-footing: up (jump) lobs high, forward throws far, neutral is between.
  const int facing = e.facing;
  const bool forward = (in.move_x > 25 && facing > 0) || (in.move_x < -25 && facing < 0);
  fp::Vec2 v;
  if ((in.buttons & kButtonJump) != 0) v = {fx100(144 * facing), fx100(320)};
  else if (forward) v = {fx100(288 * facing), fx100(192)};
  else v = {fx100(208 * facing), fx100(192)};
  *o = Object{ObjectKind::Lob, static_cast<std::int8_t>(seat), payload,
              {e.pos.x + fx100(320 * facing), e.pos.y + m.body_height - fx100(260)}, v, {}, {}, 0};
  return true;
}

void update_objects(const Rules& rules, State& state, fp::Events& scratch) {
  const fp::Rect& blast = rules.engine.stage.blast;
  for (Object& o : state.objects) {
    if (o.kind == ObjectKind::Lob) {
      const fp::Vec2 prev = o.pos;
      o.vel.y = o.vel.y - kLobGravity;
      for (const Object& g : state.objects) {  // gusts push lobs in flight
        if (g.kind != ObjectKind::Gust || g.arm > 0) continue;
        const Fixed d = (o.pos.x - g.pos.x) * g.vel.x;
        if (d >= Fixed::zero() && d <= kGustLen && fp::abs(o.pos.y - g.pos.y) < kGustHalfHeight) o.vel.x = o.vel.x + g.vel.x * kGustLobPush;
      }
      o.pos = o.pos + o.vel;
      // A wall's side bounces a lob back.
      for (const fp::Placeable& w : state.world.placeables) {
        if (w.active == 0 || w.kind != fp::PlaceableKind::Wall) continue;
        if (o.pos.y > w.rect.bottom && o.pos.y < w.rect.top &&
            ((prev.x <= w.rect.left && o.pos.x > w.rect.left) || (prev.x >= w.rect.right && o.pos.x < w.rect.right))) {
          o.pos.x = prev.x;
          o.vel.x = Fixed::zero() - o.vel.x * fx100(30);
        }
      }
      if (o.vel.y <= Fixed::zero()) {
        const Surface* best = nullptr;
        Surface found{};
        each_surface(rules, state, [&](const Surface& s) {
          if (o.pos.x < s.left || o.pos.x > s.right) return;
          if (prev.y >= s.top && o.pos.y <= s.top && (best == nullptr || s.top > best->top)) {
            found = s;
            best = &found;
          }
        });
        if (best != nullptr) {
          o.pos.y = best->top;
          const Object lob = o;
          o = {};
          land(rules, state, lob, found, scratch);
          continue;
        }
      }
      if (o.pos.y < blast.bottom || o.pos.x < blast.left || o.pos.x > blast.right) o = {};
    } else if (o.kind == ObjectKind::Anvil) {
      update_anvil(rules, state, o, scratch);
    } else if (o.kind == ObjectKind::Gust) {
      if (o.arm > 0) --o.arm;
      if (--o.timer <= 0) o = {};
    } else if (o.kind == ObjectKind::Acid) {
      // Dissolves the tiles under it in under a second.
      each_surface(rules, state, [&](const Surface& s) {
        if (s.tile < 0 || s.top != o.pos.y || s.right <= o.x0 || s.left >= o.x1) return;
        if (rules.slab_hp[static_cast<std::size_t>(s.tile)] == 0) stress_tile(rules, state, s.tile, kAcidStress, o.owner, scratch);
        else if ((kAcidLife - o.timer) % kAcidSlabEvery == 0) damage_tile(rules, state, s.tile, 1, o.owner, scratch);  // 1 hp per 0.8 s
      });
      if (--o.timer <= 0) o = {};
    }
  }
}

}  // namespace ledgefall::detail

namespace ledgefall::detail {

bool cast_gust(const Rules& rules, State& state, int seat) {
  for (Object& o : state.objects) {  // one at once: the old one goes
    if (o.kind == ObjectKind::Gust && o.owner == seat) o = {};
  }
  Object* o = free_object(state);
  if (o == nullptr) return false;
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const fp::MovementParams& m = rules.engine.params[static_cast<std::size_t>(seat)];
  *o = Object{ObjectKind::Gust, static_cast<std::int8_t>(seat), Payload::Glue,
              {e.pos.x, e.pos.y + m.body_height / Fixed::from_int(2)}, {Fixed::from_int(e.facing), Fixed::zero()}, {}, {},
              static_cast<std::int16_t>(kGustLife), static_cast<std::int16_t>(kGustArm)};
  return true;
}

bool cast_drill(State& state, int seat) {
  PlayerState& p = state.players[static_cast<std::size_t>(seat)];
  fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  if (e.mode != fp::Mode::Airborne) {
    fp::start_fall(e);
    e.vel.y = kDrillHop;
    p.drill = kDrillHopTicks;
  } else {
    p.drill = -1;
  }
  return true;
}

void apply_wind_and_drill(const Rules& rules, State& state, std::vector<fp::Intent>& intents) {
  for (std::size_t i = 0; i < state.players.size(); ++i) {
    fp::EntityState& e = state.world.entities[i];
    PlayerState& p = state.players[i];
    if (e.eliminated != 0) {
      p.drill = 0;
      continue;
    }
    const fp::MovementParams& m = rules.engine.params[i];

    // Wind: whoever stands in a spinning gust is pushed along it (its owner too).
    const Fixed cy = e.pos.y + m.body_height / Fixed::from_int(2);
    for (const Object& g : state.objects) {
      if (g.kind != ObjectKind::Gust || g.arm > 0) continue;
      const Fixed d = (e.pos.x - g.pos.x) * g.vel.x;
      if (d < Fixed::zero() || d > kGustLen || fp::abs(cy - g.pos.y) > kGustHalfHeight) continue;
      e.vel.x = e.vel.x + g.vel.x * kGustForce * (Fixed::one() - d / kGustLen / Fixed::from_int(2));
      blame(state, static_cast<int>(i), g.owner);
    }

    // The drill: an air jump cancels a dive; otherwise the hop slows, then the dive takes over.
    if (p.drill == -1 && (intents[i].jump && e.prev_jump == 0) && e.jumps_used < m.aerial_jumps &&
        e.mode == fp::Mode::Airborne) {
      p.drill = 0;
    }
    if (p.drill > 0) {
      e.vel.x = e.vel.x * fx100(80);
      if (--p.drill == 0) p.drill = -1;
    }
    if (p.drill == -1) {
      e.vel.x = Fixed::zero();
      e.vel.y = Fixed::zero() - kDrillDive;
      intents[i].move_x = Fixed::zero();
      intents[i].jump = false;
      intents[i].fast_fall = false;
    }
  }
}

void drill_landings(const Rules& rules, State& state, const fp::Events& events, fp::Events& scratch) {
  for (const fp::Event& ev : events) {
    if (ev.type != fp::EventType::Landed) continue;
    PlayerState& p = state.players[static_cast<std::size_t>(ev.id)];
    if (p.drill != -1) continue;
    p.drill = 0;
    fp::EntityState& e = state.world.entities[static_cast<std::size_t>(ev.id)];
    const auto& platforms = rules.engine.stage.platforms;
    if (ev.other >= 0 && ev.other < static_cast<int>(platforms.size())) {
      // The tile it landed on breaks (a slab takes 2 damage); the tiles around it, in the same row, are stressed.
      damage_tile(rules, state, ev.other, 2, ev.id, scratch);
      for (std::size_t t = 0; t < platforms.size(); ++t) {
        if (static_cast<int>(t) == ev.other || platforms[t].rect.top != platforms[static_cast<std::size_t>(ev.other)].rect.top) continue;
        if (!in_play(state.world.platforms[t].phase) || rules.slab_hp[t] != 0) continue;
        const Fixed mid = (platforms[t].rect.left + platforms[t].rect.right) / Fixed::from_int(2);
        if (fp::abs(mid - e.pos.x) < kDrillReach) stress_tile(rules, state, static_cast<int>(t), kDrillNeighbourStress, ev.id, scratch);
      }
    } else if (ev.other >= static_cast<int>(platforms.size())) {
      fp::remove_placeable(rules.engine, state.world, ev.other - static_cast<int>(platforms.size()), scratch);  // a wall
      state.meta[static_cast<std::size_t>(ev.other) - platforms.size()] = {};
    }
    fp::start_fall(e);
    e.vel.y = kDrillBounce;
    e.jumps_used = 0;
  }
}

}  // namespace ledgefall::detail

namespace ledgefall::detail {

bool cast_anvil(State& state, const Rules& rules, int seat, const Input& in) {
  Object* o = nullptr;
  for (Object& c : state.objects) {
    if (c.kind == ObjectKind::None) {
      o = &c;
      break;
    }
  }
  if (o == nullptr) return false;
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const fp::MovementParams& m = rules.engine.params[static_cast<std::size_t>(seat)];
  const bool forward = (in.move_x > 25 && e.facing > 0) || (in.move_x < -25 && e.facing < 0);
  *o = Object{ObjectKind::Anvil, static_cast<std::int8_t>(seat), Payload::Glue,
              {e.pos.x + fx100(384 * e.facing), e.pos.y + m.body_height - fx100(450)},
              {fx100((forward ? 160 : 112) * e.facing), fx100(160)}, {}, {}, 0, 0, 0, 0};
  return true;
}

}  // namespace ledgefall::detail

namespace ledgefall::detail {

fp::Rect object_rect(const Object& o) {
  switch (o.kind) {
    case ObjectKind::Lob: {
      const Fixed r = fx100(250);
      return {o.pos.x - r, o.pos.x + r, o.pos.y - r, o.pos.y + r};
    }
    case ObjectKind::Acid:
      return {o.x0, o.x1, o.pos.y, o.pos.y + fx100(120)};
    case ObjectKind::Gust:
      return o.vel.x > Fixed::zero() ? fp::Rect{o.pos.x, o.pos.x + kGustLen, o.pos.y - kGustHalfHeight, o.pos.y + kGustHalfHeight}
                                     : fp::Rect{o.pos.x - kGustLen, o.pos.x, o.pos.y - kGustHalfHeight, o.pos.y + kGustHalfHeight};
    case ObjectKind::Anvil:
      return {o.pos.x - kAnvilHalfWidth, o.pos.x + kAnvilHalfWidth, o.pos.y, o.pos.y + kAnvilHeight};
    case ObjectKind::None:
      break;
  }
  return {};
}

}  // namespace ledgefall::detail
