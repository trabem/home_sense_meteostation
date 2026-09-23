#include "mqtt_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "app_events.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"

#define TAG "MQTT"

#define MQTT_EVENT_CONNECTED_OK BIT0
#define MQTT_EVENT_CONNECT_FAIL BIT1
#define MQTT_EVENT_PUBLISH_DONE BIT2

#define MQTT_TOPIC_MAX_LEN 128

typedef struct {
    struct transport base;
    mqtt_transport_config_t cfg;
    esp_mqtt_client_handle_t client;
    EventGroupHandle_t events;
    int published_msg_id;
    char topic[MQTT_TOPIC_MAX_LEN];
} mqtt_transport_ctx_t;

static const char *topic_suffix(message_type_t type)
{
    switch (type) {
    case MESSAGE_TYPE_TELEMETRY:
        return "telemetry";
    case MESSAGE_TYPE_LIGHTNING:
        return "events";
    case MESSAGE_TYPE_BOOT:
        return "status";
    default:
        return NULL;
    }
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id,
                               void *event_data)
{
    mqtt_transport_ctx_t *ctx = arg;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to broker");
        xEventGroupSetBits(ctx->events, MQTT_EVENT_CONNECTED_OK);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected from broker");
        xEventGroupSetBits(ctx->events, MQTT_EVENT_CONNECT_FAIL);
        break;
    case MQTT_EVENT_ERROR:
        if (event->error_handle != NULL) {
            ESP_LOGE(TAG, "error type %d, esp-tls err 0x%x",
                     event->error_handle->error_type,
                     event->error_handle->esp_tls_last_esp_err);
        } else {
            ESP_LOGE(TAG, "unknown error");
        }
        xEventGroupSetBits(ctx->events, MQTT_EVENT_CONNECT_FAIL);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "published, msg_id %d", event->msg_id);
        ctx->published_msg_id = event->msg_id;
        xEventGroupSetBits(ctx->events, MQTT_EVENT_PUBLISH_DONE);
        break;
    default:
        break;
    }
}

