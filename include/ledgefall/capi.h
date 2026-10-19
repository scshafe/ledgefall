/* A plain C face for the core, for hosts that are not C++ (the Swift app).
 *
 * A `lf_match` owns the rules, the state and the bots of one match. Positions
 * and sizes are fighter-physics' fixed-point values as raw integers: divide by
 * LF_FIXED_ONE (65536) to get world units, y up. Nothing here uses floating
 * point; the host converts at the edge to draw. Functions never throw; a bad
 * argument returns NULL or 0 as each says.
 */
#ifndef LEDGEFALL_CAPI_H
#define LEDGEFALL_CAPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LF_FIXED_ONE 65536
#define LF_MAX_PLAYERS 4
#define LF_TICKS_PER_SECOND 60

#define LF_BUTTON_JUMP 1
#define LF_BUTTON_DOWN 2

#define LF_BOT_NONE (-1) /* a human seat */
#define LF_BOT_EASY 0
#define LF_BOT_NORMAL 1
#define LF_BOT_HARD 2

typedef struct lf_match lf_match;

typedef struct lf_input {
  int8_t move_x;    /* -100..100 */
  uint8_t buttons;  /* LF_BUTTON_* */
} lf_input;

enum lf_mode { /* mirrors fp::Mode */
  LF_MODE_IDLE, LF_MODE_WALK, LF_MODE_DASH, LF_MODE_RUN, LF_MODE_SKID, LF_MODE_TURNAROUND,
  LF_MODE_JUMP_SQUAT, LF_MODE_AIRBORNE
};

typedef struct lf_entity {
  int32_t x, y;        /* feet, centred on x */
  int32_t vx, vy;
  int32_t half_width;  /* the body is 2 * half_width wide and `height` tall */
  int32_t height;
  int8_t facing;       /* -1 or 1 */
  uint8_t mode;        /* lf_mode */
  uint8_t eliminated;  /* 1 while out of play (dead or waiting to respawn) */
  uint8_t grounded;
} lf_entity;

enum lf_platform_phase { LF_PLATFORM_SOLID, LF_PLATFORM_SHAKING, LF_PLATFORM_GONE, LF_PLATFORM_DESTROYED };

typedef struct lf_platform {
  int32_t left, right, bottom, top;  /* where it is now */
  uint8_t phase;                     /* lf_platform_phase */
  int32_t timer;                     /* ticks left in Shaking or Gone */
} lf_platform;

typedef struct lf_player {
  int32_t stocks;
  int32_t respawn_timer;  /* ticks until it comes back, 0 if none pending */
} lf_player;

/* NULL if players is not 2..4 or stocks not 1..5. */
lf_match* lf_match_create(int players, int stocks);
void lf_match_destroy(lf_match* m);

/* Make a seat a bot (LF_BOT_EASY..HARD) or a human again (LF_BOT_NONE). 0 on a bad seat or level. */
int lf_match_set_bot(lf_match* m, int seat, int level);

/* One tick. `inputs` has one entry per seat; the entries of bot seats are ignored. */
void lf_match_step(lf_match* m, const lf_input* inputs);

int lf_match_players(const lf_match* m);
uint32_t lf_match_tick(const lf_match* m);
int lf_match_over(const lf_match* m);
int lf_match_winner(const lf_match* m); /* the winning seat, -1 for none or not over */
uint64_t lf_match_hash(const lf_match* m);

/* 0 for a seat out of range. */
int lf_match_player(const lf_match* m, int seat, lf_player* out);
int lf_match_entity(const lf_match* m, int seat, lf_entity* out);

int lf_match_platform_count(const lf_match* m);
int lf_match_platform(const lf_match* m, int index, lf_platform* out);

/* The blast zone: left, right, bottom, top. */
void lf_match_blast(const lf_match* m, int32_t out[4]);

#ifdef __cplusplus
}
#endif

#endif
