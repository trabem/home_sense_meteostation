Так. Тут краще написати не «ТЗ на пару сторінок», а документ, після якого AI не зможе сказати: «я вирішив зробити ще HTTP-сервер, три singleton-и й task на кожен датчик, бо мені так здалося».



Нижче я формую ТЗ саме як \*\*implementation specification для AI-кодера\*\*. За основу беру актуальну схемотехніку: ESP32-C3-WROOM-02, E22-900M22S, DS3231, два INA226, PCA9536, зовнішню sensor board через I²C/WAKE\_IRQ; сама плата розрахована на передачу через LoRa або Wi-Fi.  На sensor board фактично стоять \*\*BME280 + CJMCU-3935/AS3935\*\*, що я також врахував із наданої схеми.



\# Технічне завдання



\## Firmware метеостанції на ESP32-C3 / ESP-IDF



\### 1. Мета проєкту



Реалізувати production-oriented firmware автономної метеостанції на \*\*ESP32-C3-WROOM-02\*\* з використанням:



\* ESP-IDF;

\* FreeRTOS;

\* Wi-Fi;

\* BLE Wi-Fi Provisioning;

\* MQTT;

\* LoRa через E22-900M22S / SX1262;

\* I²C;

\* Deep Sleep;

\* зовнішнього wake-up IRQ від AS3935;

\* RTC DS3231;

\* BME280;

\* INA226 ×2;

\* PCA9536.



Основний принцип роботи:



> \*\*wake → determine reason → read required data → transmit → put peripherals into low-power state → deep sleep\*\*



ESP32 не повинен безглуздо сидіти увімкненим 24/7 і медитувати над `vTaskDelay()`. Це автономний battery-powered пристрій, тому firmware має бути насамперед \*\*event-driven та low-power oriented\*\*.



Плата апаратно має стабільну 3.3 V шину, а ESP32-C3 є центральним контролером для I²C, LoRa та Wi-Fi. 



\---



\# 2. Target



```text

MCU: ESP32-C3

Module: ESP32-C3-WROOM-02

Framework: ESP-IDF

RTOS: FreeRTOS included in ESP-IDF

Language: C

Minimum language standard: C11

```



Не використовувати Arduino framework або Arduino compatibility layer.



ESP-IDF version має бути зафіксована в README та CI. Не писати код із розрахунком «на якійсь там версії IDF воно мало б працювати».



\---



\# 3. Архітектурні принципи



Архітектура повинна бути \*\*clean, component-oriented та idiomatic для ESP-IDF\*\*.



Не треба переносити Python Clean Architecture в embedded буквально й народжувати `AbstractWeatherRepositoryFactoryInterface`. Це вже буде не firmware, а помста наступному розробнику.



Використати такі рівні:



```text

Application

&#x20;   ↓

Domain / application models

&#x20;   ↓

Services / abstractions

&#x20;   ↓

Hardware components / transports

&#x20;   ↓

ESP-IDF drivers / HAL

```



ESP-IDF component model є основним механізмом модульності.



Приблизна структура:



```text

meteostation/

├── CMakeLists.txt

├── sdkconfig.defaults

├── partitions.csv

├── README.md

│

├── main/

│   ├── CMakeLists.txt

│   ├── app\_main.c

│   ├── app.c

│   └── app\_config.c

│

└── components/

&#x20;   ├── board/

&#x20;   ├── app\_events/

&#x20;   ├── app\_storage/

&#x20;   ├── power\_manager/

&#x20;   ├── sensor\_manager/

&#x20;   │

&#x20;   ├── i2c\_bus/

&#x20;   ├── bme280/

&#x20;   ├── as3935/

&#x20;   ├── ina226/

&#x20;   ├── ds3231/

&#x20;   ├── pca9536/

&#x20;   │

&#x20;   ├── lora/

&#x20;   ├── wifi\_manager/

&#x20;   ├── wifi\_provisioning/

&#x20;   ├── mqtt\_transport/

&#x20;   ├── transport/

&#x20;   │

&#x20;   ├── telemetry/

&#x20;   └── diagnostics/

```



Кожний component повинен мати:



```text

component/

├── CMakeLists.txt

├── Kconfig          # якщо є compile-time configuration

├── include/

│   └── component\_name.h

├── private/

│   └── ...

└── component\_name.c

```



Допустима інша внутрішня структура компонента, якщо вона виправдана.



\---



\# 4. Dependency rules



`main` є \*\*composition root\*\*.



`main`:



\* створює компоненти;

\* ін'єктує dependencies;

\* запускає application layer.



Hardware components не повинні знати:



\* MQTT;

\* JSON;

\* telemetry schema;

\* business/application flow.



Наприклад:



```text

bme280

```



повинен лише вміти:



```c

esp\_err\_t bme280\_init(...);

esp\_err\_t bme280\_read(...);

esp\_err\_t bme280\_sleep(...);

```



Він не повинен раптом усередині себе викликати:



```c

mqtt\_publish(...)

```



бо це вже архітектурний шашлик.



\---



\# 5. API conventions



Публічні API компонентів мають відповідати стилю ESP-IDF.



Основні правила:



```c

esp\_err\_t component\_init(...);

esp\_err\_t component\_start(...);

esp\_err\_t component\_stop(...);

esp\_err\_t component\_read(...);

```



Де це доцільно — використовувати opaque handle:



```c

typedef struct lora\_context\_t \*lora\_handle\_t;

```



Configuration через структури:



```c

typedef struct {

&#x20;   gpio\_num\_t irq\_gpio;

&#x20;   uint8\_t i2c\_address;

} as3935\_config\_t;

```



та default macros:



```c

\#define AS3935\_CONFIG\_DEFAULT() { ... }

```



Заборонено:



\* глобальний mutable state без необхідності;

\* доступ драйверів прямо до глобальних objects;

\* `extern` на пів проєкту;

\* hardcoded GPIO всередині драйверів.



\---



\# 6. Hardware abstraction / board component



Усі physical pin assignments мають зберігатися в одному component `board`.



На поточній PCB ESP32-C3 має таке розведення:



