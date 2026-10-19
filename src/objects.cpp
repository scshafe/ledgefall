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

// Fuse's bumper, charge and saw (lost-footing's numbers at 0.32 units a pixel).
const Fixed kBumperRadius = fx100(704), kBumperSpeed = fx100(368), kBumperLift = fx100(64);
constexpr int kBumperLife = 420, kBumperArm = 30, kBumperUses = 4, kBumperMax = 2, kBumperRecheck = 15;
const Fixed kChargeRadius = fx100(256), kChargeBlast = fx100(3680);
constexpr int kChargeFuse = 100, kChargeMax = 1;
const Fixed kSawRadius = fx100(448), kSawSpeed = fx100(224), kSawGlueSpeed = fx100(90), kSawOilSpeed = fx100(336);
const Fixed kSawDoom = fx100(80);  // a tile it cuts is at least this stressed (lost-footing: 0.8)
constexpr int kSawLife = 130;
const Fixed kObjGravity = fx100(19), kObjMaxFall = fx100(448);

// Fern's thorns and roots, Dab's rub out.
constexpr int kThornGrow = 24, kThornLife = 300;
const Fixed kThornNear = fx100(960), kThornWidth = fx100(4800);
const Fixed kThornSpeed = fx100(40);  // speed x0.4
constexpr int kRootsFuse = 60, kRootsEvery = 8;
const Fixed kRootsAhead = fx100(3520), kRootsReach = fx100(4000);
const Fixed kRubNear = fx100(256), kRubReach = fx100(5440);
constexpr int kRubDelay = 30;

// Ferro's lodestone (lost-footing's numbers at 0.32 units a pixel).
const Fixed kMagnetRadius = fx100(576), kMagnetReach = fx100(7680), kMagnetHold = fx100(768);
const Fixed kMagnetStrength = fx100(16), kMagnetBase = fx100(4), kMagnetMaxSpeed = fx100(448), kMagnetBurst = fx100(352);
const Fixed kMagnetAhead = fx100(2240), kMagnetLift = fx100(770);
constexpr int kMagnetLife = 300, kMagnetArm = 10, kMagnetFlip = 150;

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

// sqrt of a non-negative Fixed, rounded down, by integer Newton iteration.
Fixed fx_sqrt(Fixed v) {
  if (v <= Fixed::zero()) return Fixed::zero();
  const std::uint64_t n = static_cast<std::uint64_t>(v.raw()) << Fixed::kFracBits;  // the result in raw units, squared
  std::uint64_t x = n, y = (x + 1) / 2;
  while (y < x) {
    x = y;
    y = (x + n / x) / 2;
  }
  return Fixed::from_raw(static_cast<std::int32_t>(x));
}

// Does a circle overlap a box?
bool circle_box(Fixed cx, Fixed cy, Fixed r, const fp::Rect& b) {
  const Fixed nx = cx < b.left ? b.left : cx > b.right ? b.right : cx;
  const Fixed ny = cy < b.bottom ? b.bottom : cy > b.top ? b.top : cy;
  const Fixed dx = cx - nx, dy = cy - ny;
  return dx * dx + dy * dy < r * r;
}

struct Pull {
  bool any = false;
  Fixed ax{}, ay{};
  const Object* by = nullptr;
  const Object* held = nullptr;  // the lodestone it hangs on
};

// What the lodestones do to a loose thing at (x, y): pull it (push, while flipped), harder the
// closer it is; within reach of the pole it hangs on it.
Pull magnet_pull(const State& state, Fixed x, Fixed y) {
  Pull p;
  for (const Object& m : state.objects) {
    if (m.kind != ObjectKind::Magnet || m.arm > 0) continue;
    const Fixed dx = m.pos.x - x, dy = m.pos.y - y;
    const Fixed d = fx_sqrt(dx * dx + dy * dy);
    if (d > kMagnetReach) continue;
    const bool push = m.uses == 1;
    if (!push && d < kMagnetHold) p.held = &m;
    if (d < Fixed::one()) continue;
    Fixed a = kMagnetStrength * (Fixed::one() - d / kMagnetReach) + kMagnetBase;
    if (push) a = Fixed::zero() - a;
    p.ax = p.ax + a * dx / d;
    p.ay = p.ay + a * dy / d;
    p.by = &m;
    p.any = true;
  }
  return p;
}

