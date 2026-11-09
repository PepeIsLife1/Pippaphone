// LVGL per il simulatore: le stesse scelte di telefono/sdkconfig.defaults (il resto e' il
// predefinito di LVGL, come nel firmware).
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_DEF_REFR_PERIOD 20
#define LV_USE_OBSERVER 0
#define LV_USE_SNAPSHOT 0
#define LV_USE_LOG 0
#define LV_BUILD_EXAMPLES 0
// Mappe del T-Deck: tile PNG dalla microSD.
#define LV_USE_LODEPNG 1

#endif
