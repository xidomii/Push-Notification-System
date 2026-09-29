// Minimal LVGL 8.3 Konfiguration fuer SmartServe Watch (ESP32 + GC9A01).
// Nicht gesetzte Optionen -> LVGL-Defaults (lv_conf_internal.h).
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// Farbtiefe 16 Bit (RGB565). Byte-Swap fuer TFT_eSPI/SPI-Displays.
#define LV_COLOR_DEPTH        16
#define LV_COLOR_16_SWAP      1

// Arbeitsspeicher fuer LVGL (WROOM-32 hat kein PSRAM -> knapp halten).
#define LV_MEM_SIZE           (36U * 1024U)

// Tick per lv_tick_inc() aus loop() (kein eigener Timer noetig).
#define LV_TICK_CUSTOM        0

// Fonts, die die GUI nutzt (Montserrat, in LVGL enthalten).
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT       &lv_font_montserrat_14

// Logging aus (spart Flash/RAM).
#define LV_USE_LOG            0

#endif // LV_CONF_H
