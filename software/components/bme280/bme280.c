#include "bme280.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "bme280";

#define BME280_REG_CHIP_ID      0xD0
#define BME280_REG_RESET        0xE0
#define BME280_REG_CTRL_HUM     0xF2
#define BME280_REG_STATUS       0xF3
#define BME280_REG_CTRL_MEAS    0xF4
#define BME280_REG_CONFIG       0xF5
#define BME280_REG_DATA         0xF7

#define BME280_REG_CAL_T_P      0x88
#define BME280_REG_CAL_H1       0xA1
#define BME280_REG_CAL_H2       0xE1

#define BME280_CHIP_ID          0x60
#define BME280_RESET_CMD        0xB6

#define BME280_STATUS_MEASURING  0x08
#define BME280_MEAS_TIMEOUT_MS   100
#define BME280_RESET_DELAY_MS    10

/* osrs x1 for T/P, forced mode; mode bits cleared = sleep */
#define BME280_CTRL_MEAS_FORCED  0x25
#define BME280_CTRL_MEAS_SLEEP   0x24
#define BME280_CTRL_HUM_X1       0x01

struct bme280 {
    i2c_master_dev_handle_t dev;
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
    int32_t t_fine;
};

static uint16_t get_u16_le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static int16_t get_s16_le(const uint8_t *p)
{
    return (int16_t)get_u16_le(p);
}

static esp_err_t bme280_read_regs(struct bme280 *sensor, uint8_t start_reg,
                                  uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(sensor->dev, &start_reg, 1, data, len,
                                       CONFIG_METEO_I2C_TIMEOUT_MS);
}

static esp_err_t bme280_write_reg(struct bme280 *sensor, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(sensor->dev, buf, sizeof(buf),
                               CONFIG_METEO_I2C_TIMEOUT_MS);
}

static esp_err_t bme280_read_calibration(struct bme280 *sensor)
{
    uint8_t cal_tp[26];
    esp_err_t err = bme280_read_regs(sensor, BME280_REG_CAL_T_P, cal_tp, sizeof(cal_tp));
    if (err != ESP_OK) {
        return err;
    }
    sensor->dig_T1 = get_u16_le(&cal_tp[0]);
    sensor->dig_T2 = get_s16_le(&cal_tp[2]);
    sensor->dig_T3 = get_s16_le(&cal_tp[4]);
    sensor->dig_P1 = get_u16_le(&cal_tp[6]);
    sensor->dig_P2 = get_s16_le(&cal_tp[8]);
    sensor->dig_P3 = get_s16_le(&cal_tp[10]);
    sensor->dig_P4 = get_s16_le(&cal_tp[12]);
    sensor->dig_P5 = get_s16_le(&cal_tp[14]);
    sensor->dig_P6 = get_s16_le(&cal_tp[16]);
    sensor->dig_P7 = get_s16_le(&cal_tp[18]);
    sensor->dig_P8 = get_s16_le(&cal_tp[20]);
    sensor->dig_P9 = get_s16_le(&cal_tp[22]);
    sensor->dig_H1 = cal_tp[25];

    uint8_t cal_h[7];
    err = bme280_read_regs(sensor, BME280_REG_CAL_H2, cal_h, sizeof(cal_h));
    if (err != ESP_OK) {
        return err;
    }
    sensor->dig_H2 = get_s16_le(&cal_h[0]);
    sensor->dig_H3 = cal_h[2];
    sensor->dig_H4 = (int16_t)((cal_h[3] << 4) | (cal_h[4] & 0x0F));
    sensor->dig_H5 = (int16_t)((cal_h[5] << 4) | (cal_h[4] >> 4));
    sensor->dig_H6 = (int8_t)cal_h[6];
    return ESP_OK;
}

/* Bosch integer compensation: temperature 0.01 C, updates t_fine. */
static float bme280_compensate_temperature(struct bme280 *sensor, int32_t adc_T)
{
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)sensor->dig_T1 << 1))) *
                    ((int32_t)sensor->dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)sensor->dig_T1)) *
                      ((adc_T >> 4) - ((int32_t)sensor->dig_T1))) >> 12) *
                    ((int32_t)sensor->dig_T3)) >> 14;
    sensor->t_fine = var1 + var2;
    return (float)((sensor->t_fine * 5 + 128) >> 8) / 100.0f;
}

/* Bosch 64-bit compensation, returns Q24.8 Pascal. */
static float bme280_compensate_pressure(struct bme280 *sensor, int32_t adc_P)
{
    int64_t var1 = (int64_t)sensor->t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)sensor->dig_P6;
    var2 += (var1 * (int64_t)sensor->dig_P5) << 17;
    var2 += ((int64_t)sensor->dig_P4) << 35;
    var1 = ((var1 * var1 * (int64_t)sensor->dig_P3) >> 8) +
           ((var1 * (int64_t)sensor->dig_P2) << 12);
    var1 = ((((int64_t)1 << 47) + var1) * (int64_t)sensor->dig_P1) >> 33;
    if (var1 == 0) {
        return 0.0f;
    }
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)sensor->dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)sensor->dig_P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)sensor->dig_P7 << 4);
    return (float)p / 256.0f;
}

