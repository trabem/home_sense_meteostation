from pathlib import Path
from math import cos, sin, pi

from PIL import Image
from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.dml import MSO_LINE_DASH_STYLE
from pptx.enum.shapes import MSO_CONNECTOR, MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.util import Inches, Pt

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "Meteostation_Final_Project_UA_v2_Architecture.pptx"
ASSETS = ROOT / "assets"

W, H = 13.333, 7.5
FONT_HEAD = "Bahnschrift"
FONT_BODY = "Arial"
FONT_MONO = "Cascadia Mono"

C = {
    "navy": "081B26",
    "navy2": "0D2633",
    "ink": "10232E",
    "slate": "28424E",
    "muted": "6C838D",
    "muted_dark": "9BB0B8",
    "white": "F7FAFA",
    "paper": "F2F6F5",
    "card": "FFFFFF",
    "line": "D8E3E2",
    "line_dark": "28434D",
    "teal": "20C9A6",
    "teal_dark": "087F6C",
    "teal_pale": "DDF6EF",
    "blue": "55BEF0",
    "blue_pale": "E1F2FA",
    "lime": "B5DC64",
    "amber": "F0B95B",
    "amber_pale": "FFF1D9",
    "red": "E56C73",
}

prs = Presentation()
prs.slide_width = Inches(W)
prs.slide_height = Inches(H)
prs.core_properties.title = "Автономна метеостанція — фінальний проєкт курсу"
prs.core_properties.subject = "ESP32-C3, LoRa, MQTT, KiCad, ESP-IDF"
prs.core_properties.author = "Фінальний проєкт курсу"
prs.core_properties.keywords = "метеостанція, ESP32-C3, LoRa, MQTT, KiCad, ESP-IDF"
prs.core_properties.comments = "Підготовлено за актуальними KiCad-файлами та вихідним кодом репозиторію."


def rgb(hex_color):
    return RGBColor.from_string(hex_color)


def rect(slide, x, y, w, h, fill, radius=True, line=None, line_width=1):
    shape_type = MSO_SHAPE.ROUNDED_RECTANGLE if radius else MSO_SHAPE.RECTANGLE
    shp = slide.shapes.add_shape(shape_type, Inches(x), Inches(y), Inches(w), Inches(h))
    shp.fill.solid()
    shp.fill.fore_color.rgb = rgb(fill)
    if line:
        shp.line.color.rgb = rgb(line)
        shp.line.width = Pt(line_width)
    else:
        shp.line.fill.background()
    if radius:
        try:
            shp.adjustments[0] = 0.12
        except Exception:
            pass
    return shp


def circle(slide, x, y, d, fill, line=None, line_width=1):
    shp = slide.shapes.add_shape(MSO_SHAPE.OVAL, Inches(x), Inches(y), Inches(d), Inches(d))
    shp.fill.solid()
    shp.fill.fore_color.rgb = rgb(fill)
    if line:
        shp.line.color.rgb = rgb(line)
        shp.line.width = Pt(line_width)
    else:
        shp.line.fill.background()
    return shp


def text(slide, value, x, y, w, h, size=18, color=None, bold=False,
         font=FONT_BODY, align=PP_ALIGN.LEFT, valign=MSO_ANCHOR.TOP,
         margin=0, line_spacing=1.05, italic=False):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.clear()
    tf.word_wrap = True
    tf.margin_left = tf.margin_right = tf.margin_top = tf.margin_bottom = Inches(margin)
    tf.vertical_anchor = valign
    p = tf.paragraphs[0]
    p.alignment = align
    p.line_spacing = line_spacing
    p.space_before = p.space_after = Pt(0)
    run = p.add_run()
    run.text = value
    run.font.name = font
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.italic = italic
    run.font.color.rgb = rgb(color or C["ink"])
    return box


def rich_text(slide, runs, x, y, w, h, size=18, color=None, font=FONT_BODY,
              align=PP_ALIGN.LEFT, valign=MSO_ANCHOR.TOP):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.clear()
    tf.word_wrap = True
    tf.margin_left = tf.margin_right = tf.margin_top = tf.margin_bottom = 0
    tf.vertical_anchor = valign
    p = tf.paragraphs[0]
    p.alignment = align
    p.space_before = p.space_after = Pt(0)
    for item in runs:
        run = p.add_run()
        run.text = item[0]
        run.font.name = item[4] if len(item) > 4 else font
        run.font.size = Pt(item[1] if len(item) > 1 and item[1] else size)
        run.font.bold = item[2] if len(item) > 2 else False
        run.font.color.rgb = rgb(item[3] if len(item) > 3 and item[3] else (color or C["ink"]))
    return box


def bullets(slide, items, x, y, w, h, size=16, color=None, bullet_color=None,
            gap=9, font=FONT_BODY, bullet="•"):
    box = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = box.text_frame
    tf.clear()
    tf.word_wrap = True
    tf.margin_left = tf.margin_right = tf.margin_top = tf.margin_bottom = 0
    for idx, item in enumerate(items):
        p = tf.paragraphs[0] if idx == 0 else tf.add_paragraph()
        p.space_before = Pt(0)
        p.space_after = Pt(gap)
        p.line_spacing = 1.05
        r1 = p.add_run()
        r1.text = bullet + "  "
        r1.font.name = font
        r1.font.size = Pt(size)
        r1.font.bold = True
        r1.font.color.rgb = rgb(bullet_color or C["teal_dark"])
        r2 = p.add_run()
        r2.text = item
        r2.font.name = font
        r2.font.size = Pt(size)
        r2.font.color.rgb = rgb(color or C["slate"])
    return box


def line(slide, x1, y1, x2, y2, color=None, width=1.5, dash=False, arrow=False):
    ln = slide.shapes.add_connector(MSO_CONNECTOR.STRAIGHT, Inches(x1), Inches(y1), Inches(x2), Inches(y2))
    ln.line.color.rgb = rgb(color or C["line"])
    ln.line.width = Pt(width)
    if dash:
        ln.line.dash_style = MSO_LINE_DASH_STYLE.DASH
    if arrow:
        ln.line.end_arrowhead = True
    return ln


def pill(slide, label, x, y, w, fill, color, size=10, line_color=None):
    rect(slide, x, y, w, 0.32, fill, True, line_color)
    text(slide, label, x, y + 0.005, w, 0.29, size, color, True, FONT_BODY,
         PP_ALIGN.CENTER, MSO_ANCHOR.MIDDLE)


def add_picture_contain(slide, path, x, y, w, h):
    with Image.open(path) as im:
        iw, ih = im.size
    scale = min(w / iw, h / ih)
    pw, ph = iw * scale, ih * scale
    return slide.shapes.add_picture(str(path), Inches(x + (w - pw) / 2), Inches(y + (h - ph) / 2),
                                    width=Inches(pw), height=Inches(ph))


