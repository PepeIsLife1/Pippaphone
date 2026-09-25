#include "scheda/i2c.h"

#include "esp_log.h"
#include "scheda/pins.h"

namespace scheda {

namespace {
constexpr int TIMEOUT_MS = 50;
i2c_master_bus_handle_t g_bus = nullptr;
}  // namespace

i2c_master_bus_handle_t i2cBus() {
    if (g_bus) return g_bus;
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = PIN_I2C_SDA;
    cfg.scl_io_num = PIN_I2C_SCL;
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = false;  // pull-up da 4,7k sulla scheda
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &g_bus));
    return g_bus;
}

bool I2cDev::attach(i2c_master_bus_handle_t bus, uint8_t addr, uint32_t hz) {
    if (i2c_master_probe(bus, addr, TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW("i2c", "nessun dispositivo a 0x%02x", addr);
        return false;
    }
    i2c_device_config_t d = {};
    d.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    d.device_address = addr;
    d.scl_speed_hz = hz;
    return i2c_master_bus_add_device(bus, &d, &dev_) == ESP_OK;
}

bool I2cDev::write(const uint8_t* data, size_t len) {
    return dev_ && i2c_master_transmit(dev_, data, len, TIMEOUT_MS) == ESP_OK;
}

bool I2cDev::writeReg(uint8_t reg, uint8_t value) {
    uint8_t b[2] = {reg, value};
    return write(b, 2);
}

bool I2cDev::readReg(uint8_t reg, uint8_t* out, size_t len) {
    return dev_ && i2c_master_transmit_receive(dev_, &reg, 1, out, len, TIMEOUT_MS) == ESP_OK;
}

uint8_t I2cDev::readReg8(uint8_t reg, bool* ok) {
    uint8_t v = 0;
    bool r = readReg(reg, &v, 1);
    if (ok) *ok = r;
    return v;
}

bool I2cDev::readReg16be(uint8_t reg, uint16_t& out) {
    uint8_t b[2];
    if (!readReg(reg, b, 2)) return false;
    out = uint16_t((b[0] << 8) | b[1]);
    return true;
}

}  // namespace scheda
