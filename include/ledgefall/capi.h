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
#define LF_BUTTON_PLACE 4 /* move slot 1 */
#define LF_BUTTON_TRICK 8 /* move slot 2 */
#define LF_BUTTON_BREAK 16 /* move slot 3 */

enum lf_character { /* mirrors ledgefall::Character */
  LF_CHARACTER_MASON, LF_CHARACTER_GLIM, LF_CHARACTER_PIP, LF_CHARACTER_FUSE, LF_CHARACTER_DAB,
  LF_CHARACTER_FERN, LF_CHARACTER_FERRO, LF_CHARACTER_COUNT
};

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
  uint8_t sturdy;                    /* 1 for a sturdy slab, 0 for a crumble tile */
  int32_t hp;                        /* a slab's hit points left */
  int32_t stress;                    /* a tile's stress, 0..LF_FIXED_ONE */
} lf_platform;

/* The things on the stage that are not stage: placeables and objects. */
enum lf_thing_kind {
  LF_THING_WALL, LF_THING_SPRING, LF_THING_GLUE, LF_THING_OIL, LF_THING_LOB, LF_THING_ACID, LF_THING_GUST,
  LF_THING_ANVIL, LF_THING_BUMPER, LF_THING_CHARGE, LF_THING_SAW,
  LF_THING_THORNS, LF_THING_ROOTS, LF_THING_RUB
};

typedef struct lf_thing {
  int32_t kind;  /* lf_thing_kind */
  int32_t left, right, bottom, top;
  int32_t owner; /* seat */
  int32_t armed; /* 1 once it acts (a spring or gust that is still arming is 0) */
} lf_thing;

typedef struct lf_player {
  int32_t stocks;
  int32_t respawn_timer;  /* ticks until it comes back, 0 if none pending */
  int32_t character;      /* lf_character */
  int32_t cooldown[3];    /* ticks until each move can be cast again */
  int32_t place;          /* final placing once the match is over, 1 is the winner; 0 before */
  int32_t kos;            /* eliminations credited */
  int32_t falls;          /* self-eliminations */
} lf_player;

/* NULL if players is not 2..4 or stocks not 1..5. Seats get Mason, Glim, Pip and Fuse in turn. */
lf_match* lf_match_create(int players, int stocks);
enum lf_stage { LF_STAGE_PRACTICE, LF_STAGE_YARD, LF_STAGE_QUARRY, LF_STAGE_COUNT };

/* The same with the match's random seed, a character (lf_character) for each of the `players` seats
 * (NULL for the defaults), and a stage (lf_stage). */
lf_match* lf_match_create_with(int players, int stocks, uint32_t seed, const uint8_t* characters, int stage);
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

int lf_match_thing_count(const lf_match* m);
int lf_match_thing(const lf_match* m, int index, lf_thing* out);

/* The blast zone: left, right, bottom, top. */
void lf_match_blast(const lf_match* m, int32_t out[4]);

#ifdef __cplusplus
}
#endif

#endif
