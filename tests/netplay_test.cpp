// The core as a netplay-kit Simulation: the adapter's contract, the options the lobby passes on,
// and whole matches over the simulated network (a server session and clients, each with its own
// copy of the game) that must converge to the state a straight replay of the server's canonical
// inputs gives.
#include <cstdio>
#include <memory>
#include <vector>

#include "check.hpp"
#include "ledgefall/netplay.hpp"
#include "npk/json/json.hpp"
#include "npk/session/client_session.hpp"
#include "npk/session/server_session.hpp"
#include "npk/transport/sim_link.hpp"

namespace {

using namespace ledgefall;
using npk::session::Frame;
using npk::transport::Address;
using npk::transport::Micros;

constexpr Micros ms = 1000;

npk::json::Value parse(const char* text) {
  auto v = npk::json::parse(text);
  CHECK(v.has_value());
  return v ? *v : npk::json::Value();
}

void test_options() {
  const npk::json::Value none;
  CHECK(make_netplay_simulation(2, none) != nullptr);
  CHECK(make_netplay_simulation(1, none) == nullptr && make_netplay_simulation(5, none) == nullptr);
  const auto sim = make_netplay_simulation(3, parse(R"({"stocks":2,"seed":9,"stage":3,"characters":[6,5,4],"teams":[0,0,1],"seat_stocks":[0,0,7]})"));
  CHECK(sim != nullptr && sim->player_count() == 3 && sim->input_size() == 2);
  CHECK(sim->rules().setup.stocks == 2 && sim->rules().setup.seed == 9 && sim->rules().setup.stage == StageId::Tide);
  CHECK(sim->rules().setup.characters[0] == Character::Ferro && sim->rules().setup.teams[2] == 1);
  CHECK(sim->state().players[2].stocks == 7 && sim->state().players[0].stocks == 2);
  for (const char* bad : {R"({"stocks":0})", R"({"stocks":6})", R"({"stocks":"3"})", R"({"seed":-1})", R"({"seed":4294967296})",
                          R"({"stage":99})", R"({"characters":[1,2]})", R"({"characters":[1,2,9]})", R"({"teams":[0,0,2]})",
                          R"({"seat_stocks":[0,0,10]})", R"([1,2,3])", R"("x")"}) {
    CHECK(make_netplay_simulation(3, parse(bad)) == nullptr);
  }
}

void test_the_simulation_contract() {
  auto sim = make_netplay_simulation(2, parse(R"({"seed":4})"));
  CHECK(sim != nullptr);
  const std::uint8_t good[2] = {static_cast<std::uint8_t>(-100), kButtonJump | kButtonPlace};
  const std::uint8_t bad_axis[2] = {120, 0}, bad_button[2] = {0, 0x80};
  CHECK(sim->valid_input(good) && !sim->valid_input(bad_axis) && !sim->valid_input(bad_button));
  CHECK(!sim->valid_input(std::span<const std::uint8_t>(good, 1)));

  // Stepping, snapshotting and restoring: a restored copy goes on exactly as the original does.
  std::vector<std::uint8_t> inputs(4, 0);
  for (Frame f = 0; f < 200; ++f) {
    inputs[0] = static_cast<std::uint8_t>((f / 15 % 3 - 1) * 100);
    inputs[1] = f % 40 == 0 ? kButtonJump : 0;
    inputs[2] = static_cast<std::uint8_t>(-(f / 11 % 3 - 1) * 100);
    sim->step(f, inputs);
  }
  std::vector<std::uint8_t> bytes;
  sim->snapshot(bytes);
  CHECK(bytes.size() < 16 * 1024);  // one reliable message holds 64 KiB; this is far under
  auto copy = make_netplay_simulation(2, parse(R"({"seed":4})"));
  CHECK(copy->restore(bytes) && copy->hash() == sim->hash());
  for (Frame f = 200; f < 260; ++f) {
    sim->step(f, inputs);
    copy->step(f, inputs);
  }
  CHECK(copy->hash() == sim->hash());
  // A snapshot from the network may be anything.
  auto victim = make_netplay_simulation(2, parse(R"({"seed":4})"));
  const auto before = victim->hash();
  CHECK(!victim->restore({}));
  for (std::size_t cut : {std::size_t{1}, std::size_t{5}, bytes.size() / 2, bytes.size() - 1}) {
    CHECK(!victim->restore(std::span<const std::uint8_t>(bytes.data(), cut)));
  }
  auto flipped = bytes;
  flipped[0] ^= 1;
  CHECK(!victim->restore(flipped));
  CHECK(victim->hash() == before);

  // A late input is guessed as the same movement, without a press.
  const std::uint8_t prev[4] = {100, kButtonJump | kButtonPlace | kButtonDown, 0, kButtonTrick};
  std::uint8_t guess[4];
  sim->predict_input(prev, guess);
  CHECK(guess[0] == 100 && guess[1] == (kButtonJump | kButtonDown) && guess[2] == 0 && guess[3] == 0);
}

void test_a_finished_match_reports_itself() {
  Setup su;
  su.stocks = 1;
  auto sim = std::make_unique<NetplaySimulation>(*make_rules(su));
  CHECK(!sim->finished() && sim->result_json().find("\"winner\":null") != std::string::npos);
  const std::vector<std::uint8_t> idle(4, 0);
  for (Frame f = 0; f < 6000 && !sim->finished(); ++f) sim->step(f, idle);
  CHECK(sim->finished());
  const auto result = npk::json::parse(sim->result_json());
  CHECK(result.has_value() && result->find("winner") != nullptr && result->find("winner")->as_int().has_value());
  CHECK(result->find("places")->as_array()->size() == 2 && result->find("ticks")->as_int().value_or(0) > 100);
}

// A match over the simulated network: scripted inputs, a lossy link, every client converging.
struct SimMatch {
  struct Player {
    std::unique_ptr<npk::transport::SimLink> link;
    std::unique_ptr<NetplaySimulation> game;
    std::unique_ptr<npk::session::ClientSession> session;
  };

