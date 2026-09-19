// Значения по умолчанию для калибровок из прошивки ЭБУ.
//
// include/firmware_cal.h генерируется ОТДЕЛЬНОЙ командой (не при каждой
// сборке):
//     python tools\fw_cal\gen_fw_cal.py
// и в git не хранится. Поэтому обычная сборка обязана собираться и без него.
// Этот файл подключается ПОСЛЕ firmware_cal.h и добивает только то, чего там
// не оказалось, — то есть при наличии сгенерированного заголовка не меняет
// ничего.
//
// Значения ниже — ровно те, что были захардкожены в kline_test.cpp до
// появления привязки к прошивке (прошивка J5TRS251 от 31-07-2018).
#ifndef FIRMWARE_CAL_DEFAULT_H
#define FIRMWARE_CAL_DEFAULT_H

// 1 = паспорт прошивки сгенерирован и подключён; 0 = собрано на значениях
// по умолчанию, прошивку никто не читал.
#ifndef FWCAL_PRESENT
#define FWCAL_PRESENT 0
#endif

#ifndef FWCAL_FOUND
#define FWCAL_FOUND 0
#endif
#ifndef FWCAL_FILE
#define FWCAL_FILE "(не читалась)"
#endif
#ifndef FWCAL_SHA256_SHORT
#define FWCAL_SHA256_SHORT "-"
#endif
#ifndef FWCAL_NAME
#define FWCAL_NAME "J5TRS251"
#endif
#ifndef FWCAL_DATE
#define FWCAL_DATE "31-07-2018"
#endif
#ifndef FWCAL_PART_NO
#define FWCAL_PART_NO "1411010-41"
#endif
#ifndef FWCAL_ECU_NO
#define FWCAL_ECU_NO "2112-1411020-41"
#endif
#ifndef FWCAL_MARK
#define FWCAL_MARK "60-2"
#endif
#ifndef FWCAL_CYLINDERS
#define FWCAL_CYLINDERS 4
#endif

// Сверять не с чем, если паспорт не генерировали.
#ifndef FWCAL_MATCHES_DECLARED
#define FWCAL_MATCHES_DECLARED 1
#endif

// ДАД: минимум и диапазон квантования, кПа×100. Диапазон 241.0 — тот, при
// котором замерен наклон давления по ИОН, поэтому на нём наклон не меняется.
#ifndef FWCAL_DAD_MIN_KPA_X100
#define FWCAL_DAD_MIN_KPA_X100 1250
#endif
#ifndef FWCAL_DAD_SPAN_KPA_X100
#define FWCAL_DAD_SPAN_KPA_X100 24100
#endif

// Двигатель: 575 см³ × 4 цилиндра = 2.3 л.
#ifndef FWCAL_CYL_VOLUME_CM3
#define FWCAL_CYL_VOLUME_CM3 575
#endif
#ifndef FWCAL_DISP_CM3
#define FWCAL_DISP_CM3 2300
#endif

// Стехиометрия.
#ifndef FWCAL_AFR_STOICH_X100
#define FWCAL_AFR_STOICH_X100 1470
#endif

#endif // FIRMWARE_CAL_DEFAULT_H
