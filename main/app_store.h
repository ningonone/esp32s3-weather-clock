#pragma once

#include "weather_data.h"
#include <stdbool.h>

typedef struct {
  char ssid[32];
  char password[64];
  char location[32];
  bool is_fahrenheit;
} app_config_t;

void app_store_init(void);

bool app_store_save_config(const app_config_t *cfg);
bool app_store_load_config(app_config_t *cfg);

bool app_store_save_weather(const weather_info_t *info);
bool app_store_load_weather(weather_info_t *info);

void app_store_factory_reset(void);
