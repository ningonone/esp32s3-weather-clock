#include "app_hal.h"
#include "app_net.h"
#include "app_store.h"
#include "app_time.h"
#include "app_ui.h"
#include "app_weather.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "main";

void app_main(void) {
  ESP_LOGI(TAG, "Initializing...");

  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Initialize Submodules
  app_store_init();
  app_hal_init(); // Display, Button, PWM, LVGL tick/task
  app_ui_init();  // Create UI screens

  app_net_init();     // WiFi AP or STA
  app_time_init();    // SNTP
  app_weather_init(); // Weather Fetch

  ESP_LOGI(TAG, "Initialization complete. Entering loop...");

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
