#include "app_ui.h"
#include "app_hal.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>

static const char *TAG = "app_ui";

// Fonts
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_cus_16);
LV_FONT_DECLARE(lv_font_cus_36);

static lv_obj_t *s_scr_main;
static lv_obj_t *s_scr_prov;

// Main Screen Widgets
static lv_obj_t *s_label_time;
static lv_obj_t *s_label_date;
static lv_obj_t *s_label_wifi;
static lv_obj_t *s_label_weather_icon;
static lv_obj_t *s_label_weather_temp;
static lv_obj_t *s_label_weather_desc;
static lv_obj_t *s_label_weather_humidity;

// Styles
static lv_style_t s_style_bg;
static lv_style_t s_style_time;
static lv_style_t s_style_normal;
static lv_style_t s_style_cjk;
static lv_style_t s_style_cjk_36;

static void create_styles(void) {
  lv_style_init(&s_style_bg);
  lv_style_set_bg_color(&s_style_bg, lv_color_hex(0x111111));
  lv_style_set_bg_opa(&s_style_bg, LV_OPA_COVER);
  lv_style_set_text_color(&s_style_bg, lv_color_white());

  lv_style_init(&s_style_time);
  lv_style_set_text_font(&s_style_time, &lv_font_montserrat_48);
  lv_style_set_text_color(&s_style_time, lv_color_hex(0x00E5FF)); // Cyan

  lv_style_init(&s_style_normal);
  lv_style_set_text_font(&s_style_normal, &lv_font_montserrat_16);
  lv_style_set_text_color(&s_style_normal, lv_color_hex(0xAAAAAA));

  lv_style_init(&s_style_cjk);
  lv_style_set_text_font(&s_style_cjk, &lv_font_cus_16);
  lv_style_set_text_color(&s_style_cjk, lv_color_hex(0xDDDDDD));

  lv_style_init(&s_style_cjk_36);
  lv_style_set_text_font(&s_style_cjk_36, &lv_font_cus_36);
  lv_style_set_text_color(&s_style_cjk_36, lv_color_hex(0xFFFFFF));
}

static void create_main_screen(void) {
  s_scr_main = lv_obj_create(NULL);
  lv_obj_add_style(s_scr_main, &s_style_bg, 0);

  // Time Label (Center Top)
  s_label_time = lv_label_create(s_scr_main);
  lv_obj_add_style(s_label_time, &s_style_time, 0);
  lv_label_set_text(s_label_time, "--:--");
  lv_obj_align(s_label_time, LV_ALIGN_TOP_MID, 0, 30);

  // Date Label (Below Time)
  s_label_date = lv_label_create(s_scr_main);
  lv_obj_add_style(s_label_date, &s_style_cjk, 0);
  lv_label_set_text(s_label_date, "----/--/--");
  lv_obj_align_to(s_label_date, s_label_time, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  // WiFi Icon (Top Right)
  s_label_wifi = lv_label_create(s_scr_main);
  lv_obj_add_style(s_label_wifi, &s_style_normal, 0);
  lv_label_set_text(s_label_wifi, LV_SYMBOL_WIFI);
  lv_obj_align(s_label_wifi, LV_ALIGN_TOP_RIGHT, -10, 10);
  lv_obj_add_flag(s_label_wifi, LV_OBJ_FLAG_HIDDEN); // Hidden by default

  // Weather Container (Bottom)
  lv_obj_t *weather_cont = lv_obj_create(s_scr_main);
  lv_obj_remove_style_all(weather_cont);
  lv_obj_set_size(weather_cont, 240, LV_SIZE_CONTENT);
  lv_obj_align(weather_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_flex_flow(weather_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(weather_cont, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(weather_cont, 5, 0);

  lv_obj_t *temp_desc_cont = lv_obj_create(weather_cont);
  lv_obj_remove_style_all(temp_desc_cont);
  lv_obj_set_size(temp_desc_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(temp_desc_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(temp_desc_cont, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(temp_desc_cont, 15, 0);

  // Weather Temp
  s_label_weather_temp = lv_label_create(temp_desc_cont);
  lv_obj_add_style(s_label_weather_temp, &s_style_cjk_36, 0);
  lv_label_set_text(s_label_weather_temp, "--°");

  // Weather Desc (Chinese)
  s_label_weather_desc = lv_label_create(temp_desc_cont);
  lv_obj_add_style(s_label_weather_desc, &s_style_cjk_36, 0);
  lv_label_set_text(s_label_weather_desc, "查询中");

  // Humidity
  s_label_weather_humidity = lv_label_create(weather_cont);
  lv_obj_add_style(s_label_weather_humidity, &s_style_cjk, 0);
  lv_label_set_text(s_label_weather_humidity, "");
}

static void create_prov_screen(void) {
  s_scr_prov = lv_obj_create(NULL);
  lv_obj_add_style(s_scr_prov, &s_style_bg, 0);

  lv_obj_t *icon = lv_label_create(s_scr_prov);
  lv_obj_add_style(icon, &s_style_time, 0);
  lv_label_set_text(icon, LV_SYMBOL_WIFI);
  lv_obj_align(icon, LV_ALIGN_CENTER, 0, -30);

  lv_obj_t *label = lv_label_create(s_scr_prov);
  lv_obj_add_style(label, &s_style_cjk, 0);
  lv_label_set_text(label,
                    "请连接热点\nESP32_Weather\n访问 192.168.4.1\n长按8秒重置");
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, 120);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 40);
}

void app_ui_init(void) {
  ESP_LOGI(TAG, "Initializing UI...");
  app_hal_lvgl_lock();

  create_styles();
  create_main_screen();
  create_prov_screen();

  lv_scr_load(s_scr_main);

  app_hal_lvgl_unlock();
}

void app_ui_update_time(const time_info_t *time_info) {
  app_hal_lvgl_lock();
  if (time_info && s_label_time && s_label_date) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", time_info->hour, time_info->minute);
    lv_label_set_text(s_label_time, buf);

    const char *weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
    snprintf(buf, sizeof(buf), "%02d/%02d 星期%s", time_info->month,
             time_info->day, weekdays[time_info->dow % 7]);
    lv_label_set_text(s_label_date, buf);
  }
  app_hal_lvgl_unlock();
}

void app_ui_update_weather(const weather_info_t *weather_info) {
  app_hal_lvgl_lock();
  if (weather_info && weather_info->is_valid) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d°", weather_info->temp);
    lv_label_set_text(s_label_weather_temp, buf);
    lv_label_set_text(s_label_weather_desc, weather_info->description);

    snprintf(buf, sizeof(buf), "湿度%d%%", weather_info->humidity);
    lv_label_set_text(s_label_weather_humidity, buf);
  } else {
    lv_label_set_text(s_label_weather_temp, "--°");
    lv_label_set_text(s_label_weather_desc, "离线或过期");
    lv_label_set_text(s_label_weather_humidity, "");
  }
  app_hal_lvgl_unlock();
}

void app_ui_update_net_state(bool is_connected) {
  app_hal_lvgl_lock();
  if (s_label_wifi) {
    if (is_connected) {
      lv_obj_clear_flag(s_label_wifi, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_label_wifi, LV_OBJ_FLAG_HIDDEN);
    }
  }
  app_hal_lvgl_unlock();
}

void app_ui_show_provisioning(void) {
  app_hal_lvgl_lock();
  if (lv_scr_act() != s_scr_prov) {
    lv_scr_load_anim(s_scr_prov, LV_SCR_LOAD_ANIM_FADE_ON, 500, 0, false);
  }
  app_hal_lvgl_unlock();
}