```text

GPIO4   -> SPI\_MOSI

GPIO5   -> SPI\_SCK

GPIO3   -> SPI\_MISO



GPIO10  -> LORA\_NSS

GPIO0   -> LORA\_BUSY

GPIO9   -> LORA\_NRST

GPIO20  -> LORA\_DIO1



GPIO7   -> I2C\_SDA

GPIO8   -> I2C\_SCL



GPIO1   -> WAKE\_IRQ



GPIO6   -> CHARGE\_SOURCE\_STATUS\_OUT

```



Це має бути оголошено приблизно так:



```c

\#define BOARD\_GPIO\_SPI\_MOSI      GPIO\_NUM\_4

\#define BOARD\_GPIO\_SPI\_SCK       GPIO\_NUM\_5

\#define BOARD\_GPIO\_SPI\_MISO      GPIO\_NUM\_3



\#define BOARD\_GPIO\_LORA\_NSS      GPIO\_NUM\_10

\#define BOARD\_GPIO\_LORA\_BUSY     GPIO\_NUM\_0

\#define BOARD\_GPIO\_LORA\_NRST     GPIO\_NUM\_9

\#define BOARD\_GPIO\_LORA\_DIO1     GPIO\_NUM\_20



\#define BOARD\_GPIO\_I2C\_SDA       GPIO\_NUM\_7

\#define BOARD\_GPIO\_I2C\_SCL       GPIO\_NUM\_8



\#define BOARD\_GPIO\_WAKE\_IRQ      GPIO\_NUM\_1

```



Жодних:



```c

gpio\_set\_level(10, 1);

```



посеред LoRa driver.



\---



\# 7. I²C subsystem



Використовувати сучасний ESP-IDF I²C master API.



I²C bus є shared resource для:



\* DS3231;

\* INA226 battery;

\* INA226 solar;

\* PCA9536;

\* BME280;

\* AS3935.



Апаратно основна плата має pull-up 4.7 kΩ на SDA/SCL та розрахована на шину до 400 kHz; для довгого зовнішнього кабелю документація плати рекомендує розглядати 100 kHz як надійніший варіант.  



Початково:



```text

I2C frequency: 100 kHz

```



Зробити configurable через Kconfig:



```text

CONFIG\_METEO\_I2C\_FREQUENCY\_HZ

```



Не створювати окремий I²C controller для кожного sensor driver.



Має існувати один:



```c

i2c\_master\_bus\_handle\_t

```



який передається devices.



\---



\# 8. I²C devices



\## 8.1 BME280



Sensor board підключає BME280 через I²C.



Апаратно:



```text

CSB -> 3V3

SDO -> GND

```



Отже очікувана адреса:



```text

0x76

```



Bosch визначає адресу як `111011x`; SDO=GND відповідає `0x76`. (\[Bosch Sensortec]\[1])



BME280 повинен працювати в \*\*forced mode\*\*, а не постійно в normal mode.



При periodic measurement:



```text

wake ESP

→ trigger forced measurement

→ wait measurement completion

→ read T/P/RH

→ BME280 sleep

```



Дані:



```c

typedef struct {

&#x20;   float temperature\_c;

&#x20;   float pressure\_pa;

&#x20;   float humidity\_percent;

} environment\_measurement\_t;

```



Не використовувати `double`, якщо немає реальної потреби.



\---



\# 9. AS3935 lightning sensor



Sensor board містить AS3935/CJMCU-3935.



Інтерфейс:



```text

I2C

IRQ -> WAKE\_IRQ -> ESP32 GPIO1

```



На sensor board:



```text

A0 = HIGH

A1 = HIGH

SI = HIGH

CS = LOW

```



Driver має підтримувати щонайменше:



```c

esp\_err\_t as3935\_init(...);



esp\_err\_t as3935\_get\_interrupt\_reason(...);



esp\_err\_t as3935\_read\_lightning\_distance(...);



esp\_err\_t as3935\_read\_lightning\_energy(...);



esp\_err\_t as3935\_clear\_statistics(...);

```



Driver не повинен вирішувати, що робити з lightning event.



\---



\# 10. Lightning wake-up



Це критична функція системи.



При detection lightning AS3935 виставляє IRQ, який через:



```text

WAKE\_IRQ

```



приходить на:



```text

ESP32 GPIO1

```



ESP32 повинен перебувати у Deep Sleep більшість часу.



ESP-IDF для ESP32-C3 підтримує GPIO wake-up із Deep Sleep для відповідних RTC/VDD3P3\_RTC IO; перед реалізацією firmware має \*\*перевірити через API, що GPIO1 є валідним deep-sleep wake source для обраної версії ESP-IDF\*\*, а не мовчки припустити це. (\[Espressif Systems]\[2])



Initialization повинна fail loudly, якщо:



```c

esp\_sleep\_is\_valid\_wakeup\_gpio(BOARD\_GPIO\_WAKE\_IRQ) == false

```



\### Flow



```text

AS3935 detects event

&#x20;       ↓

WAKE\_IRQ HIGH

&#x20;       ↓

ESP32 exits Deep Sleep

&#x20;       ↓

app determines wake cause

&#x20;       ↓

AS3935 interrupt source is read

```



AS3935 може генерувати не тільки lightning interrupt, тому \*\*сам факт WAKE\_IRQ не означає автоматично lightning\*\*.



Після wake потрібно прочитати interrupt reason.



Якщо reason == LIGHTNING:



```text

read estimated distance

read energy if available

read RTC timestamp

transmit lightning event immediately

```



Payload:



```json

{

&#x20; "type": "lightning",

&#x20; "timestamp": 1780000000,

&#x20; "distance\_km": 12,

&#x20; "energy": 123456

}

```



`energy` може бути omitted/null, якщо application layer вирішить її не передавати.



Головне обов'язкове поле події:



```text

distance/range

```



тобто відстань/діапазон, який визначив AS3935.



Після передачі:



```text

clear/ack AS3935 interrupt

prepare wake sources

deep sleep

```



\---



\# 11. RTC / DS3231



DS3231 використовується як canonical hardware clock.



Плата має окрему CR2032, тому RTC продовжує рахувати час навіть після відключення основної 3.3 V системи. 



API:



```c

esp\_err\_t rtc\_init(...);



esp\_err\_t rtc\_get\_time(

&#x20;   rtc\_handle\_t handle,

&#x20;   struct tm \*time

);



esp\_err\_t rtc\_set\_time(...);

```



