'use strict';
// Frida-инструментация ИОН (InjOnl.exe) для снятия обмена по K-Line.
// Цепляемся к WinAPI работы с COM-портом внутри процесса ИОН:
//   CreateFileW/A      - ловим открытие "\\.\COMx", запоминаем handle
//   SetCommState       - смена скорости (DCB.BaudRate)
//   SetCommTimeouts    - Read/Write таймауты (COMMTIMEOUTS)
//   WriteFile          - TX (тестер -> ЭБУ)
//   ReadFile           - RX (ЭБУ -> тестер), в т.ч. overlapped
//   GetOverlappedResult- дочитываем отложенные overlapped-чтения
// Все события уходят в Python-драйвер через send({...}); он их печатает
// и пишет в лог с таймингами и разбором кадров.

const serialHandles = new Set(); // строковые ключи handle'ов COM-порта
const pendingOv = {};            // overlapped ptr -> { buf }  (для ReadFile)

function hexOf(ptr, len) {
  if (len <= 0) return '';
  try {
    const ab = ptr.readByteArray(len);
    if (!ab) return '';
    const b = new Uint8Array(ab);
    let s = '';
    for (let i = 0; i < b.length; i++) {
      s += b[i].toString(16).padStart(2, '0').toUpperCase();
      if (i < b.length - 1) s += ' ';
    }
    return s;
  } catch (e) { return ''; }
}

function emit(o) { o.t = Date.now(); send(o); }

// Разрешение экспортов: kernel32 (часть форвардится в kernelbase).
// Frida 17 убрал Module.getExportByName(module,name); используем
// per-module getExportByName и глобальный поиск как запас.
function resolve(name) {
  for (const m of ['kernel32.dll', 'kernelbase.dll']) {
    try {
      const mod = Process.getModuleByName(m);
      if (mod) {
        const p = mod.getExportByName(name);
        if (p && !p.isNull()) return p;
      }
    } catch (e) {}
  }
  try {
    const p = Module.getGlobalExportByName(name);
    if (p && !p.isNull()) return p;
  } catch (e) {}
  return null;
}

function hook(name, cbs) {
  const p = resolve(name);
  if (!p) { emit({ type: 'warn', msg: 'no export ' + name }); return; }
  Interceptor.attach(p, cbs);
}

// --- CreateFileW / CreateFileA: поймать открытие COM-порта ---
['CreateFileW', 'CreateFileA'].forEach(function (fn) {
  const p = resolve(fn);
  if (!p) return;
  const wide = fn.charAt(fn.length - 1) === 'W';
  Interceptor.attach(p, {
    onEnter: function (a) {
      try { this.name = wide ? a[0].readUtf16String() : a[0].readAnsiString(); }
      catch (e) { this.name = null; }
    },
    onLeave: function (r) {
      if (this.name && /COM\d+/i.test(this.name)) {
        serialHandles.add(r.toString());
        emit({ type: 'open', name: this.name, handle: r.toString() });
      }
    }
  });
});

// --- SetCommState: скорость (DCB.BaudRate @ offset 4) ---
hook('SetCommState', {
  onEnter: function (a) {
    try {
      const h = a[0].toString();
      serialHandles.add(h);
      emit({ type: 'baud', baud: a[1].add(4).readU32(), handle: h });
    } catch (e) {}
  }
});

// --- SetCommTimeouts: COMMTIMEOUTS (5 x DWORD) ---
hook('SetCommTimeouts', {
  onEnter: function (a) {
    try {
      const h = a[0].toString();
      serialHandles.add(h);
      const t = a[1];
      emit({
        type: 'timeouts', handle: h,
        readInterval: t.readU32(),
        readMult: t.add(4).readU32(),
        readConst: t.add(8).readU32(),
        writeMult: t.add(12).readU32(),
        writeConst: t.add(16).readU32()
      });
    } catch (e) {}
  }
});

// --- WriteFile: TX (буфер валиден на входе) ---
hook('WriteFile', {
  onEnter: function (a) {
    const h = a[0].toString();
    if (!serialHandles.has(h)) return;
    const n = a[2].toInt32();
    emit({ type: 'tx', bytes: hexOf(a[1], n), n: n });
  }
});

// --- ReadFile: RX (буфер валиден на выходе; overlapped -> отложить) ---
hook('ReadFile', {
  onEnter: function (a) {
    this.skip = !serialHandles.has(a[0].toString());
    if (this.skip) return;
    this.buf = a[1];
    this.pCount = a[3]; // lpNumberOfBytesRead (может быть NULL)
    this.ov = a[4];     // lpOverlapped
  },
  onLeave: function (r) {
    if (this.skip) return;
    let n = 0;
    if (this.pCount && !this.pCount.isNull()) { try { n = this.pCount.readU32(); } catch (e) {} }
    if (n > 0) {
      emit({ type: 'rx', bytes: hexOf(this.buf, n), n: n });
    } else if (this.ov && !this.ov.isNull()) {
      pendingOv[this.ov.toString()] = { buf: this.buf };
    }
  }
});

// --- GetOverlappedResult: дочитать отложенный overlapped ReadFile ---
hook('GetOverlappedResult', {
  onEnter: function (a) { this.ovk = a[1].toString(); this.pTrans = a[2]; },
  onLeave: function (r) {
    const rec = pendingOv[this.ovk];
    if (!rec) return;
    let n = 0;
    if (this.pTrans && !this.pTrans.isNull()) { try { n = this.pTrans.readU32(); } catch (e) {} }
    if (n > 0) emit({ type: 'rx', bytes: hexOf(rec.buf, n), n: n, ov: true });
    delete pendingOv[this.ovk];
  }
});

emit({ type: 'info', msg: 'sniffer armed' });