Fixed cap_speed(Fixed v) {
  return v > kMagnetMaxSpeed ? kMagnetMaxSpeed : v < Fixed::zero() - kMagnetMaxSpeed ? Fixed::zero() - kMagnetMaxSpeed : v;
}

Object* free_object(State& state) {
  for (Object& o : state.objects) {
    if (o.kind == ObjectKind::None) return &o;
  }
  return nullptr;
}

// Clip [x0, x1] to the tiles of the row at `top` that it touches; false if it touches none.
bool clip_to_row(const Rules& rules, const State& state, Fixed top, Fixed& x0, Fixed& x1) {
  Fixed lo = Fixed::from_int(1 << 14), hi = Fixed::from_int(-(1 << 14));
  each_surface(rules, state, [&](const Surface& s) {
    if (s.tile < 0 || s.top != top || s.right <= x0 || s.left >= x1) return;
    if (s.left < lo) lo = s.left;
    if (s.right > hi) hi = s.right;
  });
  if (lo > hi) return false;
  if (x0 < lo) x0 = lo;
  if (x1 > hi) x1 = hi;
  return true;
}

// A lob lands on a tile: it becomes a patch, or an acid puddle.
void land(const Rules& rules, State& state, const Object& lob, const Surface& on, fp::Events& scratch) {
  if (lob.payload == Payload::Charge) {  // a charge sticks wherever it lands
    for (;;) {
      int count = 0;
      Object* oldest = nullptr;
      for (Object& o : state.objects) {
        if (o.kind != ObjectKind::Charge || o.owner != lob.owner) continue;
        ++count;
        if (oldest == nullptr || o.timer < oldest->timer) oldest = &o;
      }
      if (count < kChargeMax) break;
      *oldest = {};
    }
    if (Object* o = free_object(state)) {
      *o = Object{ObjectKind::Charge, lob.owner, Payload::Charge, {lob.pos.x, on.top + kChargeRadius}, {}, {}, {}, static_cast<std::int16_t>(kChargeFuse)};
    }
    return;
  }
  if (on.tile < 0) return;  // the top of a wall: wasted
  const bool acid = lob.payload == Payload::Acid;
  const Fixed width = acid ? kAcidWidth : lob.payload == Payload::Glue ? kGlueWidth : kOilWidth;
  Fixed x0 = lob.pos.x - width / Fixed::from_int(2), x1 = lob.pos.x + width / Fixed::from_int(2);

  // Clip it to the tiles in the same row it touches.
  if (!clip_to_row(rules, state, on.top, x0, x1)) return;

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
  const Pull pull = magnet_pull(state, o.pos.x, o.pos.y + kAnvilHeight / Fixed::from_int(2));
  if (pull.any) {  // iron: a lodestone lifts it off the floor and carries it
    o.owner = pull.by->owner;
    o.rest = 0;
    if (pull.held != nullptr) {
      o.pos = {pull.held->pos.x, pull.held->pos.y - kAnvilHeight / Fixed::from_int(2)};
      o.vel = {};
      return;
    }
    o.vel.x = cap_speed((o.vel.x + pull.ax) * fx100(95));
    o.vel.y = cap_speed((o.vel.y + pull.ay + kAnvilGravity) * fx100(95));
  }
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
      if (o.breaks >= o.uses) {
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

// A bumper bounces whoever touches it away: the direction from its centre to
// theirs, at 11.5 px/tick, a little upward.
void bounce_off(const Rules& rules, State& state, Object& b, int seat) {
  fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const fp::MovementParams& m = rules.engine.params[static_cast<std::size_t>(seat)];
  const Fixed half = m.body_height / Fixed::from_int(2);
  Fixed dx = e.pos.x - b.pos.x, dy = e.pos.y + half - b.pos.y;
  const Fixed len = fx_sqrt(dx * dx + dy * dy);
  if (len < fx100(1)) {
    dx = Fixed::zero();
    dy = Fixed::one();
  } else {
    dx = dx / len;
    dy = dy / len;
  }
  const Fixed big = m.body_height > m.body_half_width * Fixed::from_int(2) ? m.body_height : m.body_half_width * Fixed::from_int(2);
  const Fixed push = kBumperRadius + big / Fixed::from_int(2) + fx100(30);
  e.pos.x = b.pos.x + dx * push;
  e.pos.y = b.pos.y + dy * push - half;
  fp::start_fall(e);
  e.vel.x = dx * kBumperSpeed;
  e.vel.y = dy * kBumperSpeed + kBumperLift;
  e.fast_falling = 0;
  b.cool[static_cast<std::size_t>(seat)] = kBumperRecheck;
  blame(state, seat, b.owner);
  if (--b.uses <= 0) b = {};
}

void update_bumper(const Rules& rules, State& state, Object& b) {
  for (auto& c : b.cool) c = static_cast<std::int16_t>(c > 0 ? c - 1 : 0);
  const Pull pull = magnet_pull(state, b.pos.x, b.pos.y);
  if (pull.any) {
    b.owner = pull.by->owner;
    if (pull.held != nullptr) {
      b.pos = pull.held->pos;
      b.vel = {};
    } else {
      b.vel.x = cap_speed((b.vel.x + pull.ax) * fx100(92));
      b.vel.y = cap_speed((b.vel.y + pull.ay) * fx100(92));
    }
  } else {
    b.vel = b.vel * fx100(90);
    if (fp::abs(b.vel.x) < fx100(2)) b.vel.x = Fixed::zero();
    if (fp::abs(b.vel.y) < fx100(2)) b.vel.y = Fixed::zero();
  }
  b.pos = b.pos + b.vel;
  if (--b.timer <= 0) {
    b = {};
    return;
  }
  if (b.arm > 0) {
    --b.arm;
    return;
  }
  for (std::size_t i = 0; i < state.players.size() && b.kind == ObjectKind::Bumper; ++i) {
    const fp::EntityState& e = state.world.entities[i];
    if (e.eliminated != 0 || b.cool[i] > 0) continue;
    const fp::MovementParams& m = rules.engine.params[i];
    const fp::Rect body{e.pos.x - m.body_half_width, e.pos.x + m.body_half_width, e.pos.y, e.pos.y + m.body_height};
    if (circle_box(b.pos.x, b.pos.y, kBumperRadius, body)) bounce_off(rules, state, b, static_cast<int>(i));
  }
}

// Roots: the tiles around the seed shake while it swells, then are doomed; slabs in reach take 1.
void roots_shake(const Rules& rules, State& state, const Object& o, bool burst, fp::Events& scratch) {
  each_surface(rules, state, [&](const Surface& s) {
    if (s.tile < 0) return;
    const fp::Rect box{s.left, s.right, s.top - fx100(400), s.top};
    if (!circle_box(o.pos.x, o.pos.y - fx100(190), kRootsReach, box)) return;
    const auto t = static_cast<std::size_t>(s.tile);
    if (rules.slab_hp[t] == 0) {
      const fp::PlatformStress& st = rules.engine.stage.platforms[t].stress;
      const Fixed to = (burst ? st.doom_at : st.shake_at) + fx100(3);
      const Fixed now = state.world.platforms[t].stress;
      if (now < to) stress_tile(rules, state, s.tile, to - now, o.owner, scratch);
    } else if (burst) {
      damage_tile(rules, state, s.tile, 1, o.owner, scratch);
    }
  });
}

void update_roots(const Rules& rules, State& state, Object& o, fp::Events& scratch) {
  if (--o.timer > 0) {
    if (o.timer % kRootsEvery == 0) roots_shake(rules, state, o, false, scratch);
    return;
  }
  const Object seed = o;
  o = {};
  roots_shake(rules, state, seed, true, scratch);
}

// Dab's brushstroke lands: every platform in its span, at its height, is erased.
void update_rub(const Rules& rules, State& state, Object& o, fp::Events& scratch) {
  if (--o.timer > 0) return;
  const Object stroke = o;
  o = {};
  each_surface(rules, state, [&](const Surface& s) {
    if (s.tile < 0 || s.top != stroke.pos.y || s.left >= stroke.x1 || s.right <= stroke.x0) return;
    damage_tile(rules, state, s.tile, 2, stroke.owner, scratch);
  });
}

// Everything within reach of the blast is broken or destroyed.
void explode(const Rules& rules, State& state, const Object& c, fp::Events& scratch) {
  const Fixed r = kChargeBlast;
  each_surface(rules, state, [&](const Surface& s) {
    if (s.tile < 0) return;
    const fp::Rect box{s.left, s.right, s.top - fx100(400), s.top};
    if (circle_box(c.pos.x, c.pos.y, r, box)) damage_tile(rules, state, s.tile, 2, c.owner, scratch);
  });
  for (std::size_t slot = 0; slot < state.world.placeables.size(); ++slot) {
    const fp::Placeable& p = state.world.placeables[slot];
    if (p.active == 0 || p.kind == fp::PlaceableKind::Sticky || p.kind == fp::PlaceableKind::Slick) continue;
    if (circle_box(c.pos.x, c.pos.y, r, p.rect)) {
      fp::remove_placeable(rules.engine, state.world, static_cast<int>(slot), scratch);
      state.meta[slot] = {};
    }
  }
  for (Object& q : state.objects) {
    if (&q == &c || q.kind == ObjectKind::None) continue;
    bool hit = false;
    if (q.kind == ObjectKind::Anvil || q.kind == ObjectKind::Charge) hit = circle_box(c.pos.x, c.pos.y, r, detail::object_rect(q));
    else if (q.kind == ObjectKind::Bumper) {
      const Fixed dx = q.pos.x - c.pos.x, dy = q.pos.y - c.pos.y, rr = r + kBumperRadius;
      hit = dx * dx + dy * dy < rr * rr;
    } else if (q.kind == ObjectKind::Acid) hit = q.x1 > c.pos.x - r && q.x0 < c.pos.x + r && fp::abs(q.pos.y - c.pos.y) < r;
    if (hit) q = {};
  }
}

// A charge sits where it stuck; if its ground goes it falls. It beeps, then goes off.
void update_charge(const Rules& rules, State& state, Object& c, fp::Events& scratch) {
  const Fixed bottom = c.pos.y - kChargeRadius;
  bool held = false;
  each_surface(rules, state, [&](const Surface& s) {
    if (bottom == s.top && c.pos.x >= s.left && c.pos.x <= s.right) held = true;
  });
  const Pull pull = magnet_pull(state, c.pos.x, c.pos.y);
  if (pull.any) {  // pulled loose: it flies to the lodestone and hangs there, still counting down
    c.owner = pull.by->owner;
    if (pull.held != nullptr) {
      c.pos = pull.held->pos;
      c.vel = {};
    } else {
      c.vel.x = cap_speed((c.vel.x + pull.ax) * fx100(95));
      c.vel.y = cap_speed((c.vel.y + pull.ay) * fx100(95));
      c.pos = c.pos + c.vel;
    }
  } else if (!held) {
    if (c.vel.x != Fixed::zero()) {  // thrown by a flipped lodestone
      c.pos.x = c.pos.x + c.vel.x;
      c.vel.x = fp::abs(c.vel.x) < fx100(5) ? Fixed::zero() : c.vel.x * fx100(90);
    }
    const Fixed prev = bottom;
    c.vel.y = c.vel.y - kObjGravity;
    if (c.vel.y < Fixed::zero() - kObjMaxFall) c.vel.y = Fixed::zero() - kObjMaxFall;
    Fixed ny = c.pos.y + c.vel.y;
    each_surface(rules, state, [&](const Surface& s) {
      if (c.pos.x < s.left || c.pos.x > s.right) return;
      if (prev >= s.top && ny - kChargeRadius <= s.top) {
        ny = s.top + kChargeRadius;
        c.vel.y = Fixed::zero();
      }
    });
    c.pos.y = ny;
    if (c.pos.y < rules.engine.stage.blast.bottom) {
      c = {};
      return;
    }
  }
  if (--c.timer <= 0) {
    const Object boom = c;
    c = {};
    explode(rules, state, boom, scratch);
  }
}

// A saw rolls along the surface it is on, cutting; it drops off edges, turns at walls
// and bumpers, hops on springs, is slowed by glue and hurried by oil.
void update_saw(const Rules& rules, State& state, Object& o, fp::Events& scratch) {
  if (--o.timer <= 0) {
    o = {};
    return;
  }
  const Pull pull = magnet_pull(state, o.pos.x, o.pos.y);
  if (pull.any) {
    o.owner = pull.by->owner;
    if (state.world.frame % 20 == 0 && fp::abs(pull.ax) > fx100(1)) o.vel.x = pull.ax > Fixed::zero() ? Fixed::one() : Fixed::from_int(-1);
  }
  const Fixed bottom = o.pos.y - kSawRadius;
  Fixed speed = kSawSpeed;
  for (const fp::Placeable& p : state.world.placeables) {
    if (p.active == 0 || (p.kind != fp::PlaceableKind::Sticky && p.kind != fp::PlaceableKind::Slick)) continue;
    if (o.pos.x >= p.rect.left && o.pos.x <= p.rect.right && bottom >= p.rect.bottom && bottom <= p.rect.top) {
      speed = p.kind == fp::PlaceableKind::Sticky ? kSawGlueSpeed : kSawOilSpeed;
    }
  }
  // A strong gust against it turns it around now and then.
  for (const Object& g : state.objects) {
    if (g.kind != ObjectKind::Gust || g.arm > 0) continue;
    const Fixed d = (o.pos.x - g.pos.x) * g.vel.x;
    if (d >= Fixed::zero() && d <= kGustLen && fp::abs(o.pos.y - g.pos.y) < kGustHalfHeight && g.vel.x != o.vel.x && state.world.frame % 30 == 0) o.vel.x = g.vel.x;
  }
  const Fixed nx = o.pos.x + o.vel.x * speed;
  // Walls and bumpers send it back.
  for (const fp::Placeable& w : state.world.placeables) {
    if (w.active != 0 && w.kind == fp::PlaceableKind::Wall && circle_box(nx, o.pos.y, kSawRadius, w.rect)) {
      o.vel.x = o.pos.x < (w.rect.left + w.rect.right) / Fixed::from_int(2) ? Fixed::from_int(-1) : Fixed::one();
      return;
    }
  }
  for (const Object& b : state.objects) {
    if (b.kind != ObjectKind::Bumper || b.arm > 0) continue;
    const Fixed dx = nx - b.pos.x, dy = o.pos.y - b.pos.y, rr = kBumperRadius + kSawRadius;
    if (dx * dx + dy * dy < rr * rr) {
      o.vel.x = dx < Fixed::zero() ? Fixed::from_int(-1) : Fixed::one();
      return;
    }
  }
  o.pos.x = nx;
  // Fall, and land on the highest surface crossed. `rest` counts the ticks it has been falling
  // (it starts from rest each time it leaves a surface); the heading is vel.x.
  const Fixed prev = bottom;
  Fixed fall = Fixed::zero();
  bool on_top = false;
  Fixed top = Fixed::zero();
  each_surface(rules, state, [&](const Surface& s) {
    if (o.pos.x < s.left || o.pos.x > s.right) return;
    if (prev == s.top) {
      on_top = true;
      top = s.top;
    }
  });
  if (!on_top) {
    // In the air: gravity over the ticks it has been falling.
    ++o.rest;
    fall = kObjGravity * Fixed::from_int(o.rest);
    if (fall > kObjMaxFall) fall = kObjMaxFall;
    const Fixed ny = bottom - fall;
    Fixed land_top = Fixed::zero();
    bool found = false;
    each_surface(rules, state, [&](const Surface& s) {
      if (o.pos.x < s.left || o.pos.x > s.right) return;
      if (prev >= s.top && ny <= s.top && (!found || s.top > land_top)) {
        land_top = s.top;
        found = true;
      }
    });
    if (found) {
      o.pos.y = land_top + kSawRadius;
      o.rest = 0;
      on_top = true;
      top = land_top;
    } else {
      o.pos.y = ny + kSawRadius;
    }
  } else {
    o.rest = 0;
  }
  if (on_top) {
    // Cut what it rolls over: a tile is doomed, a slab chipped once.
    each_surface(rules, state, [&](const Surface& s) {
      if (s.tile < 0 || s.top != top) return;
      if (o.pos.x + kSawRadius / Fixed::from_int(2) < s.left || o.pos.x - kSawRadius / Fixed::from_int(2) > s.right) return;
      const auto t = static_cast<std::size_t>(s.tile);
      if (rules.slab_hp[t] == 0) {
        const fp::PlatformState& ps = state.world.platforms[t];
        if (ps.stress < kSawDoom) stress_tile(rules, state, s.tile, kSawDoom - ps.stress, o.owner, scratch);
      } else {
        for (auto& h : o.hit) {
          if (h == s.tile + 1) return;
        }
        for (auto& h : o.hit) {
          if (h == 0) {
            h = static_cast<std::int16_t>(s.tile + 1);
            break;
          }
        }
        damage_tile(rules, state, s.tile, 1, o.owner, scratch);
      }
    });
  }
  const fp::Rect& blast = rules.engine.stage.blast;
  if (o.pos.y < blast.bottom || o.pos.x < blast.left || o.pos.x > blast.right) o = {};
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
      const Pull pull = magnet_pull(state, o.pos.x, o.pos.y);
      if (pull.any) {  // a lodestone bends it, and what it does next is Ferro's
        o.owner = pull.by->owner;
        o.vel.x = o.vel.x + pull.ax;
        o.vel.y = o.vel.y + pull.ay;
      }
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
    } else if (o.kind == ObjectKind::Magnet) {
      if (o.arm > 0) --o.arm;
      if (o.uses == 1 && --o.rest <= 0) o.uses = 0;  // the flip wears off
      if (--o.timer <= 0) o = {};
    } else if (o.kind == ObjectKind::Roots) {
      update_roots(rules, state, o, scratch);
    } else if (o.kind == ObjectKind::Rub) {
      update_rub(rules, state, o, scratch);
    } else if (o.kind == ObjectKind::Bumper) {
      update_bumper(rules, state, o);
    } else if (o.kind == ObjectKind::Charge) {
      update_charge(rules, state, o, scratch);
    } else if (o.kind == ObjectKind::Saw) {
      update_saw(rules, state, o, scratch);
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

bool cast_anvil(State& state, const Rules& rules, int seat, const Input& in, bool ingot) {
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
              {fx100((forward ? 160 : 112) * e.facing), fx100(160)}, {}, {}, 0, 0, 0, 0,
              static_cast<std::int8_t>(ingot ? 1 : kAnvilMaxBreaks)};
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
    case ObjectKind::Bumper:
      return {o.pos.x - kBumperRadius, o.pos.x + kBumperRadius, o.pos.y - kBumperRadius, o.pos.y + kBumperRadius};
    case ObjectKind::Charge:
      return {o.pos.x - kChargeRadius, o.pos.x + kChargeRadius, o.pos.y - kChargeRadius, o.pos.y + kChargeRadius};
    case ObjectKind::Saw:
      return {o.pos.x - kSawRadius, o.pos.x + kSawRadius, o.pos.y - kSawRadius, o.pos.y + kSawRadius};
    case ObjectKind::Magnet:
      return {o.pos.x - kMagnetRadius, o.pos.x + kMagnetRadius, o.pos.y - kMagnetRadius, o.pos.y + kMagnetRadius};
    case ObjectKind::Roots:
      return {o.pos.x - fx100(300), o.pos.x + fx100(300), o.pos.y, o.pos.y + fx100(500)};
    case ObjectKind::Rub:
      return {o.x0, o.x1, o.pos.y, o.pos.y + fx100(150)};
    case ObjectKind::None:
      break;
  }
  return {};
}

}  // namespace ledgefall::detail

