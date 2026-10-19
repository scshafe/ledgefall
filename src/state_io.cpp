// Hash, snapshot and restore of a State. The State's own fields are written
// once, here, as little-endian integers; the engine's world follows as the
// engine's own snapshot, and the hash combines the two, so what is hashed is
// exactly what restore brings back.
#include <cstddef>

#include "fp/state.hpp"
#include "ledgefall/match.hpp"

namespace ledgefall {
namespace {

constexpr std::uint8_t kMagic0 = 'L', kMagic1 = 'F', kVersion = 1;
constexpr std::size_t kHeaderBytes = 3;
constexpr std::size_t kPerPlayerBytes = 3;  // stocks, respawn timer (2)
constexpr std::size_t kTailBytes = 6;       // winner, over, rng (4)

void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void put64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// The State's own fields: players, then the result.
void write_own(const State& s, std::vector<std::uint8_t>& out) {
  for (const PlayerState& p : s.players) {
    out.push_back(static_cast<std::uint8_t>(p.stocks));
    put16(out, static_cast<std::uint16_t>(p.respawn_timer));
  }
  out.push_back(static_cast<std::uint8_t>(s.winner));
  out.push_back(s.over ? 1 : 0);
  put32(out, s.rng);
}

}  // namespace

std::uint64_t hash(const State& state) {
  std::vector<std::uint8_t> bytes;
  write_own(state, bytes);
  put64(bytes, fp::hash(state.world));
  std::uint64_t h = 14695981039346656037ull;  // FNV-1a
  for (std::uint8_t b : bytes) {
    h ^= b;
    h *= 1099511628211ull;
  }
  h ^= h >> 32;  // a final mix, as the engine does
  h *= 0x9e3779b97f4a7c15ull;
  return h ^ (h >> 29);
}

void snapshot(const State& state, std::vector<std::uint8_t>& out) {
  out = {kMagic0, kMagic1, kVersion};
  write_own(state, out);
  std::vector<std::uint8_t> world;
  fp::snapshot(state.world, world);
  out.insert(out.end(), world.begin(), world.end());
}

bool restore(const Rules& rules, State& state, std::span<const std::uint8_t> bytes) {
  const std::size_t n = static_cast<std::size_t>(rules.setup.players);
  const std::size_t own = kPerPlayerBytes * n + kTailBytes;
  if (bytes.size() < kHeaderBytes + own || bytes[0] != kMagic0 || bytes[1] != kMagic1 ||
      bytes[2] != kVersion) {
    return false;
  }
  State next = state;
  next.players.assign(n, {});
  std::size_t at = kHeaderBytes;
  for (PlayerState& p : next.players) {
    p.stocks = static_cast<std::int8_t>(bytes[at]);
    p.respawn_timer = static_cast<std::int16_t>(static_cast<std::uint16_t>(bytes[at + 1] | bytes[at + 2] << 8));
    at += kPerPlayerBytes;
    if (p.stocks < 0 || p.stocks > kMaxStocks || p.respawn_timer < 0 || p.respawn_timer > kRespawnDelayTicks) {
      return false;
    }
  }
  next.winner = static_cast<std::int8_t>(bytes[at]);
  if (next.winner < -1 || next.winner >= static_cast<int>(n) || bytes[at + 1] > 1) return false;
  next.over = bytes[at + 1] == 1;
  next.rng = static_cast<std::uint32_t>(bytes[at + 2]) | static_cast<std::uint32_t>(bytes[at + 3]) << 8 |
             static_cast<std::uint32_t>(bytes[at + 4]) << 16 | static_cast<std::uint32_t>(bytes[at + 5]) << 24;
  if (!fp::restore(rules.engine, next.world, bytes.subspan(at + kTailBytes))) return false;
  state = std::move(next);
  return true;
}

}  // namespace ledgefall
