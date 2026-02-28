#include "app_time.h"
#include "app_net.h"
#include "app_ui.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/time.h>
#include <time.h>

static const char *TAG = "app_time";

static void time_sync_notification_cb(struct timeval *tv) {
  ESP_LOGI(TAG, "Notification of a time synchronization event");

  time_t now = 0;
  struct tm timeinfo = {0};
  time(&now);
  localtime_r(&now, &timeinfo);

  time_info_t t_info = {.year = timeinfo.tm_year + 1900,
                        .month = timeinfo.tm_mon + 1,
                        .day = timeinfo.tm_mday,
                        .hour = timeinfo.tm_hour,
                        .minute = timeinfo.tm_min,
                        .second = timeinfo.tm_sec,
                        .dow = timeinfo.tm_wday,
                        .is_synced = true};
  app_ui_update_time(&t_info);
}

static void app_time_task(void *arg) {
  // Wait for network connection
  while (!app_net_is_connected()) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  ESP_LOGI(TAG, "Initializing SNTP");
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, "pool.ntp.org");
  sntp_setservername(1, "time.apple.com");
  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
  sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif
  sntp_init();

  // Set timezone to China Standard Time
  setenv("TZ", "CST-8", 1);
  tzset();

  // Loop to update UI every second
  while (1) {
    time_t now = 0;
    struct tm timeinfo = {0};
    time(&now);
    localtime_r(&now, &timeinfo);

    bool is_synced = (sntp_get_sync_status() != SNTP_SYNC_STATUS_RESET);
    // Only update if year is reasonable (e.g. > 2020)
    if (timeinfo.tm_year > (2020 - 1900)) {
      time_info_t t_info = {.year = timeinfo.tm_year + 1900,
                            .month = timeinfo.tm_mon + 1,
                            .day = timeinfo.tm_mday,
                            .hour = timeinfo.tm_hour,
                            .minute = timeinfo.tm_min,
                            .second = timeinfo.tm_sec,
                            .dow = timeinfo.tm_wday,
                            .is_synced = is_synced};
      app_ui_update_time(&t_info);
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool app_time_is_synced(void) {
  return sntp_get_sync_status() != SNTP_SYNC_STATUS_RESET;
}

void app_time_init(void) {
  xTaskCreate(app_time_task, "app_time", 4096, NULL, 5, NULL);
}
