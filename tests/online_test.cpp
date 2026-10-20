// The client end of an online match: OnlineMatch over the simulated network, and the C face over real
// UDP on the loopback, each against netplay-kit's own single-match server session.
#include <chrono>
#include <thread>
#include <vector>

#include "check.hpp"
#include "ledgefall/capi.h"
#include "ledgefall/online.hpp"
#include "npk/session/server_session.hpp"
#include "npk/transport/sim_link.hpp"
#include "npk/transport/udp_socket.hpp"

namespace {

using namespace ledgefall;
using npk::session::Frame;
using npk::transport::Address;
using npk::transport::Micros;

constexpr Micros ms = 1000;

Rules two_player_rules() {
  Setup s;
  s.players = 2;
  s.stocks = 2;
  s.seed = 3;
  s.stage = StageId::Yard;
  s.characters = {Character::Fuse, Character::Pip, Character::Mason, Character::Mason};
  return *make_rules(s);
}

void test_join_tokens() {
  const auto j = make_join("room-1", "000102030405060708090a0b0c0d0e0f");
  CHECK(j.has_value() && j->room == "room-1" && j->token[0] == 0 && j->token[15] == 15 && j->token[10] == 10);
  CHECK(make_join("room", "000102030405060708090A0B0C0D0E0F").has_value());  // upper case is fine
  CHECK(!make_join("", "000102030405060708090a0b0c0d0e0f").has_value());
  CHECK(!make_join("room", "0001").has_value());
  CHECK(!make_join("room", "000102030405060708090a0b0c0d0e0g").has_value());
}

// Two OnlineMatch clients and a single-match server session over a lossy simulated network.
void test_clients_play_and_agree_with_the_server() {
  npk::transport::SimNetwork net(21);
  net.set_default_conditions({.drop = 0.05, .delay = 20 * ms, .jitter = 10 * ms});
  const Address server_addr = Address::v4(10, 0, 0, 1, 7000);
  auto server_link = net.make_link(server_addr);
  NetplaySimulation server_game(two_player_rules());
  std::vector<std::uint8_t> canonical;
  npk::session::ServerSessionConfig sc;
  sc.transport.seed = 5;
  sc.match.on_canonical = [&](Frame, std::span<const std::uint8_t> in) { canonical.insert(canonical.end(), in.begin(), in.end()); };
  npk::session::ServerSession server(*server_link, server_game, sc);

  struct Client {
    std::unique_ptr<npk::transport::SimLink> link;
    std::unique_ptr<OnlineMatch> match;
  };
  std::vector<Client> clients;
  for (int i = 0; i < 2; ++i) {
    Client c;
    c.link = net.make_link(Address::v4(10, 1, 0, static_cast<std::uint8_t>(i + 1), 5000));
    c.match = std::make_unique<OnlineMatch>(*c.link, server_addr, two_player_rules());
    clients.push_back(std::move(c));
  }
  for (auto& c : clients) CHECK(c.match->state() == OnlineMatch::State::Connecting);
  for (auto& c : clients) c.match->connect(net.now());
  for (Micros t = 0; t < 8000 * ms; t += ms) {
    for (std::size_t i = 0; i < clients.size(); ++i) {
      // Each player runs right or left and jumps now and then.
      const long wall = static_cast<long>(t / (16 * ms));
      clients[i].match->set_input({static_cast<std::int8_t>(((wall / 20 + static_cast<long>(i)) % 2 == 0 ? 1 : -1) * 100),
                                   static_cast<std::uint8_t>(wall % 45 == 0 ? kButtonJump : 0)});
      clients[i].match->update(net.now());
    }
    server.update(net.now());
    net.advance(ms);
  }
  for (auto& c : clients) CHECK(c.match->state() == OnlineMatch::State::Running);
  CHECK(clients[0].match->seat() != clients[1].match->seat());
  CHECK(server.next_frame() > 300);

  // Every checkpoint of every client is the state a straight replay of the canonical inputs gives.
  std::vector<std::uint64_t> ref;
  NetplaySimulation replay(two_player_rules());
  const std::size_t per_frame = 2 * kInputBytes;
  for (std::size_t f = 0; (f + 1) * per_frame <= canonical.size(); ++f) {
    ref.push_back(replay.hash());
    replay.step(static_cast<Frame>(f), std::span(canonical).subspan(f * per_frame, per_frame));
  }
  ref.push_back(replay.hash());
  std::size_t checked = 0;
  for (auto& c : clients) {
    for (const auto& cp : c.match->session().checkpoints()) {
      CHECK(cp.frame < ref.size() && cp.hash == ref[cp.frame]);
      ++checked;
    }
  }
  CHECK(checked >= 2 * 5);
}

// The C face over a real socket: the server session on the loopback, two lf_net clients.
void test_the_c_face_over_udp() {
  lf_setup su{};
  su.players = 2;
  su.stocks = 2;
  su.seed = 3;
  su.stage = LF_STAGE_YARD;
  su.characters[0] = LF_CHARACTER_FUSE;
  su.characters[1] = LF_CHARACTER_PIP;
  for (int i = 0; i < 4; ++i) su.teams[i] = -1;

  CHECK(lf_net_open(nullptr, "127.0.0.1:1", nullptr, nullptr) == nullptr);
  CHECK(lf_net_open(&su, nullptr, nullptr, nullptr) == nullptr);
  CHECK(lf_net_open(&su, "no-port", nullptr, nullptr) == nullptr);
  CHECK(lf_net_open(&su, "127.0.0.1:0", nullptr, nullptr) == nullptr);
  CHECK(lf_net_open(&su, "127.0.0.1:9", "room", "short") == nullptr);  // a token that is not 32 hex digits
  lf_setup bad = su;
  bad.stage = 99;
  CHECK(lf_net_open(&bad, "127.0.0.1:9", nullptr, nullptr) == nullptr);

  std::string error;
  auto sock = npk::transport::UdpSocket::bind(Address::v4(127, 0, 0, 1, 0), &error);
  CHECK(sock != nullptr);
  if (!sock) return;
  const std::string where = "127.0.0.1:" + std::to_string(sock->local_address().port);
  NetplaySimulation server_game(two_player_rules());
  npk::session::ServerSessionConfig sc;
  sc.transport.seed = 9;
  npk::session::ServerSession server(*sock, server_game, sc);

  lf_net* a = lf_net_open(&su, where.c_str(), nullptr, nullptr);
  lf_net* b = lf_net_open(&su, where.c_str(), nullptr, nullptr);
  CHECK(a != nullptr && b != nullptr);
  if (!a || !b) return;
  const auto start = std::chrono::steady_clock::now();
  auto now_us = [] { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
  int sa = 0, sb = 0;
  while (std::chrono::steady_clock::now() - start < std::chrono::seconds(4)) {
    lf_net_set_input(a, {100, 0});
    lf_net_set_input(b, {-100, 0});
    sa = lf_net_update(a);
    sb = lf_net_update(b);
    server.update(now_us());
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CHECK(sa == LF_NET_RUNNING && sb == LF_NET_RUNNING);
  lf_net_stats stats;
  lf_net_stats_get(a, &stats);
  CHECK(stats.seat >= 0 && stats.seat < 2 && stats.input_delay >= 1 && stats.frames_simulated > 100);

  const lf_match* view = lf_net_view(a);
  CHECK(view != nullptr && lf_match_players(view) == 2 && lf_match_tick(view) > 100);
  lf_entity e;
  CHECK(lf_match_entity(view, 0, &e) && e.height > 0);
  CHECK(lf_match_platform_count(view) > 20);  // the Yard
  lf_player p;
  CHECK(lf_match_player(view, 0, &p) && p.character == LF_CHARACTER_FUSE && p.stocks > 0);
  // A view is only for looking: stepping it does nothing.
  const auto h = lf_match_hash(view);
  lf_match_step(const_cast<lf_match*>(view), nullptr);
  CHECK(lf_match_hash(view) == h);
  CHECK(!lf_match_set_bot(const_cast<lf_match*>(view), 0, LF_BOT_EASY));

  lf_net_close(a);
  lf_net_close(b);
  lf_net_close(nullptr);
  lf_net_set_input(nullptr, {0, 0});
  CHECK(lf_net_update(nullptr) == LF_NET_FAILED);
  CHECK(lf_net_view(nullptr) == nullptr);
}

}  // namespace

int main() {
  test_join_tokens();
  test_clients_play_and_agree_with_the_server();
  test_the_c_face_over_udp();
  return test::finish();
}
