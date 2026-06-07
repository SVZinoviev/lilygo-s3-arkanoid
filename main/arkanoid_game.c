/*
 * Arkanoid-like game logic implementation.
 *
 * This file owns the simulation only: data model, movement integration and
 * collision resolution. Rendering, sprite artwork, audio and the concrete
 * effects of bonuses are intentionally left as placeholders (see the TODO
 * markers) so they can be wired to any display driver / asset pipeline later.
 */

#include "arkanoid_game.h"

/* Frames a destruction / bonus-reveal animation plays for. */
#define GAME_ANIM_FRAMES   8

/* --------------------------------------------------------------------------
 * Placeholder sprite handles.
 *
 * Real RGB565 artwork is not available yet, so every object points at a
 * descriptor whose `data` is NULL. The renderer can fall back to solid
 * rectangles/circles until art is supplied.
 * ------------------------------------------------------------------------ */

static const sprite_t s_sprite_block  = { GAME_BLOCK_W,  GAME_BLOCK_H,  NULL };
static const sprite_t s_sprite_ball   = { GAME_BALL_RADIUS * 2,
                                          GAME_BALL_RADIUS * 2, NULL };
static const sprite_t s_sprite_paddle = { GAME_PADDLE_THICKNESS,
                                          GAME_PADDLE_LEN_DEF, NULL };

/* --------------------------------------------------------------------------
 * Field setup
 * ------------------------------------------------------------------------ */

/* Decide how tough a block is and whether it hides a bonus, based on its grid
 * position. Deterministic for now; swap for a level table or PRNG later. */
static void block_configure(block_t *b, uint8_t row, uint8_t col)
{
    /* Columns closer to the paddle (higher col index) are tougher. */
    b->hits_remaining = (uint8_t)(1 + (col / 2));
    b->sprite         = &s_sprite_block;
    b->anim_counter   = 0;

    /* TODO: replace with real bonus distribution / randomization. */
    if (((row + col) % 5) == 0) {
        b->has_bonus  = true;
        b->bonus_type = (uint8_t)(BONUS_EXPAND_PADDLE + ((row + col) % (BONUS_COUNT - 1)));
    } else {
        b->has_bonus  = false;
        b->bonus_type = BONUS_NONE;
    }
}

/* Park the ball on the paddle's front (left) edge, centered on the paddle. */
static void ball_park_on_paddle(game_t *g)
{
    g->ball.radius   = GAME_BALL_RADIUS;
    g->ball.sprite   = &s_sprite_ball;
    g->ball.launched = false;
    g->ball.vx       = 0;
    g->ball.vy       = 0;
    g->ball.x        = GAME_FP(g->paddle.x - GAME_BALL_RADIUS - 1);
    g->ball.y        = g->paddle.y;
}

void game_init(game_t *g)
{
    if (g == NULL) {
        return;
    }

    g->paddle.x         = GAME_PADDLE_X;
    g->paddle.thickness = GAME_PADDLE_THICKNESS;
    g->paddle.length    = GAME_PADDLE_LEN_DEF;
    g->paddle.sprite    = &s_sprite_paddle;
    g->paddle.vy        = 0;
    g->paddle.y         = GAME_FP(GAME_SCREEN_H / 2);

    g->lives            = GAME_LIVES_DEFAULT;
    g->score            = 0;

    game_reset(g);
}

void game_reset(game_t *g)
{
    if (g == NULL) {
        return;
    }

    g->blocks_remaining = 0;
    for (uint8_t row = 0; row < GAME_BLOCK_ROWS; row++) {
        for (uint8_t col = 0; col < GAME_BLOCK_COLS; col++) {
            block_configure(&g->blocks[row][col], row, col);
            g->blocks_remaining++;
        }
    }

    g->paddle.length = GAME_PADDLE_LEN_DEF;
    g->paddle.y      = GAME_FP(GAME_SCREEN_H / 2);
    g->paddle.vy     = 0;

    g->last_dir = PADDLE_DIR_NONE;
    g->state    = GAME_STATE_READY;

    ball_park_on_paddle(g);
}

/* --------------------------------------------------------------------------
 * Paddle control + ball launch
 * ------------------------------------------------------------------------ */

/* Clamp the paddle inside the field given its current length. A small margin
 * keeps the paddle off the very top/bottom edges so it is not clipped by the
 * panel's visible-area overscan. */
static void paddle_clamp(game_t *g)
{
    game_fp_t half   = GAME_FP(g->paddle.length / 2);
    game_fp_t margin = GAME_FP(GAME_EDGE_MARGIN);
    game_fp_t top    = half + margin;
    game_fp_t bot    = GAME_FP(GAME_SCREEN_H) - half - margin;

    if (g->paddle.y < top) {
        g->paddle.y = top;
    } else if (g->paddle.y > bot) {
        g->paddle.y = bot;
    }
}

