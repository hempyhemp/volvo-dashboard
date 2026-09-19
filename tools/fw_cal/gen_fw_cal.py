# -*- coding: utf-8 -*-
"""Генератор include/firmware_cal.h из прошивки ЭБУ.

ЗАЧЕМ
-----
Дашборд считает давление, наддув и расход по числам, которые физически
живут в прошивке ЭБУ. Если прошивку в блоке поменять (другой чип-тюнинг,
другой ДАД, другой объём), эти числа поедут, а дашборд об этом не узнает и
будет молча врать. Чтобы этого не было, при КАЖДОЙ сборке:

  1. читаем сам .bin и вытаскиваем из него всё, что там реально лежит в
     машиночитаемом виде — блок идентификации (имя, дата, номера, ЧИСЛО
     ЦИЛИНДРОВ) и контрольную сумму;
  2. читаем firmware_cal.ini — там лежат физические калибровки, которые в
     .bin по смещениям пока не найдены, поэтому их вписывает человек из
     своего редактора калибровок;
  3. СВЕРЯЕМ контрольную сумму .bin с той, что записана в ini. Не совпало —
     значит прошивку подменили, а калибровки в ini остались от старой:
     сборка печатает громкое предупреждение и ставит в заголовок флаг
     FWCAL_MATCHES_DECLARED=0, который прошивка печатает в Serial при старте;
  4. пишем include/firmware_cal.h.

ПОЧЕМУ ДАД НЕ БЕРЁТСЯ ИЗ .bin НАПРЯМУЮ
--------------------------------------
Искали (2026-09-19): ни float32, ни 16-бит в разумных масштабах не дают
однозначного попадания — 64 КБ кода, одиночные совпадения неотличимы от
случайных. Смещения калибровок знает только редактор калибровок (A2L/карта
прошивки), которого у нас нет. Поэтому ДАД и объём объявляются в ini, а
защиту от «съезда» даёт сверка контрольной суммы, а не угадывание.

ВАЖНО ПРО НАКЛОН ДАВЛЕНИЯ
-------------------------
Рабочий закон давления — НЕ из прошивки, а ЗАМЕРЕННЫЙ по ИОН на машине
(см. docs/KLINE.md, «ЭТАЛОН ИОН»): формула прошивки давала наклон 0.945 и
занижала давление на 5.6 кПа на атмосфере. Но замер делался на ДАД с
диапазоном квантования 241.0 кПа. Если в новой прошивке диапазон другой
(например, поставили ДАД на 3 бар), замеренный наклон нужно масштабировать
пропорционально — для этого сюда и уезжает FWCAL_DAD_SPAN_KPA_X100.

ЗАПУСК
------
Автоматически из PlatformIO (extra_scripts в platformio.ini).
Вручную:  python tools/fw_cal/gen_fw_cal.py
"""
import hashlib
import os
import re
import sys

try:
    import configparser
except ImportError:  # pragma: no cover
    import ConfigParser as configparser  # type: ignore

# PlatformIO выполняет pre-скрипт внутри SCons, где __file__ НЕ определён,
# зато рабочий каталог — корень проекта. При обычном запуске наоборот.
try:
    HERE = os.path.dirname(os.path.abspath(__file__))
    ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
except NameError:
    ROOT = os.path.abspath(os.getcwd())
    HERE = os.path.join(ROOT, "tools", "fw_cal")
INI = os.path.join(ROOT, "firmware_cal.ini")
OUT = os.path.join(ROOT, "include", "firmware_cal.h")

BANNER = "=" * 70


def warn(msg):
    sys.stderr.write("\n" + BANNER + "\n" + msg + "\n" + BANNER + "\n")


