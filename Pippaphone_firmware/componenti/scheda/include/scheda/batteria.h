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
    // In carica secondo la variazione misurata (rev. 1.1: CHG_N del BQ24074 accende solo il LED
    // rosso e non arriva piu' all'expander). Con isteresi: si' sopra 1 %/h, no sotto 0,2 %/h.
    bool charging();
    // Se l'allarme (ALRT su INT_N) e' attivo lo cancella e rilascia INT_N. true = era attivo.
    bool clearAlert();

private:
    I2cDev dev_;
    bool charging_ = false;
};

Batteria& batteria();

}  // namespace scheda
