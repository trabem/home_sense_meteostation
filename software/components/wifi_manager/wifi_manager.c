#include "wifi_manager.h"

#include <string.h>

#include "app_events.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define TAG "WIFI"

#define WIFI_EVENT_CONNECTED BIT0
#define WIFI_EVENT_FAILED    BIT1

static struct {
    bool initialized;
    esp_netif_t *netif;
    EventGroupHandle_t events;
    int max_retries;
    int retry_count;
} s_wifi;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *event_data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *disc = event_data;
        if (s_wifi.retry_count < s_wifi.max_retries) {
            s_wifi.retry_count++;
            ESP_LOGW(TAG, "disconnected (reason %d), retry %d/%d",
                     disc->reason, s_wifi.retry_count, s_wifi.max_retries);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "disconnected (reason %d), retries exhausted",
                     disc->reason);
            xEventGroupSetBits(s_wifi.events, WIFI_EVENT_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ip = event_data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&ip->ip_info.ip));
        xEventGroupSetBits(s_wifi.events, WIFI_EVENT_CONNECTED);
    }
}

esp_err_t wifi_manager_init(void)
{
    if (s_wifi.initialized) {
        return ESP_OK;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_wifi.events = xEventGroupCreate();
    if (s_wifi.events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_wifi.netif = esp_netif_create_default_wifi_sta();
    if (s_wifi.netif == NULL) {
        vEventGroupDelete(s_wifi.events);
        return ESP_FAIL;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        esp_netif_destroy_default_wifi(s_wifi.netif);
        vEventGroupDelete(s_wifi.events);
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_storage failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                     wifi_event_handler, NULL);
    if (err != ESP_OK) {
        esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                     wifi_event_handler);
        goto fail;
    }

    s_wifi.initialized = true;
    ESP_LOGI(TAG, "initialized");
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(s_wifi.netif);
    vEventGroupDelete(s_wifi.events);
    s_wifi.netif = NULL;
    s_wifi.events = NULL;
    return err;
}

esp_err_t wifi_manager_has_credentials(bool *has_credentials)
{
    if (has_credentials == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_wifi.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t cfg = {0};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_get_config failed: %s", esp_err_to_name(err));
        return err;
    }
    *has_credentials = cfg.sta.ssid[0] != 0;
    return ESP_OK;
}

esp_err_t wifi_manager_connect(uint32_t timeout_ms, int max_retries)
{
    if (!s_wifi.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_wifi.max_retries = max_retries;
    s_wifi.retry_count = 0;
    xEventGroupClearBits(s_wifi.events, WIFI_EVENT_CONNECTED | WIFI_EVENT_FAILED);

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi.events,
                                           WIFI_EVENT_CONNECTED | WIFI_EVENT_FAILED,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & WIFI_EVENT_CONNECTED) {
        esp_event_post(METEO_EVENT, METEO_EVENT_WIFI_CONNECTED, NULL, 0,
                       pdMS_TO_TICKS(100));
        return ESP_OK;
    }

    ESP_LOGE(TAG, "connect %s", (bits & WIFI_EVENT_FAILED)
                 ? "failed (retries exhausted)" : "timed out");
    esp_event_post(METEO_EVENT, METEO_EVENT_WIFI_FAILED, NULL, 0,
                   pdMS_TO_TICKS(100));
    return ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_stop(void)
{
    if (!s_wifi.initialized) {
        return ESP_OK;
    }

    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                 wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                 wifi_event_handler);

    esp_wifi_disconnect();

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_stop: %s", esp_err_to_name(err));
    }
    err = esp_wifi_deinit();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_deinit: %s", esp_err_to_name(err));
    }

    esp_netif_destroy_default_wifi(s_wifi.netif);
    s_wifi.netif = NULL;

    vEventGroupDelete(s_wifi.events);
    s_wifi.events = NULL;

    s_wifi.initialized = false;
    ESP_LOGI(TAG, "stopped");
    return ESP_OK;
}
