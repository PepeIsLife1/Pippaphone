// Display OLED SSD1306 128x64 su I2C (indirizzo 0x3C), come su Heltec WiFi LoRa 32 V3 e
// LilyGO T3-S3. Lo usa un solo compito alla volta (non c'e' un lock).
#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2c_master.h"

class Oled {
public:
    // Prepara il bus I2C e il display e lo lascia SPENTO. reset = -1 se il pin di reset del
    // display non e' collegato all'ESP32. Si puo' richiamare: il bus si crea una volta sola.
    bool begin(int sda, int scl, int reset);
    bool ok() const { return ok_; }

    void accendi(bool on);
    bool acceso() const { return on_; }
    // Immagine ruotata di 180 gradi (dipende da come e' montata la scheda nella scatola).
    void capovolto(bool si);
    // Manda il framebuffer: 1024 byte nel formato dell'SSD1306 (vedi schermo.h).
    void mostra(const uint8_t* fb);

private:
    bool comandi(const uint8_t* c, size_t n);

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    bool ok_ = false;
    bool on_ = false;
    uint8_t tx_[1 + 1024] = {};
};