def base_slide(number, section, title_value, subtitle=None, dark=False):
    slide = prs.slides.add_slide(prs.slide_layouts[6])
    number = len(prs.slides)
    bg = slide.background.fill
    bg.solid()
    bg.fore_color.rgb = rgb(C["navy"] if dark else C["paper"])
    text(slide, section.upper(), 0.65, 0.36, 5.8, 0.25, 9.5,
         C["teal"] if dark else C["teal_dark"], True)
    text(slide, title_value, 0.65, 0.72, 12.0, 0.62, 28,
         C["white"] if dark else C["ink"], True, FONT_HEAD)
    if subtitle:
        text(slide, subtitle, 0.67, 1.34, 11.8, 0.42, 12.5,
             C["muted_dark"] if dark else C["muted"])
    footer_color = C["muted_dark"] if dark else C["muted"]
    text(slide, "METEOSTATION  /  ФІНАЛЬНИЙ ПРОЄКТ", 0.66, 7.15, 5.8, 0.18, 8.5, footer_color)
    text(slide, f"{number:02d}", 12.15, 7.08, 0.52, 0.25, 10, footer_color, True,
         FONT_MONO, PP_ALIGN.RIGHT)
    line(slide, 0.65, 6.99, 12.68, 6.99, C["line_dark"] if dark else C["line"], 0.8)
    return slide


def notes(slide, value):
    slide.notes_slide.notes_text_frame.text = value


def card_title(slide, title_value, x, y, w, accent=None, dark=False, size=16):
    if accent:
        rect(slide, x, y + 0.04, 0.06, 0.25, accent, False)
        x += 0.18
        w -= 0.18
    text(slide, title_value, x, y, w, 0.38, size,
         C["white"] if dark else C["ink"], True, FONT_HEAD)


# 01 — Title
slide = prs.slides.add_slide(prs.slide_layouts[6])
slide.background.fill.solid()
slide.background.fill.fore_color.rgb = rgb(C["navy"])
rect(slide, 0, 0, 0.14, H, C["teal"], False)
circle(slide, 9.35, -1.35, 5.4, C["navy2"])
circle(slide, 10.15, 5.9, 2.25, C["navy2"])
pill(slide, "ФІНАЛЬНИЙ ПРОЄКТ КУРСУ", 0.75, 0.62, 2.45, C["teal"], C["navy"], 10)
text(slide, "Автономна\nметеостанція", 0.75, 1.38, 6.35, 1.65, 36, C["white"], True, FONT_HEAD)
text(slide, "Від живлення та датчиків —\nдо надійної передачі даних", 0.79, 3.2, 5.2, 0.9, 18, C["muted_dark"])
line(slide, 0.79, 4.37, 4.82, 4.37, C["line_dark"], 1)
rich_text(slide, [("ESP32-C3", 14, True, C["white"]), ("   •   ", 14, False, C["teal"]),
                  ("LoRa / MQTT", 14, True, C["white"]), ("   •   ", 14, False, C["teal"]),
                  ("Solar / LiFePO₄", 14, True, C["white"])],
          0.79, 4.63, 5.9, 0.4)
text(slide, "KiCad  +  ESP-IDF  +  FreeRTOS  +  C11", 0.79, 5.27, 5.5, 0.3, 11, C["muted_dark"], False, FONT_MONO)
add_picture_contain(slide, ASSETS / "main_board_3d.png", 6.15, 1.08, 6.85, 5.35)
pill(slide, "2 ПЛАТИ", 7.0, 5.84, 1.05, C["navy2"], C["teal"], 9, C["line_dark"])
pill(slide, "2 ТРАНСПОРТИ", 8.18, 5.84, 1.55, C["navy2"], C["blue"], 9, C["line_dark"])
pill(slide, "DEEP SLEEP", 9.87, 5.84, 1.35, C["navy2"], C["lime"], 9, C["line_dark"])
text(slide, "Апаратна частина • прошивка • протоколи • енергозбереження", 0.79, 6.88, 8.6, 0.22, 9.5, C["muted_dark"])
text(slide, "01", 12.12, 6.83, 0.52, 0.25, 10, C["muted_dark"], True, FONT_MONO, PP_ALIGN.RIGHT)
notes(slide, "Вітаю! Мій фінальний проєкт — автономна метеостанція на ESP32-C3.\n"
             "Це не лише програма для мікроконтролера: розроблено дві друковані плати, прошивку та два канали передачі даних.\n"
             "Основна інженерна ідея — збирати метеодані й енергетичну телеметрію, миттєво реагувати на блискавку та проводити більшість часу в deep sleep.")

# 02 — Problem and objective
slide = base_slide(2, "01 / Постановка задачі", "Проблема → інженерна відповідь",
                   "Станція має працювати там, де немає постійного живлення та стабільної інфраструктури.")
problems = [
    ("01", "Віддалена точка", "Живлення від батареї та сонячної панелі; короткі активні цикли.", C["teal"]),
    ("02", "Гроза не чекає таймера", "Окреме пробудження через AS3935 і WAKE_IRQ.", C["amber"]),
    ("03", "Різні умови зв’язку", "Апаратний вибір LoRa або Wi-Fi + MQTT через J4.", C["blue"]),
]
for i, (num, title_value, body, accent) in enumerate(problems):
    y = 1.95 + i * 1.23
    rect(slide, 0.67, y, 7.65, 1.0, C["card"], True, C["line"])
    pill(slide, num, 0.91, y + 0.19, 0.52, accent, C["navy"], 10)
    text(slide, title_value, 1.65, y + 0.14, 2.65, 0.32, 16, C["ink"], True, FONT_HEAD)
    text(slide, body, 4.18, y + 0.14, 3.76, 0.58, 12.5, C["slate"])
rect(slide, 8.63, 1.95, 4.03, 3.46, C["navy"], True)
text(slide, "МЕТА ПРОЄКТУ", 8.97, 2.25, 3.3, 0.26, 10, C["teal"], True)
text(slide, "Створити автономний\nвузол моніторингу", 8.97, 2.78, 3.3, 0.9, 22, C["white"], True, FONT_HEAD)
text(slide, "який поєднує вимірювання,\nподієву обробку, передачу\nданих і контроль живлення.", 8.97, 3.88, 3.16, 0.98, 13.5, C["muted_dark"])
metrics = [("300 с", "інтервал за замовчуванням"), ("6", "пристроїв на I²C"),
           ("2", "друковані плати"), ("2", "транспорти")]
for i, (value, label) in enumerate(metrics):
    x = 0.67 + i * 3.03
    rect(slide, x, 5.76, 2.78, 0.92, C["teal_pale"] if i == 0 else C["card"], True, C["line"])
    text(slide, value, x + 0.18, 5.9, 0.82, 0.35, 20, C["teal_dark"], True, FONT_HEAD)
    text(slide, label, x + 0.98, 5.87, 1.62, 0.5, 10.5, C["slate"])
notes(slide, "Постановка задачі визначається трьома обмеженнями: автономне живлення, асинхронні грозові події та різні можливості каналу зв’язку.\n"
             "Тому архітектура не будується навколо постійно активного контролера. Натомість реалізовано цикл пробудження та два незалежні сценарії: періодична телеметрія і блискавка.\n"
             "Число 300 секунд — конфігурований інтервал за замовчуванням, а не результат вимірювання автономності.")

# 03 — System overview
slide = base_slide(3, "02 / Система", "Система в одному погляді",
                   "Один контролер об’єднує датчики, живлення, час і два канали передачі.", dark=True)
