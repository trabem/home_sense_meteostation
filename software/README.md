# Meteostation Firmware

Автономна battery-powered метеостанція на **ESP32-C3-WROOM-02** (ESP-IDF, FreeRTOS).
Принцип роботи: **wake → determine reason → read required data → transmit →
peripherals low-power → deep sleep**.

- Періодична телеметрія: BME280 (T/P/RH), INA226 ×2 (battery/solar), стан зарядки (TP5000 через PCA9536), timestamp із DS3231.
- Подія блискавки: AS3935 → WAKE_IRQ → негайна передача distance/energy.
- Транспорт вибирається перемикачем **J4**: LoRa (SX1262/E22-900M22S, binary payload) або Wi-Fi + MQTT (JSON, mqtts).
- BLE Wi-Fi provisioning — тільки коли credentials відсутні.

## Build requirements

| Item          | Version             |
| ------------- | ------------------- |
| ESP-IDF       | **v6.0.1** (fixed) |
| Target        | `esp32c3`           |
| Language      | C (C11+)            |

Managed components (fetched automatically by the component manager):
`espressif/network_provisioning ^1.2.5`, `espressif/mqtt ^1.1.0`
(у IDF v6.0 `wifi_provisioning` та `esp-mqtt` винесені з дерева компонентів).

## Hardware

### Pin mapping (ESP32-C3)

| Signal | GPIO | Signal | GPIO |
| ------ | ---- | ------ | ---- |
| SPI_MOSI | 4 | I2C_SDA | 7 |
| SPI_SCK  | 5 | I2C_SCL | 8 |
| SPI_MISO | 3 | WAKE_IRQ (AS3935) | 1 |
| LORA_NSS | 10 | CHARGE_SOURCE_STATUS_OUT (TPS2121) | 6 |
| LORA_BUSY | 0 | | |
| LORA_NRST | 9 | | |
| LORA_DIO1 | 20 | | |

E22 DIO2 апаратно заведений на LORA_TXEN → driver використовує
`SetDIO2AsRfSwitchCtrl` (керування RF switch через DIO2, без GPIO MCU).
Фізичного power switch для LoRa немає → SX1262 переводиться в `SetSleep`
(~180 nA) у Wi-Fi mode та після кожної передачі.

### I2C devices (один shared bus, 100 kHz за замовчуванням)

| Device | Address | Примітка |
| ------ | ------- | -------- |
| INA226 (solar)   | 0x40 | Rshunt 0.020 Ω |
| PCA9536          | 0x41 | J4 + CHRG_STATUS + STDBY_STATUS |
| INA226 (battery) | 0x44 | Rshunt 0.020 Ω |
| DS3231           | 0x68 | RTC, CR2032 backup |
| BME280           | 0x76 | sensor board (CSB=3V3, SDO=GND), forced mode |
| AS3935           | 0x03 | sensor board (A0=A1=HIGH) |

FRAM (FM24CL16B) — **NOT FOR V1**, firmware її не потребує.

### PCA9536 pin assignment

| Pin | Функція |
| --- | ------- |
| P0 | CHRG_STATUS (TP5000, open-drain, LOW = charging) |
| P1 | STDBY_STATUS (TP5000, open-drain, LOW = complete) |
| P2 | J4 transport switch |

## Build / flash / monitor

```bash
idf.py set-target esp32c3
idf.py menuconfig        # секція "Meteostation"
idf.py build
idf.py -p PORT flash
idf.py -p PORT monitor
```

## J4 modes

J4 вибирає transport (не «Wi-Fi on/off»):

| J4 | Режим |
| -- | ----- |
| open / HIGH (47 kΩ pull-up) | **LoRa** (default, енергоощадний fallback) |
| closed / LOW | **Wi-Fi + MQTT** |

Полярність — board-level policy: `CONFIG_METEO_J4_WIFI_ACTIVE_LEVEL`.

## MQTT configuration

