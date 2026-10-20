#include "ledgefall/netplay.hpp"

#include <string>

namespace ledgefall {

NetplaySimulation::NetplaySimulation(Rules rules) : rules_(std::move(rules)), state_(make_state(rules_)) {}

void NetplaySimulation::step(npk::session::Frame, std::span<const std::uint8_t> inputs) {
  std::vector<Input> in(state_.players.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    if ((i + 1) * kInputBytes > inputs.size() || !decode(inputs.subspan(i * kInputBytes, kInputBytes), in[i])) in[i] = Input{};
  }
  if (!state_.over && state_.world.frame < kMaxReplayTicks) {  // a rollback steps a frame again: the latest stepping stands
    inputs_.resize(static_cast<std::size_t>(state_.world.frame) * in.size());
    inputs_.insert(inputs_.end(), in.begin(), in.end());
  }
  ledgefall::step(rules_, state_, in);
}

std::optional<Replay> NetplaySimulation::replay() const {
  const std::size_t per_tick = state_.players.size();
  if (!state_.over || per_tick == 0 || state_.world.frame >= kMaxReplayTicks || inputs_.size() != state_.world.frame * per_tick) return std::nullopt;
  Replay r;
  r.setup = rules_.setup;
  r.ticks = state_.world.frame;
  r.inputs = inputs_;
  r.final_hash = ledgefall::hash(state_);
  if (!verify(r)) return std::nullopt;
  return r;
}

void NetplaySimulation::snapshot(std::vector<std::uint8_t>& out) const { ledgefall::snapshot(state_, out); }

bool NetplaySimulation::restore(std::span<const std::uint8_t> bytes) { return ledgefall::restore(rules_, state_, bytes); }

std::uint64_t NetplaySimulation::hash() const { return ledgefall::hash(state_); }

bool NetplaySimulation::valid_input(std::span<const std::uint8_t> input) const {
  Input scratch;
  return decode(input, scratch);
}

std::string NetplaySimulation::result_json() const {
  // Built by hand: the client side of the game does not link a JSON library.
  auto list = [&](auto value) {
    std::string out = "[";
    for (std::size_t i = 0; i < state_.players.size(); ++i) out += (i ? "," : "") + std::to_string(value(state_.players[i]));
    return out + "]";
  };
  const int team = winning_team(rules_, state_);
  return "{\"winner\":" + (state_.winner >= 0 ? std::to_string(state_.winner) : std::string("null")) +
         ",\"winning_team\":" + (team >= 0 ? std::to_string(team) : std::string("null")) +
         ",\"ticks\":" + std::to_string(state_.world.frame) +
         ",\"places\":" + list([](const PlayerState& p) { return static_cast<int>(p.place); }) +
         ",\"kos\":" + list([](const PlayerState& p) { return static_cast<int>(p.stats.kos); }) +
         ",\"falls\":" + list([](const PlayerState& p) { return static_cast<int>(p.stats.falls); }) +
         ",\"stocks\":" + list([](const PlayerState& p) { return static_cast<int>(p.stocks); }) + "}";
}

void NetplaySimulation::predict_input(std::span<const std::uint8_t> previous, std::span<std::uint8_t> out) const {
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = i < previous.size() ? previous[i] : 0;
  // Per input: byte 0 is the axis, byte 1 the buttons.
  for (std::size_t at = 1; at < out.size(); at += kInputBytes) out[at] = static_cast<std::uint8_t>(out[at] & (kButtonJump | kButtonDown));
}

}  // namespace ledgefall
