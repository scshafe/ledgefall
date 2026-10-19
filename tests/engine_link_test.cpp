// The core links fighter-physics and the pinned engine steps a world.
#include <cstdio>
#include <vector>

#include "fp/state.hpp"
#include "fp/world.hpp"

int main() {
  fp::Config cfg;
  cfg.stage.blast = {fp::Fixed::from_int(-100), fp::Fixed::from_int(100), fp::Fixed::from_int(-100),
                     fp::Fixed::from_int(100)};
  cfg.stage.platforms.push_back({fp::PlatformKind::Solid,
                                 {fp::Fixed::from_int(-10), fp::Fixed::from_int(10),
                                  fp::Fixed::from_int(-1), fp::Fixed::from_int(0)}});
  cfg.params.emplace_back();
  fp::World w = fp::make_world(cfg);
  w.entities.push_back(fp::spawn_on(cfg, 0, fp::Fixed::from_int(0)));
  fp::Events events;
  const std::vector<fp::Intent> intents(1);
  const auto before = fp::hash(w);
  fp::step(cfg, w, intents, events);
  if (w.frame != 1 || fp::hash(w) == before) {
    std::fprintf(stderr, "engine step did not advance the world\n");
    return 1;
  }
  return 0;
}
