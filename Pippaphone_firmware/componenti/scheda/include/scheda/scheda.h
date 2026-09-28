// Scheda Pippaphone: accensione delle periferiche e servizi comuni.
#pragma once

#include <cstdint>

#include "scheda/audio.h"
#include "scheda/batteria.h"
#include "scheda/display.h"
#include "scheda/expander.h"
#include "scheda/gnss.h"
#include "scheda/pins.h"
#include "scheda/tastiera.h"

namespace scheda {

struct InitResult {
    bool expander = false, tastiera = false, batteria = false, display = false, audio = false, gnss = false;
};

// Come e' partito il firmware (rev. 1.1, alimentazione con autoritenuta).
enum class Avvio : uint8_t {
    TASTO,      // accensione (o brownout): il 3,3 V c'e' solo finche' PWR e' premuto
    RIAVVIO,    // riavvio del solo ESP32 (software, watchdog, USB, tasto RESET): era gia' acceso
    RISVEGLIO,  // risveglio dal deep sleep col tasto PWR (solo con JP3 chiuso)
};

// Da chiamare per prima cosa: configura l'expander e porta a 1 PWR_HOLD, cosi' il telefono
// resta acceso anche dopo aver rilasciato PWR. Restituisce il motivo dell'avvio.
Avvio holdPower();

// Accende tutto cio' che serve al telefono (full = false: solo expander e batteria, per il
// firmware del ripetitore sulla stessa scheda).
InitResult init(bool full = true);

// SCK e MOSI della SPI con la corrente d'uscita piu' bassa (fronti piu' lenti, meno disturbi
// verso GNSS e quarzo; le resistenze in serie R56/R57 sono dimensionate per questo).
// Da chiamare dopo l'inizializzazione del bus SPI.
void spiLowDrive();

// Vibrazione (non blocca).
void vibrate(uint16_t ms);
void vibratePattern(uint8_t times, uint16_t onMs, uint16_t offMs);
// LED di notifica: lampeggio (non blocca). count = 0 -> fino a ledOff().
void ledBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t onMs, uint16_t offMs, uint8_t count);
void ledOff();
void keyboardLight(uint8_t level);

// Spegne davvero: PWR_HOLD a 0 e il TPS63001 si ferma appena PWR e' rilasciato. Se il 3,3 V
// resta (JP3 chiuso) va in deep sleep. Si riaccende tenendo premuto PWR.
[[noreturn]] void powerOff();

}  // namespace scheda