# Power rail
rect(slide, 0.72, 1.87, 2.2, 0.62, C["navy2"], True, C["line_dark"])
text(slide, "USB  /  SOLAR  /  LiFePO₄", 0.83, 2.05, 1.98, 0.22, 10.5, C["lime"], True, FONT_MONO, PP_ALIGN.CENTER)
line(slide, 2.92, 2.18, 4.18, 2.18, C["lime"], 1.6, arrow=True)
# Sensor block
rect(slide, 0.72, 2.83, 2.2, 2.43, C["navy2"], True, C["line_dark"])
text(slide, "ДАТЧИКИ", 0.95, 3.07, 1.75, 0.25, 10, C["teal"], True)
text(slide, "BME280\nAS3935\nINA226 ×2\nDS3231\nPCA9536", 0.95, 3.49, 1.75, 1.48, 14, C["white"], False, FONT_MONO)
line(slide, 2.92, 4.04, 4.18, 4.04, C["teal"], 2, arrow=True)
text(slide, "I²C 100 кГц", 3.02, 3.7, 1.05, 0.22, 9, C["muted_dark"], False, FONT_MONO, PP_ALIGN.CENTER)
# MCU block
rect(slide, 4.2, 2.3, 3.15, 3.28, C["teal"], True)
text(slide, "ESP32-C3", 4.5, 2.68, 2.55, 0.46, 25, C["navy"], True, FONT_HEAD, PP_ALIGN.CENTER)
text(slide, "app_flow\nsensor_manager\nlightning_service\ntelemetry_codec\npower_manager", 4.53, 3.36, 2.5, 1.55, 13.5, C["navy"], False, FONT_MONO, PP_ALIGN.CENTER)
pill(slide, "WAKE → WORK → SLEEP", 4.62, 5.02, 2.31, C["navy"], C["white"], 9)
line(slide, 7.35, 4.04, 8.35, 4.04, C["teal"], 2, arrow=True)
# Transport blocks
rect(slide, 8.38, 2.31, 1.72, 1.35, C["navy2"], True, C["blue"])
text(slide, "Wi-Fi", 8.58, 2.61, 1.32, 0.34, 19, C["white"], True, FONT_HEAD, PP_ALIGN.CENTER)
text(slide, "MQTT / JSON", 8.53, 3.12, 1.42, 0.22, 10, C["blue"], True, FONT_MONO, PP_ALIGN.CENTER)
rect(slide, 8.38, 4.18, 1.72, 1.35, C["navy2"], True, C["teal"])
text(slide, "LoRa", 8.58, 4.47, 1.32, 0.34, 19, C["white"], True, FONT_HEAD, PP_ALIGN.CENTER)
text(slide, "binary / TX", 8.53, 4.99, 1.42, 0.22, 10, C["teal"], True, FONT_MONO, PP_ALIGN.CENTER)
text(slide, "J4", 7.68, 3.69, 0.34, 0.24, 10, C["muted_dark"], True, FONT_MONO, PP_ALIGN.CENTER)
# Outputs
line(slide, 10.1, 2.98, 10.83, 2.98, C["blue"], 1.6, arrow=True)
line(slide, 10.1, 4.85, 10.83, 4.85, C["teal"], 1.6, arrow=True)
rect(slide, 10.86, 2.31, 1.75, 1.35, C["card"], True)
text(slide, "MQTT\nbroker", 11.05, 2.69, 1.37, 0.65, 16, C["ink"], True, FONT_HEAD, PP_ALIGN.CENTER)
rect(slide, 10.86, 4.18, 1.75, 1.35, C["card"], True)
text(slide, "LoRa\ngateway", 11.05, 4.56, 1.37, 0.65, 16, C["ink"], True, FONT_HEAD, PP_ALIGN.CENTER)
# Bottom principles
principles = [("01", "Один failed sensor\n≠ втрата всіх даних"),
              ("02", "ISR лише сигналізує\nзадачу"),
              ("03", "Транспорт не створює\npayload")]
for i, (num, body) in enumerate(principles):
    x = 0.72 + i * 4.0
    text(slide, num, x, 6.02, 0.4, 0.28, 12, C["teal"], True, FONT_MONO)
    text(slide, body, x + 0.5, 5.97, 3.1, 0.58, 11.5, C["muted_dark"])
notes(slide, "На цій діаграмі видно основні межі системи. Датчики підключені до спільної шини I²C, радіомодуль SX1262 — через SPI.\n"
             "Прошивка формує предметну модель даних і лише потім обирає кодек і транспорт. Це зменшує зв’язність: драйвер BME280 нічого не знає про MQTT чи LoRa.\n"
             "Живлення має три джерела: USB, Solar та 1S LiFePO4. J4 вибирає канал, але не вмикає чи вимикає Wi-Fi як окрему функцію.")

# 04 — Hardware
slide = base_slide(4, "03 / Апаратна частина", "Дві плати, один автономний вузол",
                   "Основна плата відповідає за керування, живлення й зв’язок; окрема плата — за середовище та грозові події.")
rect(slide, 0.67, 1.88, 6.42, 4.77, C["navy"], True)
add_picture_contain(slide, ASSETS / "main_board_3d.png", 0.86, 2.03, 6.04, 4.06)
pill(slide, "ОСНОВНА ПЛАТА • 3D-РЕНДЕР KiCad", 1.1, 6.11, 3.3, C["navy2"], C["muted_dark"], 8.5, C["line_dark"])
hardware_cards = [
    ("Керування та RF", "ESP32-C3-WROOM-02\nSX1262 / E22-900M22S\nSPI + DIO2 RF-switch", C["teal"]),
    ("Живлення", "1S LiFePO₄ • USB • Solar\nTP5000 — зарядка\nTPS2121 — вибір джерела", C["lime"]),
    ("Плата датчиків", "BME280 — T / P / RH\nAS3935 — блискавки\nWAKE_IRQ → GPIO1", C["blue"]),
]
for i, (title_value, body, accent) in enumerate(hardware_cards):
    y = 1.88 + i * 1.48
    rect(slide, 7.37, y, 5.28, 1.26, C["card"], True, C["line"])
    rect(slide, 7.37, y, 0.09, 1.26, accent, False)
    text(slide, title_value, 7.7, y + 0.16, 2.05, 0.31, 15, C["ink"], True, FONT_HEAD)
    text(slide, body, 9.66, y + 0.13, 2.66, 0.86, 11.5, C["slate"], False, FONT_MONO)
rect(slide, 7.37, 6.34, 5.28, 0.31, C["teal_pale"], True)
text(slide, "DS3231 + CR2032 зберігають час між циклами", 7.55, 6.39, 4.9, 0.2, 10.5, C["teal_dark"], True)
notes(slide, "Апаратна частина розділена на основну плату та плату датчиків. Основна плата містить ESP32-C3, LoRa-модуль, RTC, вимірювання живлення та ланцюг TP5000 + TPS2121.\n"
             "Плата датчиків виносить BME280 і AS3935. Сигнал AS3935 підключений до GPIO1 і використовується як джерело пробудження.\n"
             "На слайді показано реальний 3D-рендер основної плати з поточного KiCad-проєкту, а не концептуальну ілюстрацію.")