  SimMatch(std::uint64_t seed, std::size_t players, const char* options) : net(seed), n(players), opts(options) {
    server_addr = Address::v4(10, 0, 0, 1, 7000);
    server_link = net.make_link(server_addr);
    server_game = make_netplay_simulation(players, parse(options));
    npk::session::ServerSessionConfig sc;
    sc.transport.seed = seed * 31 + 7;
    sc.match.on_canonical = [this](Frame, std::span<const std::uint8_t> in) { canonical.insert(canonical.end(), in.begin(), in.end()); };
    server = std::make_unique<npk::session::ServerSession>(*server_link, *server_game, sc);
    for (std::size_t i = 0; i < players; ++i) {
      Player p;
      p.link = net.make_link(Address::v4(10, 1, 0, static_cast<std::uint8_t>(i + 1), 5000));
      p.game = make_netplay_simulation(players, parse(options));
      npk::session::ClientSessionConfig cc;
      cc.transport.seed = seed * 977 + i + 1;
      const std::uint64_t who = seed * 100 + i;
      p.session = std::make_unique<npk::session::ClientSession>(
          *p.link, server_addr, *p.game,
          [who](Frame wall, std::span<std::uint8_t> out) {
            // Moves back and forth, jumps, casts every so often: a different script for each player.
            const std::uint64_t t = wall + who * 7;
            out[0] = static_cast<std::uint8_t>(static_cast<std::int8_t>((t / 23 % 3 - 1) * 100));
            out[1] = static_cast<std::uint8_t>((t % 37 == 0 ? kButtonJump : 0) | (t % 61 == 0 ? kButtonPlace : 0) |
                                               (t % 83 == 0 ? kButtonTrick : 0) | (t % 101 == 0 ? kButtonBreak : 0) |
                                               (t % 29 == 0 ? kButtonDown : 0));
          },
          cc);
      players_.push_back(std::move(p));
    }
  }

