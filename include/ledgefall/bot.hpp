#pragma once
// A bot fills an empty seat: it reads the match State (never the clock or a
// random source) and answers with the Input a person would send. A bot
// matches lost-footing's footing bot in kind: it picks a surface it can reach
// that is not shaking or gone, prefers the middle of the stage and nearby
// ground, runs to it, jumps when it is above and drops when it is below. The
// move-using part of lost-footing's bots waits for placeables.
//
// The bot's few fields are its own memory, not simulation state; a host that
// runs bots (the app, the server) keeps one per bot seat and gives the bot the
// same state every peer has, so every peer computes the same input.

#include <cstdint>

#include "ledgefall/match.hpp"

namespace ledgefall {

enum class BotLevel : std::uint8_t { Easy, Normal, Hard };

// Ticks between a bot's decisions (lost-footing: 0.4 s, 0.2 s, 0.08 s).
constexpr int reaction_ticks(BotLevel level) noexcept {
  return level == BotLevel::Easy ? 24 : level == BotLevel::Normal ? 12 : 5;
}

struct Bot {
  BotLevel level = BotLevel::Normal;
  std::int16_t wait = 0;      // ticks until the next decision
  std::int16_t target = -1;   // platform it is heading for, -1 for none
};

// The input for this tick of `seat`. Call once per tick, in tick order.
Input bot_input(const Rules& rules, const State& state, int seat, Bot& bot);

}  // namespace ledgefall
