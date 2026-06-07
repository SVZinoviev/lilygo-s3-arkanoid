#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "st7789v_esp_driver.h"
#include "string.h"
#include "arkanoid_game.h"

// TODO: move pins to the board pins file
#define BOARD_POWERON        15

void power_turn_on(bool is_on)
{
    gpio_config_t poweron_gpio_config = {0};
    poweron_gpio_config.pin_bit_mask  = 1ULL << BOARD_POWERON;
    poweron_gpio_config.mode          = GPIO_MODE_OUTPUT;

    ESP_ERROR_CHECK(gpio_config(&poweron_gpio_config));
    gpio_set_level(BOARD_POWERON, is_on);
}

static SemaphoreHandle_t  s_done;

bool lcd_transfer_is_done_cb(
    esp_lcd_panel_io_handle_t io,
    esp_lcd_panel_io_event_data_t *edata,
    void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;
    BaseType_t hp_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &hp_woken);
    return hp_woken == pdTRUE;
}

static uint16_t *fb;
#define WIDTH   320
#define HEIGHT  170

static game_t s_game;

/* The panel is RGB565 driven over an 8-bit i80 bus (high byte first), so the
 * framebuffer must hold byte-swapped pixels. Build colors through this helper. */
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8)); /* byte swap for the bus order */
}

#define COLOR_FIELD   0x0000                 /* black */
#define COLOR_PADDLE  rgb565(128, 128, 128)  /* grey  */
#define COLOR_BALL    rgb565(210, 210, 210)  /* slightly brighter than paddle */

/* Blocks are green; they darken as their remaining durability drops. */
static uint16_t block_color(uint8_t hits_remaining)
{
    switch (hits_remaining) {
        case 1:  return rgb565(0, 110, 0);
        case 2:  return rgb565(0, 185, 0);
        default: return rgb565(0, 255, 0);
    }
}

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WIDTH)  w = WIDTH  - x;
    if (y + h > HEIGHT) h = HEIGHT - y;

    for (int row = 0; row < h; row++) {
        uint16_t *line = &fb[(y + row) * WIDTH + x];
        for (int col = 0; col < w; col++) {
            line[col] = color;
        }
    }
}

static void fill_circle(int cx, int cy, int r, uint16_t color)
{
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r) {
                continue;
            }
            int px = cx + dx;
            int py = cy + dy;
            if (px >= 0 && px < WIDTH && py >= 0 && py < HEIGHT) {
                fb[py * WIDTH + px] = color;
            }
        }
    }
}

static void render_game(const game_t *g)
{
    memset(fb, 0, (size_t)WIDTH * HEIGHT * sizeof(uint16_t)); /* black field */

    /* Blocks. */
    for (int row = 0; row < GAME_BLOCK_ROWS; row++) {
        for (int col = 0; col < GAME_BLOCK_COLS; col++) {
            const block_t *b = &g->blocks[row][col];
            if (b->hits_remaining == 0) {
                continue;
            }
            int bx = GAME_FIELD_ORIGIN_X + col * GAME_BLOCK_W;
            int by = GAME_FIELD_ORIGIN_Y + row * GAME_BLOCK_H;
            /* 1px gap on right/bottom so the grid reads as separate blocks. */
            fill_rect(bx, by, GAME_BLOCK_W - 1, GAME_BLOCK_H - 1,
                      block_color(b->hits_remaining));
        }
    }

    /* Paddle. */
    int paddle_top = GAME_FP_TO_PX(g->paddle.y) - (int)g->paddle.length / 2;
    fill_rect(g->paddle.x, paddle_top, g->paddle.thickness, g->paddle.length,
              COLOR_PADDLE);

    /* Ball. */
    fill_circle(GAME_FP_TO_PX(g->ball.x), GAME_FP_TO_PX(g->ball.y),
                g->ball.radius, COLOR_BALL);
}

/* Placeholder controller: with no input device wired yet, auto-launch the ball
 * and steer the paddle to follow it so the demo stays in motion.
 * TODO: replace with real input (touch / buttons) calling game_paddle_move_*. */
static void demo_control(game_t *g)
{
    if (game_is_over(g) || game_is_won(g)) {
        game_reset(g);
        return;
    }
    if (!g->ball.launched) {
        game_paddle_move_up(g); /* first signal launches the ball */
        return;
    }
    int ball_y   = GAME_FP_TO_PX(g->ball.y);
    int paddle_y = GAME_FP_TO_PX(g->paddle.y);
    if (ball_y < paddle_y - 2) {
        game_paddle_move_up(g);
    } else if (ball_y > paddle_y + 2) {
        game_paddle_move_down(g);
    }
}

void app_main(void)
{
    power_turn_on(true);

    s_done = xSemaphoreCreateBinary();
    assert(s_done);
    xSemaphoreGive(s_done);

    size_t fb_bytes = (size_t)WIDTH * HEIGHT * sizeof(uint16_t);
    fb = heap_caps_malloc(fb_bytes, MALLOC_CAP_DMA);
    assert(fb);
    memset(fb, 0, fb_bytes);

    st7789v_esp_driver_init(lcd_transfer_is_done_cb);

    game_init(&s_game);

    while(true) {
        demo_control(&s_game);
        game_tick(&s_game);
        render_game(&s_game);

        st7789v_esp_driver_draw_bitmap(0, 0, WIDTH, HEIGHT, fb);
        xSemaphoreTake(s_done, portMAX_DELAY); /* wait for DMA before reusing fb */

        vTaskDelay(pdMS_TO_TICKS(21));         /* ~50 FPS */
    }
}
