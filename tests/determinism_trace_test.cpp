// The cross-platform determinism gate: a scripted 4-player match, its state
// hash every 25 ticks, committed in tests/golden/determinism_hashes.txt. CI
// runs this on Linux (x86_64) and macOS (arm64); both must reproduce the file,
// so a divergence names the first tick at which a platform differs. After an
// intended change to the rules, regenerate with LEDGEFALL_UPDATE_GOLDEN=1 and
// review the diff.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;

namespace {

// A small integer generator for the script's inputs (the test's, not the core's).
struct Script {
  std::uint32_t s = 12345;
  std::uint32_t next() {
    s = s * 1664525u + 1013904223u;
    return s >> 16;
  }
  // Each seat holds a choice for 5 to 40 ticks.
  std::vector<Input> inputs(int tick, std::vector<Input>& held, std::vector<int>& until) {
    for (std::size_t i = 0; i < held.size(); ++i) {
      if (tick < until[i]) continue;
      until[i] = tick + 5 + static_cast<int>(next() % 36);
      const int axis = static_cast<int>(next() % 5);  // -100, -50, 0, 50, 100
      held[i].move_x = static_cast<std::int8_t>((axis - 2) * 50);
      held[i].buttons = static_cast<std::uint8_t>((next() % 3 == 0 ? kButtonJump : 0) | (next() % 6 == 0 ? kButtonDown : 0));
    }
    return held;
  }
};

}  // namespace

int main() {
  const auto rules = make_rules({4, 3});
  CHECK(rules.has_value());
  State s = make_state(*rules);
  Script script;
  std::vector<Input> held(4);
  std::vector<int> until(4, 0);
  std::ostringstream out;
  out << "# tick state-hash of the scripted 4-player match (tests/determinism_trace_test.cpp), every 25 ticks\n";
  for (int tick = 0; tick < 4000 && !s.over; ++tick) {
    step(*rules, s, script.inputs(tick, held, until));
    if ((tick + 1) % 25 == 0) {
      char line[64];
      std::snprintf(line, sizeof line, "%d %016llx\n", tick + 1, static_cast<unsigned long long>(hash(s)));
      out << line;
    }
  }
  char line[64];
  std::snprintf(line, sizeof line, "end over=%d winner=%d tick=%u %016llx\n", s.over ? 1 : 0, s.winner, s.world.frame,
                static_cast<unsigned long long>(hash(s)));
  out << line;

  const std::string path = std::string(LEDGEFALL_GOLDEN_DIR) + "/determinism_hashes.txt";
  const std::string got = out.str();
  if (std::getenv("LEDGEFALL_UPDATE_GOLDEN") != nullptr) {
    std::ofstream(path) << got;
    std::printf("wrote %s\n", path.c_str());
    return 0;
  }
  std::ifstream in(path);
  std::stringstream want;
  want << in.rdbuf();
  if (want.str() != got) {
    std::istringstream a(want.str()), b(got);
    std::string la, lb;
    while (std::getline(a, la) && std::getline(b, lb)) {
      if (la != lb) {
        std::fprintf(stderr, "determinism trace differs:\n  want: %s\n  got:  %s\n", la.c_str(), lb.c_str());
        break;
      }
    }
    ++test::failures();
  }
  return test::finish();
}
