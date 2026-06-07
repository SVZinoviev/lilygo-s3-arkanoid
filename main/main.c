#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "st7789v_esp_driver.h"
#include "string.h"

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

    while(true) {
        vTaskDelay(1);
        st7789v_esp_driver_draw_bitmap(0, 0, WIDTH, HEIGHT, fb);
        xSemaphoreTake(s_done, portMAX_DELAY);
    }
}