# 05 — Sensors
slide = base_slide(5, "03 / Апаратна частина", "Що саме вимірює станція",
                   "Метеодані доповнені власною енергетичною телеметрією та джерелом точного часу.")
# Main environmental card
rect(slide, 0.67, 1.9, 4.18, 2.02, C["navy"], True)
pill(slide, "BME280", 0.96, 2.17, 0.98, C["teal"], C["navy"], 10)
text(slide, "Середовище", 0.96, 2.68, 2.65, 0.38, 20, C["white"], True, FONT_HEAD)
for i, (v, l) in enumerate([("°C", "температура"), ("Pa", "тиск"), ("%RH", "вологість")]):
    x = 0.96 + i * 1.18
    text(slide, v, x, 3.19, 1.0, 0.3, 17, C["teal"], True, FONT_MONO)
    text(slide, l, x, 3.54, 1.1, 0.2, 8.5, C["muted_dark"])
# Other cards
sensor_cards = [
    (5.08, 1.9, 3.55, 2.02, "AS3935", "Грозова подія", "distance_km  +  energy\nФільтрація non-lightning IRQ", C["amber"]),
    (8.86, 1.9, 3.79, 2.02, "INA226 ×2", "Енергетика", "Battery:  U / I / P\nSolar:      U / I / P", C["lime"]),
    (0.67, 4.18, 4.18, 1.54, "DS3231", "Час", "Unix timestamp\nCR2032 backup", C["blue"]),
    (5.08, 4.18, 3.55, 1.54, "PCA9536", "Стан плати", "J4  •  CHRG  •  STDBY", C["teal"]),
    (8.86, 4.18, 3.79, 1.54, "ESP32-C3", "Діагностика", "boot count  •  reset / wake reason\nfailure counters", C["blue"]),
]
for x, y, w, h, chip, title_value, body, accent in sensor_cards:
    rect(slide, x, y, w, h, C["card"], True, C["line"])
    pill(slide, chip, x + 0.24, y + 0.22, 1.12 if chip != "INA226 ×2" else 1.32, accent, C["navy"], 9)
    text(slide, title_value, x + 0.24, y + 0.68, w - 0.48, 0.3, 16, C["ink"], True, FONT_HEAD)
    text(slide, body, x + 0.24, y + 1.07, w - 0.48, h - 1.14, 11.2, C["slate"], False, FONT_MONO)
# I2C rail
rect(slide, 0.67, 5.99, 11.98, 0.67, C["card"], True, C["line"])
text(slide, "I²C", 0.93, 6.2, 0.46, 0.24, 13, C["teal_dark"], True, FONT_MONO)
addresses = [("0x03", "AS3935"), ("0x40", "Solar"), ("0x41", "PCA9536"),
             ("0x44", "Battery"), ("0x68", "DS3231"), ("0x76", "BME280")]
for i, (addr, dev) in enumerate(addresses):
    x = 1.62 + i * 1.78
    rich_text(slide, [(addr, 11.5, True, C["ink"], FONT_MONO), ("  " + dev, 10, False, C["muted"], FONT_BODY)],
              x, 6.17, 1.66, 0.28)
notes(slide, "BME280 дає три базові метеопараметри. AS3935 формує подію з відстанню та, за наявності, енергією.\n"
             "Два INA226 окремо вимірюють напругу, струм і потужність батареї та сонячного входу. Це дозволяє передавати не лише погоду, а й стан самої автономної системи.\n"
             "DS3231 дає timestamp, PCA9536 зчитує J4 і стани зарядки. Усі шість пристроїв працюють на одному I²C-bus за адресами, наведеними внизу.")

# 06 — Firmware architecture
slide = base_slide(6, "04 / Прошивка", "Компонентна архітектура ESP-IDF",
                   "Чіткі межі між application flow, сервісами, драйверами та транспортами.", dark=True)
# Layer diagram
layers = [
    (1.92, "APPLICATION", "app_main   •   app   •   app_config   •   app_flow", C["teal"]),
    (2.91, "SERVICES", "sensor_manager   •   lightning_service   •   power_manager", C["navy2"]),
    (3.90, "DATA & CONNECTIVITY", "telemetry/codecs  •  transport factory  •  MQTT  •  LoRa", C["navy2"]),
    (4.89, "DRIVERS & BOARD", "BME280   AS3935   INA226   DS3231   PCA9536   SX1262   board", C["navy2"]),
]
for y, label, body, fill in layers:
    rect(slide, 0.72, y, 8.05, 0.79, fill, True, C["line_dark"] if fill != C["teal"] else None)
    text(slide, label, 0.98, y + 0.16, 1.8, 0.24, 9.5, C["navy"] if fill == C["teal"] else C["teal"], True)
    text(slide, body, 2.73, y + 0.16, 5.76, 0.4, 11.3,
         C["navy"] if fill == C["teal"] else C["white"], False, FONT_MONO)
rect(slide, 0.72, 5.88, 8.05, 0.54, C["navy2"], True, C["line_dark"])
text(slide, "ESP-IDF 6.0.1    •    FreeRTOS    •    C11+    •    NVS / BLE / Wi-Fi", 0.98, 6.04, 7.53, 0.23, 11, C["muted_dark"], False, FONT_MONO)
# Side principles
rect(slide, 9.08, 1.92, 3.54, 4.5, C["card"], True)
text(slide, "Ключові рішення", 9.4, 2.23, 2.92, 0.38, 18, C["ink"], True, FONT_HEAD)
bullets(slide, [
    "board — єдине джерело GPIO та I²C-адрес",
    "codecs відокремлені від transports",
    "один спільний I²C master bus",
    "bounded buffers і finite timeouts",
    "мінімальна динамічна алокація після init",
], 9.4, 2.9, 2.85, 2.65, 12, C["slate"], C["teal_dark"], 11)
pill(slide, "PURE C: app_flow + telemetry, без HAL", 9.39, 5.77, 2.91, C["teal_pale"], C["teal_dark"], 8.5)
notes(slide, "Прошивка організована як набір ESP-IDF components. app_main мінімальний; app.c є composition root.\n"
             "app_flow та telemetry — чисті модулі, які не залежать від периферії та HAL. Драйвери відповідають лише за пристрої, board ізолює фізичний pinout.\n"
             "Таке розділення спрощує перевірку та зміни: наприклад, формат JSON не проникає в sensor driver, а номер GPIO не дублюється по проєкту.")

# 07 — Concurrency and synchronization
slide = base_slide(7, "05 / Архітектурні рішення", "Concurrency: мінімум задач, явна синхронізація",
                   "Асинхронність ізольована на межах IRQ і мережі; application flow лишається детермінованим.")
