// Turno sul bus SPI comune del T-Deck (vedi busSpiPrendi() in scheda.h): le due funzioni che usa
// solo il display, che restituisce il turno dall'interruzione di fine trasmissione.
#pragma once

#include <cstdint>

namespace scheda {

// Come busSpiPrendi(), ma rinuncia dopo ms millisecondi (false).
bool busSpiPrendiEntro(uint32_t ms);
// Da un'interruzione: true se un compito e' stato svegliato.
bool busSpiLasciaDaIsr();

}  // namespace scheda
