#include "pippa_radio/radio_link.h"

#include <RadioLib.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "pippa_radio/esp_hal.h"
#include "pippanet/airtime.h"

namespace pippa {

namespace {
const char* TAG = "radio";
EspHal* g_hal = nullptr;
Module* g_module = nullptr;
SX1262* g_radio = nullptr;
TaskHandle_t g_task = nullptr;

void IRAM_ATTR onDio1() {
    BaseType_t woken = pdFALSE;
    if (g_task) vTaskNotifyGiveFromISR(g_task, &woken);
    portYIELD_FROM_ISR(woken);
}
}  // namespace

pn::Millis RadioLink::nowMs() { return pn::Millis(esp_timer_get_time() / 1000); }

bool RadioLink::begin(const RadioConfig& cfg, pn::Node* node, SemaphoreHandle_t lock) {
    cfg_ = cfg;
    node_ = node;
    lock_ = lock;
    power_ = cfg.maxPowerDbm;

    // Reset dell'SX1262.
    if (cfg.resetFn) {
        cfg.resetFn(false);
        vTaskDelay(pdMS_TO_TICKS(2));
        cfg.resetFn(true);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    g_hal = new EspHal(cfg.host, cfg.sck, cfg.miso, cfg.mosi, cfg.initBus);
    g_module = new Module(g_hal, uint32_t(cfg.nss), uint32_t(cfg.dio1),
                          cfg.reset >= 0 ? uint32_t(cfg.reset) : RADIOLIB_NC, uint32_t(cfg.busy));
    g_radio = new SX1262(g_module);

    const pn::RadioParams& p = pn::MAIN_PARAMS;
    int16_t st = g_radio->begin(p.freqMHz, p.bwKHz, p.sf, p.cr, pn::SYNC_WORD, std::min(p.powerDbm, power_),
                                p.preamble, cfg.tcxoVoltage, false);
    if (st != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "SX1262 non risponde (errore %d)", st);
        return false;
    }
    if (cfg.rxEn >= 0 && cfg.txEn >= 0) g_radio->setRfSwitchPins(uint32_t(cfg.rxEn), uint32_t(cfg.txEn));
    else if (cfg.dio2AsRfSwitch) g_radio->setDio2AsRfSwitch(true);
    g_radio->setCurrentLimit(cfg.currentLimitMa);
    g_radio->setRxBoostedGainMode(true);
    g_radio->setCRC(2);
    g_radio->setDio1Action(onDio1);
    channel_ = pn::Channel::MAIN;
    ok_ = true;
    ESP_LOGI(TAG, "SX1262 pronto: %.3f MHz, SF%d, %.0f kHz", double(p.freqMHz), p.sf, double(p.bwKHz));
    return true;
}

void RadioLink::start(int core, int priority) {
    if (!ok_) return;
    xTaskCreatePinnedToCore(taskEntry, "radio", 8192, this, priority, &task_, core);
    g_task = task_;
}

void RadioLink::wake() {
    if (task_) xTaskNotifyGive(task_);
}

void RadioLink::setPower(int8_t dbm) {
    if (dbm > cfg_.maxPowerDbm) dbm = cfg_.maxPowerDbm;
    if (dbm < -9) dbm = -9;
    xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
    power_ = dbm;
    if (ok_) g_radio->setOutputPower(power_);
    xSemaphoreGiveRecursive(lock_);
}

void RadioLink::taskEntry(void* arg) { static_cast<RadioLink*>(arg)->loop(); }

void RadioLink::startRx() {
    int16_t st = g_radio->startReceive();
    if (st != RADIOLIB_ERR_NONE) ESP_LOGW(TAG, "startReceive: %d", st);
}

void RadioLink::applyChannel(pn::Channel ch) {
    const pn::RadioParams& p = pn::paramsFor(ch);
    g_radio->standby();
    g_radio->setFrequency(p.freqMHz);
    g_radio->setBandwidth(p.bwKHz);
    g_radio->setSpreadingFactor(p.sf);
    g_radio->setPreambleLength(p.preamble);
    g_radio->setOutputPower(std::min(p.powerDbm, power_));
    channel_ = ch;
    ESP_LOGI(TAG, "canale %s: %.3f MHz, %.0f kHz", ch == pn::Channel::VOICE ? "voce" : "messaggi", double(p.freqMHz),
             double(p.bwKHz));
}

void RadioLink::loop() {
    xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
    startRx();
    xSemaphoreGiveRecursive(lock_);
    uint32_t waitMs = 10;
    uint8_t buf[pn::MAX_FRAME];

    for (;;) {
        const uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
        xSemaphoreTakeRecursive(lock_, portMAX_DELAY);
        pn::Millis now = nowMs();

        // Interruzione (o controllo periodico durante una trasmissione).
        if (notified || txBusy_) {
            const uint32_t irq = g_radio->getIrqFlags();
            if (txBusy_ && (irq & RADIOLIB_SX126X_IRQ_TX_DONE)) {
                g_radio->finishTransmit();
                txBusy_ = false;
                node_->onTransmitted(txFrame_.data, txFrame_.len, now);
                startRx();
            } else if (txBusy_ && now - txStarted_ > 10000) {
                ESP_LOGW(TAG, "trasmissione bloccata, riparto");
                g_radio->finishTransmit();
                txBusy_ = false;
                startRx();
            } else if (!txBusy_ && (irq & RADIOLIB_SX126X_IRQ_RX_DONE)) {
                const size_t len = g_radio->getPacketLength();
                const int16_t st = (len > 0 && len <= sizeof(buf)) ? g_radio->readData(buf, len) : RADIOLIB_ERR_PACKET_TOO_LONG;
                if (st == RADIOLIB_ERR_NONE) {
                    pn::RxInfo info;
                    info.rssi = int16_t(g_radio->getRSSI());
                    info.snr = g_radio->getSNR();
                    info.channel = channel_;
                    lastRssi_ = info.rssi;
                    lastSnr_ = info.snr;
                    node_->onReceive(buf, len, info, now);
                } else {
                    rxErrors_++;
                }
                startRx();
            } else if (!txBusy_ && (irq & (RADIOLIB_SX126X_IRQ_CRC_ERR | RADIOLIB_SX126X_IRQ_HEADER_ERR))) {
                rxErrors_++;
                g_radio->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
                startRx();
            }
        }

        node_->tick(now);

        // Cambio di canale (vocali).
        const pn::Channel want = node_->activeChannel(now);
        if (!txBusy_ && want != channel_) {
            applyChannel(want);
            startRx();
        }

        // Trasmissione, dopo aver ascoltato che il canale sia libero.
        if (!txBusy_ && now >= backoffUntil_ && node_->hasTx(now, channel_)) {
            const int16_t cad = g_radio->scanChannel();
            if (cad == RADIOLIB_LORA_DETECTED) {
                backoffUntil_ = now + 100 + pn::randomBelow(600);
                startRx();
            } else if (node_->popTx(now, channel_, txFrame_)) {
                const int16_t st = g_radio->startTransmit(txFrame_.data, txFrame_.len);
                if (st == RADIOLIB_ERR_NONE) {
                    txBusy_ = true;
                    txStarted_ = now;
                } else {
                    ESP_LOGW(TAG, "startTransmit: %d", st);
                    startRx();
                }
            } else {
                startRx();
            }
        }

        pn::Millis next = node_->msUntilNextEvent(now);
        if (txBusy_) next = std::min<pn::Millis>(next, 50);
        if (backoffUntil_ > now) next = std::min<pn::Millis>(next, backoffUntil_ - now);
        waitMs = uint32_t(std::max<pn::Millis>(5, std::min<pn::Millis>(next, 1000)));
        xSemaphoreGiveRecursive(lock_);
    }
}

}  // namespace pippa