sync_rows = [
    ("GPIO IRQ", "AS3935 → WAKE_IRQ", "vTaskNotifyGiveFromISR", "main task", C["amber"]),
    ("NETWORK", "Wi-Fi / MQTT callback", "xEventGroupSetBits", "sync wait", C["blue"]),
    ("RADIO IRQ", "SX1262 DIO1", "binary semaphore", "timed TX", C["teal"]),
]
for i, (domain, source, primitive, target, accent) in enumerate(sync_rows):
    y = 1.95 + i * 1.28
    rect(slide, 0.67, y, 7.8, 1.05, C["card"], True, C["line"])
    pill(slide, domain, 0.91, y + 0.18, 1.08, accent, C["navy"], 9)
    text(slide, source, 2.18, y + 0.19, 2.25, 0.27, 12.2, C["slate"], True)
    line(slide, 4.44, y + 0.52, 4.8, y + 0.52, accent, 1.3, arrow=True)
    text(slide, primitive, 4.92, y + 0.19, 2.35, 0.27, 11.2, C["ink"], True, FONT_MONO)
    line(slide, 7.31, y + 0.52, 7.61, y + 0.52, accent, 1.3, arrow=True)
    text(slide, target, 7.66, y + 0.19, 0.74, 0.27, 9.5, C["teal_dark"], True, FONT_BODY, PP_ALIGN.CENTER)
    text(slide, "task context", 7.66, y + 0.55, 0.74, 0.18, 7.2, C["muted"], False, FONT_MONO, PP_ALIGN.CENTER)
rect(slide, 8.72, 1.95, 3.93, 4.61, C["navy"], True)
pill(slide, "ЧОМУ НЕ xQueue?", 9.03, 2.24, 1.58, C["teal"], C["navy"], 9)
text(slide, "Тому що IRQ —\nце сигнал, а не дані", 9.03, 2.82, 3.28, 0.74, 18, C["white"], True, FONT_HEAD)
bullets(slide, [
    "повідомлення не містить payload",
    "AS3935 registers читаються пізніше через I²C",
    "один producer і один main-task consumer",
    "notification allocation-free і не має queue overflow",
], 9.03, 3.78, 3.14, 2.08, 11.5, C["muted_dark"], C["teal"], 9)
rect(slide, 0.67, 6.05, 7.8, 0.5, C["teal_pale"], True)
text(slide, "Один application task володіє lifecycle: sensor flow, TX і sleep не перетинаються між задачами.", 0.92, 6.2, 7.28, 0.2, 10.3, C["teal_dark"], True)
notes(slide, "Ключове рішення — не розподіляти сенсори по окремих FreeRTOS tasks. Є один application task, який володіє послідовністю вимірювання, передачі та сну.\n"
             "Асинхронність потрібна лише на межах: GPIO ISR надсилає direct task notification, Wi-Fi і MQTT callbacks переводяться в EventGroup, SX1262 DIO1 — у binary semaphore. SPI queue_size=1 — це налаштування драйвера, не application queue.\n"
             "xQueue не використовується для WAKE_IRQ, бо подія не несе payload. Фактичні дані лишаються в регістрах AS3935 і зчитуються вже у task context; це зменшує ISR, heap і клас помилок переповнення черги.")

# 08 — Message queue trade-off
slide = base_slide(8, "05 / Архітектурні рішення", "Persistent queue: свідомо не в V1",
                   "Помилка передачі не блокує пристрій і не перетворюється на необмежений backlog.", dark=True)
flow = [("SAMPLE", "1–2 повідомлення"), ("ENCODE", "static buffers"),
        ("TRANSMIT", "finite timeout"), ("RESULT", "diagnostics"), ("SLEEP", "fresh next wake")]
for i, (title_value, sub) in enumerate(flow):
    x = 0.72 + i * 2.42
    circle(slide, x + 0.54, 2.02, 0.6, C["teal"] if i < 3 else C["navy2"], C["line_dark"] if i >= 3 else None)
    text(slide, f"{i+1}", x + 0.54, 2.17, 0.6, 0.22, 10, C["navy"] if i < 3 else C["teal"], True, FONT_MONO, PP_ALIGN.CENTER)
    text(slide, title_value, x - 0.03, 2.78, 1.74, 0.25, 12.5, C["white"], True, FONT_HEAD, PP_ALIGN.CENTER)
    text(slide, sub, x - 0.15, 3.13, 1.98, 0.22, 9.5, C["muted_dark"], False, FONT_MONO, PP_ALIGN.CENTER)
    if i < 4:
        line(slide, x + 1.16, 2.32, x + 2.37, 2.32, C["line_dark"], 1.2, arrow=True)
queue_cards = [
    ("ЩО ВИГРАЄМО", "без flash wear\nбез replay / ordering\nbounded RAM\nменший failure surface", C["teal"]),
    ("ЯКА ЦІНА", "мережева відмова втрачає поточний sample\nісторія між циклами не зберігається", C["amber"]),
    ("КОЛИ ДОДАВАТИ", "лише за результатами польових даних\nяк bounded ring buffer", C["blue"]),
]
for i, (title_value, body, accent) in enumerate(queue_cards):
    x = 0.72 + i * 4.02
    rect(slide, x, 3.88, 3.75, 2.05, C["navy2"], True, C["line_dark"])
    rect(slide, x, 3.88, 0.08, 2.05, accent, False)
    text(slide, title_value, x + 0.3, 4.15, 3.08, 0.24, 10, accent, True, FONT_MONO)
    text(slide, body, x + 0.3, 4.62, 3.06, 1.05, 11.3, C["white"])
rect(slide, 0.72, 6.15, 11.85, 0.32, C["navy2"], True, C["line_dark"])
text(slide, "MQTT QoS 1 чекає broker ACK; LoRa TX-only — без ACK. У обох випадках failed message не зберігається в persistent queue.",
     0.94, 6.21, 11.4, 0.2, 10, C["muted_dark"], True, FONT_MONO)
notes(slide, "На щіральному слайді я показую не функціональну відсутність, а архітектурний trade-off. Поточний цикл формує максимум boot + telemetry або одну lightning-подію і одразу передає її.\n"
             "Якщо передача не вдалася, діагностичний лічильник збільшується, активне вікно завершується, пристрій повертається до сну. Це дає bounded memory і не записує кожен цикл у flash.\n"
             "Ціна — можлива втрата поточного sample під час outage. Persistent queue можна додати лише як bounded ring buffer після польових даних, з окремою перевіркою зносу пам’яті, corruption, ordering і replay. FRAM не входить у V1.")

# 09 — Power lifecycle
slide = base_slide(9, "06 / Автономність", "Deep sleep — основний стан системи",
                   "Активний цикл короткий; периферія централізовано переводиться в low-power перед сном.")
steps = [("01", "WAKE", "timer / IRQ"), ("02", "REASON", "wake routing"),
         ("03", "READ", "лише потрібне"), ("04", "ENCODE", "JSON / binary"),
         ("05", "TX", "MQTT / LoRa"), ("06", "SLEEP", "low-power")]
for i, (num, label, sub) in enumerate(steps):
    x = 0.67 + i * 2.0
    circle(slide, x + 0.47, 2.02, 0.58, C["teal"] if i < 5 else C["navy"])
    text(slide, num, x + 0.47, 2.17, 0.58, 0.22, 10, C["navy"] if i < 5 else C["white"], True, FONT_MONO, PP_ALIGN.CENTER)
    text(slide, label, x + 0.05, 2.78, 1.42, 0.28, 14, C["ink"], True, FONT_HEAD, PP_ALIGN.CENTER)
    text(slide, sub, x - 0.03, 3.15, 1.58, 0.24, 10, C["muted"], False, FONT_MONO, PP_ALIGN.CENTER)
    if i < 5:
        line(slide, x + 1.09, 2.31, x + 1.97, 2.31, C["teal_dark"], 1.4, arrow=True)
