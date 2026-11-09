// LilyGO T-Deck Plus: accensione delle periferiche, bus SPI comune, compito della tastiera,
// notifiche con la luce della tastiera e spegnimento (sonno profondo).
//
// Differenze dalla PCB Pippaphone:
//  - niente autoritenuta: il telefono si accende e si spegne con l'interruttore laterale.
//    "Spegni" (e la batteria scarica) spegne le periferiche (POWERON a 0) e mette l'ESP32-S3 in
//    sonno profondo senza sorgenti di risveglio: si riaccende con l'interruttore (o RST);
//  - niente vibrazione: vibrate() non fa nulla;
//  - niente LED RGB: ledBlink() fa lampeggiare la luce della tastiera, ma solo quando e' spenta
//    (a schermo spento), cosi' non disturba mentre si scrive.
#include <algorithm>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "scheda/scheda.h"

namespace scheda {

namespace {
const char* TAG = "scheda";
bool g_spi = false;
bool g_full = false;
TaskHandle_t g_inputTask = nullptr;
constexpr TickType_t INPUT_PERIOD = pdMS_TO_TICKS(12);

uint8_t g_kbLevel = 0;  // luce della tastiera voluta dall'interfaccia
struct Blink {
    uint8_t level;
    uint16_t on, off;
    uint8_t left;
    bool infinite;
    bool lit;
};
Blink g_blink{};
bool g_blinking = false;
TimerHandle_t g_blinkTimer = nullptr;

// Luce della tastiera: quella voluta, oppure il lampeggio se la luce e' spenta.
void applyKeyboardLight() {
    if (!g_full) return;
    if (g_blinking && g_kbLevel == 0) tastiera().light(g_blink.lit ? g_blink.level : 0);
    else tastiera().light(g_kbLevel);
}

void blinkCb(TimerHandle_t) {
    if (!g_blinking) return;
    if (g_blink.lit) {
        g_blink.lit = false;
        if (!g_blink.infinite && --g_blink.left == 0) {
            g_blinking = false;
            applyKeyboardLight();
            return;
        }
        xTimerChangePeriod(g_blinkTimer, pdMS_TO_TICKS(g_blink.off), 0);
    } else {
        g_blink.lit = true;
        xTimerChangePeriod(g_blinkTimer, pdMS_TO_TICKS(g_blink.on), 0);
    }
    applyKeyboardLight();
}

void inputTask(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        tastiera().service();
        vTaskDelayUntil(&last, INPUT_PERIOD);
    }
}

void outputHigh(gpio_num_t pin) {
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pin;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    gpio_set_level(pin, 1);
}
}  // namespace

Avvio holdPower() {
    // Periferiche accese (display, tastiera, LoRa, GPS, audio).
    gpio_hold_dis(PIN_POWERON);
    gpio_deep_sleep_hold_dis();
    outputHigh(PIN_POWERON);
    // I tre dispositivi del bus SPI comune non selezionati prima di inizializzarlo.
    outputHigh(PIN_LCD_CS);
    outputHigh(PIN_LORA_NSS);
    outputHigh(PIN_SD_CS);
    gpio_set_pull_mode(PIN_SPI_MISO, GPIO_PULLUP_ONLY);
    spi_bus_config_t b = {};
    b.mosi_io_num = PIN_SPI_MOSI;
    b.miso_io_num = PIN_SPI_MISO;
    b.sclk_io_num = PIN_SPI_SCK;
    b.quadwp_io_num = -1;
    b.quadhd_io_num = -1;
    b.max_transfer_sz = Display::WIDTH * 48 * 2 + 64;  // una fascia di LVGL in un colpo solo
    g_spi = spi_bus_initialize(SPI_HOST_SCHEDA, &b, SPI_DMA_CH_AUTO) == ESP_OK;
    if (!g_spi) ESP_LOGE(TAG, "bus SPI non inizializzato");
    vTaskDelay(pdMS_TO_TICKS(100));  // il tempo di accendersi (tastiera, microSD, GPS)
    const esp_reset_reason_t why = esp_reset_reason();
    const Avvio a = why == ESP_RST_POWERON || why == ESP_RST_BROWNOUT ? Avvio::TASTO : Avvio::RIAVVIO;
    ESP_LOGI(TAG, "T-Deck Plus: avvio %s (reset %d)", a == Avvio::TASTO ? "dall'interruttore" : "riavvio",
             int(why));
    return a;
}

bool spiBusReady() { return g_spi; }

InitResult init(bool full) {
    InitResult r;
    r.expander = true;  // non c'e': niente da fare
    r.batteria = batteria().begin();
    g_blinkTimer = xTimerCreate("led", pdMS_TO_TICKS(100), pdFALSE, nullptr, blinkCb);
    if (!full) return r;
    g_full = true;

    r.tastiera = tastiera().begin();
    xTaskCreatePinnedToCore(inputTask, "input", 4096, nullptr, 12, &g_inputTask, 1);
    r.display = display().begin();
    r.audio = audio().begin();
    r.gnss = gnss().begin();
    ESP_LOGI(TAG, "tastiera %d (%s), batteria %d, display %d, audio %d, gnss %d", r.tastiera,
             !tastiera().modeKnown() ? "modo da capire al primo tasto" : tastiera().rawMode() ? "raw" : "caratteri",
             r.batteria, r.display, r.audio, r.gnss);
    return r;
}

void vibrate(uint16_t) {}

void vibratePattern(uint8_t, uint16_t, uint16_t) {}

void ledBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t onMs, uint16_t offMs, uint8_t count) {
    if (!g_blinkTimer) return;
    xTimerStop(g_blinkTimer, 0);
    uint16_t level = std::max(r, std::max(g, b));
    level = std::min<uint16_t>(255, level * 2 + 20);  // la luce della tastiera e' piu' debole del LED
    g_blink = {uint8_t(level), onMs, offMs, count, count == 0, true};
    g_blinking = true;
    applyKeyboardLight();
    xTimerChangePeriod(g_blinkTimer, pdMS_TO_TICKS(onMs), 0);
}

void ledOff() {
    if (g_blinkTimer) xTimerStop(g_blinkTimer, 0);
    g_blinking = false;
    applyKeyboardLight();
}

void keyboardLight(uint8_t level) {
    g_kbLevel = level;
    applyKeyboardLight();
}

void spiLowDrive() {}  // sulla PCB rallenta i fronti; sul T-Deck il bus porta anche il display

void powerOff() {
    ESP_LOGI(TAG, "spegnimento: periferiche spente e sonno profondo (si riaccende con l'interruttore)");
    if (g_full) {
        display().sleep();
        g_kbLevel = 0;
        ledOff();
        tastiera().light(0);
        audio().stop();
        gnss().power(false);
    }
    vTaskDelay(pdMS_TO_TICKS(300));  // comando di standby del GPS e audio svuotato
    gpio_set_level(PIN_LORA_RST, 0);
    gpio_set_level(PIN_POWERON, 0);
    gpio_hold_en(PIN_POWERON);
    gpio_deep_sleep_hold_en();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();
}

}  // namespace scheda