/* Bosch 32-bit compensation, returns Q22.10 %RH. */
static float bme280_compensate_humidity(struct bme280 *sensor, int32_t adc_H)
{
    int32_t v = sensor->t_fine - 76800;
    v = (((((adc_H << 14) - ((int32_t)sensor->dig_H4 << 20) -
            ((int32_t)sensor->dig_H5 * v)) + 16384) >> 15) *
         (((((((v * (int32_t)sensor->dig_H6) >> 10) *
              (((v * (int32_t)sensor->dig_H3) >> 11) + 32768)) >> 10) + 2097152) *
           (int32_t)sensor->dig_H2 + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)sensor->dig_H1) >> 4);
    if (v < 0) {
        v = 0;
    }
    if (v > 419430400) {
        v = 419430400;
    }
    return (float)(v >> 12) / 1024.0f;
}

esp_err_t bme280_create(i2c_master_bus_handle_t bus,
                        const bme280_config_t *config,
                        bme280_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(bus != NULL && config != NULL && out_handle != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    struct bme280 *sensor = calloc(1, sizeof(*sensor));
    ESP_RETURN_ON_FALSE(sensor != NULL, ESP_ERR_NO_MEM, TAG, "out of memory");

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_address,
        .scl_speed_hz = CONFIG_METEO_I2C_FREQUENCY_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &sensor->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        free(sensor);
        return err;
    }

    uint8_t chip_id = 0;
    err = bme280_read_regs(sensor, BME280_REG_CHIP_ID, &chip_id, 1);
    if (err != ESP_OK || chip_id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "chip ID mismatch at 0x%02X: got 0x%02X (%s)",
                 config->i2c_address, chip_id,
                 err == ESP_OK ? "wrong chip" : esp_err_to_name(err));
        err = ESP_ERR_NOT_FOUND;
        goto fail;
    }

    err = bme280_write_reg(sensor, BME280_REG_RESET, BME280_RESET_CMD);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "soft reset failed: %s", esp_err_to_name(err));
        goto fail;
    }
    vTaskDelay(pdMS_TO_TICKS(BME280_RESET_DELAY_MS));

    err = bme280_read_calibration(sensor);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "calibration read failed: %s", esp_err_to_name(err));
        goto fail;
    }

    err = bme280_write_reg(sensor, BME280_REG_CTRL_HUM, BME280_CTRL_HUM_X1);
    if (err == ESP_OK) {
        err = bme280_write_reg(sensor, BME280_REG_CONFIG, 0x00);
    }
    if (err == ESP_OK) {
        err = bme280_write_reg(sensor, BME280_REG_CTRL_MEAS, BME280_CTRL_MEAS_SLEEP);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "initial config failed: %s", esp_err_to_name(err));
        goto fail;
    }

    *out_handle = sensor;
    ESP_LOGI(TAG, "BME280 at 0x%02X ready", config->i2c_address);
    return ESP_OK;

fail:
    i2c_master_bus_rm_device(sensor->dev);
    free(sensor);
    return err;
}

esp_err_t bme280_read_forced(bme280_handle_t handle,
                             environment_measurement_t *measurement)
{
    ESP_RETURN_ON_FALSE(handle != NULL && measurement != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    esp_err_t err = bme280_write_reg(handle, BME280_REG_CTRL_MEAS, BME280_CTRL_MEAS_FORCED);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "forced trigger failed: %s", esp_err_to_name(err));
        return err;
    }

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(BME280_MEAS_TIMEOUT_MS);
    uint8_t status = 0;
    for (;;) {
        err = bme280_read_regs(handle, BME280_REG_STATUS, &status, 1);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "status read failed: %s", esp_err_to_name(err));
            return err;
        }
        if (!(status & BME280_STATUS_MEASURING)) {
            break;
        }
        if (xTaskGetTickCount() >= deadline) {
            ESP_LOGW(TAG, "measurement timeout");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint8_t raw[8];
    err = bme280_read_regs(handle, BME280_REG_DATA, raw, sizeof(raw));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "data read failed: %s", esp_err_to_name(err));
        return err;
    }

    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);
    int32_t adc_H = ((int32_t)raw[6] << 8) | raw[7];

    measurement->temperature_c = bme280_compensate_temperature(handle, adc_T);
    measurement->pressure_pa = bme280_compensate_pressure(handle, adc_P);
    measurement->humidity_percent = bme280_compensate_humidity(handle, adc_H);
    return ESP_OK;
}

esp_err_t bme280_sleep(bme280_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t err = bme280_write_reg(handle, BME280_REG_CTRL_MEAS, BME280_CTRL_MEAS_SLEEP);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sleep write failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t bme280_destroy(bme280_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t err = i2c_master_bus_rm_device(handle->dev);
    free(handle);
    return err;
}
