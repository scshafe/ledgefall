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
constexpr int kPatchMax = 2, kAcidMax = 1;

// Pip's gust: a fan that blows 340 px ahead for 3 s after a 0.4 s spin-up.
constexpr int kGustLife = 180, kGustArm = 24;
const Fixed kGustLen = fx100(10880), kGustHalfHeight = fx100(1920);
const Fixed kGustForce = fx100(9);      // 0.27 px/tick^2 at the fan, half that at the far end
const Fixed kGustLobPush = fx100(4);    // and what it does to a lob in flight (0.12 px/tick^2)

// Pip's drill: a hop, then a dive that breaks the tiles it lands on.
constexpr int kDrillHopTicks = 10;
const Fixed kDrillHop = fx100(180);     // 8 px/tick up
const Fixed kDrillDive = fx100(350);    // 16 px/tick down
const Fixed kDrillBounce = fx100(200);  // 9 px/tick up
const Fixed kDrillNeighbourStress = fx100(40);
const Fixed kDrillReach = fx100(4500);  // neighbours within 140 px
const Fixed kAcidStress = Fixed::from_raw(Fixed::kOneRaw / 40);

bool in_play(fp::PlatformPhase p) {
  return p == fp::PlatformPhase::Solid || p == fp::PlatformPhase::Shaking || p == fp::PlatformPhase::Doomed;
}

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

}  // namespace

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
    } else if (o.kind == ObjectKind::Gust) {
      if (o.arm > 0) --o.arm;
      if (--o.timer <= 0) o = {};
    } else if (o.kind == ObjectKind::Acid) {
      // Dissolves the tiles under it in under a second.
      each_surface(rules, state, [&](const Surface& s) {
        if (s.tile < 0 || s.top != o.pos.y || s.right <= o.x0 || s.left >= o.x1) return;
        stress_tile(rules, state, s.tile, kAcidStress, o.owner, scratch);
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
      // The tile it landed on breaks; the tiles around it, in the same row, are stressed.
      stress_tile(rules, state, ev.other, Fixed::one(), ev.id, scratch);
      for (std::size_t t = 0; t < platforms.size(); ++t) {
        if (static_cast<int>(t) == ev.other || platforms[t].rect.top != platforms[static_cast<std::size_t>(ev.other)].rect.top) continue;
        if (!in_play(state.world.platforms[t].phase)) continue;
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
