// PC-симулятор интерфейса приборки: LVGL рисует в обычное окно SDL2
// вместо реального TFT_eSPI/ILI9341. Логика экрана (ui/ui_demo.c) общая
// с тем, что позже будет запускаться на ESP32-2432S028.

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "lvgl.h"
#include "ui_demo.h"

#define WINDOW_WIDTH 320
#define WINDOW_HEIGHT 240

int main(void) {
  lv_init();

  lv_display_t *disp = lv_sdl_window_create(WINDOW_WIDTH, WINDOW_HEIGHT);
  (void)disp;
  lv_sdl_mouse_create();

  ui_demo_create();

  uint32_t last_tick = SDL_GetTicks();

  while (1) {
    uint32_t now = SDL_GetTicks();
    lv_tick_inc(now - last_tick);
    last_tick = now;

    ui_demo_update_uptime(now / 1000);

    uint32_t idle_ms = lv_timer_handler();
    SDL_Delay(idle_ms < 5 ? 5 : idle_ms);
  }

  return 0;
}
