#include "scheda/batteria.h"

#include "esp_log.h"
#include "scheda/pins.h"

namespace scheda {

namespace {
constexpr uint8_t REG_VCELL = 0x02, REG_SOC = 0x04, REG_CONFIG = 0x0C, REG_CRATE = 0x16, REG_STATUS = 0x1A;
constexpr uint16_t CONFIG_ALRT = 0x0020;  // CONFIG.ALRT: a 1 finche' l'allarme non viene cancellato

// STATUS: azzera i flag degli allarmi (RI, VH, VL, VR, HD, SC) e lascia EnVR.
void clearStatus(I2cDev& dev) {
    uint16_t st;
    if (!dev.readReg16be(REG_STATUS, st)) return;
    uint8_t b[3] = {REG_STATUS, uint8_t((st >> 8) & 0xC0), uint8_t(st)};
    dev.write(b, 3);
}
}

Batteria& batteria() {
    static Batteria b;
    return b;
}

bool Batteria::begin() {
    if (!dev_.attach(i2cBus(), ADDR_MAX17048)) return false;
    // Allarme sotto il 5%: ALRT tiene bassa INT_N (condivisa con expander e tastiera) finche'
    // non viene cancellato con clearAlert(). Qui si cancella anche un allarme rimasto dal
    // giro precedente: il MAX17048 e' alimentato dalla batteria e lo ricorda anche a telefono
    // spento.
    clearStatus(dev_);
    uint16_t cfg = 0;
    if (dev_.readReg16be(REG_CONFIG, cfg)) {
        cfg = uint16_t((cfg & 0xFFC0) | (32 - 5));
        uint8_t b[3] = {REG_CONFIG, uint8_t(cfg >> 8), uint8_t(cfg)};
        dev_.write(b, 3);
    }
    return true;
}

uint8_t Batteria::percent() {
    uint16_t v;
    if (!ok() || !dev_.readReg16be(REG_SOC, v)) return 255;
    unsigned p = v >> 8;
    return uint8_t(p > 100 ? 100 : p);
}

float Batteria::volts() {
    uint16_t v;
    if (!ok() || !dev_.readReg16be(REG_VCELL, v)) return 0;
    return float(v) * 78.125e-6f;
}

float Batteria::ratePerHour() {
    uint16_t v;
    if (!ok() || !dev_.readReg16be(REG_CRATE, v)) return 0;
    return float(int16_t(v)) * 0.208f;
}

bool Batteria::clearAlert() {
    uint16_t cfg;
    if (!ok() || !dev_.readReg16be(REG_CONFIG, cfg) || !(cfg & CONFIG_ALRT)) return false;
    ESP_LOGI("batteria", "allarme del MAX17048 (%u%%)", unsigned(percent()));
    clearStatus(dev_);
    cfg &= ~CONFIG_ALRT;
    uint8_t b[3] = {REG_CONFIG, uint8_t(cfg >> 8), uint8_t(cfg)};
    dev_.write(b, 3);
    return true;
}

bool Batteria::charging() {
    const float r = ratePerHour();
    if (r > 1.0f) charging_ = true;
    else if (r < 0.2f) charging_ = false;
    return charging_;
}

}  // namespace scheda