# Timeline
rect(slide, 0.67, 3.76, 7.62, 2.27, C["card"], True, C["line"])
text(slide, "BATTERY / SOLAR", 0.96, 4.02, 2.5, 0.25, 10, C["teal_dark"], True)
text(slide, "300 с", 6.75, 4.01, 1.16, 0.3, 16, C["ink"], True, FONT_HEAD, PP_ALIGN.RIGHT)
text(slide, "80 мА  /  5–7 с", 0.96, 4.31, 1.9, 0.2, 9.5, C["teal_dark"], True, FONT_MONO)
text(slide, "405 мкА", 4.55, 4.31, 1.4, 0.2, 9.5, C["muted"], True, FONT_MONO)
rect(slide, 0.96, 4.59, 1.18, 0.34, C["teal"], True)
text(slide, "ACTIVE", 0.96, 4.65, 1.18, 0.18, 9, C["navy"], True, FONT_MONO, PP_ALIGN.CENTER)
rect(slide, 2.14, 4.59, 5.46, 0.34, C["navy"], True)
text(slide, "DEEP SLEEP  +  timer wake  +  WAKE_IRQ", 2.14, 4.65, 5.46, 0.18, 9, C["white"], True, FONT_MONO, PP_ALIGN.CENTER)
text(slide, "INA226 power-down  •  BME280 sleep  •  SX1262 SetSleep", 0.96, 5.26, 6.91, 0.28, 11.2, C["slate"], False, FONT_MONO)
text(slide, "Wi-Fi / BLE не запускаються в LoRa mode", 0.96, 5.64, 6.7, 0.25, 11, C["teal_dark"], True)
# USB mode
rect(slide, 8.57, 3.76, 4.08, 2.27, C["navy"], True)
pill(slide, "USB DETECTED", 8.88, 4.05, 1.48, C["blue"], C["navy"], 9)
text(slide, "Continuous mode", 8.88, 4.57, 3.45, 0.36, 20, C["white"], True, FONT_HEAD)
text(slide, "Таймерні цикли без deep sleep\nGPIO ISR → task notification\nUSB живий для flash / monitor", 8.88, 5.07, 3.38, 0.76, 11.4, C["muted_dark"])
# Measured consumption
rect(slide, 0.67, 6.24, 11.98, 0.46, C["teal_pale"], True)
rich_text(slide, [("ВИМІРЯНО НА ПЛАТІ:  ", 10.5, True, C["teal_dark"], FONT_MONO),
                  ("активний цикл 80 мА × 5–7 с   •   deep sleep 405 мкА   •   середнє ≈ 1,2 мА у циклі 10 хв", 10.5, True, C["slate"], FONT_MONO)],
          0.92, 6.36, 11.5, 0.22)
notes(slide, "Енергозбереження реалізоване як сквозна властивість, а не окремий виклик esp_deep_sleep_start.\n"
             "Перед сном prepare_sleep послідовно зупиняє мережеві функції, вимикає INA226, переводить BME280 і SX1262 у сон, налаштовує timer та GPIO wake.\n"
             "Виміряне споживання всієї плати: 80 мА під час активного циклу тривалістю 5–7 секунд і 405 мкА у deep sleep. За штатного інтервалу 10 хвилин це дає середній струм близько 1,2 мА; основний внесок у циклі — активна фаза, а у добовому балансі — струм сну.\n"
             "405 мкА значно перевищує deep sleep самого ESP32-C3 (одиниці мкА), отже головний резерв оптимізації — периферія та ланцюги живлення плати.\n"
             "Для USB передбачено continuous mode: зручно налагоджувати плату, моніторити лог і працювати з provisioning без автоматичного засинання.")

# 08 — Lightning
slide = base_slide(8, "06 / Подієва обробка", "Блискавка: окремий швидкий сценарій",
                   "Подія не чекає наступного періодичного вимірювання.", dark=True)
# Lightning path
path_steps = [
    ("AS3935", "IRQ", C["amber"]),
    ("GPIO1", "WAKE_IRQ", C["amber"]),
    ("SERVICE", "reason?", C["teal"]),
    ("CODEC", "distance + energy", C["teal"]),
    ("TX", "MQTT / LoRa", C["blue"]),
]
for i, (top, bottom, accent) in enumerate(path_steps):
    x = 0.72 + i * 2.47
    rect(slide, x, 2.04, 1.92, 1.24, C["navy2"], True, accent)
    text(slide, top, x + 0.14, 2.32, 1.64, 0.25, 12, accent, True, FONT_MONO, PP_ALIGN.CENTER)
    text(slide, bottom, x + 0.1, 2.72, 1.72, 0.27, 12.5, C["white"], True, FONT_HEAD, PP_ALIGN.CENTER)
    if i < 4:
        line(slide, x + 1.92, 2.66, x + 2.39, 2.66, C["muted_dark"], 1.3, arrow=True)
# Decision gate
rect(slide, 0.72, 3.78, 7.63, 2.35, C["card"], True)
text(slide, "ВАЛІДАЦІЯ ПЕРЕРИВАННЯ", 1.02, 4.07, 3.35, 0.25, 10, C["teal_dark"], True)
text(slide, "reason == lightning?", 1.02, 4.53, 3.4, 0.4, 20, C["ink"], True, FONT_MONO)
pill(slide, "ТАК → ПОДІЯ", 4.76, 4.51, 1.35, C["teal"], C["navy"], 9.5)
pill(slide, "НІ → БЕЗ TX", 6.25, 4.51, 1.48, C["amber_pale"], C["slate"], 9.5)
text(slide, "Disturber / noise не створюють хибну подію блискавки.", 1.02, 5.37, 6.78, 0.31, 12.5, C["slate"])
# Minimal path card
rect(slide, 8.64, 3.78, 3.98, 2.35, C["navy2"], True, C["line_dark"])
text(slide, "МІНІМАЛЬНИЙ ШЛЯХ", 8.96, 4.07, 3.25, 0.25, 10, C["teal"], True)
text(slide, "Читаємо:", 8.96, 4.53, 1.0, 0.25, 12, C["muted_dark"])
text(slide, "RTC + AS3935", 10.03, 4.53, 2.1, 0.25, 12, C["white"], True, FONT_MONO)
text(slide, "Не читаємо:", 8.96, 5.04, 1.32, 0.25, 12, C["muted_dark"])
text(slide, "BME280 / INA226", 10.28, 5.04, 2.02, 0.25, 12, C["white"], True, FONT_MONO)
text(slide, "Після TX → одразу low-power → sleep", 8.96, 5.6, 3.23, 0.26, 11.2, C["teal"])
notes(slide, "Блискавка є другим, незалежним сценарієм. WAKE_IRQ будить контролер, після чого lightning_service обов’язково зчитує причину переривання AS3935.\n"
             "Якщо причина не lightning, жодна подія не передається. Якщо подія підтверджена, надсилаються timestamp, distance та energy за наявності.\n"
             "У цьому циклі навмисно не зчитуються BME280 і INA226: коротший шлях зменшує затримку й активний час.")

