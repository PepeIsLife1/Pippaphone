// ES7210 in modo slave, 16 kHz, MCLK 4,096 MHz, I2S normale a 16 bit, MIC1 e MIC2.
// Sequenza dei registri dal driver di Espressif adattato da LilyGO (lib/es7210 del T-Deck).
#include "es7210.h"

#include "esp_log.h"
#include "scheda/i2c.h"
#include "scheda/pins.h"

namespace scheda {
namespace es7210 {

namespace {
const char* TAG = "es7210";
I2cDev g_dev;

constexpr uint8_t RESET = 0x00, CLOCK_OFF = 0x01, MAINCLK = 0x02, LRCK_DIVH = 0x04, LRCK_DIVL = 0x05,
                  POWER_DOWN = 0x06, OSR = 0x07, TIME_CONTROL0 = 0x09, TIME_CONTROL1 = 0x0A,
                  SDP_INTERFACE1 = 0x11, SDP_INTERFACE2 = 0x12, ANALOG = 0x40, MIC12_BIAS = 0x41,
                  MIC34_BIAS = 0x42, MIC1_GAIN = 0x43, MIC1_POWER = 0x47, MIC12_POWER = 0x4B,
                  MIC34_POWER = 0x4C;

void w(uint8_t reg, uint8_t v) { g_dev.writeReg(reg, v); }

void update(uint8_t reg, uint8_t mask, uint8_t v) {
    bool ok = false;
    const uint8_t old = g_dev.readReg8(reg, &ok);
    if (ok) w(reg, uint8_t((old & ~mask) | (v & mask)));
}

// Clock per 16 kHz con MCLK = 4,096 MHz: adc_div 1, doubler, dll; osr 0x20; lrck 0x0100.
void sampleRate() {
    w(MAINCLK, 0xC1);
    w(OSR, 0x20);
    w(LRCK_DIVH, 0x01);
    w(LRCK_DIVL, 0x00);
}

// Accende ADC e PGA di MIC1 e MIC2 (MIC3 e MIC4 restano spenti).
void selectMic12() {
    for (int i = 0; i < 4; i++) update(uint8_t(MIC1_GAIN + i), 0x10, 0x00);
    w(MIC12_POWER, 0xFF);
    w(MIC34_POWER, 0xFF);
    update(CLOCK_OFF, 0x0B, 0x00);
    w(MIC12_POWER, 0x00);
    update(MIC1_GAIN, 0x10, 0x10);
    update(uint8_t(MIC1_GAIN + 1), 0x10, 0x10);
}
}  // namespace

bool begin() {
    if (!g_dev.present() && !g_dev.attach(i2cBus(), ADDR_ES7210)) {
        ESP_LOGE(TAG, "ES7210 non risponde: niente microfono");
        return false;
    }
    w(RESET, 0xFF);
    w(RESET, 0x41);
    w(CLOCK_OFF, 0x1F);
    w(TIME_CONTROL0, 0x30);
    w(TIME_CONTROL1, 0x30);
    // Slave: niente da impostare (MCLK, BCLK e LRCK arrivano dall'ESP32).
    w(ANALOG, 0xC3);      // VDDA 3,3 V, VMID 5 kohm
    w(MIC12_BIAS, 0x70);  // polarizzazione dei microfoni 2,87 V
    w(MIC34_BIAS, 0x70);
    w(OSR, 0x20);
    w(MAINCLK, 0xC1);
    sampleRate();
    selectMic12();
    // 16 bit, formato I2S normale; ADC1/2 su SDOUT1 (niente TDM).
    update(SDP_INTERFACE1, 0xE0, 0x60);
    update(SDP_INTERFACE1, 0x03, 0x00);
    w(SDP_INTERFACE2, 0x00);
    sampleRate();
    setGain(8);  // 24 dB: da regolare sull'hardware
    ESP_LOGI(TAG, "ES7210 pronto (MIC1 e MIC2, 16 kHz)");
    return true;
}

void start() {
    if (!g_dev.present()) return;
    bool ok = false;
    uint8_t clk = g_dev.readReg8(CLOCK_OFF, &ok);
    if (!ok || clk == 0x7F || clk == 0xFF) clk = 0x14;  // MIC1 e MIC2 accesi, MIC3 e MIC4 spenti
    w(CLOCK_OFF, clk);
    w(POWER_DOWN, 0x00);
    w(ANALOG, 0xC3);  // parte analogica di nuovo accesa (VMID 5 kohm, come in begin)
    for (int i = 0; i < 4; i++) w(uint8_t(MIC1_POWER + i), 0x00);
    selectMic12();
}

// Come es7210_stop() di Espressif: oltre a canali e clock si spegne anche la parte analogica (VMID), che se
// no resta accesa tra una registrazione e l'altra.
void stop() {
    if (!g_dev.present()) return;
    for (int i = 0; i < 4; i++) w(uint8_t(MIC1_POWER + i), 0xFF);
    w(MIC12_POWER, 0xFF);
    w(MIC34_POWER, 0xFF);
    w(ANALOG, 0xC0);
    w(CLOCK_OFF, 0x7F);
    w(POWER_DOWN, 0x07);
}

void setGain(uint8_t step) {
    if (!g_dev.present()) return;
    if (step > 14) step = 14;
    update(MIC1_GAIN, 0x0F, step);
    update(uint8_t(MIC1_GAIN + 1), 0x0F, step);
}

}  // namespace es7210
}  // namespace scheda
