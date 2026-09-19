# -*- coding: utf-8 -*-
"""
Сниффер обмена ИОН (InjOnl.exe) <-> ЭБУ по K-Line.

ИОН держит COM1 монопольно, поэтому вторым процессом порт не открыть.
Вместо этого через Frida перехватываем у самого ИОН вызовы WinAPI работы
с портом (WriteFile/ReadFile/SetCommState/SetCommTimeouts) и пишем весь
обмен с таймингами и разбором кадров.

Запуск (из корня репозитория):
    python tools/ion-sniffer/sniff_ion.py

По умолчанию САМ запускает ИОН (spawn) и сразу цепляется — так ловим
обмен с первого байта. Затем в окне ИОН жми "Подключить" как обычно.
Ctrl+C — остановить, лог остаётся в файле.

Флаги:
    --attach     не запускать ИОН, а прицепиться к уже открытому InjOnl.exe
    --exe PATH   путь к InjOnl.exe (по умолчанию D:\\ЧипТюненх\\3000\\ИОН)
    --out PATH   файл лога (по умолчанию ion_sniff_<дата>.log рядом)
"""
import sys, os, time, argparse, datetime, threading
import frida

DEFAULT_EXE = r"D:\ЧипТюненх\3000\ИОН\InjOnl.exe"

state = {"t0": None, "last": None, "logf": None, "rel": 0.0}


def out(line):
    print(line)
    if state["logf"]:
        state["logf"].write(line + "\n")
        state["logf"].flush()


def times(t_ms):
    if state["t0"] is None:
        state["t0"] = t_ms
        state["last"] = t_ms
    rel = (t_ms - state["t0"]) / 1000.0
    gap = t_ms - state["last"]
    state["last"] = t_ms
    state["rel"] = rel
    return "%9.3f +%-5d" % (rel, gap)


# --- Разбор кадра ISO14230 / KWP: [fmt][tgt][src][data...][cs] ---
SID_REQ = {
    0x10: "StartDiagSession/SpeedSwitch", 0x11: "ECUReset", 0x14: "ClearDTC",
    0x18: "ReadDTC", 0x1A: "ReadECUId", 0x20: "StopDiagSession",
    0x21: "ReadDataByLocalId", 0x22: "ReadDataByCommonId",
    0x23: "ReadMemoryByAddress", 0x27: "SecurityAccess",
    0x2E: "WriteDataByCommonId", 0x2F: "IOControl",
    0x30: "IOControlByLocalId", 0x31: "StartRoutine", 0x3B: "WriteDataByLocalId",
    0x3D: "WriteMemoryByAddress", 0x81: "StartCommunication",
    0x82: "StopCommunication", 0x83: "AccessTimingParams",
}


def decode(bytes_list, is_tx):
    b = bytes_list
    if len(b) < 4:
        return ""
    fmt = b[0]
    ln = fmt & 0x3F
    idx = 3
    if ln == 0 and len(b) > 3:  # длина в отдельном байте
        ln = b[3]
        idx = 4
    tgt, src = b[1], b[2]
    data = b[idx:idx + ln]
    if not data:
        return ""
    sid = data[0]
    # Ответ ЭБУ: SID = запрос|0x40; 0x7F = отрицательный.
    if sid == 0x7F and len(data) >= 3:
        return "  <- NEG отказ на SID %02X код %02X" % (data[1], data[2])
    base = sid - 0x40 if sid >= 0x40 else sid
    name = SID_REQ.get(base, "")
    tag = "  " + ("->" if is_tx else "<-") + " "
    note = "SID %02X %s" % (sid, name)
    # Самое важное: ReadMemoryByAddress — какие адреса читает ИОН.
    if base == 0x23 and is_tx and len(data) >= 5:
        # 23 00 addrHi addrLo len
        addr = (data[2] << 8) | data[3]
        note += "  addr=0x%04X len=%d" % (addr, data[4])
    if base == 0x21 and is_tx and len(data) >= 2:
        note += "  LID=0x%02X" % data[1]
    if base == 0x10 and is_tx and len(data) >= 3:
        note += "  sub=%02X param=%02X" % (data[1], data[2])
    return tag + note


# ---------------------------------------------------------------------------
# ЖИВОЙ РАЗБОР ЖИРНОГО КАДРА 0x0F
#
# ИОН циклически шлёт только `82 10 F1 21 0F B3`, а ЭБУ отвечает кадром
# эхо(6) + [80 F1 10 35 61 0F <51 байт данных> cs]. RX прилетает побайтно,
# поэтому склеиваем поток сами: разрыв по смене направления или паузе.
#
# Смещения полей — те же, что в src/kline_test.cpp (d = индекс байта данных
# ПОСЛЕ маркера 61 0F). Печатаем в том числе НЕопознанные кандидаты, чтобы
# было с чем сверять показания на экране ИОН.
FRAME_GAP_MS = 6          # пауза, по которой рвём поток на кадры
F0F_MIN_DATA = 40         # меньше — кадр неполный, не разбираем