# 09 — Transports
slide = base_slide(9, "07 / Зв’язок", "J4 — явний вибір транспорту",
                   "Немає прихованого automatic fallback: режим визначає апаратний перемикач.")
# LoRa
rect(slide, 0.67, 1.92, 5.77, 4.53, C["navy"], True)
pill(slide, "J4 OPEN / HIGH", 0.99, 2.22, 1.6, C["teal"], C["navy"], 10)
text(slide, "LoRa", 0.99, 2.82, 2.0, 0.5, 29, C["white"], True, FONT_HEAD)
text(slide, "Енергоощадний default", 0.99, 3.39, 3.8, 0.3, 14, C["teal"], True)
bullets(slide, ["SX1262 / E22-900M22S", "868.1 MHz • SF7 • BW 125 kHz", "14 dBm • TX timeout 5 s", "TX-only: без RX / ACK / retries", "binary little-endian, versioned"],
        0.99, 3.96, 4.92, 1.85, 12.5, C["muted_dark"], C["teal"], 8, FONT_BODY)
pill(slide, "Wi-Fi / BLE НЕ ЗАПУСКАЮТЬСЯ", 0.99, 5.92, 2.75, C["navy2"], C["teal"], 9, C["line_dark"])
# WiFi
rect(slide, 6.75, 1.92, 5.9, 4.53, C["card"], True, C["line"])
pill(slide, "J4 CLOSED / LOW", 7.07, 2.22, 1.75, C["blue"], C["navy"], 10)
text(slide, "Wi-Fi + MQTT", 7.07, 2.82, 4.2, 0.5, 29, C["ink"], True, FONT_HEAD)
text(slide, "Зручна інтеграція з інфраструктурою", 7.07, 3.39, 4.85, 0.3, 14, C["teal_dark"], True)
bullets(slide, ["JSON • QoS 1 за замовчуванням", "mqtts:// + certificate verification", "BLE provisioning без credentials", "SNTP лише за невалідного RTC", "SX1262 одразу переводиться в sleep"],
        7.07, 3.96, 4.95, 1.85, 12.5, C["slate"], C["blue"], 8, FONT_BODY)
pill(slide, "3 TOPICS: telemetry / events / status", 7.07, 5.92, 3.1, C["blue_pale"], C["slate"], 9)
notes(slide, "J4 — це свідомий вибір архітектури зв’язку. Розімкнений J4 відповідає LoRa і є безпечним за замовчуванням варіантом: Wi-Fi і BLE узагалі не ініціалізуються.\n"
             "Замкнений J4 вмикає сценарій Wi-Fi + MQTT. Якщо credentials відсутні, запускається BLE provisioning. LoRa працює TX-only — це не LoRaWAN і немає ACK чи retries.\n"
             "Автоматичний fallback Wi-Fi до LoRa відсутній, тож поведінка пристрою передбачувана.")

# 10 — Protocols
slide = base_slide(10, "08 / Дані", "Два формати — одна предметна модель",
                   "telemetry_codec формує payload; transports лише передають готовий буфер.", dark=True)
# JSON card
rect(slide, 0.72, 1.93, 5.1, 4.52, C["card"], True)
pill(slide, "MQTT / JSON", 1.02, 2.2, 1.42, C["blue"], C["navy"], 9.5)
text(slide, "Людиночитаний формат", 1.02, 2.74, 3.98, 0.3, 16, C["ink"], True, FONT_HEAD)
json_lines = [
    '{',
    '  "schema": 1,',
    '  "device_id": "meteo-a17c42",',
    '  "timestamp": 1735689600,',
    '  "environment": {',
    '    "temperature_c": 21.54,',
    '    "humidity_percent": 48.26,',
    '    "pressure_pa": 100812',
    '  },',
    '  "transport": "lora"',
    '}',
]
text(slide, "\n".join(json_lines), 1.02, 3.2, 4.42, 2.46, 11.2, C["slate"], False, FONT_MONO)
text(slide, "Фрагмент telemetry; battery / solar додаються за валідності.", 1.02, 5.92, 4.42, 0.26, 9.5, C["muted"], italic=True)
# Binary card
rect(slide, 6.12, 1.93, 6.5, 4.52, C["navy2"], True, C["line_dark"])
pill(slide, "LORA / BINARY", 6.44, 2.2, 1.5, C["teal"], C["navy"], 9.5)
text(slide, "Компактний versioned frame", 6.44, 2.74, 5.6, 0.3, 16, C["white"], True, FONT_HEAD)
fields = [("0", "ver", 0.65, C["teal"]), ("1", "type", 0.65, C["teal"]),
          ("2..5", "device", 1.05, C["blue"]), ("6..9", "time", 1.05, C["blue"]),
          ("10", "flags", 0.85, C["amber"]), ("…", "valid data", 1.25, C["lime"])]
x = 6.44
for idx, label, w, col in fields:
    rect(slide, x, 3.36, w, 0.78, col, False)
    text(slide, idx, x + 0.03, 3.45, w - 0.06, 0.18, 8.5, C["navy"], True, FONT_MONO, PP_ALIGN.CENTER)
    text(slide, label, x + 0.03, 3.74, w - 0.06, 0.2, 9, C["navy"], True, FONT_MONO, PP_ALIGN.CENTER)
    x += w + 0.06
text(slide, "little-endian  •  protocol_version = 1", 6.44, 4.41, 5.55, 0.28, 11.5, C["muted_dark"], False, FONT_MONO)
sizes = [("39 B", "telemetry max"), ("12/16 B", "lightning"), ("15 B", "boot")]
for i, (value, label) in enumerate(sizes):
    x = 6.44 + i * 1.83
    text(slide, value, x, 5.03, 1.58, 0.34, 20, C["teal"] if i == 0 else C["white"], True, FONT_HEAD)
    text(slide, label, x, 5.46, 1.58, 0.22, 10, C["muted_dark"])
text(slide, "Flags визначають наявність env / battery / solar.\nНевалідний датчик не робить увесь кадр непридатним.", 6.44, 5.88, 5.5, 0.46, 10.5, C["muted_dark"])
notes(slide, "Обидва транспорти використовують спільну предметну модель, але різні кодеки. MQTT отримує JSON, LoRa — компактний binary frame.\n"
             "У LoRa-заголовку є версія протоколу, тип повідомлення, hash пристрою, timestamp і flags. Поля даних присутні лише за відповідних validity bits.\n"
             "Максимальний telemetry frame — 39 байт; lightning — 12 або 16; boot — 15. JSON на слайді є ілюстративним фрагментом, не точним шаблоном усіх полів.")

# 11 — Reliability & security
slide = base_slide(11, "09 / Якість рішення", "Надійність і безпека закладені в потік",
                   "Система розрахована на відмови окремих датчиків, мережеві затримки та безпечне первинне налаштування.")