/* Send the parked ball toward the blocks. The vertical component follows the
 * initial paddle movement direction (up vs down). */
static void ball_launch(game_t *g, paddle_dir_t dir)
{
    g->ball.launched = true;
    g->ball.vx       = -GAME_BALL_SPEED;                 /* toward blocks (-X) */
    g->ball.vy       = (game_fp_t)dir * GAME_BALL_SPEED; /* up or down         */
    if (g->ball.vy == 0) {
        g->ball.vy = -GAME_BALL_SPEED; /* default to a slight upward angle */
    }
    g->state = GAME_STATE_RUNNING;
}

static void paddle_move(game_t *g, paddle_dir_t dir)
{
    if (g == NULL || g->state == GAME_STATE_OVER || g->state == GAME_STATE_WON) {
        return;
    }

    g->paddle.vy = (game_fp_t)dir * GAME_FP(GAME_PADDLE_STEP);
    g->paddle.y += g->paddle.vy;
    paddle_clamp(g);
    g->last_dir = dir;

    /* The ball rides the paddle until the first movement signal launches it. */
    if (!g->ball.launched) {
        g->ball.y = g->paddle.y;
        ball_launch(g, dir);
    }
}

void game_paddle_move_up(game_t *g)
{
    paddle_move(g, PADDLE_DIR_UP);
}

void game_paddle_move_down(game_t *g)
{
    paddle_move(g, PADDLE_DIR_DOWN);
}

/* --------------------------------------------------------------------------
 * Collision helpers
 * ------------------------------------------------------------------------ */

/* Register a strike on a block: decrement durability, kick off animation,
 * award score and reveal a bonus on destruction. Returns true if destroyed. */
static bool block_hit(game_t *g, block_t *b)
{
    if (b->hits_remaining == 0) {
        return false; /* already gone */
    }

    b->hits_remaining--;
    b->anim_counter = GAME_ANIM_FRAMES; /* drive hit/destroy animation */

    if (b->hits_remaining == 0) {
        g->blocks_remaining--;
        g->score += 100;

        if (b->has_bonus) {
            /* TODO: spawn a falling/drifting bonus pickup of b->bonus_type
             * so the paddle can collect it. */
        }
        return true;
    }

    g->score += 10;
    return false;
}

/* Resolve ball-vs-block collisions against the grid.
 *
 * The ball is treated as an AABB of side 2*radius. We test only the few cells
 * the ball overlaps, and reflect along the axis of shallowest penetration so
 * the ball bounces back off the block surface even when the block is
 * destroyed by the same strike. */
static void collide_blocks(game_t *g)
{
    int16_t cx = GAME_FP_TO_PX(g->ball.x);
    int16_t cy = GAME_FP_TO_PX(g->ball.y);
    int16_t r  = g->ball.radius;

    int16_t left   = cx - r;
    int16_t right  = cx + r;
    int16_t top    = cy - r;
    int16_t bottom = cy + r;

    /* Convert the ball's bounding box to the range of grid cells it spans. */
    int16_t col0 = (left   - GAME_FIELD_ORIGIN_X) / GAME_BLOCK_W;
    int16_t col1 = (right  - GAME_FIELD_ORIGIN_X) / GAME_BLOCK_W;
    int16_t row0 = (top    - GAME_FIELD_ORIGIN_Y) / GAME_BLOCK_H;
    int16_t row1 = (bottom - GAME_FIELD_ORIGIN_Y) / GAME_BLOCK_H;

    if (col0 < 0) col0 = 0;
    if (row0 < 0) row0 = 0;
    if (col1 >= GAME_BLOCK_COLS) col1 = GAME_BLOCK_COLS - 1;
    if (row1 >= GAME_BLOCK_ROWS) row1 = GAME_BLOCK_ROWS - 1;

    for (int16_t row = row0; row <= row1; row++) {
        for (int16_t col = col0; col <= col1; col++) {
            block_t *b = &g->blocks[row][col];
            if (b->hits_remaining == 0) {
                continue;
            }

            int16_t bx = GAME_FIELD_ORIGIN_X + col * GAME_BLOCK_W;
            int16_t by = GAME_FIELD_ORIGIN_Y + row * GAME_BLOCK_H;

            /* AABB overlap test (ball box vs block rect). */
            if (right < bx || left > bx + GAME_BLOCK_W ||
                bottom < by || top > by + GAME_BLOCK_H) {
                continue;
            }

            /* Shallowest-penetration axis decides the bounce direction. */
            int16_t pen_x = (g->ball.vx > 0) ? (right - bx)
                                             : (bx + GAME_BLOCK_W - left);
            int16_t pen_y = (g->ball.vy > 0) ? (bottom - by)
                                             : (by + GAME_BLOCK_H - top);

            if (pen_x < pen_y) {
                g->ball.vx = -g->ball.vx;
            } else {
                g->ball.vy = -g->ball.vy;
            }

            block_hit(g, b);
            return; /* one block per tick keeps the bounce stable */
        }
    }
}