static esp_err_t mqtt_transport_open(transport_t *self)
{
    mqtt_transport_ctx_t *ctx = (mqtt_transport_ctx_t *)self;
    const mqtt_transport_config_t *cfg = &ctx->cfg;

    if (ctx->client != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (cfg->broker_uri == NULL || cfg->device_id == NULL ||
        cfg->root_topic == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = cfg->broker_uri,
        .credentials.client_id = cfg->device_id,
        .network.reconnect_timeout_ms = 0,
        .network.timeout_ms = (int)cfg->connect_timeout_ms,
        .network.disable_auto_reconnect = true,
    };

    if (cfg->username != NULL && cfg->username[0] != '\0') {
        mqtt_cfg.credentials.username = cfg->username;
    }
    if (cfg->password != NULL && cfg->password[0] != '\0') {
        mqtt_cfg.credentials.authentication.password = cfg->password;
    }

    bool skip_verify = false;
#if CONFIG_METEO_MQTT_TLS_SKIP_VERIFY
    skip_verify = cfg->tls_skip_verify;
#endif
    if (skip_verify) {
        ESP_LOGW(TAG, "!!! TLS certificate validation DISABLED "
                 "(development only, never use in production) !!!");
    } else if (cfg->ca_cert_pem != NULL) {
        mqtt_cfg.broker.verification.certificate = cfg->ca_cert_pem;
    } else {
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
#endif
    }

    ctx->events = xEventGroupCreate();
    if (ctx->events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ctx->published_msg_id = -1;

    ctx->client = esp_mqtt_client_init(&mqtt_cfg);
    if (ctx->client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        vEventGroupDelete(ctx->events);
        ctx->events = NULL;
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(ctx->client, MQTT_EVENT_ANY,
                                                   mqtt_event_handler, ctx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register_event failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = esp_mqtt_client_start(ctx->client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(err));
        goto fail;
    }

    EventBits_t bits = xEventGroupWaitBits(ctx->events,
                                           MQTT_EVENT_CONNECTED_OK |
                                           MQTT_EVENT_CONNECT_FAIL,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(cfg->connect_timeout_ms));
    if (!(bits & MQTT_EVENT_CONNECTED_OK)) {
        ESP_LOGE(TAG, "connect %s", (bits & MQTT_EVENT_CONNECT_FAIL)
                     ? "refused/failed" : "timed out");
        err = ESP_ERR_TIMEOUT;
        goto fail;
    }

    esp_event_post(METEO_EVENT, METEO_EVENT_MQTT_CONNECTED, NULL, 0,
                   pdMS_TO_TICKS(100));
    return ESP_OK;

fail:
    esp_mqtt_client_stop(ctx->client);
    esp_mqtt_client_destroy(ctx->client);
    ctx->client = NULL;
    vEventGroupDelete(ctx->events);
    ctx->events = NULL;
    return err;
}

static esp_err_t mqtt_transport_send(transport_t *self, message_type_t type,
                                     const uint8_t *payload, size_t payload_size)
{
    mqtt_transport_ctx_t *ctx = (mqtt_transport_ctx_t *)self;
    const mqtt_transport_config_t *cfg = &ctx->cfg;

    if (ctx->client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *suffix = topic_suffix(type);
    if (suffix == NULL || payload == NULL || payload_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int len = snprintf(ctx->topic, sizeof(ctx->topic), "%s/%s/%s",
                       cfg->root_topic, cfg->device_id, suffix);
    if (len < 0 || len >= (int)sizeof(ctx->topic)) {
        return ESP_ERR_INVALID_SIZE;
    }

    int qos = cfg->qos;
    if (qos > 0) {
        xEventGroupClearBits(ctx->events, MQTT_EVENT_PUBLISH_DONE);
        ctx->published_msg_id = -1;
    }

    int msg_id = esp_mqtt_client_publish(ctx->client, ctx->topic,
                                         (const char *)payload,
                                         (int)payload_size, qos, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "publish to %s failed", ctx->topic);
        return ESP_FAIL;
    }

    if (qos == 0) {
        return ESP_OK;
    }

    EventBits_t bits = xEventGroupWaitBits(ctx->events,
                                           MQTT_EVENT_PUBLISH_DONE,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(cfg->publish_timeout_ms));
    if (!(bits & MQTT_EVENT_PUBLISH_DONE) ||
        ctx->published_msg_id != msg_id) {
        ESP_LOGE(TAG, "publish ack timed out (msg_id %d)", msg_id);
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static esp_err_t mqtt_transport_close(transport_t *self)
{
    mqtt_transport_ctx_t *ctx = (mqtt_transport_ctx_t *)self;

    if (ctx->client != NULL) {
        esp_mqtt_client_disconnect(ctx->client);
        esp_err_t err = esp_mqtt_client_stop(ctx->client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_mqtt_client_stop: %s", esp_err_to_name(err));
        }
        err = esp_mqtt_client_destroy(ctx->client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_mqtt_client_destroy: %s", esp_err_to_name(err));
        }
        ctx->client = NULL;
    }

    if (ctx->events != NULL) {
        vEventGroupDelete(ctx->events);
        ctx->events = NULL;
    }

    ESP_LOGI(TAG, "client closed");
    return ESP_OK;
}

static const struct transport_ops mqtt_transport_ops = {
    .open = mqtt_transport_open,
    .send = mqtt_transport_send,
    .close = mqtt_transport_close,
};

esp_err_t mqtt_transport_create(const mqtt_transport_config_t *config,
                                transport_t **out_transport)
{
    if (config == NULL || out_transport == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Single allocation, no inner resources: transport_destroy() is a
     * plain free(); the event group is owned by open()/close() and the
     * client must be closed before destroy. */
    mqtt_transport_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ctx->base.ops = &mqtt_transport_ops;
    ctx->cfg = *config;
    ctx->published_msg_id = -1;

    *out_transport = &ctx->base;
    return ESP_OK;
}
