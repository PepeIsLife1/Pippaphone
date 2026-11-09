// Codec ES7210 del microfono del T-Deck Plus (ADC a 4 canali, configurato via I2C).
//
// L'ESP32-S3 fa da master I2S: MCLK = 256 x 16 kHz, dati a 16 bit in formato I2S normale. In
// questo modo l'ES7210 manda MIC1 (sinistro) e MIC2 (destro) su SDOUT1, l'unico collegato
// all'ESP32: come nel firmware di fabbrica LilyGO, che usa MIC1 e MIC2.
#pragma once

#include <cstdint>

namespace scheda {
namespace es7210 {

// Configura il codec (dopo aver avviato MCLK sull'I2S). false se non risponde.
bool begin();
// Microfoni accesi o spenti (fuori dalle registrazioni si spengono).
void start();
void stop();
// Guadagno analogico di MIC1 e MIC2: 0 = 0 dB ... 14 = 37,5 dB (passi di 3 dB, poi 1,5 dB).
void setGain(uint8_t step);

}  // namespace es7210
}  // namespace scheda
