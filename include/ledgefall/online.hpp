#pragma once
// The client end of an online match: netplay-kit's ClientSession over a Link, running the core as the
// game. The app's side of milestone 3. It owns the Rules and State it plays (through NetplaySimulation),
// takes the local player's input each frame, and is advanced by calling `update` with the time.

#include <array>
#include <memory>
#include <optional>
#include <string>

#include "ledgefall/netplay.hpp"
#include "npk/session/client_session.hpp"

namespace ledgefall {

class OnlineMatch {
 public:
  enum class State : std::uint8_t { Connecting, Waiting, Running, Ended };

  // `server` is where the game server listens. `join`: the room and the token the lobby issued (a
  // game server that hosts many matches needs it; a single-match ServerSession does not).
  OnlineMatch(npk::transport::Link& link, const npk::transport::Address& server, Rules rules,
              std::optional<npk::session::msg::Join> join = std::nullopt);
  OnlineMatch(const OnlineMatch&) = delete;
  OnlineMatch& operator=(const OnlineMatch&) = delete;

  void connect(npk::transport::Micros now);
  void update(npk::transport::Micros now);
  void disconnect();

  // The local player's input right now; the session samples it once per frame.
  void set_input(const Input& in) { input_ = in; }

  State state() const;
  int seat() const { return session_.player(); }
  const NetplaySimulation& simulation() const { return sim_; }
  const npk::session::ClientSession& session() const { return session_; }

 private:
  NetplaySimulation sim_;
  Input input_;
  npk::session::ClientSession session_;
};

// A join message from a room id and the lobby's 32-hex-digit token; nothing if the token is malformed.
std::optional<npk::session::msg::Join> make_join(const std::string& room, const std::string& token_hex);

}  // namespace ledgefall
