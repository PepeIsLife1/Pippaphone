#include "scheda/batteria.h"

#include "scheda/pins.h"

namespace scheda {

namespace {
constexpr uint8_t REG_VCELL = 0x02, REG_SOC = 0x04, REG_CONFIG = 0x0C, REG_CRATE = 0x16;
}

Batteria& batteria() {
    static Batteria b;
    return b;
}

bool Batteria::begin() {
    if (!dev_.attach(i2cBus(), ADDR_MAX17048)) return false;
    // Allarme sotto il 5%: ALRT (INT_N) sveglia il telefono.
    uint16_t cfg = 0;
    if (dev_.readReg16be(REG_CONFIG, cfg)) {
        cfg = uint16_t((cfg & 0xFFE0) | (32 - 5));
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

}  // namespace scheda
