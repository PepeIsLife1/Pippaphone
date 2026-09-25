// Indicatore di carica MAX17048.
#pragma once

#include "scheda/i2c.h"

namespace scheda {

class Batteria {
public:
    bool begin();
    bool ok() const { return dev_.present(); }
    // Percentuale 0..100 (255 se non disponibile).
    uint8_t percent();
    float volts();
    // Variazione in %/ora (positiva = in carica).
    float ratePerHour();

private:
    I2cDev dev_;
};

Batteria& batteria();

}  // namespace scheda
