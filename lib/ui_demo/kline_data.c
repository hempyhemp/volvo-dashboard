#include "kline_data.h"

static kline_data_t g_kline_data = {0};

void kline_data_set(const kline_data_t *data) {
  g_kline_data = *data;
}

void kline_data_get(kline_data_t *out) {
  *out = g_kline_data;
}
