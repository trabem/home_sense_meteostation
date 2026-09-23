#include "app_storage.h"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "STORAGE";

#define STORAGE_NAMESPACE  "meteo"
#define KEY_BOOT_COUNT     "boot_cnt"

esp_err_t app_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs partition issue (%s), erasing", esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase failed");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init failed");
    return ESP_OK;
}

esp_err_t app_storage_next_boot_count(uint32_t *boot_count)
{
    ESP_RETURN_ON_FALSE(boot_count != NULL, ESP_ERR_INVALID_ARG, TAG, "out param required");

    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs), TAG, "nvs open failed");

    uint32_t count = 0;
    esp_err_t err = nvs_get_u32(nvs, KEY_BOOT_COUNT, &count);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs);
        return err;
    }

    count++;
    err = nvs_set_u32(nvs, KEY_BOOT_COUNT, count);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    ESP_RETURN_ON_ERROR(err, TAG, "boot count store failed");
    *boot_count = count;
    return ESP_OK;
}
