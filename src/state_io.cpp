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

constexpr std::uint8_t kMagic0 = 'L', kMagic1 = 'F', kVersion = 10;
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
  a.num(p.invuln);
  a.num(p.no_contact);
  a.num(p.drill);
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
  a.num(p.only_for);
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
    a.num(m.tag);
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
    a.num(p.tag);
  }
  for (Object& o : s.objects) {
    a.num(o.kind);
    a.num(o.owner);
    a.num(o.payload);
    a.fx(o.pos.x);
    a.fx(o.pos.y);
    a.fx(o.vel.x);
    a.fx(o.vel.y);
    a.fx(o.x0);
    a.fx(o.x1);
    a.num(o.timer);
    a.num(o.arm);
    a.num(o.breaks);
    a.num(o.rest);
    a.num(o.uses);
    for (auto& c : o.cool) a.num(c);
    for (auto& h : o.hit) a.num(h);
  }
  for (TileBlame& t : s.tile_blame) {
    a.num(t.by);
    a.num(t.tick);
  }
  for (std::int8_t& hp : s.slab_hp) a.num(hp);
  for (auto& t : s.hazards.timers) a.num(t);
  for (HazardItem& h : s.hazards.list) {
    a.num(h.kind);
    a.num(h.dir);
    a.num(h.redirected_by);
    a.fx(h.pos.x);
    a.fx(h.pos.y);
    a.fx(h.vel.x);
    a.fx(h.vel.y);
    a.num(h.timer);
    for (auto& c : h.cool) a.num(c);
  }
  a.num(s.hazards.tide);
  a.num(s.hazards.tide_timer);
  a.fx(s.hazards.tide_level);
  a.num(s.hazards.gale);
  a.num(s.hazards.gale_dir);
  a.num(s.hazards.gale_timer);
}

bool plausible(const Rules& rules, const State& s) {
  const int n = rules.setup.players;
  for (const PlayerState& p : s.players) {
    if (p.stocks < 0 || p.stocks > kMaxSeatStocks || p.respawn_timer < 0 || p.respawn_timer > kRespawnDelayTicks) return false;
    for (auto c : p.cooldown) {
      if (c < 0 || c > 20 * kTicksPerSecond) return false;
    }
    if ((p.prev_buttons & ~kButtonMask) != 0 || p.slow < 0 || p.slow > 10 * kTicksPerSecond || p.drill < -1 || p.drill > 20 || p.invuln < 0 || p.no_contact < 0) return false;
    if (p.blame_by < -1 || p.blame_by >= n || p.place < 0 || p.place > n) return false;
  }
  if (s.winner < -1 || s.winner >= n) return false;
  for (const PlaceableMeta& m : s.meta) {
    if (m.tag > kTagMax) return false;
    if (m.owner < -1 || m.owner >= n || m.support < -1 ||
        m.support >= static_cast<int>(rules.engine.stage.platforms.size())) {
      return false;
    }
  }
  for (const Pending& p : s.pending) {
    if (p.tag > kPendingHollow) return false;
    if (p.owner < -1 || p.owner >= n || p.delay < 0 || p.delay > 5 * kTicksPerSecond) return false;
    if (p.support < -1 || p.support >= static_cast<int>(rules.engine.stage.platforms.size())) return false;
    if (static_cast<int>(p.what.kind) > static_cast<int>(fp::PlaceableKind::OneWay) || p.what.only_for < -1 || p.what.only_for >= n || p.what.active > 1) return false;
    if (p.used && p.owner < 0) return false;
  }
  for (const Object& o : s.objects) {
    if (static_cast<int>(o.kind) > static_cast<int>(ObjectKind::Magnet) || static_cast<int>(o.payload) > static_cast<int>(Payload::Charge)) return false;
    if (o.owner < -1 || o.owner >= n || o.timer < 0 || o.arm < 0 || o.breaks < 0 || o.rest < 0 || o.uses < 0) return false;
    for (auto c : o.cool) {
      if (c < 0) return false;
    }
    for (auto h : o.hit) {
      if (h < 0 || h > static_cast<int>(rules.engine.stage.platforms.size())) return false;
    }
    if (o.kind != ObjectKind::None && o.owner < 0) return false;
  }
  for (const TileBlame& t : s.tile_blame) {
    if (t.by < -1 || t.by >= n) return false;
  }
  for (const HazardItem& h : s.hazards.list) {
    if (static_cast<int>(h.kind) > static_cast<int>(HazardKind::Rock) || h.redirected_by < -1 || h.redirected_by >= n || h.timer < 0) return false;
    if (h.dir < -1 || h.dir > 1) return false;
    for (auto c : h.cool) {
      if (c < 0) return false;
    }
  }
  if (static_cast<int>(s.hazards.tide) > static_cast<int>(TidePhase::Fall) || static_cast<int>(s.hazards.gale) > static_cast<int>(GalePhase::Blow)) return false;
  if (s.hazards.tide_timer < 0 || s.hazards.gale_timer < 0 || s.hazards.gale_dir < -1 || s.hazards.gale_dir > 1) return false;
  for (auto t : s.hazards.timers) {
    if (t < 0) return false;
  }
  for (std::size_t i = 0; i < s.slab_hp.size(); ++i) {
    const int max = i < rules.slab_hp.size() ? rules.slab_hp[i] : 0;
    if (s.slab_hp[i] > max || s.slab_hp[i] < -8 || (max == 0 && s.slab_hp[i] != 0)) return false;
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