items = [
    ("01", "Partial telemetry", "Один failed sensor не блокує решту; validity flags зберігають межі достовірності.", C["teal"]),
    ("02", "Finite timeouts", "I²C, Wi-Fi, MQTT, BUSY і LoRa TX мають межі; немає нескінченних retries.", C["blue"]),
    ("03", "Power invariants", "Перед сном централізовано вимикаються Wi-Fi / BLE та периферія.", C["lime"]),
    ("04", "MQTT over TLS", "Production — mqtts://; skip certificate verification лише як explicit dev-flag.", C["blue"]),
    ("05", "BLE provisioning", "Per-device PoP із factory MAC; credentials у NVS; секрети не логуються.", C["teal"]),
    ("06", "Захист від збоїв", "Watchdog і brownout detector enabled; diagnostics рахують помилки.", C["amber"]),
]
for i, (num, title_value, body, accent) in enumerate(items):
    col, row = i % 3, i // 3
    x, y = 0.67 + col * 4.08, 1.94 + row * 2.25
    rect(slide, x, y, 3.82, 1.98, C["card"], True, C["line"])
    pill(slide, num, x + 0.24, y + 0.24, 0.5, accent, C["navy"], 9)
    text(slide, title_value, x + 0.9, y + 0.25, 2.56, 0.3, 15.5, C["ink"], True, FONT_HEAD)
    text(slide, body, x + 0.24, y + 0.82, 3.33, 0.85, 11.4, C["slate"])
notes(slide, "Надійність проявляється насамперед у graceful degradation: частина телеметрії може бути валідною навіть за відмови одного датчика.\n"
             "Усі зовнішні операції обмежені часом, Wi-Fi має максимум три спроби підключення, після чого пристрій повертається до сну.\n"
             "Безпека: production MQTT використовує TLS з перевіркою сертифіката; BLE provisioning має унікальний per-device PoP. Паролі та PoP не повинні потрапляти в логи. Watchdog і brownout залишаються увімкненими.")

# 12 — Results and boundaries
slide = base_slide(12, "10 / Підсумок", "Результат і межі поточної версії",
                   "Завершений інженерний прототип V1 — з чесно визначеним обсягом та подальшими кроками.")
rect(slide, 0.67, 1.93, 5.85, 4.65, C["navy"], True)
pill(slide, "РЕАЛІЗОВАНО", 0.99, 2.23, 1.45, C["teal"], C["navy"], 9.5)
bullets(slide, [
    "Дві KiCad-плати та fabrication outputs",
    "ESP-IDF firmware з компонентною структурою",
    "Періодична telemetry + lightning events",
    "LoRa binary та Wi-Fi + MQTT JSON",
    "Deep sleep, USB continuous, BLE provisioning",
], 0.99, 2.9, 5.05, 2.95, 12.2, C["white"], C["teal"], 11)
rect(slide, 6.81, 1.93, 5.84, 2.16, C["card"], True, C["line"])
pill(slide, "МЕЖІ V1", 7.13, 2.23, 1.05, C["amber"], C["navy"], 9.5)
text(slide, "Немає backend / web UI / OTA\nLoRa без RX, ACK, retries і LoRaWAN\nНемає persistent telemetry queue", 7.13, 2.87, 4.96, 0.94, 12.2, C["slate"])
rect(slide, 6.81, 4.36, 5.84, 2.22, C["teal_pale"], True, C["line"])
pill(slide, "ДАЛІ", 7.13, 4.66, 0.65, C["card"], C["teal_dark"], 9.5, C["line"])
text(slide, "Польові випробування; зниження sleep-струму 405 мкА\nВізуалізація даних у зовнішньому backend\nОцінка потреби в queue / OTA за результатами", 7.13, 5.27, 4.98, 0.96, 12.2, C["slate"])
notes(slide, "Результат — цілісний інженерний прототип: hardware, firmware, протоколи, provisioning та енергозбереження.\n"
             "При цьому V1 свідомо не містить backend, web UI, OTA, LoRaWAN чи persistent queue. Це не приховані недоробки, а зафіксовані межі обсягу.\n"
             "Наступний практичний крок — польові випробування та зниження струму сну: виміряні 405 мкА помітно перевищують deep sleep самого ESP32-C3, тож резерв автономності — у периферії та живленні. Саме ці дані мають визначити, чи потрібні черга повідомлень, OTA або інші розширення.")

# 15 — Closing
slide = prs.slides.add_slide(prs.slide_layouts[6])
slide.background.fill.solid()
slide.background.fill.fore_color.rgb = rgb(C["navy"])
rect(slide, 0, 0, W, 0.12, C["teal"], False)
pill(slide, "ФІНАЛЬНИЙ ПРОЄКТ", 0.75, 0.66, 1.85, C["navy2"], C["teal"], 9.5, C["line_dark"])
text(slide, "Дякую за увагу", 0.75, 1.44, 7.25, 0.72, 36, C["white"], True, FONT_HEAD)
text(slide, "Питання?", 0.75, 2.3, 4.0, 0.55, 26, C["teal"], True, FONT_HEAD)
text(slide, "Автономна метеостанція поєднує", 0.79, 3.22, 5.35, 0.36, 16, C["muted_dark"])
closing = [("HARDWARE", "2 плати • power • sensors"), ("FIRMWARE", "C11 • ESP-IDF • FreeRTOS"),
           ("DATA", "LoRa binary • MQTT JSON"), ("QUALITY", "partial telemetry • finite timeouts")]
for i, (title_value, body) in enumerate(closing):
    x = 0.75 + (i % 2) * 3.15
    y = 3.86 + (i // 2) * 1.1
    rect(slide, x, y, 2.89, 0.88, C["navy2"], True, C["line_dark"])
    text(slide, title_value, x + 0.19, y + 0.14, 2.5, 0.2, 9, C["teal"], True, FONT_MONO)
    text(slide, body, x + 0.19, y + 0.47, 2.5, 0.22, 10.5, C["white"])
add_picture_contain(slide, ASSETS / "main_board_3d.png", 7.02, 1.35, 5.65, 4.85)
text(slide, "wake  →  measure  →  transmit  →  sleep", 7.35, 6.21, 4.95, 0.27, 12, C["muted_dark"], False, FONT_MONO, PP_ALIGN.CENTER)
line(slide, 0.75, 6.85, 12.63, 6.85, C["line_dark"], 0.8)
text(slide, "ESP32-C3  /  KiCad  /  ESP-IDF  /  LoRa  /  MQTT", 0.75, 7.04, 8.0, 0.2, 9, C["muted_dark"], False, FONT_MONO)
text(slide, f"{len(prs.slides):02d}", 12.12, 6.99, 0.52, 0.25, 10, C["muted_dark"], True, FONT_MONO, PP_ALIGN.RIGHT)
notes(slide, "Дякую за увагу!\n"
             "Головний результат проєкту — не окремий датчик чи бібліотека, а завершений ланцюжок: живлення → вимірювання → обробка → передача → сон.\n"
             "Готовий відповісти на питання про схемотехніку, структуру прошивки, формати даних та енергозбереження.")

# Remove default first slide if a template ever adds one; save deliverable.
prs.save(OUT)
print(f"Created {OUT} with {len(prs.slides)} slides")
