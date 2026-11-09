// Indicatore di carica MAX17048.
#pragma once

#include "scheda/i2c.h"
#include "sdkconfig.h"

namespace scheda {

#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck Plus: niente indicatore di carica, solo la tensione della cella sul partitore 1:2
// (GPIO 4). La percentuale e' stimata dalla curva di scarica di una LiPo; "in carica" si ricava
// dalla tensione che sale (o dalla USB collegata a un computer).
class Batteria {
public:
    bool begin();
    bool ok() const { return ok_; }
    uint8_t percent();
    float volts();
    float ratePerHour();
    bool charging();
    bool clearAlert() { return false; }

private:
    float measure();  // media di piu' letture, in volt
    bool ok_ = false;
    bool charging_ = false;
    float filtered_ = 0;  // tensione filtrata
    float ref_ = 0;       // tensione di riferimento per la tendenza
    int64_t refAtMs_ = 0;
    int64_t lastMs_ = 0;
    float rate_ = 0;      // %/h stimati
};
#else
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
#endif

Batteria& batteria();

}  // namespace scheda
