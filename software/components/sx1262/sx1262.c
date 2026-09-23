#include "sx1262.h"

#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TAG "sx1262"

#define SX1262_OP_SET_SLEEP             0x84
#define SX1262_OP_SET_STANDBY           0x80
#define SX1262_OP_SET_TX                0x83
#define SX1262_OP_SET_PA_CONFIG         0x95
#define SX1262_OP_SET_TX_PARAMS         0x8E
#define SX1262_OP_SET_BUFFER_BASE_ADDR  0x8F
#define SX1262_OP_SET_MODULATION_PARAMS 0x8B
#define SX1262_OP_SET_PACKET_PARAMS     0x8C
#define SX1262_OP_SET_DIO_IRQ_PARAMS    0x08
#define SX1262_OP_SET_RF_FREQUENCY      0x86
#define SX1262_OP_SET_PACKET_TYPE       0x8A
#define SX1262_OP_SET_DIO2_RF_SWITCH    0x9D
#define SX1262_OP_WRITE_BUFFER          0x0E
#define SX1262_OP_CLEAR_IRQ_STATUS      0x02
#define SX1262_OP_GET_IRQ_STATUS        0x12

#define SX1262_STDBY_RC        0x00
#define SX1262_SLEEP_WARM_RTC  0x04 /* warm start, RTC timeout disabled */
#define SX1262_PACKET_LORA     0x01

#define SX1262_IRQ_TX_DONE     0x0001
#define SX1262_IRQ_RX_TX_TIMEOUT 0x0200

#define SX1262_MAX_PAYLOAD     255
#define SX1262_TX_SCRATCH_SIZE (2 + SX1262_MAX_PAYLOAD)
#define SX1262_RX_SCRATCH_SIZE 18

#define SX1262_SPI_CLOCK_HZ    (8 * 1000 * 1000)
#define SX1262_XTAL_HZ         32000000ULL

struct sx1262 {
    sx1262_config_t config;
    spi_device_handle_t spi;
    SemaphoreHandle_t irq_sem;
    uint8_t *tx_scratch;
    uint8_t *rx_scratch;
    bool owns_bus;
    bool initialized;
    bool asleep;
};

static bool s_isr_service_installed;

