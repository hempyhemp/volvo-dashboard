# -*- coding: utf-8 -*-
"""Минимальный дизассемблер MCS-51 (8051) для прошивки ЭБУ Январь.

Зачем свой: нужен разбор нескольких сотен байт вокруг конкретных адресов,
ставить полноценный тулчейн ради этого незачем. Покрыт весь набор команд
8051 — этого достаточно, чтобы читать код индексирования таблиц.

Запуск:
    python tools/fw_cal/dis8051.py <файл.bin> <адрес-hex> [сколько-байт]
Например:
    python tools/fw_cal/dis8051.py J5TRS251_VOLVO_ORIG.bin 45a0 120
"""
import sys

# Регистры-операнды для групп команд, кодируемых младшим полубайтом.
RI = ["@R0", "@R1"]
RN = ["R%d" % i for i in range(8)]


def sfr(a):
    """Имена служебных регистров, которые реально встречаются в этом коде."""
    names = {
        0x80: "P0", 0x81: "SP", 0x82: "DPL", 0x83: "DPH", 0x87: "PCON",
        0x88: "TCON", 0x89: "TMOD", 0x8A: "TL0", 0x8B: "TL1", 0x8C: "TH0",
        0x8D: "TH1", 0x90: "P1", 0x98: "SCON", 0x99: "SBUF", 0xA0: "P2",
        0xA8: "IE", 0xB0: "P3", 0xB8: "IP", 0xD0: "PSW", 0xE0: "A",
        0xF0: "B",
    }
    return names.get(a, "0x%02X" % a)


def bitname(b):
    base = b & 0xF8 if b >= 0x80 else 0x20 + (b >> 3)
    if b >= 0x80:
        return "%s.%d" % (sfr(b & 0xF8), b & 7)
    return "0x%02X.%d" % (0x20 + (b >> 3), b & 7)


