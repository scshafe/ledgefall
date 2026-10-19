/* The C header is valid C and the C face works: a 4-bot match to the end. */
#include <stdio.h>

#include "ledgefall/capi.h"

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      return 1;                                                           \
    }                                                                     \
  } while (0)

int main(void) {
  CHECK(lf_match_create(1, 3) == NULL);
  CHECK(lf_match_create(5, 3) == NULL);
  CHECK(lf_match_create(2, 0) == NULL);
  CHECK(lf_match_create(2, 6) == NULL);

  lf_match* m = lf_match_create(4, 3);
  CHECK(m != NULL && lf_match_players(m) == 4);
  CHECK(!lf_match_set_bot(m, 4, LF_BOT_NORMAL) && !lf_match_set_bot(m, 0, 3));
  for (int i = 0; i < 4; ++i) CHECK(lf_match_set_bot(m, i, LF_BOT_NORMAL));

  lf_player p;
  CHECK(lf_match_player(m, 0, &p) && p.stocks == 3 && p.respawn_timer == 0);
  CHECK(!lf_match_player(m, 4, &p));
  lf_entity e;
  CHECK(lf_match_entity(m, 0, &e) && e.half_width > 0 && e.height > 0 && !e.eliminated);
  CHECK(lf_match_platform_count(m) > 0);
  lf_platform plat;
  CHECK(lf_match_platform(m, 0, &plat) && plat.left < plat.right && plat.bottom < plat.top);
  CHECK(!lf_match_platform(m, lf_match_platform_count(m), &plat));
  int32_t blast[4];
  lf_match_blast(m, blast);
  CHECK(blast[0] < blast[1] && blast[2] < blast[3]);

  const lf_input none[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
  int ticks = 0;
  while (!lf_match_over(m) && ticks < 6 * 60 * LF_TICKS_PER_SECOND) {
    lf_match_step(m, none);
    ++ticks;
  }
  CHECK(lf_match_over(m) && lf_match_winner(m) >= 0 && lf_match_winner(m) < 4);
  CHECK((uint32_t)ticks == lf_match_tick(m));
  lf_match_destroy(m);
  return 0;
}
