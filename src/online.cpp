#include "ledgefall/online.hpp"

namespace ledgefall {

OnlineMatch::OnlineMatch(npk::transport::Link& link, const npk::transport::Address& server, Rules rules,
                         std::optional<npk::session::msg::Join> join)
    : sim_(std::move(rules)),
      session_(link, server, sim_,
               [this](npk::session::Frame, std::span<std::uint8_t> out) { encode(input_, out.data()); },
               [&join] {
                 npk::session::ClientSessionConfig c;
                 c.join = std::move(join);
                 return c;
               }()) {}

void OnlineMatch::connect(npk::transport::Micros now) { session_.connect(now); }
void OnlineMatch::update(npk::transport::Micros now) { session_.update(now); }
void OnlineMatch::disconnect() { session_.disconnect(); }

OnlineMatch::State OnlineMatch::state() const {
  using S = npk::session::ClientSession::State;
  switch (session_.state()) {
    case S::Idle:
    case S::Connecting: return State::Connecting;
    case S::Waiting: return State::Waiting;
    case S::Running: return State::Running;
    case S::Ended: return State::Ended;
  }
  return State::Ended;
}

std::optional<npk::session::msg::Join> make_join(const std::string& room, const std::string& token_hex) {
  if (room.empty() || token_hex.size() != 2 * npk::session::msg::kTokenSize) return std::nullopt;
  npk::session::msg::Join j;
  j.room = room;
  for (std::size_t i = 0; i < npk::session::msg::kTokenSize; ++i) {
    unsigned v = 0;
    for (std::size_t k = 0; k < 2; ++k) {
      const char c = token_hex[2 * i + k];
      unsigned d;
      if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
      else return std::nullopt;
      v = v * 16 + d;
    }
    j.token[i] = static_cast<std::uint8_t>(v);
  }
  return j;
}

}  // namespace ledgefall
