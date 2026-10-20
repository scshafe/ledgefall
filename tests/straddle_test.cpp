// A player on the seam between two tiles loads each with 75% of the usual rate (lost-footing), not half.
#include <vector>

#include "check.hpp"
#include "halos.hpp"
#include "ledgefall/match.hpp"

using namespace ledgefall;
using fp::Fixed;

namespace {

struct Seam {
  int left = -1, right = -1;  // adjacent tiles of one row
};

Seam find_seam(const Rules& r) {
  const auto& p = r.engine.stage.platforms;
  for (std::size_t i = 0; i < p.size(); ++i) {
    if (r.slab_hp[i] != 0) continue;
    for (std::size_t j = 0; j < p.size(); ++j) {
      if (i == j || r.slab_hp[j] != 0 || p[i].rect.top != p[j].rect.top) continue;
      if (fp::abs(p[i].rect.right - p[j].rect.left) < Fixed::one()) return {static_cast<int>(i), static_cast<int>(j)};
    }
  }
  return {};
}

// The stress each tile has after a still player has stood at `x` for a few ticks.
void stand(const Rules& r, int tile, Fixed x, Fixed& a, Fixed& b, int other) {
  State s = make_state(r);
  clear_halos(r, s);
  for (PlayerState& p : s.players) p.invuln = 0;
  s.world.entities[1].pos = {Fixed::from_int(3000), Fixed::from_int(3000)};
  s.world.entities[1].eliminated = 1;
  s.players[1].respawn_timer = 30000;
  s.world.entities[0] = fp::spawn_on(r.engine, tile, x);
  const std::vector<Input> none(2);
  for (int t = 0; t < 4; ++t) step(r, s, none);
  a = s.world.platforms[static_cast<std::size_t>(tile)].stress;
  b = other >= 0 ? s.world.platforms[static_cast<std::size_t>(other)].stress : Fixed::zero();
}

void test_a_straddler_loads_each_tile_at_three_quarters() {
  Setup su;
  su.players = 2;
  su.stage = StageId::Yard;
  const Rules r = *make_rules(su);
  const Seam seam = find_seam(r);
  CHECK(seam.left >= 0);
  const fp::Rect& lr = r.engine.stage.platforms[static_cast<std::size_t>(seam.left)].rect;
  const fp::Rect& rr = r.engine.stage.platforms[static_cast<std::size_t>(seam.right)].rect;

  Fixed alone, none_;
  stand(r, seam.left, (lr.left + lr.right) / Fixed::from_int(2), alone, none_, -1);  // in the middle of one tile
  CHECK(alone > Fixed::zero());
  Fixed l, rt;
  stand(r, seam.left, lr.right, l, rt, seam.right);  // on the seam
  CHECK(l > Fixed::zero() && rt > Fixed::zero());
  // 75% each: more than the old half, less than all of it. (One tick or two of a quarter-width of slop.)
  const Fixed three_quarters = alone * Fixed::from_int(3) / Fixed::from_int(4);
  CHECK(fp::abs(l - three_quarters) < alone / Fixed::from_int(20));
  CHECK(fp::abs(rt - three_quarters) < alone / Fixed::from_int(20));
  (void)rr;
}

}  // namespace

int main() {
  test_a_straddler_loads_each_tile_at_three_quarters();
  return test::finish();
}
