# Автономна метеостанція на ESP32-C3

Репозиторій автономної battery-powered метеостанції на **ESP32-C3-WROOM-02**.
Містить firmware (ESP-IDF / FreeRTOS), KiCad-проєкти двох плат і матеріали
фінальної презентації.

Принцип роботи на battery: **wake → визначити причину → прочитати потрібні
дані → передати → перевести периферію в low-power → deep sleep**.

## Що вимірює і передає

- температура, тиск, вологість — **BME280**;
- напруга, струм, потужність battery і solar — **INA226 ×2**;
- час — RTC **DS3231** (CR2032 backup);
- стан зарядки (TP5000) і вибір транспорту — **PCA9536**;
- події блискавки — **AS3935** через `WAKE_IRQ` з негайною передачею
  distance/energy.

Транспорт вибирається перемикачем **J4**:

| J4 | Режим |
| -- | ----- |
| open / HIGH | **LoRa** (SX1262 / E22-900M22S, TX-only, компактний binary payload) — energy-safe default |
| closed / LOW | **Wi-Fi + MQTT** (JSON, `mqtts://`, BLE provisioning за відсутності credentials) |

Підключення USB автоматично вмикає continuous mode — цикли за таймером без
deep sleep, порт лишається живим для прошивки й монітору.

## Структура репозиторію

```text
software/       firmware: C11+, ESP-IDF v6.0.1, FreeRTOS, target esp32c3
  main/         composition root (app_main, flows, app_config)
  components/   board, app_flow, power_manager, sensor_manager,
                lightning_service, драйвери (bme280, as3935, ina226, ds3231,
                pca9536, sx1262), transport (MQTT/LoRa), telemetry, ...
  docs/         Mermaid-діаграми call flow і модулів
hardware/
  main_board/   KiCad-проєкт основної плати (MCU, LoRa, power: USB/Solar/1S LiFePO4)
  sensor_board/ KiCad-проєкт плати датчиків (BME280 + CJMCU-3935/AS3935)
presentation/   фінальна презентація (PDF/PPTX) і генератор
AGENTS.md       правила проєкту для AI-агентів і контрибуторів
TZ.md           історичне implementation specification
```

## Швидкий старт (firmware)

Потрібне activated ESP-IDF **v6.0.1** environment (`IDF_PATH`, `idf.py`):

```bash
cd software
idf.py set-target esp32c3   # тільки для clean setup
idf.py menuconfig           # меню "Meteostation"
idf.py build
idf.py -p PORT flash
idf.py -p PORT monitor
```

Деталі — у [`software/README.md`](software/README.md): pin mapping, I2C
addresses, MQTT/LoRa конфігурація, BLE provisioning, deep sleep і power
behavior.

## Hardware

Два KiCad-проєкти (generator_version 10.0):

- **main_board** — ESP32-C3, E22-900M22S (SX1262), power mux TPS2121
  (USB-over-Solar priority), зарядний контролер TP5000 для 1S LiFePO4,
  buck-boost 3.3 V, INA226 ×2, DS3231, PCA9536;
- **sensor_board** — BME280 (0x76) + AS3935 (0x03), підключення до основної
  плати через I²C і `WAKE_IRQ`.

Очікувані I2C addresses: `0x40` solar INA226, `0x41` PCA9536, `0x44` battery
INA226, `0x68` DS3231, `0x03` AS3935, `0x76` BME280.

## Поза скоупом V1

OTA, HTTP/web UI, LoRaWAN/RX/ACK, persistent telemetry queue, FRAM, SD card,
backend, remote configuration — навмисно не реалізовані.
