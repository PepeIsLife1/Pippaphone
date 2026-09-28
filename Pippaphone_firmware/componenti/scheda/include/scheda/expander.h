// Expander AW9523B: LED di notifica, retroilluminazione tastiera, vibrazione, accensioni,
// reset di LoRa e display, CS della microSD, tasti laterali e autoritenuta dell'alimentazione.
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "scheda/i2c.h"
#include "scheda/pins.h"

namespace scheda {

class Expander {
public:
    // Configura le porte. La prima scrittura porta a 1 PWR_HOLD. Si puo' chiamare piu' volte.
    bool begin();
    bool ok() const { return dev_.present(); }
    // PWR_HOLD era gia' a 1 prima di begin(): il telefono era acceso e si e' riavviato solo
    // l'ESP32 (anche col tasto RESET). Dopo un'accensione vera l'AW9523B riparte con tutto a 0.
    bool heldAtBoot() const { return heldAtBoot_; }

    // Uscite digitali (VIB_EN, AMP_SD, GNSS_EN, reset, CS, PWR_HOLD).
    void set(Exp pin, bool level);
    // LED in modo corrente costante: 0 = spento, 255 = massimo.
    void led(Exp pin, uint8_t level);
    void rgb(uint8_t r, uint8_t g, uint8_t b) { led(Exp::LED_R, r); led(Exp::LED_G, g); led(Exp::LED_B, b); }
    // Legge i tasti laterali; leggere cancella l'interruzione. Bit a 1 = premuto.
    uint8_t readButtons();
    // Solo il tasto PWR genera interruzioni (prima del deep sleep, se JP3 e' chiuso).
    void onlyPowerWakes();

    // Bit restituiti da readButtons().
    static constexpr uint8_t BTN_PWR = 0x01, BTN_PTT = 0x02, BTN_VOLUP = 0x04, BTN_VOLDN = 0x08;

private:
    I2cDev dev_;
    SemaphoreHandle_t lock_ = nullptr;
    uint8_t out0_ = 0, out1_ = 0;
    bool heldAtBoot_ = false;
};

Expander& expander();

}  // namespace scheda
