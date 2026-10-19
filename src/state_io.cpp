// Hash, snapshot and restore of a State. The State's fields are listed once,
// in `io_*` below, and visited by two archives (one writes little-endian
// integers, one reads them), so a snapshot and what restore brings back cannot
// disagree. The engine's world follows as the engine's own snapshot, and the
// hash combines the two.
#include <cstddef>
#include <type_traits>

#include "fp/state.hpp"
#include "ledgefall/match.hpp"

namespace ledgefall {
namespace {

constexpr std::uint8_t kMagic0 = 'L', kMagic1 = 'F', kVersion = 2;
constexpr std::size_t kHeaderBytes = 3;

// An integer or an enum, as the unsigned integer of its own width.
template <class T, bool = std::is_enum_v<T>>
struct Raw {
  using type = std::make_unsigned_t<T>;
};
template <class T>
struct Raw<T, true> {
  using type = std::make_unsigned_t<std::underlying_type_t<T>>;
};

struct Writer {
  std::vector<std::uint8_t>& out;
  template <class T>
  void num(T v) {
    using U = typename Raw<T>::type;
    const U u = static_cast<U>(v);
    for (std::size_t i = 0; i < sizeof(U); ++i) out.push_back(static_cast<std::uint8_t>(u >> (8 * i)));
  }
  void fx(const fp::Fixed& f) { num(f.raw()); }
};

struct Reader {
  std::span<const std::uint8_t> in;
  std::size_t at = 0;
  bool ok = true;
  template <class T>
  void num(T& v) {
    using U = typename Raw<T>::type;
    U u = 0;
    for (std::size_t i = 0; i < sizeof(U); ++i) {
      if (at >= in.size()) {
        ok = false;
        return;
      }
      u = static_cast<U>(u | static_cast<U>(static_cast<U>(in[at++]) << (8 * i)));
    }
    v = static_cast<T>(u);
  }
  void fx(fp::Fixed& f) {
    std::int32_t raw = 0;
    num(raw);
    f = fp::Fixed::from_raw(raw);
  }
};

template <class A>
void io_stats(A& a, Stats& s) {
  a.num(s.kos);
  a.num(s.falls);
  a.num(s.deaths);
  a.num(s.tiles_broken);
  a.num(s.slabs_broken);
  a.num(s.placed);
  a.num(s.stomps);
}

template <class A>
void io_player(A& a, PlayerState& p) {
  a.num(p.stocks);
  a.num(p.respawn_timer);
  for (auto& c : p.cooldown) a.num(c);
  a.num(p.prev_buttons);
  a.num(p.slow);
  a.num(p.blame_by);
  a.num(p.blame_tick);
  a.num(p.ko_tick);
  a.num(p.place);
  io_stats(a, p.stats);
}

template <class A>
void io_placeable(A& a, fp::Placeable& p) {
  a.num(p.kind);
  a.num(p.active);
  a.fx(p.rect.left);
  a.fx(p.rect.right);
  a.fx(p.rect.bottom);
  a.fx(p.rect.top);
  a.num(p.life);
  a.fx(p.traction_scale);
  a.fx(p.speed_scale);
  a.fx(p.launch.x);
  a.fx(p.launch.y);
}

// Everything but the engine's world, in a fixed order. `players` is the
// number of seats (a loading archive has it from the rules).
template <class A>
void io_state(A& a, State& s) {
  for (PlayerState& p : s.players) io_player(a, p);
  a.num(s.winner);
  std::uint8_t over = s.over ? 1 : 0;
  a.num(over);
  s.over = over != 0;
  a.num(s.rng);
  for (PlaceableMeta& m : s.meta) {
    a.num(m.owner);
    a.num(m.uses);
    a.num(m.support);
    a.num(m.born);
  }
  for (Pending& p : s.pending) {
    std::uint8_t used = p.used ? 1 : 0;
    a.num(used);
    p.used = used != 0;
    a.num(p.owner);
    a.num(p.delay);
    io_placeable(a, p.what);
    a.num(p.support);
    a.num(p.uses);
  }
}

bool plausible(const Rules& rules, const State& s) {
  const int n = rules.setup.players;
  for (const PlayerState& p : s.players) {
    if (p.stocks < 0 || p.stocks > kMaxStocks || p.respawn_timer < 0 || p.respawn_timer > kRespawnDelayTicks) return false;
    for (auto c : p.cooldown) {
      if (c < 0 || c > 20 * kTicksPerSecond) return false;
    }
    if ((p.prev_buttons & ~kButtonMask) != 0 || p.slow < 0 || p.slow > 10 * kTicksPerSecond) return false;
    if (p.blame_by < -1 || p.blame_by >= n || p.place < 0 || p.place > n) return false;
  }
  if (s.winner < -1 || s.winner >= n) return false;
  for (const PlaceableMeta& m : s.meta) {
    if (m.owner < -1 || m.owner >= n || m.support < -1 ||
        m.support >= static_cast<int>(rules.engine.stage.platforms.size())) {
      return false;
    }
  }
  for (const Pending& p : s.pending) {
    if (p.owner < -1 || p.owner >= n || p.delay < 0 || p.delay > 5 * kTicksPerSecond) return false;
    if (p.support < -1 || p.support >= static_cast<int>(rules.engine.stage.platforms.size())) return false;
    if (static_cast<int>(p.what.kind) > static_cast<int>(fp::PlaceableKind::Spring) || p.what.active > 1) return false;
    if (p.used && p.owner < 0) return false;
  }
  return true;
}

void put64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

}  // namespace

std::uint64_t hash(const State& state) {
  std::vector<std::uint8_t> bytes;
  Writer w{bytes};
  State copy = state;  // the archive visits mutable fields; the write does not change them
  io_state(w, copy);
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
  Writer w{out};
  State copy = state;
  io_state(w, copy);
  std::vector<std::uint8_t> world;
  fp::snapshot(state.world, world);
  out.insert(out.end(), world.begin(), world.end());
}

bool restore(const Rules& rules, State& state, std::span<const std::uint8_t> bytes) {
  if (bytes.size() < kHeaderBytes || bytes[0] != kMagic0 || bytes[1] != kMagic1 || bytes[2] != kVersion) return false;
  State next = state;
  next.players.assign(static_cast<std::size_t>(rules.setup.players), {});
  Reader r{bytes, kHeaderBytes};
  io_state(r, next);
  if (!r.ok || !plausible(rules, next)) return false;
  if (!fp::restore(rules.engine, next.world, bytes.subspan(r.at))) return false;
  state = std::move(next);
  return true;
}

}  // namespace ledgefall