Application layer повинен мати Unix timestamp.



Після успішного Wi-Fi connection дозволено синхронізувати system time через SNTP і записати скоригований час у DS3231.



Не синхронізувати RTC при кожному пробудженні.



\---



\# 12. INA226



На основній платі встановлено два INA226:



```text

Battery INA226 -> 0x44

Solar INA226   -> 0x40

```



Це відповідає актуальній схемі плати. 



Battery shunt:



```text

Rshunt = 0.020 Ω

```



Solar shunt:



```text

Rshunt = 0.100 Ω

```



Driver повинен бути generic:



```c

ina226\_create(

&#x20;   bus,

&#x20;   address,

&#x20;   shunt\_resistance,

&#x20;   ...

);

```



А не два copy-paste компоненти `battery\_ina.c` та `solar\_ina.c`, бо ми все-таки пишемо firmware, а не тренуємо Ctrl+C/Ctrl+V.



Measurement:



```c

typedef struct {

&#x20;   float bus\_voltage\_v;

&#x20;   float current\_a;

&#x20;   float power\_w;

} power\_measurement\_t;

```



Після measurement INA226 переводити в Power Down.



Це прямо передбачено для цієї плати як спосіб зменшити споживання між вимірюваннями. 



\---



\# 13. PCA9536



PCA9536 використовується для:



\* `CHRG\_STATUS`;

\* `STDBY\_STATUS`;

\* J4 mode switch.



Поточна апаратна документація називає J4 Wi-Fi switch та показує pull-up 47 kΩ: при розімкненому J4 вхід HIGH, при замкненому — LOW. 



У firmware \*\*нова семантика J4\*\*:



> J4 вибирає transport mode.



Тобто старе трактування «Wi-Fi on/off» більше не використовувати.



\---



\# 14. J4 / transport selector



J4 визначає один із режимів:



```c

typedef enum {

&#x20;   TRANSPORT\_MODE\_LORA,

&#x20;   TRANSPORT\_MODE\_WIFI,

} transport\_mode\_t;

```



Не розкидати перевірку PCA9536 по application:



```c

if (gpio == 0) ...

```



Має бути abstraction:



```c

esp\_err\_t board\_get\_transport\_mode(

&#x20;   transport\_mode\_t \*mode

);

```



Полярність J4 винести в board configuration.



Рекомендована default mapping:



```text

J4 HIGH / open   -> LoRa

J4 LOW / closed  -> Wi-Fi

```



Причина: якщо switch/кабель J4 відсутній або пошкоджений, pull-up переведе систему у більш енергоощадний автономний LoRa mode, замість того щоб ESP32 безуспішно палив батарею пошуком Wi-Fi.



Але це \*\*board-level policy\*\*, тому реалізація повинна дозволяти змінити mapping однією compile-time конфігурацією.



\---



\# 15. Transport abstraction



Application layer не повинен знати деталі MQTT або SX1262.



Потрібен transport interface:



```c

typedef struct transport transport\_t;



typedef struct {

&#x20;   esp\_err\_t (\*init)(transport\_t \*self);

&#x20;   esp\_err\_t (\*send)(

&#x20;       transport\_t \*self,

&#x20;       const uint8\_t \*data,

&#x20;       size\_t length

&#x20;   );

&#x20;   esp\_err\_t (\*sleep)(transport\_t \*self);

&#x20;   esp\_err\_t (\*deinit)(transport\_t \*self);

} transport\_ops\_t;

```



Або еквівалентна реалізація без надмірної C-OOP акробатики.



Реалізації:



```text

mqtt\_transport

lora\_transport

```



Application виконує:



```text

mode = board\_get\_transport\_mode()



transport = transport\_factory(mode)



transport\_send(...)

```



\---



\# 16. Wi-Fi mode



Коли J4 визначає:



```text

TRANSPORT\_MODE\_WIFI

```



система повинна:



```text

LoRa -> sleep

Wi-Fi -> start

connect

MQTT -> connect

publish

MQTT -> disconnect

Wi-Fi -> stop

deep sleep

```



Не тримати Wi-Fi увімкненим між measurement cycles.



\---



\# 17. Wi-Fi provisioning



Використати стандартний \*\*ESP-IDF Wi-Fi Provisioning Manager / protocomm\*\*, а не писати власний BLE-протокол, тому що життя й так коротке.



Provisioning transport:



```text

BLE

```



Provisioning повинен використовуватись тільки якщо:



```text

Wi-Fi credentials відсутні

```



або був виконаний explicit factory reset / reprovision command.



Після успішного provisioning:



```text

credentials -> NVS

BLE provisioning -> stop

BLE stack resources -> release

Wi-Fi connection -> attempt

```



Не запускати provisioning при кожному boot.



Device service name:



```text

METEO\_<last bytes of MAC>

```



наприклад:



```text

METEO\_A17C42

```



Proof-of-possession повинен підтримуватись.



PoP не hardcode-ити одним однаковим значенням для всіх production devices.



\---



\# 18. Wi-Fi connection policy



Connection має timeout.



Наприклад configurable:



```text

CONFIG\_METEO\_WIFI\_CONNECT\_TIMEOUT\_MS

```



Не дозволено:



```text

while (!connected) {

&#x20;   retry forever;

}

```



бо так firmware перетворює LiFePO₄ на одноразовий хімічний таймер.



При Wi-Fi failure:



1\. retry bounded count;

2\. записати diagnostic error;

3\. не зависати;

4\. повернутися в sleep.



Автоматичний fallback з Wi-Fi в LoRa \*\*не виконувати\*\*, якщо це окремо не буде задано пізніше.



J4 визначає transport явно.



\---



\# 19. MQTT



Використати офіційний ESP-MQTT component.



MQTT config:



```text

broker URI

device id

username

password/token

TLS configuration

topics

QoS

timeouts

```



не повинні бути hardcoded у business logic.



Для production:



```text

mqtts://

```



має підтримуватися обов'язково.



Рекомендована topic structure:



```text

meteostation/<device\_id>/telemetry

meteostation/<device\_id>/events

meteostation/<device\_id>/status

```



Telemetry:



```text

.../telemetry

```



Lightning:



