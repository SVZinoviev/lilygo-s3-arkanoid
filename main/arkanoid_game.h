#pragma once

/*
 * Arkanoid-like game logic (data model + API only).
 *
 * Screen orientation note:
 *   The display is used in landscape: 320 px wide (X) by 170 px tall (Y).
 *   This variant is rotated 90 deg from a classic Arkanoid:
 *
 *        X=0                                              X=319
 *      Y=0  +-------------------------------------------------+
 *           | [block grid ......]            o   <- ball   |  |  <- paddle
 *           | [block grid ......]                          |  |     (right side)
 *           | [block grid ......]                          |  |
 *     Y=169 +-------------------------------------------------+
 *
 *   - Blocks occupy the LEFT side of the field.
 *   - The paddle sits on the RIGHT side and moves UP/DOWN (along Y).
 *   - The ball travels mostly horizontally (toward the blocks = -X).
 *   - The ball bounces off the TOP, BOTTOM and LEFT walls.
 *   - The RIGHT side is open: if the ball passes the paddle it is lost.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Field geometry
 * ------------------------------------------------------------------------ */

#define GAME_SCREEN_W          320   /* play area width  (X axis) */
#define GAME_SCREEN_H          170   /* play area height (Y axis) */

/* "6x8" block field. Rows run vertically (Y), columns grow from the
 * left wall toward the paddle (X). */
#define GAME_BLOCK_ROWS        8     /* number of block rows    (along Y) */
#define GAME_BLOCK_COLS        6     /* number of block columns (along X) */

#define GAME_BLOCK_W           22    /* single block width  in px */
#define GAME_BLOCK_H           20    /* single block height in px */
#define GAME_FIELD_ORIGIN_X    2     /* top-left of the grid, X */
#define GAME_FIELD_ORIGIN_Y    4     /* top-left of the grid, Y */

#define GAME_PADDLE_X          308   /* paddle left edge (near right wall) */
#define GAME_PADDLE_THICKNESS  6     /* paddle size along X */
#define GAME_PADDLE_LEN_DEF    44    /* default paddle length (along Y) */
#define GAME_PADDLE_LEN_MIN    24
#define GAME_PADDLE_LEN_MAX    80
#define GAME_PADDLE_STEP       4     /* px moved per up/down signal */

#define GAME_BALL_RADIUS       3     /* fixed ball radius in px */

#define GAME_LIVES_DEFAULT     3

/* --------------------------------------------------------------------------
 * Fixed-point helpers (Q8: 256 == 1.0 px) for smooth sub-pixel motion.
 * ------------------------------------------------------------------------ */

typedef int32_t game_fp_t;
#define GAME_FP_SHIFT          8
#define GAME_FP_ONE            (1 << GAME_FP_SHIFT)
#define GAME_FP(px)            ((game_fp_t)(px) * GAME_FP_ONE)
#define GAME_FP_TO_PX(v)       ((int16_t)((v) >> GAME_FP_SHIFT))

/* Base ball speed magnitude per tick, per axis (fixed-point). */
#define GAME_BALL_SPEED        (GAME_FP_ONE * 2)

/* --------------------------------------------------------------------------
 * Sprites
 * ------------------------------------------------------------------------ */

/* A sprite is RGB565 pixel data plus its dimensions. `data` may be NULL
 * while the artwork is still a placeholder. */
typedef struct {
    uint16_t        width;
    uint16_t        height;
    const uint16_t *data;   /* RGB565, row-major; NULL == not yet provided */
} sprite_t;

/* --------------------------------------------------------------------------
 * Bonuses
 * ------------------------------------------------------------------------ */

typedef enum {
    BONUS_NONE = 0,
    BONUS_EXPAND_PADDLE,
    BONUS_SHRINK_PADDLE,
    BONUS_MULTI_BALL,
    BONUS_SLOW_BALL,
    BONUS_FAST_BALL,
    BONUS_EXTRA_LIFE,
    BONUS_COUNT
} bonus_type_t;

/* --------------------------------------------------------------------------
 * Block
 * ------------------------------------------------------------------------ */

typedef struct {
    const sprite_t *sprite;          /* current sprite for this block       */
    uint8_t         hits_remaining;  /* strikes left to destroy (0 == gone) */
    bool            has_bonus;        /* does this block contain a bonus     */
    uint8_t         bonus_type;       /* bonus_type_t; valid if has_bonus    */
    uint8_t         anim_counter;     /* frames left of destroy/reveal anim  */
} block_t;

/* --------------------------------------------------------------------------
 * Ball — fixed size round object
 * ------------------------------------------------------------------------ */

typedef struct {
    game_fp_t       x;        /* center X (fixed-point) */
    game_fp_t       y;        /* center Y (fixed-point) */
    game_fp_t       vx;       /* velocity X per tick    */
    game_fp_t       vy;       /* velocity Y per tick    */
    uint8_t         radius;   /* fixed radius in px      */
    bool            launched; /* false while resting on the paddle */
    const sprite_t *sprite;
} ball_t;

/* --------------------------------------------------------------------------
 * Paddle — moves along Y, variable length
 * ------------------------------------------------------------------------ */

typedef struct {
    game_fp_t       y;         /* center Y (fixed-point) */
    int16_t         x;         /* left edge X (fixed, right side of field) */
    uint16_t        length;    /* current length along Y (variable size)   */
    uint16_t        thickness; /* size along X                              */
    game_fp_t       vy;        /* last movement; used to impart ball angle  */
    const sprite_t *sprite;
} paddle_t;

/* --------------------------------------------------------------------------
 * Game state
 * ------------------------------------------------------------------------ */

typedef enum {
    GAME_STATE_READY = 0,  /* ball parked on paddle, waiting for launch */
    GAME_STATE_RUNNING,
    GAME_STATE_BALL_LOST,  /* ball passed the paddle this tick          */
    GAME_STATE_WON,        /* all blocks cleared                        */
    GAME_STATE_OVER        /* no lives left                             */
} game_state_t;

/* Direction of the external up/down paddle signal. */
typedef enum {
    PADDLE_DIR_NONE = 0,
    PADDLE_DIR_UP   = -1,
    PADDLE_DIR_DOWN = 1
} paddle_dir_t;

typedef struct {
    block_t      blocks[GAME_BLOCK_ROWS][GAME_BLOCK_COLS];
    ball_t       ball;
    paddle_t     paddle;

    game_state_t state;
    uint16_t     blocks_remaining;
    uint8_t      lives;
    uint32_t     score;

    paddle_dir_t last_dir;   /* last paddle movement (sets launch angle) */
} game_t;

/* --------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

/* Initialize a fresh game: build the block field, place paddle and park the
 * ball on the paddle edge facing the blocks. */
void game_init(game_t *g);

/* Restart the current game (lives, score, field) from scratch. */
void game_reset(game_t *g);

/* External "move paddle up" signal. Moves the paddle one step toward the top.
 * While the ball is still parked this also LAUNCHES it toward the blocks with
 * an upward angle. */
void game_paddle_move_up(game_t *g);

/* External "move paddle down" signal. Mirror of game_paddle_move_up(). */
void game_paddle_move_down(game_t *g);

/* Advance the simulation by one tick (call once per rendered frame):
 * integrate ball motion, resolve wall / paddle / block collisions, run
 * animation counters and update game state. */
void game_tick(game_t *g);

/* Convenience predicates. */
bool game_is_over(const game_t *g);
bool game_is_won(const game_t *g);

#ifdef __cplusplus
}
#endif
