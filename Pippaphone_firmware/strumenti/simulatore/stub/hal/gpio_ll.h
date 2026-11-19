#pragma once
// Quello che usa tastiera_tdeck.cpp nelle sue interruzioni a livello (C3_INT e clic, solo col firmware del C3
// del Pippaphone): nel simulatore nel browser non scattano mai.
#include <cstddef>
#include <cstdint>
struct gpio_dev_t;
#define GPIO_LL_GET_HW(num) (static_cast<gpio_dev_t*>(nullptr))
static inline void gpio_ll_intr_disable(gpio_dev_t*, uint32_t) {}