```text

.../events

```



\---



\# 20. LoRa subsystem



Hardware:



```text

E22-900M22S

SX1262

SPI

```



Модуль E22-900M22S працює в діапазоні приблизно 850–930 MHz та має SPI interface. (\[Ebyte]\[3])



Початковий production region:



```text

EU868

```



Frequency/configuration не hardcode-ити в driver.



Через Kconfig:



```text

CONFIG\_METEO\_LORA\_FREQUENCY\_HZ

CONFIG\_METEO\_LORA\_TX\_POWER\_DBM

CONFIG\_METEO\_LORA\_SPREADING\_FACTOR

CONFIG\_METEO\_LORA\_BANDWIDTH

CONFIG\_METEO\_LORA\_CODING\_RATE

```



\---



\# 21. Особливість TXEN/RXEN



На поточній PCB:



```text

E22 DIO2 -> LORA\_TXEN

```



а RXEN має hardware pull-down; MCU control RXEN через GPIO21 передбачено тільки опціональним DNP connection.



Тому firmware не має вигадувати неіснуюче GPIO-керування RF switch.



SX1262 треба конфігурувати:



```text

DIO2 as RF switch control

```



EBYTE саме рекомендує `SetDIO2AsRfSwitchCtrl`, після чого DIO2/TXEN автоматично керує TX path. (\[Ebyte]\[4])



\---



\# 22. LoRa power management



Фізичного power switch для LoRa у схемі немає.



Тому при Wi-Fi mode зробити:



```text

SX1262 -> SetSleep

```



Після переходу SX1262 у sleep MCU може залишити SPI inactive.



EBYTE для E22-900M22S заявляє типовий sleep current близько \*\*180 nA\*\*, тому програмний sleep тут цілком має сенс, навіть без load switch. (\[Ebyte]\[3])



Таким чином:



\### Wi-Fi mode



```text

LoRa initialized enough to issue sleep

→ SX1262 sleep

→ Wi-Fi active

```



\### LoRa mode



```text

Wi-Fi disabled

Bluetooth disabled

LoRa wake/init

send

LoRa sleep

```



Не залишати SX1262 у standby без причини.



\---



\# 23. LoRa communication model



Поточна метеостанція працює як:



```text

TX-only application

```



Не реалізовувати:



\* LoRa receive loop;

\* ACK protocol;

\* mesh;

\* retries based on receiver acknowledgement;



якщо вони явно не будуть додані до вимог.



Модуль фізично є transceiver, але application використовує тільки transmission.



\---



\# 24. Telemetry model



Внутрішня модель не повинна залежати від JSON.



```c

typedef struct {

&#x20;   int64\_t timestamp;



&#x20;   environment\_measurement\_t environment;



&#x20;   power\_measurement\_t battery;

&#x20;   power\_measurement\_t solar;



&#x20;   bool charging;

&#x20;   bool charge\_complete;



&#x20;   transport\_mode\_t transport;

} telemetry\_t;

```



Додатково можна передавати:



```text

firmware version

device id

boot counter

reset reason

wake reason

```



\---



\# 25. Lightning event model



Окрема модель:



```c

typedef struct {

&#x20;   int64\_t timestamp;



&#x20;   uint8\_t distance\_km;

&#x20;   uint32\_t energy;



&#x20;   bool has\_energy;

} lightning\_event\_t;

```



Lightning не треба пхати в `telemetry\_t` полем:



```c

bool lightning = true;

```



бо це різні типи подій із різною семантикою.



\---



\# 26. Wire format



Потрібно розділити:



```text

domain model

&#x20;       ↓

serialization

&#x20;       ↓

transport

```



\## MQTT



Для MQTT використовувати JSON.



Приклад:



```json

{

&#x20; "schema": 1,

&#x20; "device\_id": "meteo-a17c42",

&#x20; "timestamp": 1780000000,

&#x20; "environment": {

&#x20;   "temperature\_c": 21.34,

&#x20;   "humidity\_percent": 63.2,

&#x20;   "pressure\_pa": 100821

&#x20; },

&#x20; "battery": {

&#x20;   "voltage\_v": 3.31,

&#x20;   "current\_a": -0.042,

&#x20;   "power\_w": -0.139

&#x20; },

&#x20; "solar": {

&#x20;   "voltage\_v": 5.87,

&#x20;   "current\_a": 0.212,

&#x20;   "power\_w": 1.244

&#x20; },

&#x20; "charging": true

}

```



\## LoRa



JSON \*\*не використовувати за замовчуванням\*\*.



LoRa payload має бути compact binary format.



Наприклад:



```text

byte 0      protocol version

byte 1      message type

byte 2..5   device id/hash

byte 6..9   unix timestamp

...

```



Serialization винести в:



```text

telemetry\_codec

```



Щоб transport не знав, як створюється payload.



\---



\# 27. Protocol versioning



Кожний wire message повинен містити:



```text

protocol\_version

message\_type

```



Типи:



```c

typedef enum {

&#x20;   MESSAGE\_TYPE\_TELEMETRY = 0x01,

&#x20;   MESSAGE\_TYPE\_LIGHTNING = 0x02,

&#x20;   MESSAGE\_TYPE\_BOOT      = 0x03,

} message\_type\_t;

```



Це треба зробити з першої версії, а не через рік, коли сервер уже має три покоління payload і один древній if на 200 рядків.



\---



\# 28. Main state machine



Application lifecycle:



```text

BOOT

&#x20;↓

INIT\_MINIMAL

&#x20;↓

DETERMINE\_WAKE\_REASON

&#x20;↓

READ\_TRANSPORT\_MODE

&#x20;↓

EVENT?

&#x20;├── lightning

&#x20;│      ↓

&#x20;│   PROCESS\_LIGHTNING

&#x20;│      ↓

&#x20;│   SEND\_EVENT

&#x20;│

&#x20;└── periodic

&#x20;       ↓

&#x20;    READ\_SENSORS

&#x20;       ↓

&#x20;    BUILD\_TELEMETRY

&#x20;       ↓

&#x20;    SEND\_TELEMETRY

&#x20;↓

PREPARE\_SLEEP

&#x20;↓

DEEP\_SLEEP

```



Рекомендована enum:



