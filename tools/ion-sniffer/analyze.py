# -*- coding: utf-8 -*-
"""Пересборка кадров K-Line из лога сниффера (RX приходит побайтно).

Склеивает подряд идущие байты одного направления в кадры (разрыв по смене
направления или паузе >= GAP мс), декодирует ISO14230/KWP и печатает:
 - поток TX/RX пересобранными кадрами,
 - разбор ответа на readData LID 0x0F (жирный кадр — основной опрос ИОН).

Запуск: python tools/ion-sniffer/analyze.py [лог]   (по умолчанию последний)
"""
import sys, os, glob, re

GAP_MS = 6  # пауза между кадрами

def latest_log():
    here = os.path.dirname(os.path.abspath(__file__))
    fs = sorted(glob.glob(os.path.join(here, "ion_sniff_*.log")))
    return fs[-1] if fs else None

def parse(path):
    # -> список (t_sec, dir, [bytes])
    rows = []
    rx = re.compile(r"^\s*([0-9.]+)\s+\+\d+\s+(TX|RX)\[\s*\d+\]\s+(.*)$")
    for line in open(path, encoding="utf-8"):
        m = rx.match(line)
        if not m:
            continue
        t = float(m.group(1)); d = m.group(2)
        hexpart = m.group(3).split("  ")[0].strip()  # до колонки разбора
        try:
            bs = [int(x, 16) for x in hexpart.split()]
        except ValueError:
            continue
        rows.append((t, d, bs))
    return rows

def assemble(rows):
    frames = []  # (t, dir, [bytes])
    cur_dir = None; cur = []; cur_t = 0.0; last_t = None
    for t, d, bs in rows:
        gap = (t - last_t) * 1000.0 if last_t is not None else 0
        if cur and (d != cur_dir or gap >= GAP_MS):
            frames.append((cur_t, cur_dir, cur)); cur = []
        if not cur:
            cur_dir = d; cur_t = t
        cur.extend(bs); last_t = t
    if cur:
        frames.append((cur_t, cur_dir, cur))
    return frames

def hx(bs):
    return " ".join("%02X" % b for b in bs)

def decode_0f(data):
    # data = байты полезной нагрузки после [len tgt src], начиная с SID(0x61) LID(0x0F)
    out = []
    for i, b in enumerate(data):
        out.append("[%02d]=%3d(0x%02X)" % (i, b, b))
    return out

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else latest_log()
    if not path:
        print("нет лог-файла"); return
    print("# лог:", path)
    rows = parse(path)
    frames = assemble(rows)
    print("# сырых строк:", len(rows), " кадров:", len(frames))

    # Соберём ответы на readData 0x0F: ищем TX 21 0F, следующий RX-кадр = эхо+ответ
    resp_lens = {}
    samples = []
    for idx in range(len(frames) - 1):
        t, d, bs = frames[idx]
        if d == "TX" and len(bs) >= 5 and bs[3] == 0x21 and bs[4] == 0x0F:
            # следующий RX кадр
            for j in range(idx + 1, min(idx + 3, len(frames))):
                tt, dd, rb = frames[j]
                if dd == "RX":
                    resp_lens[len(rb)] = resp_lens.get(len(rb), 0) + 1
                    if len(samples) < 6:
                        samples.append((tt, rb))
                    break
    print("\n# длины RX на 21 0F:", resp_lens)
    for tt, rb in samples:
        print("\n@%.3f RX(%d): %s" % (tt, len(rb), hx(rb)))
        # rb = эхо(6) + ответ. Найдём начало ответа: после эха 82 10 F1 21 0F B3
        # ответ: [len] F1 10 61 0F <data...> cs
        # ищем 61 0F
        pos = -1
        for k in range(len(rb) - 1):
            if rb[k] == 0x61 and rb[k + 1] == 0x0F:
                pos = k; break
        if pos >= 0:
            payload = rb[pos:]  # начиная с 61 0F
            print("   ответ payload от 61 0F (%d б):" % len(payload))
            # индексируем ОТ байта после 61 0F (данные)
            data = payload[2:]
            line = ""
            for i, b in enumerate(data):
                line += "d%02d=%3d " % (i, b)
                if (i + 1) % 8 == 0:
                    line += "\n   "
            print("   " + line)

if __name__ == "__main__":
    main()