Kconfig: `METEO_MQTT_URI` (для production — `mqtts://`), username/password,
QoS (default 1), connect/publish timeouts, root topic (default `meteostation`).

Topics:

```text
meteostation/<device_id>/telemetry
meteostation/<device_id>/events      # lightning
meteostation/<device_id>/status      # boot
```

`<device_id>` = `meteo-<last 3 bytes of MAC>`, напр. `meteo-a17c42`.
Publish з QoS > 0 чекає delivery confirmation (`MQTT_EVENT_PUBLISHED`) або
timeout. TLS certificate validation вимикається тільки explicit dev-flag
`CONFIG_METEO_MQTT_TLS_SKIP_VERIFY` (default: off).

## LoRa configuration

Kconfig: `METEO_LORA_FREQUENCY_HZ` (default 868.1 MHz, EU868),
`METEO_LORA_TX_POWER_DBM`, `METEO_LORA_SPREADING_FACTOR`,
`METEO_LORA_BANDWIDTH`, `METEO_LORA_CODING_RATE`, timeouts.
TX-only: без RX loop, ACK, retries, LoRaWAN. Payload — compact binary,
versioned (`protocol_version`, `message_type`: telemetry=0x01,
lightning=0x02, boot=0x03). Формат задокументований у
`components/telemetry/include/telemetry_codec.h`.

## Provisioning

- Запускається **тільки** у Wi-Fi mode за відсутності credentials (або після
  `wifi_provisioning_reset()`).
- BLE service name: `METEO_<last 3 MAC bytes>`, напр. `METEO_A17C42`.
- Proof-of-possession: детерміновано виводиться з заводського MAC
  (унікальний per-device, не hardcoded; не логується).
- Після успіху: credentials → NVS, provisioning stop, BLE resources release,
  наступні boot-и provisioning не запускають.
- Timeout: `CONFIG_METEO_PROV_TIMEOUT_MS` (default 5 хв), далі deep sleep.

### Як передати Wi-Fi credentials (покроково)

1. Транспорт має бути Wi-Fi: на платі — замкнути J4 (LOW). Для bring-up на
   платі без PCA9536: `menuconfig → Meteostation → Board → Transport
   selection source → Force Wi-Fi (development)`.
2. Якщо credentials відсутні — BLE provisioning стартує автоматично.
3. З телефона: застосунок **ESP BLE Provisioning** (Android/iOS) або
   `esp_prov` з idf-extra-components → вибрати пристрій `METEO_XXXXXX` →
   ввести PoP → вибрати/ввести SSID+password.
4. PoP детерміновано виводиться з MAC (FNV-1a, 8 hex). Для bring-up увімкни
   `CONFIG_METEO_PROV_LOG_POP` — PoP друкується в лог при старті
   provisioning (dev-only, не для production).
5. Після успіху credentials у NVS; наступні boot-и підключаються напряму.
   Стерти: `wifi_provisioning_reset()` або повне стирання flash.

### Як скинути provisioning (стерти Wi-Fi credentials)

1. Тільки NVS-розділ (рекомендовано; credentials живуть у NVS):
   `parttool.py --port COMx erase_partition --partition-name=nvs`
   (`parttool.py` — з ESP-IDF: `$IDF_PATH/components/partition_table/`).
2. Повне стирання flash: `idf.py -p COMx erase-flash`, потім знову `flash`.
3. Програмно: `wifi_provisioning_reset()` — стирає саме Wi-Fi credentials,
   не чіпаючи решту NVS. З готової прошивки цей виклик у runtime нічим не
   тригериться.

Після стирання наступне завантаження у Wi-Fi mode автоматично запустить
BLE provisioning. Нюанси:

- Варіанти 1–2 стирають усю NVS, тобто разом із credentials обнуляються
  boot counter і diagnostics counters (`app_storage`, `diagnostics`).
  Варіант 3 цього не робить.
- Для переналаштування J4 має бути замкнений (Wi-Fi mode): у LoRa mode
  provisioning не запускається взагалі.
