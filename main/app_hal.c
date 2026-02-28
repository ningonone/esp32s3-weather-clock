#include "app_hal.h"
#include "app_net.h"
#include "app_store.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "iot_button.h"

static const char *TAG = "app_hal";
static SemaphoreHandle_t s_lvgl_mux = NULL;

/* Hardware Pins */
#define TFT_MOSI 35
#define TFT_SCLK 36
#define TFT_CS 7
#define TFT_DC 39
#define TFT_RST 40
#define TFT_BL 45
#define TFT_POWER 21
#define BUTTON_BOOT 0

#define LCD_W 135
#define LCD_H 240
#define LCD_X_GAP 52
#define LCD_Y_GAP 40

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_map) {
  esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;

#if LV_COLOR_16_SWAP == 0
  /* lv_conf.h 未开启字节交换时，手动交换保证颜色正确 */
  int pixel_count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
  uint16_t *p = (uint16_t *)color_map;
  for (int i = 0; i < pixel_count; i++) {
    p[i] = (p[i] >> 8) | (p[i] << 8); /* 交换高低字节 */
  }
#endif

  esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1,
                            area->y2 + 1, color_map);
  lv_disp_flush_ready(drv);
}

static void lvgl_tick_cb(void *arg) { lv_tick_inc(1); }

void app_hal_lvgl_lock(void) {
  if (s_lvgl_mux)
    xSemaphoreTake(s_lvgl_mux, portMAX_DELAY);
}

void app_hal_lvgl_unlock(void) {
  if (s_lvgl_mux)
    xSemaphoreGive(s_lvgl_mux);
}

void app_hal_set_backlight(int percent) {
  if (percent < 0)
    percent = 0;
  if (percent > 100)
    percent = 100;
  uint32_t duty = (8191 * percent) / 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static uint32_t s_btn_press_time = 0;

static void button_press_down_cb(void *arg, void *usr_data) {
  s_btn_press_time = xTaskGetTickCount();
}

static void button_press_up_cb(void *arg, void *usr_data) {
  uint32_t press_duration =
      (xTaskGetTickCount() - s_btn_press_time) * portTICK_PERIOD_MS;
  if (press_duration >= 8000) {
    ESP_LOGE(TAG, "BOOT held for 8s: Factory Reset");
    app_store_factory_reset();
    esp_restart();
  } else if (press_duration >= 5000) {
    ESP_LOGW(TAG, "BOOT held for 5s: Enter Provisioning");
    app_net_start_provisioning();
  }
}

static void lvgl_task(void *arg) {
  while (1) {
    app_hal_lvgl_lock();
    uint32_t task_delay = lv_timer_handler();
    app_hal_lvgl_unlock();
    if (task_delay > 500)
      task_delay = 500;
    vTaskDelay(pdMS_TO_TICKS(task_delay > 0 ? task_delay : 5));
  }
}

void app_hal_init(void) {
  ESP_LOGI(TAG, "Initializing HAL (Display, Input)...");
  s_lvgl_mux = xSemaphoreCreateMutex();

  /* 1. Power Config */
  gpio_set_direction(TFT_POWER, GPIO_MODE_OUTPUT);
  gpio_set_level(TFT_POWER, 1);
  vTaskDelay(pdMS_TO_TICKS(10));

  /* 2. LEDC Backlight */
  ledc_timer_config_t ledc_timer = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                    .timer_num = LEDC_TIMER_0,
                                    .duty_resolution = LEDC_TIMER_13_BIT,
                                    .freq_hz = 5000,
                                    .clk_cfg = LEDC_AUTO_CLK};
  ledc_timer_config(&ledc_timer);
  ledc_channel_config_t ledc_channel = {.speed_mode = LEDC_LOW_SPEED_MODE,
                                        .channel = LEDC_CHANNEL_0,
                                        .timer_sel = LEDC_TIMER_0,
                                        .intr_type = LEDC_INTR_DISABLE,
                                        .gpio_num = TFT_BL,
                                        .duty = 8191, // 100%
                                        .hpoint = 0};
  ledc_channel_config(&ledc_channel);

  /* 3. SPI Display */
  spi_bus_config_t buscfg = {
      .mosi_io_num = TFT_MOSI,
      .miso_io_num = -1,
      .sclk_io_num = TFT_SCLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = LCD_W * 40 * sizeof(lv_color_t),
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_cfg = {
      .dc_gpio_num = TFT_DC,
      .cs_gpio_num = TFT_CS,
      .pclk_hz = 40 * 1000 * 1000,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
      .spi_mode = 0,
      .trans_queue_depth = 10,
  };
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                           &io_cfg, &io_handle));

  esp_lcd_panel_handle_t panel_handle = NULL;
  esp_lcd_panel_dev_config_t panel_cfg = {
      .reset_gpio_num = TFT_RST,
      .rgb_endian = LCD_RGB_ENDIAN_RGB,
      .bits_per_pixel = 16,
  };
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
  ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, LCD_X_GAP, LCD_Y_GAP));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  /* 4. LVGL Init */
  lv_init();
  static lv_color_t *buf1 = NULL;
  static lv_color_t *buf2 = NULL;
  buf1 = heap_caps_malloc(LCD_W * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  buf2 = heap_caps_malloc(LCD_W * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);
  static lv_disp_draw_buf_t draw_buf;
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LCD_W * 40);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = LCD_W;
  disp_drv.ver_res = LCD_H;
  disp_drv.flush_cb = lvgl_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.user_data = panel_handle;
  lv_disp_drv_register(&disp_drv);

  const esp_timer_create_args_t tick_timer_args = {.callback = &lvgl_tick_cb,
                                                   .name = "lvgl_tick"};
  esp_timer_handle_t tick_timer;
  ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000));

  xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 1);

  /* 5. BOOT Button */
  button_config_t gpio_btn_cfg = {
      .type = BUTTON_TYPE_GPIO,
      .long_press_time = 0,
      .short_press_time = 0,
      .gpio_button_config =
          {
              .gpio_num = BUTTON_BOOT,
              .active_level = 0,
          },
  };
  button_handle_t btn = iot_button_create(&gpio_btn_cfg);
  iot_button_register_cb(btn, BUTTON_PRESS_DOWN, button_press_down_cb, NULL);
  iot_button_register_cb(btn, BUTTON_PRESS_UP, button_press_up_cb, NULL);
}
