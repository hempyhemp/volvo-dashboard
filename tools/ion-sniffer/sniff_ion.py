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
import sys, os, time, argparse, datetime
import frida

DEFAULT_EXE = r"D:\ЧипТюненх\3000\ИОН\InjOnl.exe"

state = {"t0": None, "last": None, "logf": None}


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
        out("%s  %s[%2d]%s  %s%s" % (ts, dirn, p.get("n", 0), ovf, raw, dec))
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
    out("# столбцы: <сек> +<пауза,мс>  <напр>[<n>]  <hex>  <разбор>")
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

    # Держим процесс живым, пока ИОН открыт (или пока нас не убьют).
    # В фоне stdin недоступен, поэтому просто спим циклом.
    try:
        while not detached["v"]:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        out("\n# остановлено. Лог: %s" % args.out)
        if state["logf"]:
            state["logf"].close()


if __name__ == "__main__":
    main()
