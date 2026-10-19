// The C face runs the same simulation as the C++ core: a mixed human and bot
// match through lf_match_* has the hash of the same match run directly, and
// the host's inputs are clamped.
#include <vector>

#include "check.hpp"
#include "ledgefall/bot.hpp"
#include "ledgefall/capi.h"

using namespace ledgefall;

namespace {

void test_matches_direct_run() {
  lf_match* m = lf_match_create(3, 2);
  CHECK(m != nullptr && lf_match_set_bot(m, 2, LF_BOT_HARD));
  const auto rules = make_rules({3, 2});
  State s = make_state(*rules);
  Bot bot;
  bot.level = BotLevel::Hard;
  for (int t = 0; t < 1500; ++t) {
    const lf_input a{static_cast<std::int8_t>((t / 21 % 3 - 1) * 100), static_cast<std::uint8_t>(t % 27 == 0 ? LF_BUTTON_JUMP : 0)};
    const lf_input b{static_cast<std::int8_t>(-(t / 17 % 3 - 1) * 100), static_cast<std::uint8_t>(t % 35 == 0 ? LF_BUTTON_DOWN : 0)};
    const lf_input ins[3] = {a, b, {100, 3}};  // the bot seat's entry is ignored
    lf_match_step(m, ins);
    const std::vector<Input> direct{{a.move_x, a.buttons}, {b.move_x, b.buttons}, bot_input(*rules, s, 2, bot)};
    step(*rules, s, direct);
    CHECK(lf_match_hash(m) == hash(s));
  }
  lf_match_destroy(m);
}

void test_hostile_inputs_are_clamped() {
  lf_match* m = lf_match_create(2, 3);
  const lf_input wild[2] = {{-128, 0xff}, {127, 0x80}};
  for (int t = 0; t < 200; ++t) lf_match_step(m, wild);  // must not misbehave
  lf_match_step(m, nullptr);
  CHECK(lf_match_players(m) == 2);
  lf_entity e;
  CHECK(lf_match_entity(m, 1, &e));
  lf_match_destroy(m);
  lf_match_step(nullptr, nullptr);
  lf_match_destroy(nullptr);
}

}  // namespace

int main() {
  test_matches_direct_run();
  test_hostile_inputs_are_clamped();
  return test::finish();
}
