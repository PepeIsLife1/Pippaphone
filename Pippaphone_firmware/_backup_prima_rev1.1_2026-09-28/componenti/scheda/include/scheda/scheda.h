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

// Accende tutto cio' che serve al telefono (full = false: solo expander e batteria, per il
// firmware del ripetitore sulla stessa scheda).
InitResult init(bool full = true);

// Vibrazione (non blocca).
void vibrate(uint16_t ms);
void vibratePattern(uint8_t times, uint16_t onMs, uint16_t offMs);
// LED di notifica: lampeggio (non blocca). count = 0 -> fino a ledOff().
void ledBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t onMs, uint16_t offMs, uint8_t count);
void ledOff();
void keyboardLight(uint8_t level);

// Spegne (deep sleep). Si riaccende tenendo premuto PWR.
[[noreturn]] void powerOff();
// true se l'avvio viene dal deep sleep (tasto PWR).
bool wokeFromSleep();

}  // namespace scheda
