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