# --------------------------------------------------------------------------
# Разбор блока идентификации прошивки
#
# В J5TRS251 он лежит подряд нуль-терминированными ASCII-строками (0x9D3F):
#   "2112-1411020-41 " "NOTSUPPORT" "1411010-41" "Cil:4,mark:60-2"
#   "31-07-2018" "J5TRS251" "1411010-41"
# Это те же строки, что ЭБУ отдаёт на ReadECUId (SID 0x1A). Якоримся на
# "Cil:" — он уникален и машиночитаем; остальное собираем вокруг него.
# --------------------------------------------------------------------------
def parse_firmware(path):
    with open(path, "rb") as f:
        blob = f.read()

    info = {
        "file": os.path.basename(path),
        "size": len(blob),
        "sha256": hashlib.sha256(blob).hexdigest(),
        "name": "",
        "date": "",
        "part_no": "",
        "ecu_no": "",
        "cylinders": 0,
        "mark": "",
    }

    m = re.search(rb"Cil:(\d+)(?:,mark:([ -~]*))?", blob)
    if m:
        info["cylinders"] = int(m.group(1))
        if m.group(2):
            info["mark"] = m.group(2).decode("ascii", "replace").strip()
        # Строки блока идут вплотную; соберём соседей вокруг маркера.
        lo = max(0, m.start() - 96)
        hi = min(len(blob), m.end() + 96)
        # Режем окно на печатные куски: разделителем бывает не только
        # 0x00, но и мусор из соседнего кода (перед блоком идут 0xE0).
        parts = [p.decode("ascii").strip()
                 for p in re.findall(rb"[ -~]{3,}", blob[lo:hi])]
        for p in parts:
            if re.fullmatch(r"\d\d-\d\d-\d{4}", p):
                info["date"] = p
            elif re.fullmatch(r"\d{4}-\d{7}-\d{2}", p):
                info["ecu_no"] = p
            elif re.fullmatch(r"\d{7}-\d{2}", p) and not info["part_no"]:
                info["part_no"] = p

    # Имя прошивки: короткая «голая» строка вида J5TRS251, встречается
    # несколько раз — берём самую частую.
    names = re.findall(rb"\b([A-Z]\d[A-Z]{2,4}\d{2,4})\b", blob)
    if names:
        best = {}
        for n in names:
            k = n.decode("ascii")
            best[k] = best.get(k, 0) + 1
        info["name"] = max(best.items(), key=lambda kv: kv[1])[0]
    return info


def read_ini():
    cfg = configparser.ConfigParser()
    if os.path.exists(INI):
        cfg.read(INI, encoding="utf-8")
    return cfg


def getf(cfg, sec, key, default):
    try:
        return float(cfg.get(sec, key))
    except Exception:
        return default


def geti(cfg, sec, key, default):
    try:
        return int(float(cfg.get(sec, key)))
    except Exception:
        return default


def gets(cfg, sec, key, default=""):
    try:
        return cfg.get(sec, key).strip()
    except Exception:
        return default