```c

typedef enum {

&#x20;   APP\_STATE\_BOOT,

&#x20;   APP\_STATE\_INIT,

&#x20;   APP\_STATE\_MEASURE,

&#x20;   APP\_STATE\_TRANSMIT,

&#x20;   APP\_STATE\_PREPARE\_SLEEP,

&#x20;   APP\_STATE\_ERROR,

} app\_state\_t;

```



Але не треба будувати мегамашину станів на 3000 рядків, якщо flow можна чисто розкласти функціями.



\---



\# 29. Wake reasons



Application повинна розрізняти:



```c

typedef enum {

&#x20;   APP\_WAKE\_POWER\_ON,

&#x20;   APP\_WAKE\_TIMER,

&#x20;   APP\_WAKE\_LIGHTNING\_IRQ,

&#x20;   APP\_WAKE\_UNKNOWN,

} app\_wakeup\_reason\_t;

```



Mapping робиться на основі ESP-IDF wakeup/reset APIs.



\---



\# 30. Periodic measurements



Інтервал задається Kconfig:



```text

CONFIG\_METEO\_MEASUREMENT\_INTERVAL\_SEC

```



Не зашивати magic number у код.



Initial default можна встановити:



```text

300 s

```



але це параметр конфігурації, а не фундаментальний закон Всесвіту.



Перед deep sleep:



```c

esp\_sleep\_enable\_timer\_wakeup(...);

```



і:



```text

WAKE\_IRQ wake source

```



мають бути налаштовані одночасно.



\---



\# 31. Оптимізований lightning boot



При wake через AS3935 \*\*не треба спочатку читати BME280 + два INA226 тільки тому, що вони є\*\*.



Lightning flow має бути максимально коротким:



```text

boot

↓

init I2C

↓

init RTC

↓

init AS3935

↓

read AS3935 reason

↓

if lightning:

&#x20;   read distance

&#x20;   timestamp

&#x20;   determine transport

&#x20;   send

↓

sleep

```



Environment measurement може бути omitted для lightning event.



Це зменшує:



\* latency;

\* active time;

\* battery usage.



\---



\# 32. FreeRTOS model



Не створювати task для кожного компонента.



Не потрібно:



```text

bme\_task

rtc\_task

ina\_task

as3935\_task

pca\_task

lora\_task

weather\_task

another\_manager\_task

manager\_of\_managers\_task

```



ESP32-C3 одразу попросить адвоката.



Для V1 достатньо:



```text

application task

network/MQTT internals managed by ESP-IDF

event callbacks

```



Окремі tasks допускаються лише коли є реальна asynchronous responsibility.



\---



\# 33. Interrupt handling



ISR має бути мінімальним.



ISR не повинен:



\* читати I²C;

\* логувати великі повідомлення;

\* алокувати heap;

\* формувати telemetry;

\* відправляти MQTT.



У runtime/light-sleep scenario ISR тільки сигналізує:



```text

queue

task notification

event bit

```



При Deep Sleep wake application читає AS3935 вже після boot.



\---



\# 34. Event loop



Для application-wide asynchronous events бажано використовувати стандартний:



```text

esp\_event

```



Наприклад:



```text

METEO\_EVENT\_WIFI\_CONNECTED

METEO\_EVENT\_WIFI\_FAILED

METEO\_EVENT\_MQTT\_CONNECTED

METEO\_EVENT\_TRANSMIT\_COMPLETE

```



Не винаходити власний event bus, якщо ESP-IDF уже один подарував.



\---



\# 35. NVS



В NVS зберігати:



```text

provisioning state

Wi-Fi credentials — через стандартні ESP mechanisms

device configuration

MQTT configuration if runtime configurable

boot counter

possibly calibration data

```



Не використовувати NVS як log database.



\---



\# 36. FRAM



На схемі FM24CL16B позначена:



```text

NOT FOR V1

```



і на поточній PCB не встановлюється. 



Отже firmware V1:



\* не повинен вимагати FRAM;

\* не повинен fail boot при її відсутності;

\* driver FRAM взагалі не потрібен у MVP.



\---



\# 37. Offline message persistence



Для V1 \*\*не реалізовувати persistent queue telemetry\*\*, якщо це окремо не буде затверджено.



При transmit failure:



```text

log error

increment diagnostics

sleep

```



Архітектура повинна дозволяти пізніше додати:



```text

outbox / persistent queue

```



без переписування sensor drivers.



\---



\# 38. Charge state



PCA9536 читає:



```text

CHRG\_STATUS

STDBY\_STATUS

```



TP5000 outputs є open-drain, а плата має pull-ups. 



Application abstraction:



```c

typedef enum {

&#x20;   CHARGE\_STATE\_UNKNOWN,

&#x20;   CHARGE\_STATE\_CHARGING,

&#x20;   CHARGE\_STATE\_COMPLETE,

&#x20;   CHARGE\_STATE\_IDLE,

&#x20;   CHARGE\_STATE\_FAULT,

} charge\_state\_t;

```



Raw GPIO state не повинен потрапляти в telemetry.



\---



\# 39. Power source status



ESP32 GPIO6 читає:



```text

CHARGE\_SOURCE\_STATUS\_OUT

```



від TPS2121.



Створити abstraction:



```c

typedef enum {

&#x20;   POWER\_SOURCE\_UNKNOWN,

&#x20;   POWER\_SOURCE\_USB,

&#x20;   POWER\_SOURCE\_SOLAR,

} power\_source\_t;

```



Якщо по одному digital signal неможливо достовірно розрізнити всі стани — не вигадувати інформацію. Driver має повертати тільки те, що реально визначається схемою.



\---



\# 40. Sleep preparation



Перед кожним Deep Sleep application повинна пройти централізований `prepare\_sleep()`.



Приблизно:



```text

MQTT disconnect

↓

Wi-Fi stop

↓

BLE stop if active

↓

INA226 power-down

↓

BME280 sleep

↓

SX1262 sleep

↓

flush required state

↓

configure timer wake

↓

configure WAKE\_IRQ

↓

esp\_deep\_sleep\_start()

```



Якщо transport == LoRa:



```text

Wi-Fi навіть не запускати.

```



Якщо transport == Wi-Fi:



```text

LoRa залишити/перевести в SX1262 sleep.

```