namespace ledgefall::detail {

bool cast_bumper(State& state, const Rules& rules, int seat, fp::Events& events) {
  (void)events;
  for (;;) {  // two at once: the oldest goes
    int count = 0;
    Object* oldest = nullptr;
    for (Object& o : state.objects) {
      if (o.kind != ObjectKind::Bumper || o.owner != seat) continue;
      ++count;
      if (oldest == nullptr || o.timer < oldest->timer) oldest = &o;
    }
    if (count < kBumperMax) break;
    *oldest = {};
  }
  Object* slot = nullptr;
  for (Object& o : state.objects) {
    if (o.kind == ObjectKind::None) {
      slot = &o;
      break;
    }
  }
  if (slot == nullptr) return false;
  fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const fp::MovementParams& m = rules.engine.params[static_cast<std::size_t>(seat)];
  Object b{ObjectKind::Bumper, static_cast<std::int8_t>(seat), Payload::Glue, {}, {}, {}, {}, static_cast<std::int16_t>(kBumperLife), 0, 0, 0,
           static_cast<std::int8_t>(kBumperUses), {}, {}};
  if (e.mode == fp::Mode::Airborne && e.vel.y < Fixed::zero()) {
    // Falling: it appears underfoot, armed, and bounces Fuse up.
    b.pos = {e.pos.x, e.pos.y - fx100(180)};
    *slot = b;
    bounce_off(rules, state, *slot, seat);
    return true;
  }
  b.pos = {e.pos.x + fx100(1856 * e.facing), e.pos.y + m.body_height / Fixed::from_int(2) - fx100(190)};
  b.arm = kBumperArm;
  *slot = b;
  return true;
}

bool cast_saw(State& state, const Rules& rules, int seat) {
  Object* slot = nullptr;
  for (Object& o : state.objects) {
    if (o.kind == ObjectKind::None) {
      slot = &o;
      break;
    }
  }
  if (slot == nullptr) return false;
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  (void)rules;
  *slot = Object{ObjectKind::Saw, static_cast<std::int8_t>(seat), Payload::Glue,
                 {e.pos.x + fx100(130 * e.facing), e.pos.y + kSawRadius}, {Fixed::from_int(e.facing), Fixed::zero()},
                 {}, {}, static_cast<std::int16_t>(kSawLife), 0, 0, 0, 0, {}, {}};
  return true;
}

}  // namespace ledgefall::detail

