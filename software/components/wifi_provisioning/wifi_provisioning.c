#include "wifi_provisioning.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"

#define TAG "PROV"

#define PROV_EVENT_SUCCESS BIT0
#define PROV_EVENT_FAILED  BIT1
#define PROV_EVENT_END     BIT2

#define PROV_END_WAIT_MS 10000

static EventGroupHandle_t s_prov_events;

static void prov_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *event_data)
{
    if (base == NETWORK_PROV_EVENT) {
        switch (id) {
        case NETWORK_PROV_WIFI_CRED_RECV: {
            const wifi_sta_config_t *cfg = event_data;
            ESP_LOGI(TAG, "credentials received, SSID \"%.32s\"",
                     (const char *)cfg->ssid);
            break;
        }
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "provisioning successful");
            xEventGroupSetBits(s_prov_events, PROV_EVENT_SUCCESS);
            break;
        case NETWORK_PROV_WIFI_CRED_FAIL:
            ESP_LOGE(TAG, "provisioning failed (bad credentials)");
            xEventGroupSetBits(s_prov_events, PROV_EVENT_FAILED);
            break;
        case NETWORK_PROV_END:
            xEventGroupSetBits(s_prov_events, PROV_EVENT_END);
            break;
        default:
            break;
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGD(TAG, "station disconnected during provisioning");
    }
}

static esp_err_t prov_register_handlers(void)
{
    esp_err_t err = esp_event_handler_register(NETWORK_PROV_EVENT,
                                               ESP_EVENT_ANY_ID,
                                               prov_event_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }
    return esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      prov_event_handler, NULL);
}

static void prov_unregister_handlers(void)
{
    esp_event_handler_unregister(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID,
                                 prov_event_handler);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                 prov_event_handler);
}

static uint32_t pop_hash(const uint8_t *mac, size_t len)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash = (hash ^ mac[i]) * 16777619u;
    }
    return hash;
}

esp_err_t wifi_provisioning_run(uint32_t timeout_ms)
{
    esp_err_t err;

    s_prov_events = xEventGroupCreate();
    if (s_prov_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = prov_register_handlers();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event handler registration failed: %s",
                 esp_err_to_name(err));
        vEventGroupDelete(s_prov_events);
        s_prov_events = NULL;
        return err;
    }

    network_prov_mgr_config_t config = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        .app_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
    };
    err = network_prov_mgr_init(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "network_prov_mgr_init failed: %s", esp_err_to_name(err));
        goto out;
    }

    bool provisioned = false;
    err = network_prov_mgr_is_wifi_provisioned(&provisioned);
    if (err == ESP_OK && provisioned) {
        ESP_LOGI(TAG, "already provisioned");
        network_prov_mgr_deinit();
        goto out;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "provisioned state unknown: %s", esp_err_to_name(err));
    }

    uint8_t mac[6];
    err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_efuse_mac_get_default failed: %s",
                 esp_err_to_name(err));
        network_prov_mgr_deinit();
        goto out;
    }

    char service_name[13];
    snprintf(service_name, sizeof(service_name), "METEO_%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    char pop[9];
    snprintf(pop, sizeof(pop), "%08lX",
             (unsigned long)pop_hash(mac, sizeof(mac)));

    ESP_LOGI(TAG, "starting BLE provisioning, service \"%s\"", service_name);
    err = network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, pop,
                                              service_name, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start_provisioning failed: %s", esp_err_to_name(err));
        network_prov_mgr_deinit();
        goto out;
    }

    EventBits_t bits = xEventGroupWaitBits(s_prov_events,
                                           PROV_EVENT_SUCCESS | PROV_EVENT_FAILED,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & PROV_EVENT_SUCCESS) {
        network_prov_mgr_stop_provisioning();
        EventBits_t end = xEventGroupWaitBits(s_prov_events, PROV_EVENT_END,
                                              pdTRUE, pdFALSE,
                                              pdMS_TO_TICKS(PROV_END_WAIT_MS));
        if (!(end & PROV_EVENT_END)) {
            ESP_LOGW(TAG, "timed out waiting for provisioning end");
        }
        network_prov_mgr_deinit();
        err = ESP_OK;
    } else if (bits & PROV_EVENT_FAILED) {
        network_prov_mgr_deinit();
        err = ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "provisioning timed out");
        network_prov_mgr_deinit();
        err = ESP_ERR_TIMEOUT;
    }

out:
    prov_unregister_handlers();
    vEventGroupDelete(s_prov_events);
    s_prov_events = NULL;
    return err;
}

esp_err_t wifi_provisioning_reset(void)
{
    esp_err_t err = network_prov_mgr_reset_wifi_provisioning();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reset_wifi_provisioning failed: %s",
                 esp_err_to_name(err));
    }
    return err;
}
