#include "ledgefall/netplay.hpp"

namespace ledgefall {

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