namespace ledgefall::detail {

int tile_under_feet(const Rules& rules, const fp::EntityState& e) {
  return e.mode != fp::Mode::Airborne && e.support >= 0 && e.support < static_cast<int>(rules.engine.stage.platforms.size()) ? e.support : -1;
}

namespace {
Object* free_slot(State& state) {
  for (Object& o : state.objects) {
    if (o.kind == ObjectKind::None) return &o;
  }
  return nullptr;
}
}  // namespace

// Thorns: a bramble patch ahead, growing for 0.4 s, then 5 s. A sticky patch (speed x0.4) that
// chews the tiles under it (see after_step).
bool cast_thorns(State& state, const Rules& rules, int seat) {
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const int tile = tile_under_feet(rules, e);
  if (tile < 0) return false;
  const Fixed top = rules.engine.stage.platforms[static_cast<std::size_t>(tile)].rect.top;
  Fixed x0, x1;
  if (e.facing > 0) {
    x0 = e.pos.x + kThornNear;
    x1 = x0 + kThornWidth;
  } else {
    x1 = e.pos.x - kThornNear;
    x0 = x1 - kThornWidth;
  }
  if (!clip_to_row(rules, state, top, x0, x1)) return false;
  for (Pending& p : state.pending) {
    if (p.used) continue;
    fp::Placeable patch;
    patch.kind = fp::PlaceableKind::Sticky;
    patch.rect = {x0, x1, top - Fixed::one(), top + Fixed::one()};
    patch.life = kThornLife;
    patch.traction_scale = Fixed::from_int(2);
    patch.speed_scale = kThornSpeed;
    p = {true, static_cast<std::int8_t>(seat), kThornGrow, patch, static_cast<std::int16_t>(tile), 0, 1};
    return true;
  }
  return false;
}

bool cast_roots(State& state, const Rules& rules, int seat) {
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const int tile = tile_under_feet(rules, e);
  if (tile < 0) return false;
  for (Object& o : state.objects) {  // one at once
    if (o.kind == ObjectKind::Roots && o.owner == seat) o = {};
  }
  Object* o = free_slot(state);
  if (o == nullptr) return false;
  *o = Object{ObjectKind::Roots, static_cast<std::int8_t>(seat), Payload::Glue,
              {e.pos.x + kRootsAhead * Fixed::from_int(e.facing), rules.engine.stage.platforms[static_cast<std::size_t>(tile)].rect.top}, {}, {}, {},
              static_cast<std::int16_t>(kRootsFuse)};
  return true;
}

bool cast_rub(State& state, const Rules& rules, int seat) {
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const int tile = tile_under_feet(rules, e);
  if (tile < 0) return false;
  const fp::Rect& own = rules.engine.stage.platforms[static_cast<std::size_t>(tile)].rect;
  // The stroke starts past the tile Dab stands on: it never erases her own footing.
  Fixed x0;
  if (e.facing > 0) {
    x0 = e.pos.x + kRubNear;
    if (own.right > x0) x0 = own.right;
  } else {
    Fixed edge = e.pos.x - kRubNear;
    if (own.left < edge) edge = own.left;
    x0 = edge - kRubReach;
  }
  for (Object& o : state.objects) {
    if (o.kind == ObjectKind::Rub && o.owner == seat) o = {};
  }
  Object* o = free_slot(state);
  if (o == nullptr) return false;
  *o = Object{ObjectKind::Rub, static_cast<std::int8_t>(seat), Payload::Glue, {x0, own.top}, {}, x0, x0 + kRubReach,
              static_cast<std::int16_t>(kRubDelay)};
  return true;
}

}  // namespace ledgefall::detail

