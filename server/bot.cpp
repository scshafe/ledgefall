// A bot that plays ledgefall through the lobby, end to end: finds or creates a room, takes a seat,
// readies, gets its token, joins the game server over UDP and plays until the match ends, with the
// core's own bot choosing its inputs. A demo of the client side, and the end-to-end test
// (scripts/e2e).
//
//   ledgefall_bot --lobby HOST:PORT --room NAME [--create SEATS] [--stocks N] [--stage N]
//                 [--name BOT] [--timeout SECONDS]
// (--stocks and --stage must be the same for every bot of a room.)
//
// Prints "RESULT outcome=<outcome> frames=<n> winner=<seat>" and exits 0 if the match completed.

#include <chrono>
#include <cstring>
#include <thread>

#include "ledgefall/bot.hpp"
#include "ledgefall/netplay.hpp"
#include "npk/app/env.hpp"
#include "npk/http/client.hpp"
#include "npk/json/json.hpp"
#include "npk/session/client_session.hpp"
#include "npk/transport/udp_socket.hpp"

using namespace npk;

namespace {

struct Args {
  std::string lobby, room, name = "bot";
  long create = 0, stocks = 1, stage = 0, timeout = 120;
};

bool parse_args(int argc, char** argv, Args& a) {
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string k = argv[i], v = argv[i + 1];
    if (k == "--lobby") a.lobby = v;
    else if (k == "--room") a.room = v;
    else if (k == "--name") a.name = v;
    else if (k == "--create") a.create = std::atol(v.c_str());
    else if (k == "--stocks") a.stocks = std::atol(v.c_str());
    else if (k == "--stage") a.stage = std::atol(v.c_str());
    else if (k == "--timeout") a.timeout = std::atol(v.c_str());
    else return false;
  }
  return !a.lobby.empty() && !a.room.empty() && a.create >= 0 && a.create <= 4 && a.timeout > 0;
}

struct Lobby {
  transport::Address addr;
  std::string host;
  http::Client client;

  // One blocking request; the body parsed if it is JSON.
  std::pair<int, json::Value> call(const std::string& method, const std::string& path, const std::string& body = "",
                                   const std::string& bearer = "") {
    http::OutgoingRequest q;
    q.method = method;
    q.path = path;
    q.body = body;
    if (!bearer.empty()) q.headers.emplace_back("Authorization", "Bearer " + bearer);
    std::optional<http::Result> got;
    client.request(addr, host, q, transport::steady_now_us(), [&](const http::Result& r) { got = r; });
    while (!got && !app::stop_requested()) {
      client.update(transport::steady_now_us());
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!got || !got->ok) return {-1, {}};
    auto v = json::parse(got->body);
    return {got->status, v ? std::move(*v) : json::Value()};
  }
};