def u16(d, i):
    """16-бит из данных кадра, младший байт первым (как в прошивке)."""
    if i + 1 >= len(d):
        return None
    return d[i] | (d[i + 1] << 8)


def decode_0f_data(d):
    """d — байты данных кадра 0x0F (после 61 0F). -> (строка, dict)."""
    if len(d) < F0F_MIN_DATA:
        return None, None
    f80c = d[26]                     # АЦП ДАД, старший байт
    f80d = d[27]                     # спорный байт: у нас = напряжение
    v = dict(
        rpm=d[12] * 40,              # RAM 0x55
        thr=d[11],                   # RAM 0x52, %
        ow=d[8] - 40,                # ОЖ, degC
        air=d[37] - 40 if len(d) > 37 else None,   # F885, degC
        spd=d[35] if len(d) > 35 else None,        # F90A, км/ч
        f80c=f80c,
        f80d=f80d,
        volt=5.2 + f80d * 0.05,      # гипотеза "F80D = напряжение"
        gbc=u16(d, 45),              # F808 — кандидат в расход воздуха
        f82e=u16(d, 47),             # второй вариант наполнения
        f841=u16(d, 43),
        f97b=u16(d, 39),
        f98d=u16(d, 41),
        f862=u16(d, 49),
    )
    # Давление по калибровке ДАД из прошивки, прямо из кадра (F80C).
    # Смещение 10 единиц между F80C и F9A0 выведено из лога 2026-09-19.
    v["map_kpa"] = 12.5 + 241.0 * (f80c - 10) / 255.0
    line = ("RPM=%-5d дрос=%-3d%% ОЖ=%-4d возд=%-4s V=%.2f | F80C=%-3d "
            "-> MAP~%.1f кПа | GBC=%-5s F82E=%-5s F841=%-5s F97B=%-5s "
            "F98D=%-5s F862=%-5s" % (
                v["rpm"], v["thr"], v["ow"], v["air"], v["volt"], f80c,
                v["map_kpa"], v["gbc"], v["f82e"], v["f841"], v["f97b"],
                v["f98d"], v["f862"]))
    return line, v


# Состояние склейки потока и пиков.
asm = {"dir": None, "buf": [], "t": 0.0}
peaks = {}
PEAK_KEYS = ("rpm", "f80c", "gbc", "f82e", "f841", "f97b", "f98d", "f862")
last_print = {"t": 0.0}


def note_peaks(v):
    for k in PEAK_KEYS:
        x = v.get(k)
        if x is None:
            continue
        if peaks.get(k) is None or x > peaks[k]["val"]:
            peaks[k] = {"val": x, "rpm": v["rpm"], "f80c": v["f80c"],
                        "t": asm["t"]}


def flush_frame():
    """Разобрать накопленный RX-кадр, если это ответ на 21 0F."""
    b = asm["buf"]
    asm["buf"] = []
    if asm["dir"] != "rx" or len(b) < 20:
        return
    pos = -1
    for k in range(len(b) - 1):
        if b[k] == 0x61 and b[k + 1] == 0x0F:
            pos = k
            break
    if pos < 0:
        return
    line, v = decode_0f_data(b[pos + 2:])
    if not line:
        return
    note_peaks(v)
    # В лог — каждый кадр, на экран — раз в секунду, чтобы не залить консоль.
    txt = "%9.3f  [0F] %s" % (asm["t"], line)
    if state["logf"]:
        state["logf"].write(txt + "\n")
        state["logf"].flush()
    now = time.time()
    if now - last_print["t"] >= 1.0:
        last_print["t"] = now
        print(txt)


def feed(dirn, t_rel, blist):
    """Подать байты в склейку потока."""
    if asm["dir"] != dirn or (t_rel - asm["t"]) * 1000.0 >= FRAME_GAP_MS:
        flush_frame()
    asm["dir"] = dirn
    asm["t"] = t_rel
    asm["buf"].extend(blist)


