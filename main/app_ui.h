#pragma once

#include "weather_data.h"

void app_ui_init(void);
void app_ui_update_time(const time_info_t *time_info);
void app_ui_update_weather(const weather_info_t *weather_info);
void app_ui_update_net_state(bool is_connected);
void app_ui_show_provisioning(void);
