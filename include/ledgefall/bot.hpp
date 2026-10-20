#pragma once
// A bot fills an empty seat: it reads the match State (never the clock or a
// random source) and answers with the Input a person would send. A bot
// matches lost-footing's footing bot in kind: it picks a surface it can reach
// that is not shaking or gone, prefers the middle of the stage and nearby
// ground, runs to it, jumps when it is above and drops when it is below, uses
// its character's moves when they suit the situation, dodges comets and rocks,
// keeps above the tide, and recovers with a move when it is falling out.
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

// Ticks a bot waits between moves, and the chance it makes one it has planned.
constexpr int attack_gap(BotLevel level) noexcept {
  return level == BotLevel::Easy ? 180 : level == BotLevel::Normal ? 90 : 45;
}
constexpr int attack_percent(BotLevel level) noexcept {
  return level == BotLevel::Easy ? 25 : level == BotLevel::Normal ? 60 : 90;
}

// How often a bot fails to see a hazard coming (lost-footing: 45%, 15%, never).
constexpr int hazard_miss_percent(BotLevel level) noexcept {
  return level == BotLevel::Easy ? 45 : level == BotLevel::Normal ? 15 : 0;
}

struct Bot {
  BotLevel level = BotLevel::Normal;
  std::int16_t wait = 0;      // ticks until the next decision
  std::int16_t target = -1;   // platform it is heading for, -1 for none
  std::int16_t attack_wait = 0;  // ticks until it may use a move again
  bool blind = false;         // misses the hazards it would otherwise dodge, until its next decision
  std::uint32_t rng = 1;      // its own stream (a bot's choices are not the match's randomness)
};

// The input for this tick of `seat`. Call once per tick, in tick order.
Input bot_input(const Rules& rules, const State& state, int seat, Bot& bot);

}  // namespace ledgefall
