#include "internal.hpp"

namespace ledgefall::detail {

fp::MovementParams movement_for(Character c) {
  fp::MovementParams p;  // fighter-physics' Smash-like defaults
  switch (c) {
    case Character::Glim:
      p.aerial_jumps = 2;  // light: two air jumps
      break;
    default:
      break;
  }
  return p;
}

int cooldown_ticks(Character c, int slot) {
  // Seconds from lost-footing's table, in ticks.
  static constexpr int kSeconds100[kCharacterCount][kMoveSlots] = {
      {125, 600, 500},   // Mason: wall 1.25 s, ledge 6 s, anvil 5 s
      {230, 230, 550},   // Glim: glue 2.3 s, oil 2.3 s, acid 5.5 s
      {180, 800, 450},   // Pip: spring 1.8 s, gust 8 s, drill 4.5 s
      {220, 500, 450},   // Fuse: bumper 2.2 s, charge 5 s, saw 4.5 s
      {330, 700, 450},   // Dab: decoy 3.3 s, hollow 7 s, rub out 4.5 s
      {550, 400, 550},   // Fern: vine 5.5 s, thorns 4 s, roots 5.5 s
      {250, 600, 350},   // Ferro: lodestone 2.5 s, polarity 6 s, ingot 3.5 s
  };
  return kSeconds100[static_cast<int>(c)][slot] * kTicksPerSecond / 100;
}

}  // namespace ledgefall::detail
