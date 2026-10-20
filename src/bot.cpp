#include "ledgefall/bot.hpp"

#include "internal.hpp"

namespace ledgefall {
namespace {

using fp::Fixed;

Fixed centre_x(const fp::PlatformDef& d) { return (d.rect.left + d.rect.right) / Fixed::from_int(2); }

// How costly a platform is to head for: the distance to its middle, a penalty
// for the climb, and a small pull towards the middle of the stage.
Fixed cost(const fp::PlatformDef& d, const fp::EntityState& e) {
  const Fixed cx = centre_x(d);
  Fixed c = fp::abs(cx - e.pos.x) + fp::abs(cx) / Fixed::from_int(4);
  const Fixed climb = d.rect.top - e.pos.y;
  if (climb > Fixed::zero()) c = c + climb * Fixed::from_int(2);
  return c;
}

// Whether the tide covers (or is about to cover) a surface at this height.
bool tide_over(const State& state, Fixed top) {
  const Hazards& hz = state.hazards;
  if (hz.tide == TidePhase::Calm) return false;
  return top < hz.tide_level + Fixed::from_int(6);
}

void choose_target(const Rules& rules, const State& state, const fp::EntityState& e, Bot& bot) {
  bot.target = -1;
  Fixed best = Fixed::from_int(1 << 14);
  const auto& stage = rules.engine.stage;
  for (std::size_t i = 0; i < stage.platforms.size(); ++i) {
    if (state.world.platforms[i].phase != fp::PlatformPhase::Solid) continue;  // not shaking, gone or destroyed
    if (e.support == static_cast<std::int16_t>(i)) continue;  // standing still on a tile is what breaks it
    if (tide_over(state, stage.platforms[i].rect.top)) continue;  // under water, or about to be
    const Fixed c = cost(stage.platforms[i], e);
    if (c < best) {
      best = c;
      bot.target = static_cast<std::int16_t>(i);
    }
  }
}

std::uint32_t next_bot_random(Bot& b) {
  b.rng += 0x6D2B79F5u;
  std::uint32_t t = b.rng;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return t ^ (t >> 14);
}

using detail::fx100;

bool grounded(const fp::EntityState& e) { return e.mode != fp::Mode::Airborne; }

// What a bot would like to cast: a slot, and the direction it must face (0: any).
struct Plan {
  int slot = -1;
  int face = 0;
  bool forward = false;  // hold forward as it casts (a far throw, a far anvil)
  bool high = false;     // a high lob
};

// A lob (or a charge) that lands near an opponent standing on a tile.
bool plan_lob(const Rules& rules, const State& state, int seat, int slot, int tolerance, Plan& plan) {
  const fp::EntityState& me = state.world.entities[static_cast<std::size_t>(seat)];
  for (std::size_t q = 0; q < state.players.size(); ++q) {
    const fp::EntityState& o = state.world.entities[q];
    if (same_side(rules, seat, static_cast<int>(q)) || o.eliminated != 0 || state.players[q].invuln > 0 || !grounded(o)) continue;
    for (const int facing : {static_cast<int>(me.facing), -static_cast<int>(me.facing)}) {
      for (int aim = 0; aim < 3; ++aim) {  // neutral, forward (the only one that may turn round), high
        if (facing != me.facing && aim != 1) continue;
        Input in;
        if (aim == 1) in.move_x = static_cast<std::int8_t>(facing * kAxisMax);
        if (aim == 2) in.buttons = kButtonJump;
        fp::Vec2 pos, vel;
        detail::lob_launch(rules, state, seat, facing, in, pos, vel);
        Fixed lx, ltop;
        if (!detail::predict_lob(rules, state, pos, vel, lx, ltop)) continue;
        if (ltop != o.pos.y || fp::abs(lx - o.pos.x) > Fixed::from_int(tolerance)) continue;
        if (fp::abs(lx - me.pos.x) < Fixed::from_int(22) && ltop == me.pos.y) continue;  // not on myself
        plan = {slot, facing, aim == 1, aim == 2};
        return true;
      }
    }
  }
  return false;
}

// An opponent on my tier, between `lo` and `hi` units away: the direction to face.
int on_my_tier(const Rules& rules, const State& state, int seat, int lo, int hi) {
  const fp::EntityState& me = state.world.entities[static_cast<std::size_t>(seat)];
  if (!grounded(me)) return 0;
  for (std::size_t q = 0; q < state.players.size(); ++q) {
    const fp::EntityState& o = state.world.entities[q];
    if (same_side(rules, seat, static_cast<int>(q)) || o.eliminated != 0 || state.players[q].invuln > 0 || !grounded(o)) continue;
    if (fp::abs(o.pos.y - me.pos.y) > Fixed::from_int(2)) continue;
    const Fixed dx = o.pos.x - me.pos.x;
    if (fp::abs(dx) >= Fixed::from_int(lo) && fp::abs(dx) <= Fixed::from_int(hi)) return dx > Fixed::zero() ? 1 : -1;
  }
  return 0;
}

// A slot to cast now, if the kit has a good use for it here.
bool plan_move(const Rules& rules, const State& state, int seat, const Bot& bot, Plan& plan) {
  const Character c = rules.setup.characters[static_cast<std::size_t>(seat)];
  const PlayerState& ps = state.players[static_cast<std::size_t>(seat)];
  const fp::EntityState& me = state.world.entities[static_cast<std::size_t>(seat)];
  const int tol = bot.level == BotLevel::Easy ? 14 : bot.level == BotLevel::Normal ? 10 : 6;
  auto ready = [&](int slot) { return ps.cooldown[static_cast<std::size_t>(slot)] == 0; };
  auto lob = [&](int slot) { return ready(slot) && plan_lob(rules, state, seat, slot, tol, plan); };
  auto ahead = [&](int slot, int lo, int hi) {
    if (!ready(slot)) return false;
    const int face = on_my_tier(rules, state, seat, lo, hi);
    if (face == 0) return false;
    plan = {slot, face, false, false};
    return true;
  };
  // A thrown anvil: an opponent on the ground within reach and not far above.
  auto anvil = [&](int slot) {
    if (!ready(slot)) return false;
    for (std::size_t q = 0; q < state.players.size(); ++q) {
      const fp::EntityState& o = state.world.entities[q];
      if (same_side(rules, seat, static_cast<int>(q)) || o.eliminated != 0 || state.players[q].invuln > 0 || !grounded(o)) continue;
      const Fixed dx = o.pos.x - me.pos.x, dy = me.pos.y - o.pos.y;
      if (dy < Fixed::from_int(-6) || fp::abs(dx) > Fixed::from_int(83) || fp::abs(dx) < Fixed::from_int(10)) continue;
      plan = {slot, dx > Fixed::zero() ? 1 : -1, fp::abs(dx) > Fixed::from_int(38), false};
      return true;
    }
    return false;
  };
  switch (c) {
    case Character::Mason:  // wall in the way of someone coming; anvil on someone below
      return ahead(0, 22, 50) || anvil(2);
    case Character::Glim:
      return lob(2) || lob(0) || lob(1);
    case Character::Pip: {
      if (ahead(0, 22, 70)) return true;
      if (ahead(1, 10, 96)) return true;
      if (!ready(2)) return false;
      for (std::size_t q = 0; q < state.players.size(); ++q) {  // drill: someone straight below
        const fp::EntityState& o = state.world.entities[q];
        if (same_side(rules, seat, static_cast<int>(q)) || o.eliminated != 0 || state.players[q].invuln > 0) continue;
        const Fixed below = me.pos.y - o.pos.y;
        if (fp::abs(o.pos.x - me.pos.x) < Fixed::from_int(8) && below > Fixed::from_int(8) && below < Fixed::from_int(120)) {
          plan = {2, 0, false, false};
          return true;
        }
      }
      return false;
    }
    case Character::Fuse: {
      if (ahead(2, 19, 134)) return true;  // saw along the tier
      if (lob(1)) return true;             // charge
      if (!ready(0)) return false;         // bumper in front of someone in the air
      for (std::size_t q = 0; q < state.players.size(); ++q) {
        const fp::EntityState& o = state.world.entities[q];
        if (same_side(rules, seat, static_cast<int>(q)) || o.eliminated != 0 || state.players[q].invuln > 0 || grounded(o)) continue;
        const Fixed dx = o.pos.x - me.pos.x, dy = o.pos.y - me.pos.y;
        if (fp::abs(dx) > Fixed::from_int(13) && fp::abs(dx) < Fixed::from_int(35) && dy > Fixed::from_int(-6) && dy < Fixed::from_int(30)) {
          plan = {0, dx > Fixed::zero() ? 1 : -1, false, false};
          return true;
        }
      }
      return false;
    }
    case Character::Dab:
      return ahead(1, 14, 50) || ahead(2, 16, 58);  // hollow the tiles under someone, or rub them out
    case Character::Fern:
      return ahead(1, 16, 54) || ahead(2, 20, 50);
    case Character::Ferro: {
      if (anvil(2)) return true;
      if (ready(0)) {  // a lodestone near someone else's loose metal
        for (const Object& o : state.objects) {
          if (o.owner == seat || o.owner < 0) continue;
          if (o.kind != ObjectKind::Anvil && o.kind != ObjectKind::Saw && o.kind != ObjectKind::Charge && o.kind != ObjectKind::Bumper) continue;
          const Fixed dx = o.pos.x - (me.pos.x + fx100(2240) * Fixed::from_int(me.facing)), dy = o.pos.y - me.pos.y;
          if (dx * dx + dy * dy < Fixed::from_int(64 * 64)) {
            plan = {0, 0, false, false};
            return true;
          }
        }
      }
      if (ready(1)) {  // flip it when it holds something and an opponent is near
        for (const Object& m : state.objects) {
          if (m.kind != ObjectKind::Magnet || m.owner != seat || m.arm > 0 || m.uses == 1) continue;
          bool holds = false;
          for (const Object& o : state.objects) {
            if (&o == &m || (o.kind != ObjectKind::Anvil && o.kind != ObjectKind::Charge && o.kind != ObjectKind::Saw && o.kind != ObjectKind::Bumper)) continue;
            const Fixed dx = o.pos.x - m.pos.x, dy = o.pos.y - m.pos.y;
            holds = holds || dx * dx + dy * dy < Fixed::from_int(20 * 20);
          }
          if (!holds) continue;
          for (std::size_t q = 0; q < state.players.size(); ++q) {
            const fp::EntityState& o = state.world.entities[q];
            if (same_side(rules, seat, static_cast<int>(q)) || o.eliminated != 0) continue;
            const Fixed dx = o.pos.x - m.pos.x, dy = o.pos.y - m.pos.y;
            if (dx * dx + dy * dy < Fixed::from_int(100 * 100)) {
              plan = {1, 0, false, false};
              return true;
            }
          }
        }
      }
      return false;
    }
  }
  return false;
}

// Use the kit now and then: pick a move that suits the situation, face its target, cast it.
void use_kit(const Rules& rules, const State& state, int seat, Bot& bot, Input& in) {
  if (bot.attack_wait > 0) {
    --bot.attack_wait;
    return;
  }
  if ((state.world.frame + static_cast<std::uint32_t>(seat)) % 3 != 0) return;
  const fp::EntityState& me = state.world.entities[static_cast<std::size_t>(seat)];
  if (state.players[static_cast<std::size_t>(seat)].drill != 0) return;
  Plan plan;
  if (!plan_move(rules, state, seat, bot, plan)) return;
  if (plan.face != 0 && plan.face != me.facing) {  // turn to it first
    in.move_x = static_cast<std::int8_t>(plan.face * kAxisMax);
    return;
  }
  if (static_cast<int>(next_bot_random(bot) % 100) >= attack_percent(bot.level)) return;
  bot.attack_wait = static_cast<std::int16_t>(attack_gap(bot.level));
  in.buttons |= static_cast<std::uint8_t>(kButtonPlace << plan.slot);
  if (plan.forward && plan.face != 0) in.move_x = static_cast<std::int8_t>(plan.face * kAxisMax);
  if (plan.high) in.buttons |= kButtonJump;
}

// Standing on, or about to fall onto, something: a tile in play or a placeable platform whose x-range holds us.
bool surface_below(const Rules& rules, const State& state, const fp::EntityState& e) {
  const auto& platforms = rules.engine.stage.platforms;
  const Fixed slack = Fixed::from_int(3);
  for (std::size_t i = 0; i < platforms.size(); ++i) {
    if (!detail::in_play(state.world.platforms[i].phase)) continue;
    const fp::Rect& r = platforms[i].rect;
    if (r.top <= e.pos.y + Fixed::one() && e.pos.x >= r.left - slack && e.pos.x <= r.right + slack) return true;
  }
  for (std::size_t slot = 0; slot < state.world.placeables.size(); ++slot) {
    const fp::Placeable& p = state.world.placeables[slot];
    if (p.active == 0 || (p.kind != fp::PlaceableKind::OneWay && p.kind != fp::PlaceableKind::Wall)) continue;
    if (p.rect.top <= e.pos.y + Fixed::one() && e.pos.x >= p.rect.left - slack && e.pos.x <= p.rect.right + slack) return true;
  }
  return false;
}

// Falling out of the stage with nothing under us.
bool falling_out(const Rules& rules, const State& state, const fp::EntityState& e) {
  return e.mode == fp::Mode::Airborne && e.vel.y <= Fixed::zero() && !surface_below(rules, state, e);
}

// The move that gets this character back in play from the air, if it has one: its slot.
int recovery_slot(Character c) {
  switch (c) {
    case Character::Pip: return 0;   // an air spring launches Pip
    case Character::Fuse: return 0;  // a bumper placed while falling appears underfoot and bounces Fuse
    case Character::Mason: return 1; // a ledge under his feet
    case Character::Dab: return 0;   // a decoy under her feet
    default: return -1;
  }
}

// Comets and rocks: jump a comet that is close and in our row, step out from under a rock.
void dodge_hazards(const State& state, int seat, Bot& bot, Input& in) {
  if (bot.blind) return;
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  const bool grounded_now = e.mode != fp::Mode::Airborne;
  const Fixed mid = e.pos.y + Fixed::from_int(6);
  for (const HazardItem& h : state.hazards.list) {
    if (h.kind == HazardKind::Comet) {
      const Fixed dx = e.pos.x - h.pos.x;
      const bool coming = (dx > Fixed::zero()) == (h.vel.x > Fixed::zero());
      if (coming && fp::abs(dx) < Fixed::from_int(48) && fp::abs(h.pos.y - mid) < Fixed::from_int(14)) {
        if (grounded_now) in.buttons |= kButtonJump;                       // over it
        else if (e.vel.y > Fixed::zero()) in.buttons |= kButtonJump;       // hold the jump up
      }
    } else if (h.kind == HazardKind::RockWarn || h.kind == HazardKind::Rock) {
      const Fixed dx = e.pos.x - h.pos.x;
      const bool above = h.kind == HazardKind::RockWarn || h.pos.y > e.pos.y;
      if (above && fp::abs(dx) < Fixed::from_int(16) && (h.kind == HazardKind::RockWarn || h.pos.y - e.pos.y < Fixed::from_int(200))) {
        in.move_x = static_cast<std::int8_t>((dx >= Fixed::zero() ? 1 : -1) * kAxisMax);  // out of its column
      }
    }
  }
}

// Falling out: fly towards the stage and use the character's recovery move.
void recover(const Rules& rules, const State& state, int seat, Bot& bot, Input& in) {
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  if (!falling_out(rules, state, e)) return;
  // Head for the middle of the stage.
  const fp::Rect& blast = rules.engine.stage.blast;
  const Fixed centre = (blast.left + blast.right) / Fixed::from_int(2);
  in.move_x = static_cast<std::int8_t>((e.pos.x < centre ? 1 : -1) * kAxisMax);
  const int slot = recovery_slot(rules.setup.characters[static_cast<std::size_t>(seat)]);
  const PlayerState& ps = state.players[static_cast<std::size_t>(seat)];
  // Only once we are really below the stage's lowest tier, and when the move is ready.
  if (slot < 0 || ps.cooldown[static_cast<std::size_t>(slot)] != 0) return;
  if ((state.world.frame + static_cast<std::uint32_t>(seat)) % 3 != 0) return;
  in.buttons |= static_cast<std::uint8_t>(kButtonPlace << slot);
  (void)bot;
}

}  // namespace

Input bot_input(const Rules& rules, const State& state, int seat, Bot& bot) {
  const fp::EntityState& e = state.world.entities[static_cast<std::size_t>(seat)];
  if (e.eliminated != 0 || state.over) return {};

  // Decide at the bot's reaction time, or at once if the tile it was heading for is no longer in play.
  const bool lost_target =
      bot.target >= 0 && state.world.platforms[static_cast<std::size_t>(bot.target)].phase != fp::PlatformPhase::Solid;
  if (bot.wait > 0 && !(lost_target && bot.level == BotLevel::Hard)) {
    --bot.wait;
  } else {
    bot.wait = static_cast<std::int16_t>(reaction_ticks(bot.level));
    choose_target(rules, state, e, bot);
    bot.blind = static_cast<int>(next_bot_random(bot) % 100) < hazard_miss_percent(bot.level);
  }

  Input in;
  fp::Fixed goal_x = Fixed::zero();
  Fixed goal_top = Fixed::zero();
  if (bot.target >= 0) {
    const fp::PlatformDef& d = rules.engine.stage.platforms[static_cast<std::size_t>(bot.target)];
    goal_x = centre_x(d);
    goal_top = d.rect.top;
  }
  const Fixed dx = goal_x - e.pos.x;
  if (dx > Fixed::from_int(2)) in.move_x = kAxisMax;
  if (dx < Fixed::from_int(-2)) in.move_x = -kAxisMax;

  const Fixed climb = goal_top - e.pos.y;
  const bool airborne = e.mode == fp::Mode::Airborne;
  if (climb > Fixed::from_int(2) || (bot.target < 0 && e.pos.y < Fixed::zero())) {
    // Jump on alternate ticks so each press is a fresh one (the air jump needs a release).
    // A falling bot uses its air jump; a grounded one jumps.
    if (!airborne || e.vel.y <= Fixed::zero()) in.buttons |= (state.world.frame % 8 < 4) ? kButtonJump : 0;
    else in.buttons |= kButtonJump;  // rising: hold for height
  } else if (climb < Fixed::from_int(-2) && !airborne) {
    in.buttons |= kButtonDown;  // drop to a lower tier
  }
  dodge_hazards(state, seat, bot, in);
  recover(rules, state, seat, bot, in);
  use_kit(rules, state, seat, bot, in);
  return in;
}

}  // namespace ledgefall
