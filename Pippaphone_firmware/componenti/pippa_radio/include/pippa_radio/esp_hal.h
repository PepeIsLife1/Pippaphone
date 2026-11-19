// Collegamento di RadioLib all'hardware dell'ESP32-S3 con i driver di ESP-IDF.
#pragma once

#include <RadioLib.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

namespace pippa {

class EspHal : public RadioLibHal {
public:
    // initBus = false se il bus SPI e' gia' stato inizializzato da altri (es. microSD).
    EspHal(spi_host_device_t host, int sck, int miso, int mosi, bool initBus, int clockHz = 8000000);
    // Bus condiviso con dispositivi che hanno bisogno di un turno in piu' oltre al lock di ESP-IDF
    // (T-Deck Plus): prima e dopo ogni transazione. Senza, non si chiama niente.
    void setBusTurn(void (*take)(), void (*give)()) {
        busTake_ = take;
        busGive_ = give;
    }
    // Interruzione di DIO1 a livello invece che sul fronte, valida anche come risveglio dal light sleep
    // (gpio_wakeup_enable vuole un livello; un fronte durante il light sleep si perde). Da chiamare prima di
    // attachInterrupt. Quando scatta l'interruzione si spegne da sola (niente raffica finche' DIO1 resta
    // alto): la riaccende rearmInterrupt(), dopo aver letto la radio.
    void setLevelInterrupt(bool on) { level_ = on; }
    // Riaccende l'interruzione a livello se il pin e' tornato basso: true; false se e' ancora alto (la radio
    // ha un'altra interruzione da leggere: si riguarda presto). Senza livello: sempre true.
    bool rearmInterrupt();

    void init() override;
    void term() override;
    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;
    void delay(RadioLibTime_t ms) override;
    void delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;
    void spiBegin() override {}
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd() override {}

private:
    spi_host_device_t host_;
    int sck_, miso_, mosi_;
    bool initBus_;
    int clockHz_;
    spi_device_handle_t dev_ = nullptr;
    void (*busTake_)() = nullptr;
    void (*busGive_)() = nullptr;
    bool level_ = false;
};

}  // namespace pippa
