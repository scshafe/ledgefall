// Replays: record a match, write it down, read it back, watch it again.
#include <vector>

#include "check.hpp"
#include "ledgefall/bot.hpp"
#include "ledgefall/capi.h"
#include "ledgefall/replay.hpp"

using namespace ledgefall;

namespace {

// A four-bot match to the end, recorded.
Replay record_bot_match(Setup su, State* out_state = nullptr) {
  const Rules rules = *make_rules(su);
  State s = make_state(rules);
  Recorder rec(su);
  std::vector<Bot> bots(static_cast<std::size_t>(su.players));
  for (Bot& b : bots) b.level = BotLevel::Hard;
  std::vector<Input> in(static_cast<std::size_t>(su.players));
  for (int t = 0; t < 4000 && !s.over; ++t) {
    for (int i = 0; i < su.players; ++i) in[static_cast<std::size_t>(i)] = bot_input(rules, s, i, bots[static_cast<std::size_t>(i)]);
    rec.record(in);
    step(rules, s, in);
  }
  if (out_state != nullptr) *out_state = s;
  return rec.finish(s);
}

Setup setup_for_test() {
  Setup su;
  su.players = 4;
  su.stocks = 2;
  su.seed = 77;
  su.stage = StageId::Yard;
  su.characters = {Character::Fuse, Character::Glim, Character::Pip, Character::Ferro};
  su.teams = {0, 0, 1, 1};
  su.seat_stocks = {0, 3, 0, 0};
  return su;
}

void test_a_replay_reproduces_its_match() {
  const Replay r = record_bot_match(setup_for_test());
  CHECK(r.ticks > 300);
  CHECK(verify(r));
  Replay bent = r;
  for (std::size_t i = bent.inputs.size() / 4; i < bent.inputs.size(); i += 5) {  // a different match from a quarter in
    bent.inputs[i].move_x = static_cast<std::int8_t>(-bent.inputs[i].move_x + 50);
    bent.inputs[i].buttons ^= kButtonJump;
  }
  CHECK(!verify(bent));  // a different match does not end with the same state
}

void test_bytes_round_trip_and_are_small() {
  const Replay r = record_bot_match(setup_for_test());
  const auto bytes = encode(r);
  const auto back = decode_replay(bytes);
  CHECK(back.has_value());
  CHECK(back->ticks == r.ticks && back->final_hash == r.final_hash && back->inputs == r.inputs);
  CHECK(back->setup.seed == 77 && back->setup.teams[2] == 1 && back->setup.seat_stocks[1] == 3);
  CHECK(bytes.size() < static_cast<std::size_t>(r.ticks) * 4);  // far under the 2 bytes x 4 seats a tick of the raw inputs
  CHECK(verify(*back));
}

void test_damaged_replays_are_refused() {
  const auto good = encode(record_bot_match(setup_for_test()));
  CHECK(!decode_replay({}).has_value());
  for (std::size_t cut : {std::size_t{3}, std::size_t{20}, good.size() / 2, good.size() - 1}) {
    CHECK(!decode_replay(std::span<const std::uint8_t>(good.data(), cut)).has_value());
  }
  auto bad = good;
  bad[0] ^= 1;
  CHECK(!decode_replay(bad).has_value());  // magic
  bad = good;
  bad[4] = 9;  // players
  CHECK(!decode_replay(bad).has_value());
  bad = good;
  bad.push_back(0);  // trailing bytes
  CHECK(!decode_replay(bad).has_value());
  bad = good;
  bad[10] = 99;  // the stage
  CHECK(!decode_replay(bad).has_value());
  // A flipped bit in the recorded hash is caught by the replay; one in the middle must at worst
  // give a different match (some inputs change nothing: a move on cooldown), never a crash.
  bad = good;
  bad[good.size() - 3] ^= 0x04;
  const auto tail = decode_replay(bad);
  CHECK(tail.has_value() && !verify(*tail));
  for (std::size_t at : {good.size() / 3, good.size() / 2, good.size() - 12}) {
    bad = good;
    bad[at] ^= 0x04;
    const auto r = decode_replay(bad);
    if (r.has_value()) (void)verify(*r);
  }
}

void test_the_c_face_records_and_replays() {
  lf_match* m = lf_match_create_with(2, 1, 5, nullptr, LF_STAGE_PRACTICE);
  CHECK(m != nullptr);
  for (int i = 0; i < 2; ++i) CHECK(lf_match_set_bot(m, i, LF_BOT_HARD));
  for (int t = 0; t < 3000 && !lf_match_over(m); ++t) lf_match_step(m, nullptr);
  CHECK(lf_match_over(m));
  const size_t need = lf_match_replay_size(m);
  CHECK(need > 20);
  std::vector<uint8_t> buf(need);
  CHECK(lf_match_replay_write(m, buf.data(), need - 1) == 0);  // too small
  CHECK(lf_match_replay_write(m, buf.data(), need) == need);

  lf_replay* r = lf_replay_open(buf.data(), buf.size());
  CHECK(r != nullptr && lf_replay_verify(r) == 1);
  CHECK(lf_replay_ticks(r) == lf_match_tick(m));
  CHECK(lf_replay_final_hash(r) == lf_match_hash(m));

  // Watching it: a fresh match from the replay's setup, stepped with the recorded inputs, ends the same.
  lf_setup su;
  lf_replay_setup(r, &su);
  CHECK(su.players == 2 && su.stocks == 1 && su.seed == 5);
  lf_match* watch = lf_match_create_setup(&su);
  CHECK(watch != nullptr);
  lf_input in[4];
  for (uint32_t t = 0; t < lf_replay_ticks(r); ++t) {
    CHECK(lf_replay_inputs(r, t, in));
    lf_match_step(watch, in);
  }
  CHECK(lf_match_hash(watch) == lf_match_hash(m));
  CHECK(!lf_replay_inputs(r, lf_replay_ticks(r), in));
  lf_replay_close(r);
  lf_match_destroy(watch);
  lf_match_destroy(m);
  CHECK(lf_replay_open(buf.data(), 5) == nullptr);
}

}  // namespace

int main() {
  test_a_replay_reproduces_its_match();
  test_bytes_round_trip_and_are_small();
  test_damaged_replays_are_refused();
  test_the_c_face_records_and_replays();
  return test::finish();
}