def disasm_one(mem, pc):
    """-> (длина, текст). pc — адрес первого байта команды."""
    op = mem[pc]
    b1 = mem[pc + 1] if pc + 1 < len(mem) else 0
    b2 = mem[pc + 2] if pc + 2 < len(mem) else 0
    rel = lambda: (pc + 2 + (b1 - 256 if b1 > 127 else b1)) & 0xFFFF
    rel3 = lambda: (pc + 3 + (b2 - 256 if b2 > 127 else b2)) & 0xFFFF
    a11 = lambda: ((pc + 2) & 0xF800) | ((op >> 5) << 8) | b1

    hi, lo = op >> 4, op & 0x0F

    # Группы, где младший полубайт выбирает операнд.
    if lo >= 0x8:
        r = RN[lo - 8]
        src = r
    elif lo in (0x6, 0x7):
        src = RI[lo - 6]
    else:
        src = None

    tbl = {
        0x00: (1, "NOP"),
        0x02: (3, "LJMP  0x%04X" % ((b1 << 8) | b2)),
        0x12: (3, "LCALL 0x%04X" % ((b1 << 8) | b2)),
        0x22: (1, "RET"),
        0x32: (1, "RETI"),
        0x03: (1, "RR    A"),
        0x13: (1, "RRC   A"),
        0x23: (1, "RL    A"),
        0x33: (1, "RLC   A"),
        0x04: (1, "INC   A"),
        0x14: (1, "DEC   A"),
        0x05: (2, "INC   %s" % sfr(b1)),
        0x15: (2, "DEC   %s" % sfr(b1)),
        0xA3: (1, "INC   DPTR"),
        0x53: (3, "ANL   %s,#0x%02X" % (sfr(b1), b2)),
        0x43: (3, "ORL   %s,#0x%02X" % (sfr(b1), b2)),
        0x63: (3, "XRL   %s,#0x%02X" % (sfr(b1), b2)),
        0x52: (2, "ANL   %s,A" % sfr(b1)),
        0x42: (2, "ORL   %s,A" % sfr(b1)),
        0x62: (2, "XRL   %s,A" % sfr(b1)),
        0x54: (2, "ANL   A,#0x%02X" % b1),
        0x44: (2, "ORL   A,#0x%02X" % b1),
        0x64: (2, "XRL   A,#0x%02X" % b1),
        0x55: (2, "ANL   A,%s" % sfr(b1)),
        0x45: (2, "ORL   A,%s" % sfr(b1)),
        0x65: (2, "XRL   A,%s" % sfr(b1)),
        0x74: (2, "MOV   A,#0x%02X" % b1),
        0xE5: (2, "MOV   A,%s" % sfr(b1)),
        0xF5: (2, "MOV   %s,A" % sfr(b1)),
        0xC4: (1, "SWAP  A"),
        0x75: (3, "MOV   %s,#0x%02X" % (sfr(b1), b2)),
        0x85: (3, "MOV   %s,%s" % (sfr(b2), sfr(b1))),
        0x90: (3, "MOV   DPTR,#0x%04X" % ((b1 << 8) | b2)),
        0x93: (1, "MOVC  A,@A+DPTR"),
        0x83: (1, "MOVC  A,@A+PC"),
        0xE0: (1, "MOVX  A,@DPTR"),
        0xF0: (1, "MOVX  @DPTR,A"),
        0xE4: (1, "CLR   A"),
        0xF4: (1, "CPL   A"),
        0xC3: (1, "CLR   C"),
        0xD3: (1, "SETB  C"),
        0xB3: (1, "CPL   C"),
        0xA4: (1, "MUL   AB"),
        0x84: (1, "DIV   AB"),
        0xD4: (1, "DA    A"),
        0xC0: (2, "PUSH  %s" % sfr(b1)),
        0xD0: (2, "POP   %s" % sfr(b1)),
        0xC5: (2, "XCH   A,%s" % sfr(b1)),
        0x24: (2, "ADD   A,#0x%02X" % b1),
        0x34: (2, "ADDC  A,#0x%02X" % b1),
        0x94: (2, "SUBB  A,#0x%02X" % b1),
        0x25: (2, "ADD   A,%s" % sfr(b1)),
        0x35: (2, "ADDC  A,%s" % sfr(b1)),
        0x95: (2, "SUBB  A,%s" % sfr(b1)),
        0x30: (3, "JNB   %s,0x%04X" % (bitname(b1), rel3())),
        0x20: (3, "JB    %s,0x%04X" % (bitname(b1), rel3())),
        0x10: (3, "JBC   %s,0x%04X" % (bitname(b1), rel3())),
        0xC2: (2, "CLR   %s" % bitname(b1)),
        0xD2: (2, "SETB  %s" % bitname(b1)),
        0xB2: (2, "CPL   %s" % bitname(b1)),
        0x82: (2, "ANL   C,%s" % bitname(b1)),
        0x72: (2, "ORL   C,%s" % bitname(b1)),
        0xA2: (2, "MOV   C,%s" % bitname(b1)),
        0x92: (2, "MOV   %s,C" % bitname(b1)),
        0x40: (2, "JC    0x%04X" % rel()),
        0x50: (2, "JNC   0x%04X" % rel()),
        0x60: (2, "JZ    0x%04X" % rel()),
        0x70: (2, "JNZ   0x%04X" % rel()),
        0x80: (2, "SJMP  0x%04X" % rel()),
        0x73: (1, "JMP   @A+DPTR"),
        0xB4: (3, "CJNE  A,#0x%02X,0x%04X" % (b1, rel3())),
        0xB5: (3, "CJNE  A,%s,0x%04X" % (sfr(b1), rel3())),
        0xD5: (3, "DJNZ  %s,0x%04X" % (sfr(b1), rel3())),
    }
    if op in tbl:
        return tbl[op]

    # AJMP/ACALL: младшие 5 бит кода = 0x01 / 0x11
    if lo == 0x1:
        return 2, "AJMP  0x%04X" % a11()
    if lo == 0x11:
        return 2, "ACALL 0x%04X" % a11()

    if src is not None:
        n = 1 if lo >= 8 else 1
        if hi == 0x0:
            return n, "INC   %s" % src
        if hi == 0x1:
            return n, "DEC   %s" % src
        if hi == 0x2:
            return n, "ADD   A,%s" % src
        if hi == 0x3:
            return n, "ADDC  A,%s" % src
        if hi == 0x4:
            return n, "ORL   A,%s" % src
        if hi == 0x5:
            return n, "ANL   A,%s" % src
        if hi == 0x6:
            return n, "XRL   A,%s" % src
        if hi == 0x7:
            return 2, "MOV   %s,#0x%02X" % (src, b1)
        if hi == 0x8:
            return 2, "MOV   %s,%s" % (sfr(b1), src)
        if hi == 0x9:
            return n, "SUBB  A,%s" % src   # SUBB A,Rn / @Ri — ОДИН байт
        if hi == 0xA:
            return 2, "MOV   %s,%s" % (src, sfr(b1))
        if hi == 0xB:
            return 3, "CJNE  %s,#0x%02X,0x%04X" % (src, b1, rel3())
        if hi == 0xC:
            return n, "XCH   A,%s" % src
        if hi == 0xD:
            if lo >= 8:
                return 2, "DJNZ  %s,0x%04X" % (src, rel())
            return n, "XCHD  A,%s" % src
        if hi == 0xE:
            if lo in (2, 3):
                return 1, "MOVX  A,@%s" % RI[lo - 2]
            return n, "MOV   A,%s" % src
        if hi == 0xF:
            if lo in (2, 3):
                return 1, "MOVX  @%s,A" % RI[lo - 2]
            return n, "MOV   %s,A" % src
    if op == 0xE2 or op == 0xE3:
        return 1, "MOVX  A,@%s" % RI[op - 0xE2]
    if op == 0xF2 or op == 0xF3:
        return 1, "MOVX  @%s,A" % RI[op - 0xF2]
    return 1, "DB    0x%02X" % op


def disasm(mem, start, count):
    pc = start
    end = start + count
    out = []
    while pc < end and pc < len(mem):
        n, txt = disasm_one(mem, pc)
        raw = " ".join("%02X" % mem[pc + i] for i in range(n))
        out.append("0x%04X  %-9s %s" % (pc, raw, txt))
        pc += n
    return out


if __name__ == "__main__":
    path = sys.argv[1]
    start = int(sys.argv[2], 16)
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 64
    mem = open(path, "rb").read()
    print("\n".join(disasm(mem, start, count)))
