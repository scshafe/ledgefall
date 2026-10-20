#include "ledgefall/replay.hpp"

namespace ledgefall {
namespace {

constexpr std::uint8_t kMagic[4] = {'L', 'F', 'R', '1'};

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void put64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

struct In {
  std::span<const std::uint8_t> bytes;
  std::size_t at = 0;
  bool ok = true;
  std::uint8_t u8() {
    if (at >= bytes.size()) {
      ok = false;
      return 0;
    }
    return bytes[at++];
  }
  std::uint32_t u32() {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(u8()) << (8 * i);
    return v;
  }
  std::uint64_t u64() {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(u8()) << (8 * i);
    return v;
  }
};

}  // namespace

Recorder::Recorder(const Setup& setup) { replay_.setup = setup; }

void Recorder::record(std::span<const Input> inputs) {
  if (replay_.ticks >= kMaxReplayTicks) return;
  for (int i = 0; i < replay_.setup.players; ++i) {
    replay_.inputs.push_back(static_cast<std::size_t>(i) < inputs.size() ? inputs[static_cast<std::size_t>(i)] : Input{});
  }
  ++replay_.ticks;
}

Replay Recorder::finish(const State& state) const {
  Replay r = replay_;
  r.final_hash = hash(state);
  return r;
}

std::vector<std::uint8_t> encode(const Replay& r) {
  std::vector<std::uint8_t> out(kMagic, kMagic + 4);
  const Setup& s = r.setup;
  out.push_back(static_cast<std::uint8_t>(s.players));
  out.push_back(static_cast<std::uint8_t>(s.stocks));
  put32(out, s.seed);
  out.push_back(static_cast<std::uint8_t>(s.stage));
  for (int i = 0; i < kMaxPlayers; ++i) {
    out.push_back(static_cast<std::uint8_t>(s.characters[static_cast<std::size_t>(i)]));
    out.push_back(static_cast<std::uint8_t>(s.teams[static_cast<std::size_t>(i)]));
    out.push_back(static_cast<std::uint8_t>(s.seat_stocks[static_cast<std::size_t>(i)]));
  }
  put32(out, r.ticks);
  // Per tick: a mask of the seats whose input changed, then those inputs.
  std::vector<Input> prev(static_cast<std::size_t>(s.players));
  for (std::uint32_t t = 0; t < r.ticks; ++t) {
    const Input* now = r.tick_inputs(t);
    std::uint8_t mask = 0;
    for (int i = 0; i < s.players; ++i) {
      if (!(now[i] == prev[static_cast<std::size_t>(i)])) mask = static_cast<std::uint8_t>(mask | (1 << i));
    }
    out.push_back(mask);
    for (int i = 0; i < s.players; ++i) {
      if ((mask & (1 << i)) == 0) continue;
      out.push_back(static_cast<std::uint8_t>(now[i].move_x));
      out.push_back(now[i].buttons);
      prev[static_cast<std::size_t>(i)] = now[i];
    }
  }
  put64(out, r.final_hash);
  return out;
}

std::optional<Replay> decode_replay(std::span<const std::uint8_t> bytes) {
  In in{bytes};
  for (std::uint8_t m : kMagic) {
    if (in.u8() != m) return std::nullopt;
  }
  Replay r;
  Setup& s = r.setup;
  s.players = in.u8();
  s.stocks = in.u8();
  s.seed = in.u32();
  s.stage = static_cast<StageId>(in.u8());
  for (int i = 0; i < kMaxPlayers; ++i) {
    s.characters[static_cast<std::size_t>(i)] = static_cast<Character>(in.u8());
    s.teams[static_cast<std::size_t>(i)] = static_cast<std::int8_t>(in.u8());
    s.seat_stocks[static_cast<std::size_t>(i)] = static_cast<std::int8_t>(in.u8());
  }
  r.ticks = in.u32();
  if (!in.ok || !valid(s) || r.ticks > kMaxReplayTicks) return std::nullopt;
  r.inputs.reserve(static_cast<std::size_t>(r.ticks) * static_cast<std::size_t>(s.players));
  std::vector<Input> cur(static_cast<std::size_t>(s.players));
  for (std::uint32_t t = 0; t < r.ticks; ++t) {
    const std::uint8_t mask = in.u8();
    if ((mask >> s.players) != 0) return std::nullopt;  // a seat that is not in the match
    for (int i = 0; i < s.players; ++i) {
      if ((mask & (1 << i)) == 0) continue;
      const std::uint8_t pair[2] = {in.u8(), in.u8()};
      Input decoded;
      if (!in.ok || !ledgefall::decode(pair, decoded)) return std::nullopt;
      cur[static_cast<std::size_t>(i)] = decoded;
    }
    r.inputs.insert(r.inputs.end(), cur.begin(), cur.end());
  }
  r.final_hash = in.u64();
  if (!in.ok || in.at != bytes.size()) return std::nullopt;
  return r;
}

bool verify(const Replay& replay) {
  const auto rules = make_rules(replay.setup);
  if (!rules || replay.inputs.size() != static_cast<std::size_t>(replay.ticks) * static_cast<std::size_t>(replay.setup.players)) return false;
  State state = make_state(*rules);
  for (std::uint32_t t = 0; t < replay.ticks; ++t) {
    step(*rules, state, std::span<const Input>(replay.tick_inputs(t), static_cast<std::size_t>(replay.setup.players)));
  }
  return hash(state) == replay.final_hash;
}

}  // namespace ledgefall
