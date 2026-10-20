// Test helper: a new match starts with a halo under every player; tests that want an empty stage take them away.
#pragma once
#include "ledgefall/match.hpp"

inline void clear_halos(const ledgefall::Rules& rules, ledgefall::State& s) {
  fp::Events scratch;
  for (std::size_t slot = 0; slot < s.world.placeables.size(); ++slot) {
    if (s.world.placeables[slot].active != 0 && s.meta[slot].tag == ledgefall::kTagHalo) {
      fp::remove_placeable(rules.engine, s.world, static_cast<int>(slot), scratch);
      s.meta[slot] = {};
    }
  }
}
