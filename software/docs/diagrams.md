# Діаграми прошивки метеостанції

Діаграми у форматі [Mermaid](https://mermaid.js.org/) — рендеряться в GitHub,
VS Code (розширення Mermaid) та на mermaid.live. Усі вузли відповідають
реальним функціям і компонентам коду — діаграми можна використовувати для
навігації репозиторієм.

## 1. Діаграма розгалуження — дерево викликів функцій

Ієрархія викликів від `app_main()` до драйверів. Кожна гілка — це окремий
сценарій: ініціалізація, телеметрія, блискавка, передача. У дужках — файл.

```mermaid
flowchart TD
    MAIN["app_main()<br/>(main/app_main.c)"]

    MAIN --> INIT["app_init()<br/>(main/app.c)"]
    MAIN --> RUN["app_run()<br/>(main/app.c)"]

    subgraph INITBR["Ініціалізація"]
        INIT --> I1["app_storage_init / next_boot_count<br/>(app_storage)"]
        INIT --> I2["i2c_bus_init → board_init → board_get_device_id<br/>(i2c_bus, board)"]
        INIT --> I3["ds3231_create?<br/>as3935_create?  (опційно)"]
        INIT --> I4["power_manager_create<br/>→ get_wakeup_reason"]
        INIT --> I5{"board_usb_power_present()?<br/>→ continuous режим"}
    end

    RUN --> MODE{"s_app.continuous?"}

    MODE -- "Так (USB)" --> CL["continuous_loop()<br/>не повертається"]
    CL --> CL1{"таймер циклу?<br/>METEO_MEASUREMENT_INTERVAL_SEC"}
    CL1 -- Так --> TF
    CL1 -- "WAKE_IRQ (ISR → notify)" --> HL

    MODE -- "Ні (батарея)" --> WR{"wake_reason?"}
    WR -- "LIGHTNING_IRQ" --> HL["handle_lightning_irq()"]
    WR -- "TIMER / POWER_ON / UNKNOWN" --> DECIDE

    HL --> LS["lightning_service_process_irq()<br/>(lightning_service)"]
    LS --> ISL{"is_lightning?"}
    ISL -- Так --> LF["run_lightning_flow()"]
    ISL -- Ні --> DECIDE["app_flow_decide()<br/>(app_flow)"]

    LF --> ENC1["lightning_codec_encode_mqtt / _lora<br/>(telemetry)"]
    ENC1 --> TXM

    DECIDE --> ACT{"app_action_t?"}
    ACT -- "TELEMETRY" --> TF["run_telemetry_flow()"]
    ACT -- "LIGHTNING / SLEEP" --> SLEEP

    TF --> S1["bme280_create / ina226_create ×2"]
    TF --> S2["sensor_manager_measure()<br/>(sensor_manager)"]
    TF --> S3["rtc_now() (ds3231)"]
    TF --> ENC2["telemetry_codec_encode + boot_codec<br/>якщо wake = POWER_ON"]
    ENC2 --> TXM["transmit_messages()"]

    TXM --> TMODE{"transport_mode?<br/>(board_get_transport_mode ← J4)"}
    TMODE -- "Wi-Fi" --> TXW["transmit_via_wifi()"]
    TMODE -- "LoRa" --> TXL["transmit_via_lora()"]

    TXW --> W1["ensure_radio_asleep() (sx1262_sleep)"]
    TXW --> W2{"has_credentials?<br/>(wifi_manager)"}
    W2 -- Ні --> W3["wifi_provisioning_run() (BLE)"]
    W2 -- Так --> W4["wifi_manager_connect()"]
    W3 --> W4
    W4 --> W5["maybe_sync_rtc() (SNTP → ds3231_set_unix_time)"]
    W4 --> W6["transport_create/open/send<br/>(mqtt_transport, JSON)"]

    TXL --> L1["ensure_radio_created() (sx1262_create)"]
    TXL --> L2["transport_create/open/send/close<br/>(lora_transport, binary → SetSleep)"]

    W6 --> DIAG["diagnostics_increment()<br/>при збоях (diagnostics)"]
    L2 --> DIAG
    W6 --> SLEEP
    L2 --> SLEEP

    SLEEP["ensure_radio_asleep()<br/>power_manager_prepare_sleep()"]
    SLEEP --> DS["power_manager_enter_deep_sleep()"]
    DS -. "наступне пробудження" .-> RUN
```

## 2. Функціональна діаграма (IDEF0)

Декомпозиція головної функції системи. Нотація IDEF0:
**входи** — зліва, **виходи** — справа, **керування** — зверху,
**механізми** (чим виконується) — знизу.

### A0. Контекстна діаграма

```mermaid
flowchart LR
    ENV["Повітря (T/P/RH)<br/>Електромагнітне поле грози"] --> A0
    ENERGY["Сонце / USB"] --> A0
    TIME["Час"] -.-> A0
    CFG["Конфігурація<br/>(Kconfig, J4, Wi-Fi creds)"] -.-> A0

    subgraph A0["A0. Автономний моніторинг метеостанції"]
        F["wake → measure →<br/>encode → transmit → sleep"]
    end

    A0 --> OUT1["Телеметрія<br/>JSON / binary"]
    A0 --> OUT2["Події блискавок<br/>distance + energy"]
    A0 --> OUT3["Діагностика збоїв"]

    HW["ESP32-C3 + датчики + SX1262"] --- A0
    NET["MQTT broker / LoRa gateway"] --- A0
```

### A0-декомпозиція. Функції A1–A4

```mermaid
flowchart LR
    subgraph INPUTS["Входи"]
        D1["Дані датчиків<br/>BME280, INA226×2, PCA9536"]
        D2["IRQ блискавки<br/>AS3935"]
        D3["Причина пробудження"]
    end

    subgraph CTRL["Керування"]
        C1["Інтервал вимірювань<br/>CONFIG_METEO_MEASUREMENT_INTERVAL_SEC"]
        C2["J4 → transport_mode<br/>Wi-Fi ↔ LoRa"]
        C3["app_flow_decide:<br/>LIGHTNING / TELEMETRY / SLEEP"]
    end

    subgraph FUNCS[" "]
        A1["A1. Вимірювання<br/>sensor_manager<br/>rtc_now"]
        A2["A2. Обробка події<br/>lightning_service<br/>відсів disturber"]
        A3["A3. Кодування<br/>telemetry_codec<br/>JSON / binary"]
        A4["A4. Передача<br/>mqtt_transport /<br/>lora_transport"]
    end

    subgraph OUTPUTS["Виходи"]
        O1["telemetry_t"]
        O2["lightning_event_t"]
        O3["Пакети до broker / gateway"]
    end

    subgraph MECH["Механізми"]
        M1["I2C bus 100 кГц"]
        M2["power_manager<br/>deep sleep + wake sources"]
        M3["Wi-Fi + BLE provisioning /<br/>SX1262 SPI"]
    end

    D1 --> A1
    D2 --> A2
    D3 --> A1
    C1 -.-> A1
    C3 -.-> A2
    C2 -.-> A4
    A1 --> O1 --> A3
    A2 --> O2 --> A3
    A3 --> A4 --> O3
    M1 --- A1
    M2 --- A1
    M3 --- A4
```

## 3. Функціональна діаграма системи (блокова)

```mermaid
flowchart LR
    subgraph PWR["Живлення"]
        SOLAR[Solar panel] --> PMIC[TP5000 + TPS2121]
        BAT[Battery] --> PMIC
        USBPWR[USB 5V] --> PMIC
    end

    subgraph SENS["Датчики — I2C bus 100 кГц"]
        BME[BME280 — T/P/RH]
        INAB[INA226 battery]
        INAS[INA226 solar]
        RTC[DS3231 RTC + CR2032]
        AS[AS3935 блискавки]
        EXP[PCA9536: J4/CHRG/STDBY]
    end

    subgraph FW["ESP32-C3 — прошивка"]
        MCU[app / app_flow]
        SM[sensor_manager]
        LS[lightning_service]
        PM[power_manager]
        CODEC[telemetry_codec]
        TF[transport_factory]
        MCU --> SM & LS & PM
        SM & LS --> CODEC --> TF
    end

    subgraph NET["Транспорти"]
        WIFI[wifi_manager + BLE prov] --> MQTT[mqtt_transport mqtts]
        LORADRV[sx1262 SPI] --> LORAT[lora_transport]
    end

    subgraph CLOUD["Зовнішні системи"]
        BROKER[MQTT broker]
        GW[LoRa gateway]
        PHONE[Смартфон BLE]
    end

    PMIC --> FW
    SENS --> MCU
    AS -. WAKE_IRQ .-> PM
    EXP -. J4 .-> MCU
    TF --> WIFI & LORADRV
    PHONE --> WIFI
    MQTT --> BROKER
    LORAT --> GW
```

## Легенда потоків даних

| Потік | Формат | Частота | Код |
| ----- | ------ | ------- | --- |
| Телеметрія | JSON (MQTT) / binary (LoRa) | `CONFIG_METEO_MEASUREMENT_INTERVAL_SEC` | `run_telemetry_flow()` |
| BOOT-повідомлення | JSON / binary | лише POWER_ON | `boot_codec_encode_*` |
| Подія блискавки | JSON / binary | негайно, WAKE_IRQ | `run_lightning_flow()` |
| RTC синхронізація | SNTP → DS3231 | коли час невалідний | `maybe_sync_rtc()` |
