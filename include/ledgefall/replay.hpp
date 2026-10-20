#pragma once
// Replays (lost-footing #14): a match is its setup and every seat's input on
// every tick, so recording is writing inputs down and watching it again is
// stepping the same state with them. The recording ends with the state's hash,
// which a replay checks (a replay that does not reproduce its hash was made by
// a different version of the rules, or is damaged).

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ledgefall/match.hpp"

namespace ledgefall {

struct Replay {
  Setup setup;
  std::uint32_t ticks = 0;
  std::vector<Input> inputs;  // setup.players inputs per tick, in seat order
  std::uint64_t final_hash = 0;  // the state's hash after the last tick

  const Input* tick_inputs(std::uint32_t tick) const {
    return inputs.data() + static_cast<std::size_t>(tick) * static_cast<std::size_t>(setup.players);
  }
};

constexpr std::uint32_t kMaxReplayTicks = 60 * 60 * 60;  // an hour

// Writes down what a match is given, tick by tick.
class Recorder {
 public:
  explicit Recorder(const Setup& setup);
  // The inputs the match was stepped with this tick.
  void record(std::span<const Input> inputs);
  // The replay so far, with the hash of `state` as its end.
  Replay finish(const State& state) const;

 private:
  Replay replay_;
};

// A compact, platform-independent byte string (a tick stores only the seats whose input changed), and back.
// `decode` rejects anything that is not a well-formed replay of a valid setup.
std::vector<std::uint8_t> encode(const Replay& replay);
std::optional<Replay> decode_replay(std::span<const std::uint8_t> bytes);

// Run a replay from its setup and compare the hash at the end.
bool verify(const Replay& replay);

}  // namespace ledgefall