def generate():
    cfg = read_ini()
    fw_name = gets(cfg, "firmware", "file", "J5TRS251_VOLVO_ORIG.bin")
    fw_path = os.path.join(ROOT, fw_name)

    if not os.path.exists(fw_path):
        warn("[fw_cal] НЕ НАЙДЕНА ПРОШИВКА: %s\n"
             "[fw_cal] Положи .bin в корень репозитория или поправь\n"
             "[fw_cal] firmware_cal.ini -> [firmware] file=...\n"
             "[fw_cal] Собираю с калибровками из ini, БЕЗ сверки." % fw_path)
        info = {"file": fw_name, "size": 0, "sha256": "", "name": "",
                "date": "", "part_no": "", "ecu_no": "", "cylinders": 0,
                "mark": ""}
        found = 0
    else:
        info = parse_firmware(fw_path)
        found = 1

    declared = gets(cfg, "firmware", "sha256")
    matches = 1
    if found and declared:
        matches = 1 if declared.lower() == info["sha256"].lower() else 0
    elif found and not declared:
        matches = 0

    # Физические калибровки — из ini (в .bin по смещениям не найдены).
    dad_min = getf(cfg, "dad", "min_kpa", 12.5)
    dad_span = getf(cfg, "dad", "span_kpa", 241.0)
    cyl_vol = geti(cfg, "engine", "cyl_volume_cm3", 575)
    afr = getf(cfg, "fuel", "afr_stoich", 14.7)
    cyls = info["cylinders"] or geti(cfg, "engine", "cylinders", 4)

    if not matches:
        if found and declared:
            warn("[fw_cal] !!! ПРОШИВКА НЕ ТА, ПОД КОТОРУЮ СНЯТЫ КАЛИБРОВКИ !!!\n"
                 "[fw_cal] файл:    %s\n"
                 "[fw_cal] в файле: %s\n"
                 "[fw_cal] в ini:   %s\n"
                 "[fw_cal] Калибровки ДАД/объёма в firmware_cal.ini могли\n"
                 "[fw_cal] устареть. Перепроверь их в редакторе калибровок,\n"
                 "[fw_cal] обнови ini и впиши туда новую sha256.\n"
                 "[fw_cal] Сборка продолжается, прошивка сообщит об этом в Serial."
                 % (info["file"], info["sha256"], declared))
        elif found:
            warn("[fw_cal] В firmware_cal.ini не записана sha256 прошивки.\n"
                 "[fw_cal] Сверять не с чем — защита от «съехавших» калибровок\n"
                 "[fw_cal] не работает. Впиши в [firmware]:\n"
                 "[fw_cal]   sha256 = %s" % info["sha256"])

    lines = []
    a = lines.append
    a("// СГЕНЕРИРОВАНО tools/fw_cal/gen_fw_cal.py — РУКАМИ НЕ ПРАВИТЬ.")
    a("// Источники: прошивка %s + firmware_cal.ini" % info["file"])
    a("// Правь firmware_cal.ini, заголовок пересоберётся сам.")
    a("#ifndef FIRMWARE_CAL_H")
    a("#define FIRMWARE_CAL_H")
    a("")
    a("// --- вытащено ИЗ САМОЙ ПРОШИВКИ (блок идентификации + контрольная сумма) ---")
    a('#define FWCAL_FOUND            %d' % found)
    a('#define FWCAL_FILE             "%s"' % info["file"])
    a('#define FWCAL_SIZE             %d' % info["size"])
    a('#define FWCAL_SHA256           "%s"' % info["sha256"])
    a('#define FWCAL_SHA256_SHORT     "%s"' % info["sha256"][:12])
    a('#define FWCAL_NAME             "%s"' % info["name"])
    a('#define FWCAL_DATE             "%s"' % info["date"])
    a('#define FWCAL_PART_NO          "%s"' % info["part_no"])
    a('#define FWCAL_ECU_NO           "%s"' % info["ecu_no"])
    a('#define FWCAL_MARK             "%s"' % info["mark"])
    a('#define FWCAL_CYLINDERS        %d' % cyls)
    a("")
    a("// 1 = прошивка в корне та же, под которую сняты калибровки в ini.")
    a("// 0 = подменили или sha256 не записана -> калибровки могли поехать.")
    a('#define FWCAL_MATCHES_DECLARED %d' % matches)
    a("")
    a("// --- объявлено в firmware_cal.ini (в .bin по смещениям не найдено) ---")
    a('#define FWCAL_DAD_MIN_KPA_X100  %d' % int(round(dad_min * 100)))
    a('#define FWCAL_DAD_SPAN_KPA_X100 %d' % int(round(dad_span * 100)))
    a('#define FWCAL_CYL_VOLUME_CM3    %d' % cyl_vol)
    a('#define FWCAL_DISP_CM3          %d' % (cyl_vol * cyls))
    a('#define FWCAL_AFR_STOICH_X100   %d' % int(round(afr * 100)))
    a("")
    a("#endif // FIRMWARE_CAL_H")
    text = "\n".join(lines) + "\n"

    old = ""
    if os.path.exists(OUT):
        with open(OUT, "r", encoding="utf-8") as f:
            old = f.read()
    if old != text:
        d = os.path.dirname(OUT)
        if not os.path.isdir(d):
            os.makedirs(d)
        with open(OUT, "w", encoding="utf-8") as f:
            f.write(text)

    print("[fw_cal] %s: %s %s  цил=%d  sha=%s  калибровки=%s"
          % (info["file"], info["name"] or "?", info["date"] or "?",
             cyls, info["sha256"][:12] or "?",
             "СОВПАЛИ" if matches else "НЕ СВЕРЕНЫ"))
    return matches


# PlatformIO зовёт скрипт внутри SCons (там в области видимости есть Import),
# при обычном запуске из консоли Import отсутствует.
try:
    Import("env")  # type: ignore # noqa: F821
    _UNDER_PIO = True
except NameError:
    _UNDER_PIO = False

if _UNDER_PIO or __name__ == "__main__":
    generate()
