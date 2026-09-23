#include "ds3231.h"

#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "ds3231";

#define DS3231_REG_SECONDS   0x00
#define DS3231_REG_COUNT     7

#define DS3231_CENTURY_BIT   0x80
#define DS3231_HOUR_12H_BIT  0x40

struct ds3231 {
    i2c_master_dev_handle_t dev;
};

static uint8_t to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static uint8_t from_bcd(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
}

/* Howard Hinnant's civil calendar algorithms, days relative to 1970-01-01. */
static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy = (153u * (month + (month > 2 ? (unsigned)-3 : 9u)) + 2u) / 5u + day - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *year, unsigned *month, unsigned *day)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *day = doy - (153 * mp + 2) / 5 + 1;
    *month = mp + (mp < 10 ? 3 : (unsigned)-9);
    *year = y + (*month <= 2);
}

static esp_err_t ds3231_read_regs(struct ds3231 *rtc, uint8_t start_reg,
                                  uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(rtc->dev, &start_reg, 1, data, len,
                                       CONFIG_METEO_I2C_TIMEOUT_MS);
}

static esp_err_t ds3231_write_regs(struct ds3231 *rtc, uint8_t start_reg,
                                   const uint8_t *data, size_t len)
{
    uint8_t buf[1 + DS3231_REG_COUNT];
    if (len > DS3231_REG_COUNT) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = start_reg;
    memcpy(&buf[1], data, len);
    return i2c_master_transmit(rtc->dev, buf, 1 + len, CONFIG_METEO_I2C_TIMEOUT_MS);
}

esp_err_t ds3231_create(i2c_master_bus_handle_t bus,
                        const ds3231_config_t *config,
                        ds3231_handle_t *out_handle)
{
    ESP_RETURN_ON_FALSE(bus != NULL && config != NULL && out_handle != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    struct ds3231 *rtc = calloc(1, sizeof(*rtc));
    ESP_RETURN_ON_FALSE(rtc != NULL, ESP_ERR_NO_MEM, TAG, "out of memory");

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->i2c_address,
        .scl_speed_hz = CONFIG_METEO_I2C_FREQUENCY_HZ,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &rtc->dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        free(rtc);
        return err;
    }

    *out_handle = rtc;
    ESP_LOGI(TAG, "DS3231 at 0x%02X ready", config->i2c_address);
    return ESP_OK;
}

esp_err_t ds3231_get_time(ds3231_handle_t handle, struct tm *time)
{
    ESP_RETURN_ON_FALSE(handle != NULL && time != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    uint8_t regs[DS3231_REG_COUNT];
    esp_err_t err = ds3231_read_regs(handle, DS3231_REG_SECONDS, regs, sizeof(regs));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "time read failed: %s", esp_err_to_name(err));
        return err;
    }

    if (regs[2] & DS3231_HOUR_12H_BIT) {
        ESP_LOGE(TAG, "RTC is in 12h mode, refusing to decode");
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t sec = from_bcd(regs[0] & 0x7F);
    uint8_t min = from_bcd(regs[1] & 0x7F);
    uint8_t hour = from_bcd(regs[2] & 0x3F);
    uint8_t date = from_bcd(regs[4] & 0x3F);
    uint8_t month = from_bcd(regs[5] & 0x1F);
    int year = 2000 + ((regs[5] & DS3231_CENTURY_BIT) ? 100 : 0) + from_bcd(regs[6]);

    if (month < 1 || month > 12 || date < 1 || date > 31 || hour > 23 || min > 59 || sec > 59) {
        ESP_LOGE(TAG, "RTC holds invalid time %04d-%02u-%02u %02u:%02u:%02u",
                 year, month, date, hour, min, sec);
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(time, 0, sizeof(*time));
    time->tm_sec = sec;
    time->tm_min = min;
    time->tm_hour = hour;
    time->tm_mday = date;
    time->tm_mon = month - 1;
    time->tm_year = year - 1900;
    return ESP_OK;
}

esp_err_t ds3231_set_time(ds3231_handle_t handle, const struct tm *time)
{
    ESP_RETURN_ON_FALSE(handle != NULL && time != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    int year = time->tm_year + 1900;
    if (year < 2000 || year > 2199 || time->tm_mon < 0 || time->tm_mon > 11 ||
        time->tm_mday < 1 || time->tm_mday > 31) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t regs[DS3231_REG_COUNT];
    regs[0] = to_bcd((uint8_t)time->tm_sec);
    regs[1] = to_bcd((uint8_t)time->tm_min);
    regs[2] = to_bcd((uint8_t)time->tm_hour); /* bit6 clear -> 24h mode */
    regs[3] = time->tm_wday > 0 ? (uint8_t)time->tm_wday : 1;
    regs[4] = to_bcd((uint8_t)time->tm_mday);
    regs[5] = to_bcd((uint8_t)(time->tm_mon + 1));
    if (year >= 2100) {
        regs[5] |= DS3231_CENTURY_BIT;
    }
    regs[6] = to_bcd((uint8_t)(year % 100));

    esp_err_t err = ds3231_write_regs(handle, DS3231_REG_SECONDS, regs, sizeof(regs));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "time write failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t ds3231_get_unix_time(ds3231_handle_t handle, int64_t *timestamp)
{
    ESP_RETURN_ON_FALSE(handle != NULL && timestamp != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    struct tm time;
    esp_err_t err = ds3231_get_time(handle, &time);
    if (err != ESP_OK) {
        return err;
    }

    int64_t days = days_from_civil(time.tm_year + 1900, (unsigned)(time.tm_mon + 1),
                                   (unsigned)time.tm_mday);
    *timestamp = days * 86400 + time.tm_hour * 3600 + time.tm_min * 60 + time.tm_sec;
    return ESP_OK;
}

esp_err_t ds3231_set_unix_time(ds3231_handle_t handle, int64_t timestamp)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    int64_t days = timestamp / 86400;
    int64_t secs = timestamp % 86400;
    if (secs < 0) {
        secs += 86400;
        days -= 1;
    }

    struct tm time;
    memset(&time, 0, sizeof(time));
    unsigned month, day;
    int year;
    civil_from_days(days, &year, &month, &day);
    time.tm_year = year - 1900;
    time.tm_mon = (int)month - 1;
    time.tm_mday = (int)day;
    time.tm_hour = (int)(secs / 3600);
    time.tm_min = (int)((secs % 3600) / 60);
    time.tm_sec = (int)(secs % 60);

    return ds3231_set_time(handle, &time);
}

esp_err_t ds3231_destroy(ds3231_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    esp_err_t err = i2c_master_bus_rm_device(handle->dev);
    free(handle);
    return err;
}