static esp_err_t sx1262_wait_busy(sx1262_handle_t handle)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)handle->config.busy_timeout_ms * 1000;
    while (gpio_get_level(handle->config.busy_gpio) == 1) {
        if (esp_timer_get_time() >= deadline) {
            ESP_LOGE(TAG, "BUSY line timeout");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
    return ESP_OK;
}

static esp_err_t sx1262_write_command(sx1262_handle_t handle, uint8_t opcode,
                                      const uint8_t *params, size_t len)
{
    if (len + 1 > SX1262_TX_SCRATCH_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = sx1262_wait_busy(handle);
    if (err != ESP_OK) {
        return err;
    }
    handle->tx_scratch[0] = opcode;
    if (params != NULL && len > 0) {
        memcpy(handle->tx_scratch + 1, params, len);
    }
    spi_transaction_t trans = {
        .length = (len + 1) * 8,
        .tx_buffer = handle->tx_scratch,
        .rx_buffer = NULL,
    };
    return spi_device_transmit(handle->spi, &trans);
}

static esp_err_t sx1262_read_command(sx1262_handle_t handle, uint8_t opcode,
                                     uint8_t *out, size_t len)
{
    if (len + 2 > SX1262_RX_SCRATCH_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = sx1262_wait_busy(handle);
    if (err != ESP_OK) {
        return err;
    }
    memset(handle->tx_scratch, 0, len + 2);
    handle->tx_scratch[0] = opcode;
    spi_transaction_t trans = {
        .length = (len + 2) * 8,
        .tx_buffer = handle->tx_scratch,
        .rx_buffer = handle->rx_scratch,
    };
    err = spi_device_transmit(handle->spi, &trans);
    if (err != ESP_OK) {
        return err;
    }
    memcpy(out, handle->rx_scratch + 2, len);
    return ESP_OK;
}

static esp_err_t sx1262_write_buffer(sx1262_handle_t handle, uint8_t offset,
                                     const uint8_t *data, size_t len)
{
    esp_err_t err = sx1262_wait_busy(handle);
    if (err != ESP_OK) {
        return err;
    }
    handle->tx_scratch[0] = SX1262_OP_WRITE_BUFFER;
    handle->tx_scratch[1] = offset;
    memcpy(handle->tx_scratch + 2, data, len);
    spi_transaction_t trans = {
        .length = (len + 2) * 8,
        .tx_buffer = handle->tx_scratch,
        .rx_buffer = NULL,
    };
    return spi_device_transmit(handle->spi, &trans);
}

static esp_err_t sx1262_hw_reset(sx1262_handle_t handle)
{
    gpio_set_level(handle->config.nrst_gpio, 0);
    esp_rom_delay_us(1000);
    gpio_set_level(handle->config.nrst_gpio, 1);
    esp_rom_delay_us(5000);
    return sx1262_wait_busy(handle);
}

static esp_err_t sx1262_wake(sx1262_handle_t handle)
{
    handle->tx_scratch[0] = 0x00; /* NOP; the NSS falling edge wakes the chip */
    spi_transaction_t trans = {
        .length = 8,
        .tx_buffer = handle->tx_scratch,
        .rx_buffer = NULL,
    };
    esp_err_t err = spi_device_transmit(handle->spi, &trans);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(1);
    err = sx1262_wait_busy(handle);
    if (err != ESP_OK) {
        return err;
    }
    handle->asleep = false;
    return ESP_OK;
}

static esp_err_t sx1262_set_standby(sx1262_handle_t handle)
{
    uint8_t mode = SX1262_STDBY_RC;
    return sx1262_write_command(handle, SX1262_OP_SET_STANDBY, &mode, 1);
}

static esp_err_t sx1262_clear_irq(sx1262_handle_t handle, uint16_t mask)
{
    uint8_t params[2] = {(uint8_t)(mask >> 8), (uint8_t)(mask & 0xFF)};
    return sx1262_write_command(handle, SX1262_OP_CLEAR_IRQ_STATUS, params, sizeof(params));
}

static esp_err_t sx1262_get_irq(sx1262_handle_t handle, uint16_t *out_mask)
{
    uint8_t data[2];
    esp_err_t err = sx1262_read_command(handle, SX1262_OP_GET_IRQ_STATUS, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }
    *out_mask = ((uint16_t)data[0] << 8) | data[1];
    return ESP_OK;
}

static esp_err_t sx1262_set_packet_params(sx1262_handle_t handle, uint8_t payload_length)
{
    uint8_t params[9] = {
        0x00, 0x08,     /* preamble length 8 */
        0x00,           /* explicit header */
        payload_length,
        0x01,           /* CRC on */
        0x00,           /* standard IQ */
        0x00, 0x00, 0x00,
    };
    return sx1262_write_command(handle, SX1262_OP_SET_PACKET_PARAMS, params, sizeof(params));
}

static void IRAM_ATTR sx1262_dio1_isr(void *arg)
{
    sx1262_handle_t handle = (sx1262_handle_t)arg;
    BaseType_t higher_priority_woken = pdFALSE;
    xSemaphoreGiveFromISR(handle->irq_sem, &higher_priority_woken);
    portYIELD_FROM_ISR(higher_priority_woken);
}

esp_err_t sx1262_create(const sx1262_config_t *config, sx1262_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (config->spreading_factor < 5 || config->spreading_factor > 12) {
        ESP_LOGE(TAG, "invalid spreading factor %u", config->spreading_factor);
        return ESP_ERR_INVALID_ARG;
    }
    if (config->coding_rate < 1 || config->coding_rate > 4) {
        ESP_LOGE(TAG, "invalid coding rate %u", config->coding_rate);
        return ESP_ERR_INVALID_ARG;
    }
    if (config->tx_timeout_ms == 0 || config->busy_timeout_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    struct sx1262 *handle = calloc(1, sizeof(*handle));
    if (handle == NULL) {
        return ESP_ERR_NO_MEM;
    }
    handle->config = *config;
    esp_err_t err;

    handle->tx_scratch = malloc(SX1262_TX_SCRATCH_SIZE);
    handle->rx_scratch = malloc(SX1262_RX_SCRATCH_SIZE);
    handle->irq_sem = xSemaphoreCreateBinary();
    if (handle->tx_scratch == NULL || handle->rx_scratch == NULL || handle->irq_sem == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    spi_bus_config_t bus_config = {
        .mosi_io_num = config->mosi_gpio,
        .miso_io_num = config->miso_gpio,
        .sclk_io_num = config->sck_gpio,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    err = spi_bus_initialize(config->spi_host, &bus_config, SPI_DMA_CH_AUTO);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPI host already initialized, sharing the bus");
    } else if (err != ESP_OK) {
        goto fail;
    } else {
        handle->owns_bus = true;
    }

    spi_device_interface_config_t dev_config = {
        .mode = 0,
        .clock_speed_hz = SX1262_SPI_CLOCK_HZ,
        .spics_io_num = config->nss_gpio,
        .queue_size = 1,
    };
    err = spi_bus_add_device(config->spi_host, &dev_config, &handle->spi);
    if (err != ESP_OK) {
        goto fail_bus;
    }

    gpio_config_t out_conf = {
        .pin_bit_mask = 1ULL << config->nrst_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&out_conf);
    if (err != ESP_OK) {
        goto fail_dev;
    }
    gpio_set_level(config->nrst_gpio, 1);

    gpio_config_t busy_conf = {
        .pin_bit_mask = 1ULL << config->busy_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&busy_conf);
    if (err != ESP_OK) {
        goto fail_dev;
    }

    gpio_config_t dio1_conf = {
        .pin_bit_mask = 1ULL << config->dio1_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    err = gpio_config(&dio1_conf);
    if (err != ESP_OK) {
        goto fail_dev;
    }

    if (!s_isr_service_installed) {
        err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            goto fail_dev;
        }
        s_isr_service_installed = true;
    }
    err = gpio_isr_handler_add(config->dio1_gpio, sx1262_dio1_isr, handle);
    if (err != ESP_OK) {
        goto fail_dev;
    }

    *out_handle = handle;
    ESP_LOGI(TAG, "SX1262 created on SPI host %d", config->spi_host);
    return ESP_OK;

fail_dev:
    spi_bus_remove_device(handle->spi);
fail_bus:
    if (handle->owns_bus) {
        spi_bus_free(config->spi_host);
    }
fail:
    if (handle->irq_sem != NULL) {
        vSemaphoreDelete(handle->irq_sem);
    }
    free(handle->tx_scratch);
    free(handle->rx_scratch);
    free(handle);
    return err;
}

esp_err_t sx1262_init(sx1262_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (handle->asleep) {
        esp_err_t err = sx1262_wake(handle);
        if (err != ESP_OK) {
            return err;
        }
    }

    esp_err_t err = sx1262_hw_reset(handle);
    if (err != ESP_OK) {
        return err;
    }

    if ((err = sx1262_set_standby(handle)) != ESP_OK) {
        return err;
    }

    uint8_t rf_switch = 0x01;
    if ((err = sx1262_write_command(handle, SX1262_OP_SET_DIO2_RF_SWITCH, &rf_switch, 1)) != ESP_OK) {
        return err;
    }

    uint8_t packet_type = SX1262_PACKET_LORA;
    if ((err = sx1262_write_command(handle, SX1262_OP_SET_PACKET_TYPE, &packet_type, 1)) != ESP_OK) {
        return err;
    }

    uint32_t freq_reg = (uint32_t)(((uint64_t)handle->config.frequency_hz << 25) / SX1262_XTAL_HZ);
    uint8_t freq_params[4] = {
        (uint8_t)(freq_reg >> 24), (uint8_t)(freq_reg >> 16),
        (uint8_t)(freq_reg >> 8), (uint8_t)(freq_reg),
    };
    if ((err = sx1262_write_command(handle, SX1262_OP_SET_RF_FREQUENCY, freq_params, sizeof(freq_params))) != ESP_OK) {
        return err;
    }

    int8_t power = handle->config.tx_power_dbm;
    if (power > 22) {
        power = 22;
    } else if (power < -9) {
        power = -9;
    }
    uint8_t pa_duty_cycle = (power > 14) ? 0x04 : 0x02;
    uint8_t hp_max = (power > 14) ? 0x07 : 0x02;
    uint8_t pa_params[4] = {pa_duty_cycle, hp_max, 0x00 /* SX1262 */, 0x01};
    if ((err = sx1262_write_command(handle, SX1262_OP_SET_PA_CONFIG, pa_params, sizeof(pa_params))) != ESP_OK) {
        return err;
    }

    uint8_t tx_params[2] = {(uint8_t)power, 0x02 /* 40 us ramp */};
    if ((err = sx1262_write_command(handle, SX1262_OP_SET_TX_PARAMS, tx_params, sizeof(tx_params))) != ESP_OK) {
        return err;
    }

    uint8_t bw;
    switch (handle->config.bandwidth_hz) {
    case 125000:
        bw = 0x04;
        break;
    case 250000:
        bw = 0x05;
        break;
    case 500000:
        bw = 0x06;
        break;
    default:
        ESP_LOGW(TAG, "unsupported bandwidth %lu Hz, using 125 kHz", (unsigned long)handle->config.bandwidth_hz);
        bw = 0x04;
        break;
    }
    uint8_t ldro = ((1ULL << handle->config.spreading_factor) * 1000ULL >
                    16ULL * handle->config.bandwidth_hz) ? 0x01 : 0x00;
    uint8_t mod_params[4] = {handle->config.spreading_factor, bw, handle->config.coding_rate, ldro};
    if ((err = sx1262_write_command(handle, SX1262_OP_SET_MODULATION_PARAMS, mod_params, sizeof(mod_params))) != ESP_OK) {
        return err;
    }

    if ((err = sx1262_set_packet_params(handle, SX1262_MAX_PAYLOAD)) != ESP_OK) {
        return err;
    }

    uint16_t irq_mask = SX1262_IRQ_TX_DONE | SX1262_IRQ_RX_TX_TIMEOUT;
    uint8_t irq_params[8] = {
        (uint8_t)(irq_mask >> 8), (uint8_t)(irq_mask & 0xFF),
        (uint8_t)(irq_mask >> 8), (uint8_t)(irq_mask & 0xFF),
        0x00, 0x00,
        0x00, 0x00,
    };
    if ((err = sx1262_write_command(handle, SX1262_OP_SET_DIO_IRQ_PARAMS, irq_params, sizeof(irq_params))) != ESP_OK) {
        return err;
    }

    uint8_t base_addr[2] = {0x00, 0x00};
    if ((err = sx1262_write_command(handle, SX1262_OP_SET_BUFFER_BASE_ADDR, base_addr, sizeof(base_addr))) != ESP_OK) {
        return err;
    }

    if ((err = sx1262_clear_irq(handle, 0xFFFF)) != ESP_OK) {
        return err;
    }

    handle->initialized = true;
    handle->asleep = false;
    ESP_LOGI(TAG, "SX1262 initialized: %lu Hz, SF%u, BW %lu Hz, %d dBm",
             (unsigned long)handle->config.frequency_hz,
             handle->config.spreading_factor,
             (unsigned long)handle->config.bandwidth_hz,
             power);
    return ESP_OK;
}

esp_err_t sx1262_send(sx1262_handle_t handle, const uint8_t *data, size_t length)
{
    if (handle == NULL || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (length == 0 || length > SX1262_MAX_PAYLOAD) {
        ESP_LOGE(TAG, "invalid payload length %u", (unsigned)length);
        return ESP_ERR_INVALID_ARG;
    }
    if (!handle->initialized) {
        ESP_LOGE(TAG, "driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err;
    if (handle->asleep) {
        err = sx1262_wake(handle);
        if (err != ESP_OK) {
            return err;
        }
    }

    if ((err = sx1262_set_standby(handle)) != ESP_OK) {
        return err;
    }
    if ((err = sx1262_write_buffer(handle, 0x00, data, length)) != ESP_OK) {
        return err;
    }
    if ((err = sx1262_set_packet_params(handle, (uint8_t)length)) != ESP_OK) {
        return err;
    }
    if ((err = sx1262_clear_irq(handle, 0xFFFF)) != ESP_OK) {
        return err;
    }

    xSemaphoreTake(handle->irq_sem, 0);

    uint8_t tx_timeout[3] = {0x00, 0x00, 0x00};
    err = sx1262_write_command(handle, SX1262_OP_SET_TX, tx_timeout, sizeof(tx_timeout));
    if (err != ESP_OK) {
        return err;
    }

    if (xSemaphoreTake(handle->irq_sem, pdMS_TO_TICKS(handle->config.tx_timeout_ms)) != pdTRUE) {
        ESP_LOGE(TAG, "TX_DONE wait timeout");
        sx1262_set_standby(handle);
        return ESP_ERR_TIMEOUT;
    }

    uint16_t irq = 0;
    err = sx1262_get_irq(handle, &irq);
    if (err != ESP_OK) {
        sx1262_set_standby(handle);
        return err;
    }
    sx1262_clear_irq(handle, 0xFFFF);
    sx1262_set_standby(handle);

    if ((irq & SX1262_IRQ_TX_DONE) == 0) {
        ESP_LOGW(TAG, "TX finished without TX_DONE (irq=0x%04X)", irq);
        return (irq & SX1262_IRQ_RX_TX_TIMEOUT) ? ESP_ERR_TIMEOUT : ESP_FAIL;
    }

    ESP_LOGD(TAG, "sent %u bytes", (unsigned)length);
    return ESP_OK;
}

esp_err_t sx1262_sleep(sx1262_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!handle->initialized || handle->asleep) {
        return ESP_OK;
    }
    uint8_t sleep_config = SX1262_SLEEP_WARM_RTC;
    esp_err_t err = sx1262_write_command(handle, SX1262_OP_SET_SLEEP, &sleep_config, 1);
    if (err != ESP_OK) {
        return err;
    }
    handle->asleep = true;
    ESP_LOGD(TAG, "SX1262 in warm-start sleep");
    return ESP_OK;
}

esp_err_t sx1262_destroy(sx1262_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    gpio_isr_handler_remove(handle->config.dio1_gpio);
    if (handle->spi != NULL) {
        spi_bus_remove_device(handle->spi);
    }
    if (handle->owns_bus) {
        spi_bus_free(handle->config.spi_host);
    }
    if (handle->irq_sem != NULL) {
        vSemaphoreDelete(handle->irq_sem);
    }
    free(handle->tx_scratch);
    free(handle->rx_scratch);
    free(handle);
    return ESP_OK;
}