\---



\# 41. Initialization levels



Рекомендую розділити initialization на:



\### Minimal



```text

board

NVS

I2C

PCA9536

RTC

AS3935

```



\### Measurement



```text

BME280

INA226 battery

INA226 solar

```



\### Transport-specific



```text

Wi-Fi + MQTT

```



або



```text

SPI + LoRa

```



Так lightning event не змушує firmware ініціалізувати пів плати просто для краси.



\---



\# 42. Error handling



Всі hardware operations повертають `esp\_err\_t`.



Не використовувати:



```c

ESP\_ERROR\_CHECK()

```



на помилках, які є recoverable у production.



Наприклад:



```text

BME280 missing

MQTT timeout

LoRa transmit timeout

INA226 read error

```



не повинні автоматично reboot-loop пристрій.



`ESP\_ERROR\_CHECK()` допустимий для справді fatal initialization conditions.



\---



\# 43. Sensor failure policy



Один несправний sensor не повинен обов'язково блокувати telemetry.



Наприклад:



```text

BME280 OK

battery INA OK

solar INA failed

```



повинно бути можливо передати partial telemetry.



Data model має мати validity flags:



```c

typedef struct {

&#x20;   bool environment\_valid;

&#x20;   bool battery\_valid;

&#x20;   bool solar\_valid;



&#x20;   ...

} telemetry\_t;

```



\---



\# 44. Logging



Використовувати:



```c

ESP\_LOGE

ESP\_LOGW

ESP\_LOGI

ESP\_LOGD

```



Component-specific tags:



```text

APP

BME280

AS3935

INA226

LORA

MQTT

WIFI

POWER

```



Не використовувати `printf()` для runtime logging.



Не логувати:



\* Wi-Fi passwords;

\* MQTT passwords/tokens;

\* proof-of-possession secrets.



\---



\# 45. Diagnostics



Збирати:



```c

typedef struct {

&#x20;   uint32\_t boot\_count;

&#x20;   uint32\_t wifi\_failures;

&#x20;   uint32\_t mqtt\_failures;

&#x20;   uint32\_t lora\_failures;

&#x20;   uint32\_t sensor\_failures;

} diagnostics\_t;

```



Boot log:



```text

firmware version

ESP-IDF version

reset reason

wake reason

selected transport

```



\---



\# 46. Device identity



Device ID генерувати з ESP MAC.



Наприклад:



```text

meteo-A1B2C3

```



Не hardcode-ити device ID у firmware binary.



\---



\# 47. Firmware version



У telemetry/status передбачити:



```text

firmware\_version

```



отриману з:



```text

esp\_app\_get\_description()

```



\---



\# 48. Kconfig



Усі суттєві compile-time параметри мають бути в Kconfig.



Мінімум:



```text

METEO\_MEASUREMENT\_INTERVAL\_SEC



METEO\_I2C\_FREQUENCY\_HZ



METEO\_WIFI\_CONNECT\_TIMEOUT\_MS

METEO\_WIFI\_MAX\_RETRIES



METEO\_MQTT\_URI

METEO\_MQTT\_QOS



METEO\_LORA\_FREQUENCY\_HZ

METEO\_LORA\_TX\_POWER\_DBM

METEO\_LORA\_SPREADING\_FACTOR

METEO\_LORA\_BANDWIDTH

METEO\_LORA\_CODING\_RATE



METEO\_J4\_WIFI\_ACTIVE\_LEVEL



METEO\_LOG\_LEVEL

```



\---



\# 49. Component interfaces



Орієнтовні public APIs.



\### sensor\_manager



```c

esp\_err\_t sensor\_manager\_init(

&#x20;   sensor\_manager\_handle\_t \*handle,

&#x20;   const sensor\_manager\_config\_t \*config

);



esp\_err\_t sensor\_manager\_measure(

&#x20;   sensor\_manager\_handle\_t handle,

&#x20;   telemetry\_t \*telemetry

);

```



\### lightning



```c

esp\_err\_t lightning\_service\_process\_irq(

&#x20;   lightning\_service\_handle\_t handle,

&#x20;   lightning\_event\_t \*event,

&#x20;   bool \*is\_lightning

);

```



\### transport



```c

esp\_err\_t transport\_open(...);



esp\_err\_t transport\_send(

&#x20;   transport\_handle\_t handle,

&#x20;   message\_type\_t type,

&#x20;   const uint8\_t \*payload,

&#x20;   size\_t payload\_size

);



esp\_err\_t transport\_close(...);

```



\### power manager



```c

esp\_err\_t power\_manager\_prepare\_sleep(

&#x20;   power\_manager\_handle\_t handle,

&#x20;   uint64\_t wake\_after\_us

);

```



\---



\# 50. `app\_main`



`app\_main()` повинен залишатися маленьким.



Приблизно:



```c

void app\_main(void)

{

&#x20;   ESP\_ERROR\_CHECK(app\_init());

&#x20;   app\_run();

}

```



Не робити з `app\_main.c` файл на 2500 рядків, де живе половина цивілізації.



\---



\# 51. Normal periodic flow



Повний normal cycle:



```text

ESP32 wakes by timer

&#x20;       ↓

initialize minimal system

&#x20;       ↓

read J4

&#x20;       ↓

initialize measurement subsystem

&#x20;       ↓

BME280 forced measurement

&#x20;       ↓

battery INA226 measurement

&#x20;       ↓

solar INA226 measurement

&#x20;       ↓

read charger state

&#x20;       ↓

read RTC

&#x20;       ↓

construct telemetry

&#x20;       ↓

serialize according to transport

&#x20;       ↓

J4 == Wi-Fi?

&#x20;      /      \\

&#x20;    yes       no

&#x20;     ↓         ↓

LoRa sleep    Wi-Fi remains disabled

Wi-Fi start   SX1262 wake/init

connect AP    transmit LoRa

MQTT connect  SX1262 sleep

publish

disconnect

Wi-Fi stop

&#x20;      \\       /

&#x20;       ↓

configure timer + IRQ wake

&#x20;       ↓

Deep Sleep

```



\---



\# 52. Lightning flow



