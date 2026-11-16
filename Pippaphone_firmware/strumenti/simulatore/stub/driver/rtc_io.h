#pragma once
// Quello che usa la tastiera del T-Deck (tastiera_tdeck.cpp) dopo il risveglio col trackball: nel
// simulatore nel browser il sonno profondo non c'e', quindi non fa niente.
#include "driver/gpio.h"
static inline int rtc_gpio_deinit(gpio_num_t) { return 0; }
