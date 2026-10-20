#pragma once
// The core as netplay-kit's Simulation: what the game server and every client
// run so that the rollback session can step, snapshot, restore and compare it.
// One of these owns the Rules and the State of one match.

#include <memory>
#include <optional>

#include "ledgefall/match.hpp"
#include "npk/json/json.hpp"
#include "npk/session/simulation.hpp"

namespace ledgefall {

class NetplaySimulation final : public npk::session::Simulation {
 public:
  // `players` seats played on `rules.setup`'s stage; rules.setup.players must equal it.
  explicit NetplaySimulation(Rules rules);

  std::size_t player_count() const override { return state_.players.size(); }
  std::size_t input_size() const override { return kInputBytes; }
  void step(npk::session::Frame frame, std::span<const std::uint8_t> inputs) override;
  void snapshot(std::vector<std::uint8_t>& out) const override;
  bool restore(std::span<const std::uint8_t> snapshot) override;
  std::uint64_t hash() const override;
  bool valid_input(std::span<const std::uint8_t> input) const override;
  bool finished() const override { return state_.over; }
  std::string result_json() const override;
  // A guess for a late input: the same movement again, but a move button is a press, not a hold.
  void predict_input(std::span<const std::uint8_t> previous, std::span<std::uint8_t> out) const override;

  const Rules& rules() const { return rules_; }
  const State& state() const { return state_; }

 private:
  Rules rules_;
  State state_;
};

// (The server's factory, below, is in netplay_options.cpp: the client side of the game does not link JSON.)

// A match for `players` seats from the options the lobby passed on: all optional.
//   "stocks"      1..5 (default 3)
//   "seed"        0..2^32-1 (default: 1)
//   "stage"       0..5, a ledgefall::StageId
//   "characters"  an array of players integers (ledgefall::Character)
//   "teams"       an array of players integers, -1 alone, 0 or 1
//   "seat_stocks" an array of players integers, 0 for the match's stocks, else 1..9
// Null if the options are wrong or the setup is not valid.
std::unique_ptr<NetplaySimulation> make_netplay_simulation(std::size_t players, const npk::json::Value& options);

}  // namespace ledgefall