```text

AS3935 IRQ

&#x20;       ↓

ESP32 wake from Deep Sleep

&#x20;       ↓

minimal init

&#x20;       ↓

read AS3935 IRQ reason

&#x20;       ↓

not lightning?

&#x20;  ↓

handle/log if necessary

&#x20;  ↓

sleep



lightning?

&#x20;       ↓

read estimated distance

&#x20;       ↓

read lightning energy

&#x20;       ↓

read RTC

&#x20;       ↓

read J4

&#x20;       ↓

create lightning\_event

&#x20;       ↓

send via selected transport

&#x20;       ↓

radio/network shutdown

&#x20;       ↓

Deep Sleep

```



Від IRQ до transmission починати processing якомога швидше.



\---



\# 53. Provisioning flow



```text

boot

↓

J4 == Wi-Fi

↓

credentials available?

&#x20;  ├─ yes

&#x20;  │    ↓

&#x20;  │ connect

&#x20;  │

&#x20;  └─ no

&#x20;       ↓

&#x20;     start BLE provisioning

&#x20;       ↓

&#x20;     receive credentials

&#x20;       ↓

&#x20;     stop provisioning

&#x20;       ↓

&#x20;     connect Wi-Fi

```



Якщо J4 == LoRa:



```text

BLE provisioning не запускати.

Wi-Fi stack не запускати.

```



\---



\# 54. Provisioning reset



Передбачити API:



```c

esp\_err\_t wifi\_provisioning\_reset(void);

```



V1 не обов'язково має мати фізичну кнопку factory reset.



Але функціональність повинна бути доступна для:



\* development command;

\* future control interface;

\* flashing/debug build.



\---



\# 55. Unit tests



Компоненти application/domain layer мають бути тестовані без реального hardware.



Обов'язково покрити:



\### Transport selection



```text

J4 Wi-Fi -> MQTT transport

J4 LoRa  -> LoRa transport

```



\### Wake routing



```text

timer -> telemetry flow

WAKE\_IRQ + lightning -> lightning flow

WAKE\_IRQ + non-lightning -> no lightning transmission

```



\### Serialization



```text

telemetry MQTT JSON

lightning MQTT JSON

LoRa binary telemetry

LoRa binary lightning

```



\### Failure cases



```text

BME280 failure

INA failure

Wi-Fi timeout

MQTT timeout

LoRa timeout

RTC read failure

```



\---



\# 56. Hardware integration tests



Окремо реалізувати hardware test mode/build.



Tests:



```text

I2C scan

BME280 read

AS3935 register communication

DS3231 read/write

INA226 battery

INA226 solar

PCA9536

SPI LoRa

SX1262 sleep/wakeup

LoRa TX

Wi-Fi provisioning

Wi-Fi reconnect

MQTT publish

Deep Sleep timer wake

WAKE\_IRQ wake

```



Очікуваний I²C scan основної плати включає DS3231, PCA9536 та INA226 `0x40/0x44`; FRAM у V1 не очікується. 



З підключеною sensor board також очікувати:



```text

BME280 0x76

AS3935 configured address

```



\---



\# 57. LoRa driver tests



Перевірити:



```text

reset

BUSY handling

SPI communication

SetStandby

SetDIO2AsRfSwitchCtrl

SetRfFrequency

SetTxParams

SetModulationParams

SetPacketParams

SetBufferBaseAddress

WriteBuffer

SetTx

DIO1 TX\_DONE

timeout

SetSleep

```



BUSY повинен мати timeout.



Ніяких:



```c

while (gpio\_get\_level(BUSY)) {}

```



без timeout.



\---



\# 58. MQTT delivery semantics



Для V1:



```text

QoS 1

```



рекомендований для important event/telemetry, якщо broker підтримує потрібний flow.



Для lightning події не завершувати Wi-Fi cycle відразу після виклику `esp\_mqtt\_client\_publish()` без перевірки відповідної delivery semantics/event.



Треба дочекатися:



```text

publish accepted/acknowledged according to selected QoS

```



або timeout.



\---



\# 59. Timeouts



Кожна зовнішня operation повинна мати finite timeout:



```text

I2C

SPI BUSY

LoRa TX

Wi-Fi connect

MQTT connect

MQTT publish

sensor conversion

```



У firmware не повинно бути нескінченних очікувань peripheral state.



\---



\# 60. Security



Wi-Fi provisioning:



```text

BLE provisioning

Proof of Possession

```



MQTT:



```text

TLS support mandatory

```



Не дозволяти production build із:



```text

TLS certificate validation disabled

```



без explicit development flag.



Secrets:



```text

не логувати

не hardcode-ити в git

```



\---



\# 61. Memory rules



Avoid unnecessary dynamic allocation after initialization.



Для short-lived serialization допустима controlled allocation, але перевага:



```text

bounded buffers

static/configured maximum sizes

```



LoRa payload size має бути перевірений до transmission.



\---



\# 62. Watchdog



Application не повинна вимикати watchdog «щоб перестало падати».



Long operations повинні:



\* block appropriately;

\* yield;

\* мати timeout;

\* не busy-loop.



\---



\# 63. Code style



Дотримуватися стилю ESP-IDF:



```text

snake\_case

esp\_err\_t

ESP\_LOGx

CONFIG\_\*

component-oriented API

CMake component dependencies

```



Функції мають бути короткими й мати одну зрозумілу responsibility.



Заборонити функції типу:



```c

void handle\_everything(void);

```



на 900 рядків. AI таке любить: отримав clean architecture, а через десять хвилин створив `system\_manager.c`, який знає буквально все.



\---



\# 64. README



README повинен містити:



```text

Project overview

Hardware revision

Pin mapping

I2C devices / addresses

Build requirements

ESP-IDF version

idf.py set-target esp32c3

idf.py menuconfig

Build

Flash

Monitor

Provisioning procedure

J4 modes

MQTT configuration

LoRa configuration

Deep Sleep behavior

How to run tests

```



Також окремий section:



```text

Power behaviour

```



із таблицею:



| State        | ESP32  | Wi-Fi        | BLE | LoRa   |

| ------------ | ------ | ------------ | --- | ------ |

| Deep sleep   | sleep  | off          | off | sleep  |

| Measurement  | active | off          | off | sleep  |

| Wi-Fi TX     | active | on           | off | sleep  |

| LoRa TX      | active | off          | off | active |

