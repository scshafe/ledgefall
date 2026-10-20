// The C face of an online match: ledgefall::OnlineMatch over a UDP socket.
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <new>
#include <string>

#include "capi_internal.hpp"
#include "ledgefall/online.hpp"
#include "npk/transport/udp_socket.hpp"

struct lf_net {
  std::unique_ptr<npk::transport::UdpSocket> socket;
  std::unique_ptr<ledgefall::OnlineMatch> match;
  std::unique_ptr<lf_match> view;  // points into match->simulation()
  bool failed = false;
};

namespace {

using namespace ledgefall;

// "host:port" to an address: an IP literal, or a name looked up (IPv4 first).
std::optional<npk::transport::Address> resolve(const std::string& hostport) {
  const auto colon = hostport.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= hostport.size()) return std::nullopt;
  std::string host = hostport.substr(0, colon);
  if (host.size() > 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
  const long port = std::strtol(hostport.c_str() + colon + 1, nullptr, 10);
  if (port < 1 || port > 65535) return std::nullopt;
  if (auto a = npk::transport::Address::parse(host, static_cast<std::uint16_t>(port))) return a;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* found = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || found == nullptr) return std::nullopt;
  std::optional<npk::transport::Address> result;
  for (addrinfo* a = found; a != nullptr && !result; a = a->ai_next) {
    char text[128];
    if (a->ai_family == AF_INET) {
      const auto* in = reinterpret_cast<const sockaddr_in*>(a->ai_addr);
      if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof text) != nullptr) result = npk::transport::Address::parse(text, static_cast<std::uint16_t>(port));
    }
  }
  freeaddrinfo(found);
  return result;
}

npk::transport::Micros now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

extern "C" {

lf_net* lf_net_open(const lf_setup* in, const char* server, const char* room, const char* token_hex) {
  if (in == nullptr || server == nullptr) return nullptr;
  Setup setup;
  setup.players = in->players;
  setup.stocks = in->stocks;
  setup.seed = in->seed;
  if (in->stage < 0 || in->stage >= LF_STAGE_COUNT) return nullptr;
  setup.stage = static_cast<StageId>(in->stage);
  for (std::size_t i = 0; i < 4; ++i) {
    setup.characters[i] = static_cast<Character>(in->characters[i]);
    setup.teams[i] = in->teams[i];
    setup.seat_stocks[i] = in->seat_stocks[i];
  }
  auto rules = make_rules(setup);
  const auto address = resolve(server);
  if (!rules || !address) return nullptr;

  std::optional<npk::session::msg::Join> join;
  if (room != nullptr || token_hex != nullptr) {
    join = make_join(room ? room : "", token_hex ? token_hex : "");
    if (!join) return nullptr;
  }
  auto socket = npk::transport::UdpSocket::bind(address->is_v4() ? npk::transport::Address::any_v4(0) : npk::transport::Address::any_v6(0));
  if (!socket) return nullptr;
  auto* n = new (std::nothrow) lf_net;
  if (n == nullptr) return nullptr;
  n->socket = std::move(socket);
  n->match = std::make_unique<OnlineMatch>(*n->socket, *address, std::move(*rules), std::move(join));
  n->view = std::make_unique<lf_match>(lf_match{ledgefall::Rules{}, ledgefall::Recorder(ledgefall::Setup{}), ledgefall::State{}, {}, {}, {}, nullptr, nullptr, {}});
  n->view->view_rules = &n->match->simulation().rules();
  n->view->view_state = &n->match->simulation().state();
  OnlineMatch* online = n->match.get();
  n->view->view_replay = [online] { return online->replay(); };
  n->match->connect(now_us());
  return n;
}

void lf_net_close(lf_net* n) { delete n; }

void lf_net_set_input(lf_net* n, lf_input input) {
  if (n == nullptr) return;
  int axis = input.move_x;
  if (axis < -kAxisMax) axis = -kAxisMax;
  if (axis > kAxisMax) axis = kAxisMax;
  n->match->set_input({static_cast<std::int8_t>(axis), static_cast<std::uint8_t>(input.buttons & kButtonMask)});
}

int lf_net_update(lf_net* n) {
  if (n == nullptr) return LF_NET_FAILED;
  n->match->update(now_us());
  switch (n->match->state()) {
    case OnlineMatch::State::Connecting: return LF_NET_CONNECTING;
    case OnlineMatch::State::Waiting: return LF_NET_WAITING;
    case OnlineMatch::State::Running: return LF_NET_RUNNING;
    case OnlineMatch::State::Ended: return LF_NET_ENDED;
  }
  return LF_NET_FAILED;
}

void lf_net_stats_get(const lf_net* n, lf_net_stats* out) {
  if (n == nullptr || out == nullptr) return;
  const auto& s = n->match->session();
  const bool started = n->match->state() == OnlineMatch::State::Running || n->match->state() == OnlineMatch::State::Ended;
  out->input_delay = s.input_delay();
  out->seat = started ? n->match->seat() : -1;
  out->rollbacks = static_cast<uint32_t>(s.rollback().stats().rollbacks);
  out->frames_simulated = static_cast<uint32_t>(s.stats().frames_simulated);
}

const lf_match* lf_net_view(const lf_net* n) { return n ? n->view.get() : nullptr; }

}  // extern "C"