def on_message(msg, data):
    if msg.get("type") == "error":
        out("[FRIDA ERROR] " + str(msg.get("stack") or msg.get("description")))
        return
    p = msg.get("payload") or {}
    typ = p.get("type")
    ts = times(p.get("t", int(time.time() * 1000)))
    if typ == "tx" or typ == "rx":
        raw = p.get("bytes", "")
        blist = [int(x, 16) for x in raw.split()] if raw else []
        dirn = "TX" if typ == "tx" else "RX"
        ovf = " (ov)" if p.get("ov") else ""
        dec = decode(blist, typ == "tx")
        # Сырой поток — только в файл (на экране от него не видно разбора).
        raw_line = "%s  %s[%2d]%s  %s%s" % (ts, dirn, p.get("n", 0), ovf, raw, dec)
        if state["logf"]:
            state["logf"].write(raw_line + "\n")
        feed(typ, state["rel"], blist)
    elif typ == "baud":
        out("%s  === SetCommState BaudRate=%d ===" % (ts, p.get("baud")))
    elif typ == "timeouts":
        out("%s  === SetCommTimeouts RI=%d RM=%d RC=%d WM=%d WC=%d ===" % (
            ts, p.get("readInterval"), p.get("readMult"), p.get("readConst"),
            p.get("writeMult"), p.get("writeConst")))
    elif typ == "open":
        out("%s  === CreateFile %s handle=%s ===" % (ts, p.get("name"), p.get("handle")))
    elif typ == "info":
        out("%s  [i] %s" % (ts, p.get("msg")))
    elif typ == "warn":
        out("%s  [!] %s" % (ts, p.get("msg")))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--attach", action="store_true")
    ap.add_argument("--exe", default=DEFAULT_EXE)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    js_path = os.path.join(here, "sniff_ion.js")
    with open(js_path, "r", encoding="utf-8") as f:
        js = f.read()

    if args.out is None:
        stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        args.out = os.path.join(here, "ion_sniff_%s.log" % stamp)
    state["logf"] = open(args.out, "w", encoding="utf-8")
    out("# Лог сниффера ИОН, %s" % datetime.datetime.now())
    out("# сырой поток: <сек> +<пауза,мс>  <напр>[<n>]  <hex>  <разбор>")
    out("# разбор кадра: <сек>  [0F] <поля>")
    out("# МЕТКА: набери текст в этой консоли и нажми Enter — уйдёт в лог")
    out("")

    dev = frida.get_local_device()

    if args.attach:
        out("# режим attach: ищу InjOnl.exe ...")
        session = dev.attach("InjOnl.exe")
        script = session.create_script(js)
        script.on("message", on_message)
        script.load()
        out("# прицепился. Работай в ИОН. Ctrl+C для выхода.")
    else:
        exe = args.exe
        if not os.path.exists(exe):
            out("# НЕ найден ИОН: %s (укажи --exe PATH)" % exe)
            return
        cwd = os.path.dirname(exe)
        out("# запускаю ИОН: %s" % exe)
        pid = dev.spawn([exe], cwd=cwd)
        session = dev.attach(pid)
        script = session.create_script(js)
        script.on("message", on_message)
        script.load()
        dev.resume(pid)
        out("# ИОН запущен (pid=%d). В окне ИОН жми 'Подключить'. Ctrl+C — выход." % pid)

    detached = {"v": False}

    def on_detached(reason, *a):
        out("\n# сессия отцеплена: %s" % reason)
        detached["v"] = True

    try:
        session.on("detached", on_detached)
    except Exception:
        pass

    # МЕТКИ РЕЖИМА. Всё, что наберёшь в этой консоли и отправишь Enter,
    # уходит в лог отдельной строкой с текущим временем. Так режим
    # ("заглушен", "хх", "газ") и показания с экрана ИОН привязываются
    # к байтам без секундомера.
    def marker_loop():
        while not detached["v"]:
            try:
                line = sys.stdin.readline()
            except Exception:
                return
            if not line:
                return
            label = line.strip()
            if not label:
                continue
            out("%9.3f  ===== МЕТКА: %s =====" % (state["rel"], label))

    threading.Thread(target=marker_loop, daemon=True).start()

    # Держим процесс живым, пока ИОН открыт (или пока нас не убьют).
    try:
        while not detached["v"]:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        # Пики за сессию — чтобы сопоставить максимум на экране ИОН
        # (наддув, расход воздуха) с максимумом в байтах кадра.
        if peaks:
            out("")
            out("# ПИКИ ЗА СЕССИЮ (значение @ время, обороты, F80C):")
            for k in PEAK_KEYS:
                pk = peaks.get(k)
                if pk:
                    out("#   %-5s max=%-6d @ %8.3f с  RPM=%-5d F80C=%d"
                        % (k, pk["val"], pk["t"], pk["rpm"], pk["f80c"]))
        out("\n# остановлено. Лог: %s" % args.out)
        if state["logf"]:
            state["logf"].close()


if __name__ == "__main__":
    main()
