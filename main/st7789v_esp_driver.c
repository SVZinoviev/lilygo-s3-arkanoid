#include "st7789v_esp_driver.h"

#include "driver/gpio.h"
#include "esp_dma_utils.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "st7789v_esp_driver_board.h"

#define LCD_PIXEL_CLOCK_HZ (10 * 1000 * 1000)

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

typedef struct {
  uint8_t addr;
  uint8_t param[14];  // 14 is enough
  uint8_t len;
} st7789v_cmd_t;

static const st7789v_cmd_t st7789v_init_sequence[] = {
    // Command, {Parameters}, Parameter Length
    {0x01, {0}, 0},     // Software Reset
    {0x11, {0}, 0x80},  // Sleep Out (0x80 flag for delay)

    {0x3A, {0x05}, 1},  // Interface Pixel Format (16-bit)
    {0x36, {0x60}, 1},  // MADCTL: Landscape (Adjust for your orientation)

    {0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 5},  // Porch Setting
    {0xB7, {0x35}, 1},                          // Gate Control
    {0xBB, {0x19}, 1},                          // VCOM Setting
    {0xC0, {0x2C}, 1},                          // LCM Control
    {0xC2, {0x01}, 1},                          // VDV/VRH Command Enable
    {0xC3, {0x12}, 1},                          // VRH Set
    {0xC4, {0x20}, 1},                          // VDV Set
    {0xC6, {0x0F}, 1},                          // Frame Rate Control (60Hz)
    {0xD0, {0xA4, 0xA1}, 2},                    // Power Control 1

    // Positive Gamma Correction
    {0xE0,
     {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B,
      0x1F, 0x23},
     14},
    // Negative Gamma Correction
    {0xE1,
     {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F,
      0x20, 0x23},
     14},

    {0x21, {0}, 0},     // Display Inversion On
    {0x29, {0}, 0x80},  // Display On (0x80 flag for delay)
};

int st7789v_esp_driver_init(
    esp_lcd_panel_io_color_trans_done_cb_t bus_transmission_complete_cb) {
  gpio_config_t rd_gpio_config = {.mode = GPIO_MODE_OUTPUT,
                                  .pin_bit_mask = 1ULL << BOARD_LCD_RD};
  ESP_ERROR_CHECK(gpio_config(&rd_gpio_config));
  gpio_set_level(BOARD_LCD_RD, 1);

  esp_lcd_i80_bus_handle_t i80_bus = NULL;
  esp_lcd_i80_bus_config_t i80_bus_config = {
      .dc_gpio_num = BOARD_LCD_DC,
      .wr_gpio_num = BOARD_LCD_WR,
      .clk_src = LCD_CLK_SRC_DEFAULT,
      .data_gpio_nums =
          {
              BOARD_LCD_DATA0,
              BOARD_LCD_DATA1,
              BOARD_LCD_DATA2,
              BOARD_LCD_DATA3,
              BOARD_LCD_DATA4,
              BOARD_LCD_DATA5,
              BOARD_LCD_DATA6,
              BOARD_LCD_DATA7,
          },
      .bus_width = 8,
      .max_transfer_bytes = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
      .psram_trans_align = 64,
      .sram_trans_align = 4};
  esp_lcd_new_i80_bus(&i80_bus_config, &i80_bus);

  esp_lcd_panel_io_i80_config_t io_config = {
      .cs_gpio_num = BOARD_LCD_CS,
      .pclk_hz = LCD_PIXEL_CLOCK_HZ,
      .trans_queue_depth = 10,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .dc_levels =
          {
              .dc_idle_level = 0,
              .dc_cmd_level = 0,
              .dc_dummy_level = 0,
              .dc_data_level = 1,
          },
      .on_color_trans_done = bus_transmission_complete_cb,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

  esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = BOARD_LCD_RST,
      .color_space = ESP_LCD_COLOR_SPACE_RGB,
      .bits_per_pixel = 16,
      .vendor_config = NULL};
  esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle);

  esp_lcd_panel_reset(panel_handle);

  esp_lcd_panel_init(panel_handle);

  esp_lcd_panel_invert_color(panel_handle, true);

  esp_lcd_panel_swap_xy(panel_handle, true);

  esp_lcd_panel_mirror(panel_handle, false, true);

  esp_lcd_panel_set_gap(panel_handle, 0, 35);

  for (uint8_t i = 0;
       i < (sizeof(st7789v_init_sequence) / sizeof(st7789v_cmd_t)); i++) {
    esp_lcd_panel_io_tx_param(io_handle, st7789v_init_sequence[i].addr,
                              st7789v_init_sequence[i].param,
                              st7789v_init_sequence[i].len & 0x7f);
    if (st7789v_init_sequence[i].len & 0x80) {
      vTaskDelay(pdMS_TO_TICKS(120));
    }
  }

  esp_lcd_panel_disp_on_off(panel_handle, true);

  gpio_config_t bl_config = {.mode = GPIO_MODE_OUTPUT,
                             .pin_bit_mask = 1ULL << BOARD_LCD_BACKLIGHT};
  ESP_ERROR_CHECK(gpio_config(&bl_config));
  gpio_set_level(BOARD_LCD_BACKLIGHT, 1);

  return 0;
}

void st7789v_esp_driver_draw_bitmap(uint16_t x, uint16_t y, uint16_t width,
                                    uint16_t hight, uint16_t *data) {
  esp_lcd_panel_draw_bitmap(panel_handle, x, y, width, hight, data);
}
