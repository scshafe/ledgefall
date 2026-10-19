#include "ledgefall/bot.hpp"

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

void choose_target(const Rules& rules, const State& state, const fp::EntityState& e, Bot& bot) {
  bot.target = -1;
  Fixed best = Fixed::from_int(1 << 14);
  const auto& stage = rules.engine.stage;
  for (std::size_t i = 0; i < stage.platforms.size(); ++i) {
    if (state.world.platforms[i].phase != fp::PlatformPhase::Solid) continue;  // not shaking, gone or destroyed
    if (e.support == static_cast<std::int16_t>(i)) continue;  // standing still on a tile is what breaks it
    const Fixed c = cost(stage.platforms[i], e);
    if (c < best) {
      best = c;
      bot.target = static_cast<std::int16_t>(i);
    }
  }
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
  return in;
}

}  // namespace ledgefall
