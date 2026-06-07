/*
 * Host-side unit tests for the Arkanoid game logic.
 *
 * The game module has no hardware/ESP-IDF dependencies, so these tests build
 * and run on a normal desktop with any C compiler:
 *
 *     cd test && make run
 *
 * The fixture is intentionally framework-free: a couple of CHECK macros and a
 * tiny runner that prints a pass/fail summary and exits non-zero on failure.
 */

#include <stdio.h>
#include "arkanoid_game.h"

/* --------------------------------------------------------------------------
 * Minimal test harness
 * ------------------------------------------------------------------------ */

static int g_tests_run    = 0;
static int g_tests_failed = 0;
static int g_checks_failed = 0;
static int g_current_failed = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            g_checks_failed++;                                             \
            g_current_failed = 1;                                          \
        }                                                                  \
    } while (0)

#define RUN(test_fn)                                                       \
    do {                                                                   \
        g_current_failed = 0;                                              \
        printf("- %s\n", #test_fn);                                        \
        test_fn();                                                         \
        g_tests_run++;                                                     \
        if (g_current_failed) g_tests_failed++;                            \
    } while (0)

/* --------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

/* Build a game whose ball is already launched and in play. */
static void setup_running(game_t *g)
{
    game_init(g);
    g->state         = GAME_STATE_RUNNING;
    g->ball.launched = true;
}

/* Wipe the whole block field so wall/paddle tests aren't perturbed by an
 * incidental block collision. blocks_remaining is left untouched so the win
 * condition does not fire. */
static void disable_all_blocks(game_t *g)
{
    for (int row = 0; row < GAME_BLOCK_ROWS; row++) {
        for (int col = 0; col < GAME_BLOCK_COLS; col++) {
            g->blocks[row][col].hits_remaining = 0;
        }
    }
}

/* Pixel coordinates of a block's top-left corner. */
static int block_x(int col) { return GAME_FIELD_ORIGIN_X + col * GAME_BLOCK_W; }
static int block_y(int row) { return GAME_FIELD_ORIGIN_Y + row * GAME_BLOCK_H; }

/* --------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

/* Sanity: a freshly initialized game parks the ball on the paddle face. */
static void test_initial_state(void)
{
    game_t g;
    game_init(&g);

    CHECK(g.state == GAME_STATE_READY);
    CHECK(g.ball.launched == false);
    CHECK(g.lives == GAME_LIVES_DEFAULT);
    CHECK(g.blocks_remaining == GAME_BLOCK_ROWS * GAME_BLOCK_COLS);

    /* Parked just in front of the paddle, vertically centered on it. */
    CHECK(g.ball.x == GAME_FP(g.paddle.x - g.ball.radius - 1));
    CHECK(g.ball.y == g.paddle.y);
    CHECK(g.ball.vx == 0 && g.ball.vy == 0);
}

/* (1) The ball starts moving (toward the blocks) when a paddle signal fires. */
static void test_ball_launches_on_signal(void)
{
    game_t g;
    game_init(&g);

    CHECK(g.ball.launched == false);
    CHECK(g.ball.vx == 0);

    game_paddle_move_up(&g);

    CHECK(g.ball.launched == true);
    CHECK(g.state == GAME_STATE_RUNNING);
    CHECK(g.ball.vx < 0);          /* moving left, toward the block field */
}

/* The launch angle follows the initial paddle direction (up vs down). */
static void test_launch_direction_follows_paddle(void)
{
    game_t g_up, g_down;

    game_init(&g_up);
    game_paddle_move_up(&g_up);
    CHECK(g_up.ball.vy < 0);       /* up signal -> upward angle */

    game_init(&g_down);
    game_paddle_move_down(&g_down);
    CHECK(g_down.ball.vy > 0);     /* down signal -> downward angle */
}

/* While parked, repeated ticks must not move the ball. */
static void test_ball_parked_until_signal(void)
{
    game_t g;
    game_init(&g);

    game_fp_t x0 = g.ball.x;
    for (int i = 0; i < 10; i++) {
        game_tick(&g);
    }
    CHECK(g.state == GAME_STATE_READY);
    CHECK(g.ball.launched == false);
    CHECK(g.ball.x == x0);
}

/* (2) The ball strikes the block at the grid cell its coordinates fall on,
 *     and only that cell. */
static void test_ball_hits_correct_block(void)
{
    game_t g;
    setup_running(&g);

    const int row = 5, col = 3;            /* an interior cell */
    int rx = block_x(col) + GAME_BLOCK_W;  /* block right face */
    int cy = block_y(row) + GAME_BLOCK_H / 2;

    uint8_t before = g.blocks[row][col].hits_remaining;
    CHECK(before >= 2);                    /* must survive one hit to compare */

    /* Approach the right face from the right, moving left. */
    g.ball.x  = GAME_FP(rx + 2);
    g.ball.y  = GAME_FP(cy);
    g.ball.vx = -GAME_BALL_SPEED;
    g.ball.vy = 0;

    game_tick(&g);

    CHECK(g.blocks[row][col].hits_remaining == (uint8_t)(before - 1));
    /* Neighbours untouched -> coordinates mapped to the right cell only. */
    CHECK(g.blocks[row][col - 1].hits_remaining ==
          (uint8_t)(1 + ((col - 1) / 2)));
    CHECK(g.blocks[row][col + 1].hits_remaining ==
          (uint8_t)(1 + ((col + 1) / 2)));
    CHECK(g.blocks[row - 1][col].hits_remaining ==
          (uint8_t)(1 + (col / 2)));
}

/* (3) After hitting a block the ball mirrors its motion (reflects on the
 *     contact axis); the perpendicular component is unchanged. */
static void test_ball_bounces_off_block(void)
{
    game_t g;
    setup_running(&g);

    const int row = 2, col = 2;            /* col 2 needs 2 hits -> survives */
    int rx = block_x(col) + GAME_BLOCK_W;
    int cy = block_y(row) + GAME_BLOCK_H / 2;

    g.ball.x  = GAME_FP(rx + 2);
    g.ball.y  = GAME_FP(cy);
    g.ball.vx = -GAME_BALL_SPEED;
    g.ball.vy = 0;

    game_fp_t vx_in = g.ball.vx;
    game_tick(&g);

    CHECK(g.ball.vx == -vx_in);            /* horizontal motion mirrored */
    CHECK(g.ball.vx > 0);                  /* now heading back to the paddle */
    CHECK(g.ball.vy == 0);                 /* vertical component preserved */
    CHECK(g.blocks[row][col].anim_counter > 0); /* hit animation started */
}

/* (4) The ball bounces off the paddle face back toward the blocks. */
static void test_ball_bounces_off_paddle(void)
{
    game_t g;
    setup_running(&g);
    disable_all_blocks(&g);

    /* Heading right, vertically centered on the paddle, just short of it. */
    g.ball.x  = GAME_FP(g.paddle.x - g.ball.radius - 1);
    g.ball.y  = g.paddle.y;
    g.ball.vx = GAME_BALL_SPEED;
    g.ball.vy = 0;

    game_tick(&g);

    CHECK(g.ball.vx < 0);                  /* reflected back toward blocks */
    CHECK(g.ball.x <= GAME_FP(g.paddle.x)); /* pushed out of the paddle */
    CHECK(g.state == GAME_STATE_RUNNING);  /* not lost */
}

/* The paddle imparts angle: hitting off-center adds a vertical component. */
static void test_paddle_imparts_angle(void)
{
    game_t g;
    setup_running(&g);
    disable_all_blocks(&g);

    /* Strike below the paddle center -> ball should gain downward velocity. */
    g.ball.x  = GAME_FP(g.paddle.x - g.ball.radius - 1);
    g.ball.y  = g.paddle.y + GAME_FP(10);
    g.ball.vx = GAME_BALL_SPEED;
    g.ball.vy = 0;

    game_tick(&g);

    CHECK(g.ball.vx < 0);
    CHECK(g.ball.vy > 0);                  /* deflected downward */
}

/* (5) A single-hit block is destroyed, decrementing the remaining count. */
static void test_ball_destroys_block(void)
{
    game_t g;
    setup_running(&g);

    const int row = 3, col = 0;            /* col 0 needs exactly 1 hit */
    CHECK(g.blocks[row][col].hits_remaining == 1);
    uint16_t remaining_before = g.blocks_remaining;

    int rx = block_x(col) + GAME_BLOCK_W;
    int cy = block_y(row) + GAME_BLOCK_H / 2;

    g.ball.x  = GAME_FP(rx + 2);
    g.ball.y  = GAME_FP(cy);
    g.ball.vx = -GAME_BALL_SPEED;
    g.ball.vy = 0;

    game_tick(&g);

    CHECK(g.blocks[row][col].hits_remaining == 0);     /* destroyed */
    CHECK(g.blocks_remaining == remaining_before - 1);
    CHECK(g.ball.vx > 0);                              /* still bounced back */
}

/* A multi-hit block survives the first strike but still bounces the ball. */
static void test_multi_hit_block_survives(void)
{
    game_t g;
    setup_running(&g);

    const int row = 4, col = 4;            /* col 4 needs 3 hits */
    uint8_t before = g.blocks[row][col].hits_remaining;
    CHECK(before == 3);
    uint16_t remaining_before = g.blocks_remaining;

    int rx = block_x(col) + GAME_BLOCK_W;
    int cy = block_y(row) + GAME_BLOCK_H / 2;

    g.ball.x  = GAME_FP(rx + 2);
    g.ball.y  = GAME_FP(cy);
    g.ball.vx = -GAME_BALL_SPEED;
    g.ball.vy = 0;

    game_tick(&g);

    CHECK(g.blocks[row][col].hits_remaining == (uint8_t)(before - 1));
    CHECK(g.blocks_remaining == remaining_before); /* not yet destroyed */
}

/* Walls: top, bottom and left all reflect the ball. */
static void test_wall_bounces(void)
{
    game_t g;

    /* Top wall (free column at x=200, no blocks there). */
    setup_running(&g);
    disable_all_blocks(&g);
    g.ball.x = GAME_FP(200); g.ball.y = GAME_FP(2);
    g.ball.vx = 0; g.ball.vy = -GAME_BALL_SPEED;
    game_tick(&g);
    CHECK(g.ball.vy > 0);                  /* bounced down */

    /* Bottom wall. */
    setup_running(&g);
    disable_all_blocks(&g);
    g.ball.x = GAME_FP(200); g.ball.y = GAME_FP(GAME_SCREEN_H - 2);
    g.ball.vx = 0; g.ball.vy = GAME_BALL_SPEED;
    game_tick(&g);
    CHECK(g.ball.vy < 0);                  /* bounced up */

    /* Left wall. */
    setup_running(&g);
    disable_all_blocks(&g);
    g.ball.x = GAME_FP(2); g.ball.y = GAME_FP(80);
    g.ball.vx = -GAME_BALL_SPEED; g.ball.vy = 0;
    game_tick(&g);
    CHECK(g.ball.vx > 0);                  /* bounced right */
}

/* Right side is open: a ball that passes the paddle costs a life and re-parks. */
static void test_ball_lost_past_right(void)
{
    game_t g;
    setup_running(&g);
    disable_all_blocks(&g);

    g.ball.x  = GAME_FP(GAME_SCREEN_W + 4); /* already past the open edge */
    g.ball.y  = GAME_FP(10);                /* clear of the paddle span    */
    g.ball.vx = GAME_BALL_SPEED;
    g.ball.vy = 0;
    uint8_t lives_before = g.lives;

    game_tick(&g);

    CHECK(g.lives == (uint8_t)(lives_before - 1));
    CHECK(g.state == GAME_STATE_READY);     /* re-served on the paddle */
    CHECK(g.ball.launched == false);
}

/* Losing the last life ends the game. */
static void test_game_over_on_last_life(void)
{
    game_t g;
    setup_running(&g);
    disable_all_blocks(&g);
    g.lives = 1;

    g.ball.x  = GAME_FP(GAME_SCREEN_W + 4);
    g.ball.y  = GAME_FP(10);
    g.ball.vx = GAME_BALL_SPEED;
    g.ball.vy = 0;

    game_tick(&g);

    CHECK(g.lives == 0);
    CHECK(g.state == GAME_STATE_OVER);
    CHECK(game_is_over(&g) == true);
}

/* Clearing the field wins the game. */
static void test_win_when_field_cleared(void)
{
    game_t g;
    setup_running(&g);
    disable_all_blocks(&g);
    g.blocks_remaining = 0;

    g.ball.x  = GAME_FP(200); g.ball.y = GAME_FP(80);
    g.ball.vx = -GAME_BALL_SPEED; g.ball.vy = 0;

    game_tick(&g);

    CHECK(g.state == GAME_STATE_WON);
    CHECK(game_is_won(&g) == true);
}

/* --------------------------------------------------------------------------
 * Runner
 * ------------------------------------------------------------------------ */

int main(void)
{
    printf("Arkanoid game logic tests\n\n");

    RUN(test_initial_state);
    RUN(test_ball_launches_on_signal);
    RUN(test_launch_direction_follows_paddle);
    RUN(test_ball_parked_until_signal);
    RUN(test_ball_hits_correct_block);
    RUN(test_ball_bounces_off_block);
    RUN(test_ball_bounces_off_paddle);
    RUN(test_paddle_imparts_angle);
    RUN(test_ball_destroys_block);
    RUN(test_multi_hit_block_survives);
    RUN(test_wall_bounces);
    RUN(test_ball_lost_past_right);
    RUN(test_game_over_on_last_life);
    RUN(test_win_when_field_cleared);

    printf("\n%d tests, %d failed (%d checks failed)\n",
           g_tests_run, g_tests_failed, g_checks_failed);

    return (g_tests_failed == 0) ? 0 : 1;
}
