// Batteria del LilyGO T3-S3: tensione della cella letta su un ingresso dell'ADC attraverso il
// partitore 100k/100k della scheda (meta' della tensione).
#pragma once

#include <cstdint>

namespace batteria {

bool begin(int pin);
// Tensione della cella in mV, media di 16 letture; 0 se l'ADC non funziona.
int millivolt();
// Percentuale stimata dalla curva di scarica di una cella al litio.
uint8_t percento(int mv);
// Sotto questa tensione la batteria non e' collegata (si legge il caricabatterie a vuoto o niente).
constexpr int MV_ASSENTE = 2500;

}  // namespace batteria
