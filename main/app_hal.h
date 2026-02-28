#pragma once

#include "lvgl.h"

void app_hal_init(void);

// Provide locked access to LVGL tick/timer handling if needed
void app_hal_lvgl_lock(void);
void app_hal_lvgl_unlock(void);

void app_hal_set_backlight(int percent); // 0-100
