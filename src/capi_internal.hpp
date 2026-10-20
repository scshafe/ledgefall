#pragma once
// The C face's handles, shared by the files that implement it.
#include "ledgefall/capi.h"

#include <functional>
#include <optional>
#include <vector>

#include "ledgefall/bot.hpp"
#include "ledgefall/match.hpp"
#include "ledgefall/replay.hpp"

struct lf_replay {
  ledgefall::Replay replay;
};

// A match to look at, and for a local one to play. A local match owns its rules and state; the view
// of an online match (lf_net_view) points into the simulation the network session runs.
struct lf_match {
  ledgefall::Rules rules;
  ledgefall::Recorder recorder;
  ledgefall::State state;
  std::vector<ledgefall::Bot> bots;
  std::vector<int> is_bot;
  std::vector<ledgefall::Input> inputs;  // scratch, one per seat
  const ledgefall::Rules* view_rules = nullptr;  // set for a view of someone else's match
  const ledgefall::State* view_state = nullptr;
  std::function<std::optional<ledgefall::Replay>()> view_replay;  // set for an online view: the finished match, once it is certain

  std::optional<ledgefall::Replay> replay() const { return view_replay ? view_replay() : std::optional(recorder.finish(S())); }

  const ledgefall::Rules& R() const { return view_rules ? *view_rules : rules; }
  const ledgefall::State& S() const { return view_state ? *view_state : state; }
  bool is_view() const { return view_state != nullptr; }
};
