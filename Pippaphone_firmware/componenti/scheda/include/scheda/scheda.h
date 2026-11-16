// Scheda Pippaphone: accensione delle periferiche e servizi comuni.
#pragma once

#include <cstdint>

#include "scheda/audio.h"
#include "scheda/batteria.h"
#include "scheda/display.h"
#include "sdkconfig.h"
#if !CONFIG_SCHEDA_TDECK_PLUS
#include "scheda/expander.h"
#endif
#include "scheda/gnss.h"
#include "scheda/pins.h"
#include "scheda/tastiera.h"

namespace scheda {

#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck Plus: niente expander, vibrazione, LED RGB e autoritenuta. Le funzioni qui sotto ci sono
// lo stesso: la vibrazione non fa nulla, il LED di notifica lampeggia con la luce della tastiera
// (solo quando e' spenta), "spegni" e' il sonno profondo con le periferiche spente (si riaccende
// con l'interruttore laterale). In piu' la microSD sul bus SPI comune.
constexpr bool HAS_VIBRATION = false;
// Bus SPI comune gia' pronto (lo crea holdPower()).
bool spiBusReady();
// Turno sul bus SPI comune (display, LoRa, microSD): un dispositivo alla volta, dall'inizio della
// sua transazione finche' l'hardware ha finito davvero. Il lock di ESP-IDF da solo non bastava: il
// display libera il bus con le trasmissioni ancora in coda e ogni tanto (dopo un riavvio software)
// la microSD partiva con l'hardware ancora occupato -> assert in spi_hal_setup_trans e riavvio.
void busSpiPrendi();
void busSpiLascia();
#else
constexpr bool HAS_VIBRATION = true;
#endif

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

// Sveglia a telefono spento: prima di powerOff() chiede di riaccendersi tra 'seconds' secondi
// (0 = mai). Vale solo sul T-Deck, dove "Spegni" e' un sonno profondo; sulla PCB lo spegnimento e'
// vero (l'ESP32 resta senza corrente) e non fa niente.
void wakeAfter(uint32_t seconds);
// Acceso da quel risveglio programmato (T-Deck); sulla PCB sempre false.
bool wokeForAlarm();
// Uscito dal sonno profondo (sveglia, trackball del T-Deck, PWR sulla PCB col ponticello JP3): la
// memoria RTC e l'orologio del chip sono quelli di prima di "Spegni".
bool wokeFromSleep();

}  // namespace scheda