namespace ledgefall::detail {

bool cast_lodestone(State& state, const Rules& rules, int seat) {
  for (Object& o : state.objects) {  // one at once
    if (o.kind == ObjectKind::Magnet && o.owner == seat) o = {};
  }
  Object* slot = nullptr;
  for (Object& o : state.objects) {
    if (o.kind == ObjectKind::None) {
      slot = &o;
      break;
    }
  }
  if (slot == nullptr) return false;
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const fp::MovementParams& m = rules.engine.params[static_cast<std::size_t>(seat)];
  *slot = Object{ObjectKind::Magnet, static_cast<std::int8_t>(seat), Payload::Glue,
                 {e.pos.x + kMagnetAhead * Fixed::from_int(e.facing), e.pos.y + m.body_height / Fixed::from_int(2) + kMagnetLift}, {}, {}, {},
                 static_cast<std::int16_t>(kMagnetLife), static_cast<std::int16_t>(kMagnetArm)};
  return true;
}

// Polarity: flips the caster's lodestone for 2.5 s; everything loose within reach is thrown away at once.
bool cast_polarity(State& state, const Rules& rules, int seat) {
  (void)rules;
  Object* m = nullptr;
  for (Object& o : state.objects) {
    if (o.kind == ObjectKind::Magnet && o.owner == seat && o.arm == 0) m = &o;
  }
  if (m == nullptr) return false;  // nothing to flip
  m->uses = 1;
  m->rest = kMagnetFlip;
  for (Object& o : state.objects) {
    if (&o == m || o.kind == ObjectKind::None) continue;
    Fixed cx = o.pos.x, cy = o.pos.y;
    if (o.kind == ObjectKind::Anvil) cy = cy + kAnvilHeight / Fixed::from_int(2);
    const Fixed dx = cx - m->pos.x, dy = cy - m->pos.y;
    Fixed d = fx_sqrt(dx * dx + dy * dy);
    if (d > kMagnetReach) continue;
    if (d < Fixed::one()) d = Fixed::one();
    const fp::Vec2 v{dx / d * kMagnetBurst, dy / d * kMagnetBurst};
    switch (o.kind) {
      case ObjectKind::Anvil:
        o.rest = 0;
        o.vel = v;
        break;
      case ObjectKind::Lob:
      case ObjectKind::Bumper:
      case ObjectKind::Charge:
        o.vel = v;
        break;
      case ObjectKind::Saw:
        o.vel.x = dx < Fixed::zero() ? Fixed::from_int(-1) : Fixed::one();
        break;
      default:
        continue;
    }
    o.owner = m->owner;
  }
  return true;
}

}  // namespace ledgefall::detail
