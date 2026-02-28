#include "app_weather.h"
#include "app_net.h"
#include "app_store.h"
#include "app_time.h"
#include "app_ui.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "miniz.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "app_weather";

// User should replace this with their QWeather API Key
#define WEATHER_API_KEY "ec368e1ae8524e9cb298bbd1823a65be"

// Buffer for HTTP response
#define MAX_HTTP_RECV_BUFFER 4096

static bool fetch_weather_and_parse(void) {
  app_config_t cfg = {0};
  if (!app_store_load_config(&cfg)) {
    ESP_LOGE(TAG, "Failed to load config, cannot fetch weather");
    return false;
  }

  if (strlen(cfg.location) == 0) {
    ESP_LOGE(TAG, "Location is empty in config");
    return false;
  }

  char url[256];
  snprintf(url, sizeof(url),
           "https://pd2tupjbcu.re.qweatherapi.com/v7/weather/"
           "now?location=%s&key=" WEATHER_API_KEY "&lang=zh",
           cfg.location);

  ESP_LOGI(TAG, "Fetching URL: %s", url);

  esp_http_client_config_t config = {
      .url = url,
      .crt_bundle_attach = esp_crt_bundle_attach,
      .timeout_ms = 10000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);

  // 明确要求服务器不使用 GZIP 压缩，直接返回纯 JSON
  esp_http_client_set_header(client, "Accept-Encoding", "identity");

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  int content_length = esp_http_client_fetch_headers(client);
  ESP_LOGI(TAG, "Content-Length: %d", content_length);

  char *buf = malloc(MAX_HTTP_RECV_BUFFER);
  if (!buf) {
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }

  int total_read = 0;
  int read_len;
  while ((read_len = esp_http_client_read(client, buf + total_read,
                                          MAX_HTTP_RECV_BUFFER - total_read -
                                              1)) > 0) {
    total_read += read_len;
  }
  buf[total_read] = '\0';

  int status = esp_http_client_get_status_code(client);
  ESP_LOGI(TAG, "HTTP status: %d, body(%d bytes)", status, total_read);

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (status != 200 || total_read == 0) {
    free(buf);
    return false;
  }

  // 自动检测 GZIP 魔数（0x1f 0x8b），如果是则解压
  char *json_str = NULL;
  bool json_allocated = false;
  if (total_read > 10 && (uint8_t)buf[0] == 0x1f && (uint8_t)buf[1] == 0x8b) {
    uint8_t flags = (uint8_t)buf[3];
    int header_len = 10;

    // 跳过 FEXTRA
    if (flags & 0x04) {
      if (total_read > header_len + 2) {
        int xlen =
            (uint8_t)buf[header_len] | ((uint8_t)buf[header_len + 1] << 8);
        header_len += 2 + xlen;
      }
    }
    // 跳过 FNAME
    if (flags & 0x08) {
      while (header_len < total_read && buf[header_len] != 0)
        header_len++;
      header_len++; // Skip the null byte
    }
    // 跳过 FCOMMENT
    if (flags & 0x10) {
      while (header_len < total_read && buf[header_len] != 0)
        header_len++;
      header_len++;
    }
    // 跳过 FHCRC
    if (flags & 0x02) {
      header_len += 2;
    }

    if (header_len < total_read) {
      const uint8_t *deflate_data = (const uint8_t *)buf + header_len;
      size_t deflate_len = total_read - header_len;

      // ESP32 miniz doesn't auto discard gzip footer, so subtract 8 bytes for
      // CRC and ISIZE
      if (deflate_len > 8) {
        deflate_len -= 8;
      }

      size_t out_len = 4096;
      json_str = (char *)malloc(out_len);
      if (json_str) {
        tinfl_decompressor *decomp = malloc(sizeof(tinfl_decompressor));
        if (decomp) {
          tinfl_init(decomp);
          size_t inbytes = deflate_len;
          size_t outbytes = out_len - 1;

          int st = tinfl_decompress(decomp, deflate_data, &inbytes,
                                    (uint8_t *)json_str, (uint8_t *)json_str,
                                    &outbytes,
                                    TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
          free(decomp);

          if (st >= 0 || st == -1) {
            json_str[outbytes] = '\0';
            json_allocated = true;
          } else {
            ESP_LOGE(TAG,
                     "tinfl_decompress failed with status: %d (header_len=%d)",
                     st, header_len);
            free(json_str);
            json_str = NULL;
          }
        } else {
          free(json_str);
          json_str = NULL;
        }
      }

      if (!json_allocated) {
        free(buf);
        return false;
      }
    } else {
      ESP_LOGE(TAG, "Invalid GZIP header length");
      free(buf);
      return false;
    }
  } else {
    // 未压缩，直接使用
    json_str = buf;
  }

  // Note: buf/json_str may not be fully null-terminated if it was decompressed
  // or read exactly to its length
  ESP_LOGI(TAG, "JSON: %.*s", 100,
           json_str ? json_str : ""); // 打印前100字符用于调试

  bool parser_success = false;
  cJSON *root = cJSON_Parse(json_str);
  if (root) {
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (code && code->valuestring && strcmp(code->valuestring, "200") == 0) {
      cJSON *now = cJSON_GetObjectItem(root, "now");
      if (now) {
        cJSON *temp = cJSON_GetObjectItem(now, "temp");
        cJSON *feelsLike = cJSON_GetObjectItem(now, "feelsLike");
        cJSON *text = cJSON_GetObjectItem(now, "text");
        cJSON *icon = cJSON_GetObjectItem(now, "icon");
        cJSON *windSpeed = cJSON_GetObjectItem(now, "windSpeed");
        cJSON *humidity = cJSON_GetObjectItem(now, "humidity");

        weather_info_t winfo = {0};
        if (text && text->valuestring)
          strncpy(winfo.description, text->valuestring,
                  sizeof(winfo.description) - 1);
        if (temp && temp->valuestring)
          winfo.temp = atoi(temp->valuestring);
        if (feelsLike && feelsLike->valuestring)
          winfo.feels_like = atoi(feelsLike->valuestring);
        if (windSpeed && windSpeed->valuestring)
          winfo.wind_speed = atoi(windSpeed->valuestring);
        if (humidity && humidity->valuestring)
          winfo.humidity = atoi(humidity->valuestring);
        if (icon && icon->valuestring)
          strncpy(winfo.icon, icon->valuestring, sizeof(winfo.icon) - 1);

        winfo.is_valid = true;

        if (cfg.is_fahrenheit) {
          winfo.temp = (winfo.temp * 9 / 5) + 32;
          winfo.feels_like = (winfo.feels_like * 9 / 5) + 32;
        }

        app_store_save_weather(&winfo);
        app_ui_update_weather(&winfo);
        parser_success = true;
      }
    } else {
      ESP_LOGE(TAG, "API error code: %s", code ? code->valuestring : "null");
    }
    cJSON_Delete(root);
  } else {
    ESP_LOGE(TAG, "Failed to parse JSON");
  }

  if (json_allocated)
    free(json_str);
  free(buf);
  return parser_success;
}

static void weather_task(void *arg) {
  // Wait for the time to be synchronized before making HTTPS requests
  // Otherwise, MBEDTLS will fail the certificate validation due to the time
  // being 1970
  while (!app_time_is_synced()) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  // Attempt to load cached weather at startup
  weather_info_t cached = {0};
  if (app_store_load_weather(&cached) && cached.is_valid) {
    cached.is_valid = false; // Mark as cached/offline for UI visually if needed
    app_ui_update_weather(&cached);
  }

  int retry_delay_min = 1;
  while (1) {
    if (app_net_is_connected()) {
      if (fetch_weather_and_parse()) {
        // Success, wait 15 minutes before next update
        ESP_LOGI(TAG, "Weather fetched successfully. Sleep 15m.");
        retry_delay_min = 1; // reset delay
        vTaskDelay(pdMS_TO_TICKS(15 * 60 * 1000));
        continue;
      } else {
        ESP_LOGW(TAG, "Weather fetch failed. Retry in %d min.",
                 retry_delay_min);
        vTaskDelay(pdMS_TO_TICKS(retry_delay_min * 60 * 1000));
        retry_delay_min *= 2; // exponential backoff
        if (retry_delay_min > 15)
          retry_delay_min = 15;
      }
    } else {
      // Not connected, sleep briefly and check again
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }
}

void app_weather_init(void) {
  xTaskCreate(weather_task, "app_weather", 40960, NULL, 4, NULL);
}

void app_weather_update(void) {
  // If we wanted to force update immediately, we could use a FreeRTOS event
  // group or task notify. For simplicity, we just let the polling loop handle
  // it.
}
