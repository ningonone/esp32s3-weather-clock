#include "app_store.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "app_store";
static const char *NVS_NAMESPACE = "weather_cfg";

void app_store_init(void) {
  ESP_LOGI(TAG, "Initializing NVS Store...");
  // NVS init is already called in main.c
}

bool app_store_save_config(const app_config_t *cfg) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
    return false;
  esp_err_t err = nvs_set_blob(h, "app_cfg", cfg, sizeof(app_config_t));
  if (err == ESP_OK)
    nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

bool app_store_load_config(app_config_t *cfg) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
    return false;
  size_t len = sizeof(app_config_t);
  esp_err_t err = nvs_get_blob(h, "app_cfg", cfg, &len);
  nvs_close(h);
  return err == ESP_OK && len == sizeof(app_config_t);
}

bool app_store_save_weather(const weather_info_t *info) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
    return false;
  esp_err_t err = nvs_set_blob(h, "weather", info, sizeof(weather_info_t));
  if (err == ESP_OK)
    nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

bool app_store_load_weather(weather_info_t *info) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK)
    return false;
  size_t len = sizeof(weather_info_t);
  esp_err_t err = nvs_get_blob(h, "weather", info, &len);
  nvs_close(h);
  return err == ESP_OK && len == sizeof(weather_info_t);
}

void app_store_factory_reset(void) {
  nvs_handle_t h;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "Factory Reset completed");
  }
}
