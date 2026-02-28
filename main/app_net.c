#include "app_net.h"
#include "app_store.h"
#include "app_ui.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include <string.h>

static const char *TAG = "app_net";
static bool s_is_connected = false;
static httpd_handle_t s_server = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    s_is_connected = false;
    app_ui_update_net_state(false);
    ESP_LOGI(TAG, "Disconnected. Reconnecting...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    s_is_connected = true;
    app_ui_update_net_state(true);
  }
}

static esp_err_t index_get_handler(httpd_req_t *req) {
  const char *html =
      "<!DOCTYPE html><html><body>"
      "<h2>WiFi Config</h2>"
      "<form action=\"/save\" method=\"post\">"
      "SSID:<br><input type=\"text\" name=\"ssid\"><br>"
      "Password:<br><input type=\"password\" name=\"password\"><br>"
      "City Name or Location ID:<br><input type=\"text\" name=\"location\"><br>"
      "Unit (C or F):<br><input type=\"text\" name=\"unit\" "
      "value=\"C\"><br><br>"
      "<input type=\"submit\" value=\"Save and Restart\">"
      "</form></body></html>";
  httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t save_post_handler(httpd_req_t *req) {
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0)
    return ESP_FAIL;
  buf[ret] = '\0';

  app_config_t cfg = {0};

  // Simple naive parsing of application/x-www-form-urlencoded
  // A robust impl would use httpd_query_key_value
  if (httpd_query_key_value(buf, "ssid", cfg.ssid, sizeof(cfg.ssid)) != ESP_OK)
    return ESP_FAIL;
  if (httpd_query_key_value(buf, "password", cfg.password,
                            sizeof(cfg.password)) != ESP_OK)
    return ESP_FAIL;
  if (httpd_query_key_value(buf, "location", cfg.location,
                            sizeof(cfg.location)) != ESP_OK)
    return ESP_FAIL;

  char unit_val[4] = {0};
  if (httpd_query_key_value(buf, "unit", unit_val, sizeof(unit_val)) ==
      ESP_OK) {
    cfg.is_fahrenheit = (unit_val[0] == 'F' || unit_val[0] == 'f');
  }

  // Replace '+' with ' ' or decode URL properly in a real product
  app_store_save_config(&cfg);

  const char *html = "Saved. Device will restart.";
  httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);

  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;
}

static const httpd_uri_t index_uri = {.uri = "/",
                                      .method = HTTP_GET,
                                      .handler = index_get_handler,
                                      .user_ctx = NULL};
static const httpd_uri_t save_uri = {.uri = "/save",
                                     .method = HTTP_POST,
                                     .handler = save_post_handler,
                                     .user_ctx = NULL};

void app_net_start_provisioning(void) {
  ESP_LOGI(TAG, "Starting AP Provisioning...");

  esp_wifi_stop();

  app_ui_show_provisioning();

  wifi_config_t ap_config = {
      .ap = {.ssid = "ESP32_Weather",
             .ssid_len = strlen("ESP32_Weather"),
             .channel = 1,
             .password = "",
             .max_connection = 4,
             .authmode = WIFI_AUTH_OPEN},
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  if (httpd_start(&s_server, &config) == ESP_OK) {
    httpd_register_uri_handler(s_server, &index_uri);
    httpd_register_uri_handler(s_server, &save_uri);
  }
}

bool app_net_is_connected(void) { return s_is_connected; }

void app_net_init(void) {
  ESP_LOGI(TAG, "Initializing Network...");

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  app_config_t app_cfg = {0};
  if (app_store_load_config(&app_cfg) && strlen(app_cfg.ssid) > 0) {
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, app_cfg.ssid,
            sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, app_cfg.password,
            sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
  } else {
    // Automatically start AP if no SSID is configured
    app_net_start_provisioning();
  }
}
