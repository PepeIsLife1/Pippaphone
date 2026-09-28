#include "scheda/expander.h"

#include "esp_log.h"

namespace scheda {

namespace {
// Registri AW9523B.
constexpr uint8_t REG_IN0 = 0x00, REG_IN1 = 0x01, REG_OUT0 = 0x02, REG_OUT1 = 0x03, REG_CFG0 = 0x04,
                  REG_CFG1 = 0x05, REG_INTEN0 = 0x06, REG_INTEN1 = 0x07, REG_ID = 0x10, REG_GCR = 0x11,
                  REG_LEDMODE0 = 0x12, REG_LEDMODE1 = 0x13, REG_DIM_P0_0 = 0x24;
}  // namespace

Expander& expander() {
    static Expander e;
    return e;
}

bool Expander::begin() {
    if (ok()) return true;
    if (!lock_) lock_ = xSemaphoreCreateMutex();
    if (!dev_.attach(i2cBus(), ADDR_AW9523)) return false;
    heldAtBoot_ = (dev_.readReg8(REG_OUT1) & 0x80) != 0;
    // Per prima cosa l'autoritenuta: PWR_HOLD (P1_7) uscita a 1, CS di display e microSD alti.
    // Niente reset software (REG_RESET): dopo un riavvio del solo ESP32 l'AW9523B tiene ancora
    // acceso il telefono, e un reset porterebbe PWR_HOLD a 0 spegnendo tutto.
    // Stato iniziale: display fuori reset, CS display e CS microSD alti (non selezionati).
    out1_ = 0x80 | 0x07;
    dev_.writeReg(REG_OUT1, out1_);
    // Direzioni della porta 1: P1_0..P1_2 e P1_7 uscite, P1_3..P1_6 ingressi (tasti).
    dev_.writeReg(REG_CFG1, 0x78);
    const uint8_t id = dev_.readReg8(REG_ID);
    if (id != 0x23) ESP_LOGW("expander", "ID inatteso 0x%02x", id);
    // P0 push-pull (serve per AMP_SD e GNSS_EN che hanno pull-down), corrente LED = 1/4 del massimo.
    dev_.writeReg(REG_GCR, 0x10 | 0x03);
    // P0_0..P0_3 in modo LED, il resto GPIO.
    dev_.writeReg(REG_LEDMODE0, 0xF0);
    dev_.writeReg(REG_LEDMODE1, 0xFF);
    // Stato iniziale: GNSS acceso, LoRa fuori reset, amplificatore spento, vibrazione spenta.
    out0_ = (1 << 6) | (1 << 7);
    dev_.writeReg(REG_OUT0, out0_);
    // Porta 0 tutta uscita.
    dev_.writeReg(REG_CFG0, 0x00);
    // Interruzioni (0 = abilitata) solo sui tasti P1_3..P1_6.
    dev_.writeReg(REG_INTEN0, 0xFF);
    dev_.writeReg(REG_INTEN1, 0x87);
    for (int i = 0; i < 4; i++) dev_.writeReg(REG_DIM_P0_0 + i, 0);
    readButtons();
    return true;
}

void Expander::set(Exp pin, bool level) {
    if (!ok()) return;
    xSemaphoreTake(lock_, portMAX_DELAY);
    const uint8_t n = uint8_t(pin);
    if (n < 8) {
        out0_ = level ? (out0_ | (1 << n)) : (out0_ & ~(1 << n));
        dev_.writeReg(REG_OUT0, out0_);
    } else {
        const uint8_t b = n - 8;
        out1_ = level ? (out1_ | (1 << b)) : (out1_ & ~(1 << b));
        dev_.writeReg(REG_OUT1, out1_);
    }
    xSemaphoreGive(lock_);
}

void Expander::led(Exp pin, uint8_t level) {
    const uint8_t n = uint8_t(pin);
    if (!ok() || n > 3) return;
    xSemaphoreTake(lock_, portMAX_DELAY);
    dev_.writeReg(REG_DIM_P0_0 + n, level);
    xSemaphoreGive(lock_);
}

uint8_t Expander::readButtons() {
    if (!ok()) return 0;
    xSemaphoreTake(lock_, portMAX_DELAY);
    dev_.readReg8(REG_IN0);  // cancella anche l'interruzione della porta 0
    const uint8_t in1 = dev_.readReg8(REG_IN1);
    xSemaphoreGive(lock_);
    uint8_t r = 0;
    if (!(in1 & (1 << 3))) r |= BTN_PWR;
    if (!(in1 & (1 << 4))) r |= BTN_PTT;
    if (!(in1 & (1 << 5))) r |= BTN_VOLUP;
    if (!(in1 & (1 << 6))) r |= BTN_VOLDN;
    return r;
}

void Expander::onlyPowerWakes() {
    if (!ok()) return;
    xSemaphoreTake(lock_, portMAX_DELAY);
    dev_.writeReg(REG_INTEN1, 0xF7);
    xSemaphoreGive(lock_);
    readButtons();
}

}  // namespace scheda