| Provisioning | active | provisioning | on  | sleep  |



\---



\# 65. Definition of Done



V1 вважається готовою, коли виконуються всі такі сценарії.



1\. Після clean flash ESP32 запускається без crash/reboot loop.



2\. У LoRa mode Wi-Fi та BLE не запускаються.



3\. У Wi-Fi mode SX1262 переводиться в sleep.



4\. Якщо Wi-Fi credentials відсутні, provisioning працює через BLE.



5\. Після provisioning credentials зберігаються і наступний boot не запускає provisioning повторно.



6\. Timer wake:



&#x20;  \* ESP32 прокидається;

&#x20;  \* читає BME280;

&#x20;  \* читає обидва INA226;

&#x20;  \* читає charge state;

&#x20;  \* додає timestamp;

&#x20;  \* передає telemetry;

&#x20;  \* повертається у Deep Sleep.



7\. AS3935 IRQ:



&#x20;  \* пробуджує ESP32;

&#x20;  \* firmware читає interrupt reason;

&#x20;  \* якщо це lightning — отримує distance/range;

&#x20;  \* формує lightning event;

&#x20;  \* негайно передає його через transport, вибраний J4;

&#x20;  \* повертається у Deep Sleep.



8\. Якщо AS3935 IRQ не є lightning event, firmware не надсилає фальшиву lightning подію.



9\. Wi-Fi failure не призводить до нескінченного active state.



10\. MQTT failure не призводить до нескінченного active state.



11\. LoRa BUSY/TX failure має timeout.



12\. INA226 переходять у Power Down після використання.



13\. BME280 не залишається в continuous measurement mode.



14\. SX1262 переходить у sleep після transmission або коли використовується Wi-Fi.



15\. У Deep Sleep активними залишаються лише hardware blocks, необхідні для:



\* RTC;

\* AS3935 detection;

\* wake-up.



\---



\## 66. Що AI \*\*не має реалізовувати у V1\*\*



Щоб модель не вирішила за один prompt побудувати ще NASA Deep Space Network:



\* OTA — поки не реалізовувати;

\* web UI — не реалізовувати;

\* HTTP API — не реалізовувати;

\* LoRa RX — не реалізовувати;

\* LoRa ACK/retry protocol — не реалізовувати;

\* LoRaWAN — не реалізовувати;

\* persistent telemetry queue — не реалізовувати;

\* FRAM — не реалізовувати;

\* SD card — не реалізовувати;

\* cloud backend — не реалізовувати;

\* mobile application — не реалізовувати;

\* dynamic remote configuration — не реалізовувати.



Але архітектура не повинна робити їх подальше додавання болючим.



\---



\# 67. Порядок реалізації для AI



Я б прямо наказав AI реалізовувати проєкт \*\*поетапно\*\*, а не одразу генерувати 48 файлів, з яких 17 навіть не компілюються.



\### Phase 1 — Skeleton



```text

ESP-IDF project

components

board config

logging

NVS

app lifecycle

```



Результат:



```text

idf.py build

```



проходить.



\### Phase 2 — I²C



```text

i2c\_bus

DS3231

PCA9536

INA226

BME280

AS3935

```



І hardware diagnostics.



\### Phase 3 — Power/sleep



```text

timer wake

GPIO wake

wake reason

prepare\_sleep

```



\### Phase 4 — LoRa



```text

SPI

SX1262

E22 configuration

TX

sleep

```



\### Phase 5 — Wi-Fi



```text

Wi-Fi manager

BLE provisioning

credential persistence

connection lifecycle

```



\### Phase 6 — MQTT



```text

MQTT connection

TLS

publish

timeouts

```



\### Phase 7 — Application flows



```text

periodic telemetry

lightning event

J4 routing

```



\### Phase 8 — tests / stabilization



```text

unit tests

hardware integration tests

error injection

power measurements

```



\*\*Кожна phase повинна завершуватись buildable firmware.\*\*



\---



\## Окремо важливий нюанс по твоєму hardware



У твоєму випадку рішення з LoRa вийшло навіть приємніше, ніж могло бути. Фізично відрізати живлення E22 ти вже не можеш без нової ревізії PCB, але це не трагедія: E22/SX1262 підтримує sleep, а EBYTE заявляє близько \*\*180 нА\*\* в цьому режимі. (\[Ebyte]\[3]) Тобто в Wi-Fi mode я б \*\*однозначно відправляв SX1262 у `SetSleep`\*\*, а не городив програмну магію навколо RXEN.



І ще важливіше: на твоїй PCB \*\*DIO2 уже заведений на TXEN\*\*, тому driver повинен включати RF-switch control через DIO2. Це не просто «можна зробити» — саме під таку конфігурацію схема фактично й розведена. EBYTE описує цей варіант як автоматичне керування TX path через `SetDIO2AsRfSwitchCtrl`. (\[Ebyte]\[4])



А от J4 я навмисно наказав сховати за `board\_get\_transport\_mode()`. Сьогодні це перемикач Wi-Fi/LoRa, завтра ти вирішиш поміняти полярність або поведінку — і замість археологічної експедиції по 14 файлах зміниться одне місце.



Це ТЗ уже можна \*\*практично напряму кидати Codex/Kimi/Astra як master specification\*\*. Я б лише разом із ним передавав AI `.kicad\_sch`/PDF як hardware source of truth і додав правило: \*\*«при конфлікті припущення firmware зі схемою не вигадувати рішення — позначити конфлікт і зупинитися на цьому пункті»\*\*. Це добре страхує від улюбленої AI-звички впевнено програмувати GPIO, якого фізично немає.



\[1]: https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf?utm\_source=chatgpt.com "BME280 Datasheet"

\[2]: https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/system/sleep\_modes.html?utm\_source=chatgpt.com "Sleep Modes - ESP32-C3 - — ESP-IDF Programming Guide latest documentation"

\[3]: https://www.ebyte.com/product/435.html?utm\_source=chatgpt.com "E22-900M22S SX1262芯片LoRa模块-LoRa扩频模组"

\[4]: https://www.ebyte.com/news/4956.html?utm\_source=chatgpt.com "GraftSense E22-900M22S硬件电路设计与PCB布线指南-亿佰特物联网"



