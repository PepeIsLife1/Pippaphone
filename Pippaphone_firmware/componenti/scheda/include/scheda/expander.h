// Expander AW9523B: LED di notifica, retroilluminazione tastiera, vibrazione, accensioni,
// reset di LoRa e display, CS della microSD e tasti laterali.
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "scheda/i2c.h"
#include "scheda/pins.h"

namespace scheda {

class Expander {
public:
    bool begin();
    bool ok() const { return dev_.present(); }

    // Uscite digitali (VIB_EN, AMP_SD, GNSS_EN, reset, CS).
    void set(Exp pin, bool level);
    // LED in modo corrente costante: 0 = spento, 255 = massimo.
    void led(Exp pin, uint8_t level);
    void rgb(uint8_t r, uint8_t g, uint8_t b) { led(Exp::LED_R, r); led(Exp::LED_G, g); led(Exp::LED_B, b); }
    // Legge gli ingressi (tasti laterali e carica); leggere cancella l'interruzione.
    // Bit a 1 = premuto / in carica.
    uint8_t readButtons();
    bool charging();
    // Solo il tasto PWR genera interruzioni (prima dello spegnimento).
    void onlyPowerWakes();

    // Bit restituiti da readButtons().
    static constexpr uint8_t BTN_PWR = 0x01, BTN_PTT = 0x02, BTN_VOLUP = 0x04, BTN_VOLDN = 0x08, CHARGING = 0x10;

private:
    I2cDev dev_;
    SemaphoreHandle_t lock_ = nullptr;
    uint8_t out0_ = 0, out1_ = 0;
};

Expander& expander();

}  // namespace scheda
