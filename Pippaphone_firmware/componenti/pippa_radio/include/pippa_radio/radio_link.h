// Compito FreeRTOS che fa girare la radio SX1262 per un nodo PippaNet.
//
// Riceve (interruzione su DIO1), passa i pacchetti al nodo, gli fa scorrere il tempo e
// trasmette cio' che il nodo ha in coda, ascoltando prima il canale (CAD). Cambia da solo
// frequenza e banda quando il nodo passa al canale voce.
#pragma once

#include <functional>

#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "pippanet/node.h"

namespace pippa {

struct RadioConfig {
    spi_host_device_t host = SPI2_HOST;
    int sck = -1, mosi = -1, miso = -1;
    int nss = -1, dio1 = -1, busy = -1;
    int reset = -1;                      // -1: reset con resetFn (es. da un expander)
    std::function<void(bool)> resetFn;   // livello del pin di reset
    bool initBus = true;
    float tcxoVoltage = 1.8f;            // 0 = quarzo senza TCXO
    bool dio2AsRfSwitch = true;
    int rxEn = -1, txEn = -1;            // moduli con switch RF comandato a parte (es. Ebyte E22)
    float currentLimitMa = 140.0f;
    int8_t maxPowerDbm = 22;
};

class RadioLink {
public:
    // lock (mutex RICORSIVO) protegge il nodo: chi chiama i metodi del nodo da un altro
    // compito deve prenderlo con xSemaphoreTakeRecursive.
    bool begin(const RadioConfig& cfg, pn::Node* node, SemaphoreHandle_t lock);
    void start(int core = 0, int priority = 10);

    bool ok() const { return ok_; }
    // Potenza di trasmissione (dBm), limitata a maxPowerDbm.
    void setPower(int8_t dbm);
    int8_t power() const { return power_; }
    int16_t lastRssi() const { return lastRssi_; }
    float lastSnr() const { return lastSnr_; }
    uint32_t rxErrors() const { return rxErrors_; }
    // Richiama il compito (per esempio dopo aver accodato un messaggio).
    void wake();
    // Per le statistiche: millisecondi dall'avvio (stessa base del nodo).
    static pn::Millis nowMs();

private:
    static void taskEntry(void* arg);
    void loop();
    void applyChannel(pn::Channel ch);
    void startRx();

    RadioConfig cfg_;
    pn::Node* node_ = nullptr;
    SemaphoreHandle_t lock_ = nullptr;
    TaskHandle_t task_ = nullptr;
    bool ok_ = false;
    pn::Channel channel_ = pn::Channel::MAIN;
    int8_t power_ = 22;
    int16_t lastRssi_ = 0;
    float lastSnr_ = 0;
    uint32_t rxErrors_ = 0;
    bool txBusy_ = false;
    pn::Millis txStarted_ = 0;
    pn::Millis backoffUntil_ = 0;
    pn::Frame txFrame_;
};

}  // namespace pippa