/* Reflect off the top, bottom and left walls. The right side is open. */
static void collide_walls(game_t *g)
{
    game_fp_t r = GAME_FP(g->ball.radius);

    if (g->ball.x - r <= 0) {                 /* left wall */
        g->ball.x  = r;
        g->ball.vx = -g->ball.vx;
    }
    if (g->ball.y - r <= 0) {                 /* top wall */
        g->ball.y  = r;
        g->ball.vy = -g->ball.vy;
    }
    if (g->ball.y + r >= GAME_FP(GAME_SCREEN_H)) { /* bottom wall */
        g->ball.y  = GAME_FP(GAME_SCREEN_H) - r;
        g->ball.vy = -g->ball.vy;
    }
}

/* Bounce off the paddle and bias the vertical angle by where the ball hit
 * and by the paddle's own motion. Returns true on contact. */
static bool collide_paddle(game_t *g)
{
    if (g->ball.vx <= 0) {
        return false; /* moving away from the paddle */
    }

    game_fp_t r        = GAME_FP(g->ball.radius);
    game_fp_t face     = GAME_FP(g->paddle.x);        /* paddle front edge (X) */
    game_fp_t half_len = GAME_FP(g->paddle.length / 2);
    game_fp_t p_top    = g->paddle.y - half_len;
    game_fp_t p_bot    = g->paddle.y + half_len;

    if (g->ball.x + r < face) {
        return false; /* not reached the paddle face yet */
    }
    if (g->ball.y < p_top || g->ball.y > p_bot) {
        return false; /* missed along Y */
    }

    g->ball.x  = face - r;
    g->ball.vx = -g->ball.vx; /* back toward the blocks */

    /* Angle control: offset from paddle center plus a nudge from paddle motion.
     * TODO: tune the response curve once gameplay feel is evaluated. */
    game_fp_t offset = g->ball.y - g->paddle.y; /* [-half_len, +half_len] */
    g->ball.vy += offset / 8;
    g->ball.vy += g->paddle.vy / 2;

    return true;
}

/* --------------------------------------------------------------------------
 * Per-frame animation bookkeeping
 * ------------------------------------------------------------------------ */

static void advance_animations(game_t *g)
{
    for (uint8_t row = 0; row < GAME_BLOCK_ROWS; row++) {
        for (uint8_t col = 0; col < GAME_BLOCK_COLS; col++) {
            block_t *b = &g->blocks[row][col];
            if (b->anim_counter > 0) {
                b->anim_counter--;
                /* TODO: advance b->sprite through the animation frames. */
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * Main tick
 * ------------------------------------------------------------------------ */

void game_tick(game_t *g)
{
    if (g == NULL) {
        return;
    }

    advance_animations(g);

    /* Paddle momentum decays each frame; movement is re-applied by signals. */
    g->paddle.vy = 0;

    if (g->state != GAME_STATE_RUNNING) {
        if (g->state == GAME_STATE_READY) {
            g->ball.y = g->paddle.y; /* keep the parked ball glued to paddle */
        }
        return;
    }

    /* Integrate motion. */
    g->ball.x += g->ball.vx;
    g->ball.y += g->ball.vy;

    /* Resolve collisions. */
    collide_walls(g);
    collide_paddle(g);
    collide_blocks(g);

    /* Ball lost: it slipped past the open right side. */
    if (g->ball.x - GAME_FP(g->ball.radius) > GAME_FP(GAME_SCREEN_W)) {
        if (g->lives > 0) {
            g->lives--;
        }
        if (g->lives == 0) {
            g->state = GAME_STATE_OVER;
        } else {
            g->state = GAME_STATE_BALL_LOST;
            ball_park_on_paddle(g);
            g->state = GAME_STATE_READY;
        }
        return;
    }

    /* Win condition. */
    if (g->blocks_remaining == 0) {
        g->state = GAME_STATE_WON;
    }
}

/* --------------------------------------------------------------------------
 * Predicates
 * ------------------------------------------------------------------------ */

bool game_is_over(const game_t *g)
{
    return (g != NULL) && (g->state == GAME_STATE_OVER);
}

bool game_is_won(const game_t *g)
{
    return (g != NULL) && (g->state == GAME_STATE_WON);
}
