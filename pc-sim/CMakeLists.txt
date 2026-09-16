cmake_minimum_required(VERSION 3.15)
project(cyd_dashboard_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Демо/примеры LVGL нам не нужны — отключаем, чтобы сборка была быстрее.
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(lib/lvgl)

find_package(SDL2 CONFIG REQUIRED)

# Драйвер LV_USE_SDL компилируется внутри самой библиотеки lvgl (см.
# lv_conf.h), поэтому SDL2 нужно подключить и к таргету lvgl, а не
# только к нашему исполняемому файлу — иначе SDL2/SDL.h не найдётся
# при сборке lvgl.
target_link_libraries(lvgl PUBLIC SDL2::SDL2)

# ../lib/ui_demo — ЕДИНСТВЕННАЯ копия кода интерфейса, общая с прошивкой
# ESP32 (там она лежит в lib/ui_demo — стандартном месте PlatformIO для
# локальных библиотек проекта). Файл не дублируется — оба сборщика
# (CMake здесь и PlatformIO у прошивки) ссылаются на один и тот же файл.
add_executable(sim
    src/main.c
    ../lib/ui_demo/ui_demo.c
    ../lib/ui_demo/screen_debug.c
    ../lib/ui_demo/screen_online.c
    ../lib/ui_demo/kline_data.c
    ../lib/ui_demo/fonts/font_cyrillic_16.c
    ../lib/ui_demo/images/img_cat_bg.c
)

target_include_directories(sim PRIVATE ../lib/ui_demo)

target_link_libraries(sim PRIVATE
    lvgl::lvgl
    SDL2::SDL2
)
