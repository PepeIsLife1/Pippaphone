#include "oled.h"

#include <cstring>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
const char* TAG = "oled";
constexpr uint16_t ADDR = 0x3C;
constexpr int TIMEOUT_MS = 100;
}  // namespace

bool Oled::begin(int sda, int scl, int reset) {
    if (!bus_) {
        if (reset >= 0) {
            gpio_config_t io = {};
            io.pin_bit_mask = 1ULL << reset;
            io.mode = GPIO_MODE_OUTPUT;
            gpio_config(&io);
            gpio_set_level(gpio_num_t(reset), 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            gpio_set_level(gpio_num_t(reset), 1);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        i2c_master_bus_config_t bc = {};
        bc.i2c_port = -1;  // il primo libero
        bc.sda_io_num = gpio_num_t(sda);
        bc.scl_io_num = gpio_num_t(scl);
        bc.clk_source = I2C_CLK_SRC_DEFAULT;
        bc.glitch_ignore_cnt = 7;
        bc.flags.enable_internal_pullup = true;  // le schede hanno gia' le loro, non guasta
        if (i2c_new_master_bus(&bc, &bus_) != ESP_OK) {
            ESP_LOGE(TAG, "bus I2C non disponibile");
            bus_ = nullptr;
            return false;
        }
        if (i2c_master_probe(bus_, ADDR, TIMEOUT_MS) != ESP_OK) {
            ESP_LOGW(TAG, "display non trovato all'indirizzo 0x3C");
            return false;
        }
        i2c_device_config_t dc = {};
        dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dc.device_address = ADDR;
        dc.scl_speed_hz = 400000;
        if (i2c_master_bus_add_device(bus_, &dc, &dev_) != ESP_OK) return false;
    }
    if (!dev_) return false;
    static const uint8_t init[] = {
        0xAE,        // spento
        0xD5, 0x80,  // frequenza dell'oscillatore
        0xA8, 0x3F,  // 64 righe
        0xD3, 0x00,  // nessuno spostamento verticale
        0x40,        // si parte dalla riga 0
        0x8D, 0x14,  // pompa di carica interna
        0x20, 0x00,  // indirizzamento orizzontale
        0xA1, 0xC8,  // orientamento normale
        0xDA, 0x12,  // collegamento delle righe del pannello 128x64
        0x81, 0xCF,  // contrasto
        0xD9, 0xF1,  // precarica
        0xDB, 0x40,  // livello VCOMH
        0xA4,        // mostra la RAM
        0xA6,        // non invertito
        0x2E,        // niente scorrimento
    };
    ok_ = comandi(init, sizeof(init));
    on_ = false;
    if (!ok_) ESP_LOGW(TAG, "il display non risponde");
    return ok_;
}

bool Oled::comandi(const uint8_t* c, size_t n) {
    uint8_t b[32];
    if (!dev_ || n + 1 > sizeof(b)) return false;
    b[0] = 0x00;  // seguono comandi
    memcpy(b + 1, c, n);
    return i2c_master_transmit(dev_, b, n + 1, TIMEOUT_MS) == ESP_OK;
}

void Oled::accendi(bool on) {
    const uint8_t c = on ? 0xAF : 0xAE;
    if (comandi(&c, 1)) on_ = on;
}

void Oled::capovolto(bool si) {
    const uint8_t c[2] = {uint8_t(si ? 0xA0 : 0xA1), uint8_t(si ? 0xC0 : 0xC8)};
    comandi(c, 2);
}

void Oled::mostra(const uint8_t* fb) {
    if (!ok_) return;
    static const uint8_t finestra[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};  // colonne 0-127, righe 0-7
    if (!comandi(finestra, sizeof(finestra))) return;
    tx_[0] = 0x40;  // seguono i dati
    memcpy(tx_ + 1, fb, 1024);
    i2c_master_transmit(dev_, tx_, sizeof(tx_), TIMEOUT_MS);
}