- Ефект стирання проявляється на наступному wake/boot: на battery це
  означає почекати до наступного timer wake або перезавантажити плату.

## Deep Sleep behavior

Перед кожним deep sleep централізований `prepare_sleep()`: MQTT disconnect →
Wi-Fi stop → BLE off → INA226 power-down → BME280 sleep → SX1262 SetSleep →
timer wake (`CONFIG_METEO_MEASUREMENT_INTERVAL_SEC`, default 300 s) +
WAKE_IRQ GPIO wake (HIGH) → `esp_deep_sleep_start()`.

- У LoRa mode Wi-Fi/BLE stack взагалі не запускається.
- У Wi-Fi mode SX1262 одразу переводиться в sleep.
- Lightning wake: мінімальний шлях (I2C → RTC → AS3935 reason → distance →
  transport → sleep), BME280/INA226 не читаються. AS3935 interrupt reason ≠
  lightning → жодна подія не надсилається.
- RTC (DS3231) синхронізується через SNTP тільки коли час явно невалідний
  (< 2024-01-01) — не при кожному пробудженні.
- `esp_sleep_is_valid_wakeup_gpio(GPIO1)` перевіряється при init; невалідний
  pin = loud failure.

### Continuous mode (USB-powered)

Підключення USB до комп'ютера автоматично вимикає deep sleep
(`CONFIG_METEO_USB_CONTINUOUS_MODE`, default on) — без жодної ручної
конфігурації. Детекція комбінована:

- основна плата: TPS2121 `CHARGE_SOURCE_STATUS_OUT` (GPIO6 HIGH = USB);
- fallback (`CONFIG_METEO_USB_DETECT_USJ`, default on): енумерація хоста на
  native USB-Serial-JTAG (GPIO18/19) — покриває Super Mini та будь-яку плату,
  де status line не заведена.

У continuous mode цикли вимірювання йдуть по таймеру
(`CONFIG_METEO_MEASUREMENT_INTERVAL_SEC`), WAKE_IRQ працює через runtime GPIO
ISR (ISR лише сигналізує task notification), USB-порт лишається живим для
прошивки/монітору, а BLE provisioning доступний у Wi-Fi mode за відсутності
credentials. Плаваючий GPIO6 читається як "не USB" (pull-down) → battery-safe
поведінка за замовчуванням. Крайній випадок —
`CONFIG_METEO_FORCE_CONTINUOUS` (dev-only).

### Power behaviour

| State              | ESP32  | Wi-Fi        | BLE | LoRa   |
| ------------------ | ------ | ------------ | --- | ------ |
| Deep sleep         | sleep  | off          | off | sleep  |
| Measurement        | active | off          | off | sleep  |
| Wi-Fi TX           | active | on           | off | sleep  |
| LoRa TX            | active | off          | off | active |
| Provisioning       | active | provisioning | on  | sleep  |
| Continuous (USB)   | active | per cycle    | off | sleep  |

## Project structure

```text
main/                  composition root (app_main, app flows, app_config)
components/
  board/               pin map, J4 transport, charge state, power source
  app_flow/            pure wake routing + J4 mapping
  app_events/          METEO_EVENT base on esp_event
  app_storage/         NVS boot counter
  power_manager/       wake reason mapping, sleep preparation
  sensor_manager/      periodic measurement cycle (partial telemetry OK)
  lightning_service/   AS3935 IRQ processing
  i2c_bus/             single shared I2C master bus
  bme280/ as3935/ ina226/ ds3231/ pca9536/   hardware drivers
  sx1262/              LoRa radio driver (TX-only, DIO2 RF switch)
  lora_transport/ mqtt_transport/ transport/ transport abstraction + factory
  wifi_manager/ wifi_provisioning/           Wi-Fi lifecycle + BLE provisioning
  telemetry/           domain models + wire codecs (JSON / binary)
  diagnostics/         failure counters
```
