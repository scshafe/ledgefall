#include "ledgefall/netplay.hpp"

#include <string>

namespace ledgefall {

NetplaySimulation::NetplaySimulation(Rules rules) : rules_(std::move(rules)), state_(make_state(rules_)) {}

void NetplaySimulation::step(npk::session::Frame, std::span<const std::uint8_t> inputs) {
  std::vector<Input> in(state_.players.size());
  for (std::size_t i = 0; i < in.size(); ++i) {
    if ((i + 1) * kInputBytes > inputs.size() || !decode(inputs.subspan(i * kInputBytes, kInputBytes), in[i])) in[i] = Input{};
  }
  ledgefall::step(rules_, state_, in);
}

void NetplaySimulation::snapshot(std::vector<std::uint8_t>& out) const { ledgefall::snapshot(state_, out); }

bool NetplaySimulation::restore(std::span<const std::uint8_t> bytes) { return ledgefall::restore(rules_, state_, bytes); }

std::uint64_t NetplaySimulation::hash() const { return ledgefall::hash(state_); }

bool NetplaySimulation::valid_input(std::span<const std::uint8_t> input) const {
  Input scratch;
  return decode(input, scratch);
}

std::string NetplaySimulation::result_json() const {
  using npk::json::Value;
  Value::Array places, kos, falls, stocks;
  for (const PlayerState& p : state_.players) {
    places.emplace_back(static_cast<int>(p.place));
    kos.emplace_back(static_cast<int>(p.stats.kos));
    falls.emplace_back(static_cast<int>(p.stats.falls));
    stocks.emplace_back(static_cast<int>(p.stocks));
  }
  Value r{Value::Object{}};
  r.set("winner", state_.winner >= 0 ? Value(static_cast<int>(state_.winner)) : Value());
  const int team = winning_team(rules_, state_);
  r.set("winning_team", team >= 0 ? Value(team) : Value());
  r.set("ticks", Value(static_cast<std::int64_t>(state_.world.frame)));
  r.set("places", Value(std::move(places)));
  r.set("kos", Value(std::move(kos)));
  r.set("falls", Value(std::move(falls)));
  r.set("stocks", Value(std::move(stocks)));
  return npk::json::dump(r);
}

void NetplaySimulation::predict_input(std::span<const std::uint8_t> previous, std::span<std::uint8_t> out) const {
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = i < previous.size() ? previous[i] : 0;
  // Per input: byte 0 is the axis, byte 1 the buttons.
  for (std::size_t at = 1; at < out.size(); at += kInputBytes) out[at] = static_cast<std::uint8_t>(out[at] & (kButtonJump | kButtonDown));
}

namespace {

// An integer option in [lo, hi], or false if present and wrong.
bool int_option(const npk::json::Value& o, const char* key, std::int64_t lo, std::int64_t hi, std::int64_t& out) {
  const npk::json::Value* v = o.find(key);
  if (v == nullptr || v->is_null()) return true;
  const auto n = v->as_int();
  if (!n || *n < lo || *n > hi) return false;
  out = *n;
  return true;
}

// An array of `count` integers in [lo, hi].
bool seat_array(const npk::json::Value& o, const char* key, std::size_t count, std::int64_t lo, std::int64_t hi,
                std::array<std::int64_t, kMaxPlayers>& out) {
  const npk::json::Value* v = o.find(key);
  if (v == nullptr || v->is_null()) return true;
  const auto* a = v->as_array();
  if (a == nullptr || a->size() != count) return false;
  for (std::size_t i = 0; i < count; ++i) {
    const auto n = (*a)[i].as_int();
    if (!n || *n < lo || *n > hi) return false;
    out[i] = *n;
  }
  return true;
}

}  // namespace

std::unique_ptr<NetplaySimulation> make_netplay_simulation(std::size_t players, const npk::json::Value& options) {
  if (players < static_cast<std::size_t>(kMinPlayers) || players > static_cast<std::size_t>(kMaxPlayers)) return nullptr;
  Setup setup;
  setup.players = static_cast<int>(players);
  if (!options.is_null() && !options.is_object()) return nullptr;
  std::int64_t stocks = setup.stocks, seed = setup.seed, stage = static_cast<std::int64_t>(setup.stage);
  if (!int_option(options, "stocks", kMinStocks, kMaxStocks, stocks) || !int_option(options, "seed", 0, 0xFFFFFFFFll, seed) ||
      !int_option(options, "stage", 0, kStageCount - 1, stage)) {
    return nullptr;
  }
  setup.stocks = static_cast<int>(stocks);
  setup.seed = static_cast<std::uint32_t>(seed);
  setup.stage = static_cast<StageId>(stage);

  std::array<std::int64_t, kMaxPlayers> characters{}, teams{}, handicaps{};
  for (std::size_t i = 0; i < kMaxPlayers; ++i) {
    characters[i] = static_cast<std::int64_t>(setup.characters[i]);
    teams[i] = -1;
  }
  if (!seat_array(options, "characters", players, 0, kCharacterCount - 1, characters) ||
      !seat_array(options, "teams", players, -1, 1, teams) ||
      !seat_array(options, "seat_stocks", players, 0, kMaxSeatStocks, handicaps)) {
    return nullptr;
  }
  for (std::size_t i = 0; i < kMaxPlayers; ++i) {
    setup.characters[i] = static_cast<Character>(characters[i]);
    setup.teams[i] = static_cast<std::int8_t>(teams[i]);
    setup.seat_stocks[i] = static_cast<std::int8_t>(handicaps[i]);
  }
  auto rules = make_rules(setup);
  if (!rules) return nullptr;
  return std::make_unique<NetplaySimulation>(std::move(*rules));
}

}  // namespace ledgefall