  void run(Micros duration) {
    for (Micros t = 0; t < duration; t += ms) {
      for (auto& p : players_) p.session->update(net.now());
      server->update(net.now());
      net.advance(ms);
    }
  }

  // The hash of the right state at every frame the server finalised.
  std::vector<std::uint64_t> reference() const {
    std::vector<std::uint64_t> h;
    auto g = make_netplay_simulation(n, parse(opts));
    const std::size_t per_frame = n * kInputBytes;
    for (std::size_t f = 0; f * per_frame < canonical.size(); ++f) {
      h.push_back(g->hash());
      g->step(static_cast<Frame>(f), std::span(canonical).subspan(f * per_frame, per_frame));
    }
    h.push_back(g->hash());
    return h;
  }

  npk::transport::SimNetwork net;
  std::size_t n;
  const char* opts;
  Address server_addr;
  std::unique_ptr<npk::transport::SimLink> server_link;
  std::unique_ptr<NetplaySimulation> server_game;
  std::unique_ptr<npk::session::ServerSession> server;
  std::vector<Player> players_;
  std::vector<std::uint8_t> canonical;
};

// fighter-physics#23: until the pin has the fix, restore() rejects a world with a doomed tile, so a
// client cannot roll back through one and the matches below cannot converge. Say so, and skip them.
bool engine_can_restore_doomed_tiles() {
  auto sim = make_netplay_simulation(2, npk::json::Value());
  sim->step(0, std::vector<std::uint8_t>(4, 0));
  const_cast<State&>(sim->state()).world.platforms[0].phase = fp::PlatformPhase::Doomed;
  std::vector<std::uint8_t> bytes;
  sim->snapshot(bytes);
  auto other = make_netplay_simulation(2, npk::json::Value());
  return other->restore(bytes);
}

void converges(SimMatch& m, std::size_t at_least) {
  const auto ref = m.reference();
  std::size_t checked = 0;
  for (const auto& p : m.players_) {
    for (const auto& c : p.session->checkpoints()) {
      CHECK(c.frame < ref.size());
      CHECK(c.hash == ref[c.frame]);
      ++checked;
    }
  }
  CHECK(checked >= at_least);
}

void test_clients_converge_on_a_clean_link() {
  SimMatch m(1, 2, R"({"seed":11,"stage":1})");
  m.net.set_default_conditions({.delay = 10 * ms});
  for (auto& p : m.players_) p.session->connect(m.net.now());
  m.run(10'000 * ms);
  CHECK(m.server->state() == npk::session::ServerSession::State::Running || m.server_game->finished());
  CHECK(m.server->next_frame() > 500);
  converges(m, 2 * 10);
  for (const auto& p : m.players_) CHECK(p.session->rollback().ok());
}

void test_clients_converge_on_a_bad_link_with_four_players() {
  for (std::uint64_t seed = 1; seed <= 3; ++seed) {
    SimMatch m(seed, 4, R"({"seed":5,"stage":4,"characters":[0,2,3,6],"teams":[0,0,1,1]})");
    m.net.set_default_conditions({.drop = 0.08, .delay = 30 * ms, .jitter = 20 * ms, .reorder = 0.05, .reorder_delay = 40 * ms,
                                  .duplicate = 0.05});
    for (auto& p : m.players_) p.session->connect(m.net.now());
    m.run(15'000 * ms);
    CHECK(m.server->state() == npk::session::ServerSession::State::Running || m.server_game->finished());
    converges(m, 4 * 10);
    for (const auto& p : m.players_) {
      CHECK(p.session->rollback().ok());
      CHECK(p.session->rollback().stats().rollbacks > 0);
    }
  }
}

}  // namespace

int main() {
  test_options();
  test_the_simulation_contract();
  test_a_finished_match_reports_itself();
  if (!engine_can_restore_doomed_tiles()) {
    std::printf("SKIPPED the network matches: fighter-physics#23 (restore rejects a doomed tile) is not fixed in the pin\n");
  } else {
    test_clients_converge_on_a_clean_link();
    test_clients_converge_on_a_bad_link_with_four_players();
  }
  return test::finish();
}
