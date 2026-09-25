// Bus I2C della scheda (driver i2c_master di ESP-IDF, gia' protetto per l'uso da piu' compiti).
#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2c_master.h"

namespace scheda {

class I2cDev {
public:
    bool attach(i2c_master_bus_handle_t bus, uint8_t addr, uint32_t hz = 400000);
    bool present() const { return dev_ != nullptr; }
    bool write(const uint8_t* data, size_t len);
    bool writeReg(uint8_t reg, uint8_t value);
    bool readReg(uint8_t reg, uint8_t* out, size_t len);
    uint8_t readReg8(uint8_t reg, bool* ok = nullptr);
    bool readReg16be(uint8_t reg, uint16_t& out);

private:
    i2c_master_dev_handle_t dev_ = nullptr;
};

// Crea il bus (una volta sola).
i2c_master_bus_handle_t i2cBus();

}  // namespace scheda