[[noreturn]] void fail(const char* why) {
  std::fprintf(stderr, "bot: %s\n", why);
  std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
  Args a;
  if (!parse_args(argc, argv, a)) {
    std::fprintf(stderr,
                 "usage: ledgefall_bot --lobby HOST:PORT --room NAME [--create SEATS] [--stocks N] [--stage N] [--name BOT] [--timeout S]\n");
    return 2;
  }
  app::install_signal_handlers();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(a.timeout);
  auto expired = [&] { return std::chrono::steady_clock::now() > deadline || app::stop_requested(); };
  auto nap = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };

  const auto hp = http::parse_host_port(a.lobby, 80);
  const auto addr = hp ? http::resolve(hp->host, hp->port) : std::nullopt;
  if (!addr) fail("cannot resolve the lobby");
  Lobby lobby{*addr, a.lobby, http::Client{}};

  // A game server must be registered before a room can start.
  for (;;) {
    auto [st, v] = lobby.call("GET", "/api/status");
    if (st == 200 && v.find("game_servers") && v.find("game_servers")->as_int().value_or(0) > 0) break;
    if (expired()) fail("no game server registered");
    nap(200);
  }

  std::string room_id;
  if (a.create > 0) {
    json::Value body, options;
    options.set("stocks", static_cast<std::int64_t>(a.stocks));
    options.set("stage", static_cast<std::int64_t>(a.stage));
    body.set("name", a.room);
    body.set("game", "ledgefall");
    body.set("seats", static_cast<std::int64_t>(a.create));
    body.set("options", options);
    auto [st, v] = lobby.call("POST", "/api/rooms", json::dump(body));
    if (st != 201) fail("cannot create the room");
    room_id = *v.find("id")->as_string();
  } else {
    while (room_id.empty()) {
      auto [st, v] = lobby.call("GET", "/api/rooms");
      if (st == 200) {
        for (const auto& r : *v.find("rooms")->as_array()) {
          if (*r.find("name")->as_string() == a.room && *r.find("state")->as_string() == "open") room_id = *r.find("id")->as_string();
        }
      }
      if (room_id.empty() && expired()) fail("room not found");
      if (room_id.empty()) nap(200);
    }
  }

  json::Value jb;
  jb.set("name", a.name);
  auto [jst, joined] = lobby.call("POST", "/api/rooms/" + room_id + "/join", json::dump(jb));
  if (jst != 200) fail("cannot take a seat");
  const std::string secret = *joined.find("secret")->as_string();
  const std::string path = "/api/rooms/" + room_id;
  json::Value rb;
  rb.set("ready", true);
  if (lobby.call("POST", path + "/ready", json::dump(rb), secret).first != 200) fail("cannot ready");

  std::string token, udp;
  json::Value options;
  for (;;) {
    auto [st, v] = lobby.call("POST", path + "/me", "", secret);
    if (st == 200 && *v.find("state")->as_string() == "playing") {
      token = *v.find("token")->as_string();
      udp = *v.find("server")->find("udp")->as_string();
      break;
    }
    if (expired()) fail("the match did not start");
    nap(200);
  }
  std::int64_t seats = 0;
  if (auto [st, v] = lobby.call("GET", path); st == 200) seats = *v.find("seat_count")->as_int();
  // The lobby does not show a room's options to its players (a netplay-kit gap: a client needs them
  // to make the same game the server did), so every bot is told them on its command line.
  options.set("stocks", static_cast<std::int64_t>(a.stocks));
  options.set("stage", static_cast<std::int64_t>(a.stage));

  // Play.
  const auto server_hp = http::parse_host_port(udp, 0);
  const auto server = server_hp ? http::resolve(server_hp->host, server_hp->port) : std::nullopt;
  if (!server || seats < 1) fail("cannot reach the game server");
  std::string err;
  auto sock = transport::UdpSocket::bind(server->is_v4() ? transport::Address::any_v4(0) : transport::Address::any_v6(0), &err);
  if (!sock) fail("cannot open a UDP socket");
  // The same options the server got: the lobby keeps them with the room.
  auto game = ledgefall::make_netplay_simulation(static_cast<std::size_t>(seats), options);
  if (!game) fail("cannot make the game from the room's options");
  session::ClientSessionConfig cc;
  session::msg::Join j;
  j.room = room_id;
  for (std::size_t i = 0; i < session::msg::kTokenSize; ++i) j.token[i] = static_cast<std::uint8_t>(std::stoi(token.substr(2 * i, 2), nullptr, 16));
  cc.join = j;

  ledgefall::Bot bot;
  bot.level = ledgefall::BotLevel::Hard;
  session::ClientSession* me = nullptr;
  session::ClientSession session(*sock, *server, *game, [&](session::Frame, std::span<std::uint8_t> out) {
    const ledgefall::Input in = ledgefall::bot_input(game->rules(), game->state(), me->player(), bot);
    ledgefall::encode(in, out.data());
  }, cc);
  me = &session;
  session.connect(transport::steady_now_us());
  while (session.state() != session::ClientSession::State::Ended && !expired()) {
    session.update(transport::steady_now_us());
    sock->wait_readable(2);
  }
  if (session.state() != session::ClientSession::State::Ended) fail("the match did not end");

  // The result as the lobby has it.
  for (;;) {
    auto [st, v] = lobby.call("GET", path);
    if (st == 200 && *v.find("state")->as_string() == "finished") {
      const auto* result = v.find("result");
      const std::string outcome = *result->find("outcome")->as_string();
      const json::Value* game_result = result->find("game");
      const std::int64_t frames = result->find("frames") ? result->find("frames")->as_int().value_or(-1) : -1;
      const std::int64_t winner = game_result && game_result->find("winner") ? game_result->find("winner")->as_int().value_or(-1) : -1;
      std::printf("RESULT outcome=%s frames=%lld winner=%lld\n", outcome.c_str(), static_cast<long long>(frames), static_cast<long long>(winner));
      return outcome == "completed" ? 0 : 1;
    }
    if (expired()) fail("the lobby has no result");
    nap(200);
  }
}
