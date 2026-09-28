#include "scheda/scheda.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

namespace scheda {

namespace {
const char* TAG = "scheda";
TaskHandle_t g_inputTask = nullptr;

void IRAM_ATTR onIntN(void*) {
    BaseType_t woken = pdFALSE;
    if (g_inputTask) vTaskNotifyGiveFromISR(g_inputTask, &woken);
    portYIELD_FROM_ISR(woken);
}

// Legge tastiera e tasti laterali quando INT_N scende (e comunque ogni tanto, se un fronte
// si perde mentre la linea e' gia' bassa).
void inputTask(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
        tastiera().service();
        // Linea ancora bassa: c'e' altro da leggere.
        int guard = 0;
        while (gpio_get_level(PIN_INT_N) == 0 && guard++ < 8) tastiera().service();
    }
}

// Vibrazione e LED gestiti da un timer software.
struct Blink {
    uint8_t r, g, b;
    uint16_t on, off;
    uint8_t left;
    bool infinite;
    bool lit;
};
Blink g_blink{};
TimerHandle_t g_blinkTimer = nullptr;
TimerHandle_t g_vibTimer = nullptr;
uint8_t g_vibLeft = 0;
uint16_t g_vibOn = 0, g_vibOff = 0;
bool g_vibActive = false;

void blinkCb(TimerHandle_t) {
    if (g_blink.lit) {
        expander().rgb(0, 0, 0);
        g_blink.lit = false;
        if (!g_blink.infinite && --g_blink.left == 0) return;
        xTimerChangePeriod(g_blinkTimer, pdMS_TO_TICKS(g_blink.off), 0);
    } else {
        expander().rgb(g_blink.r, g_blink.g, g_blink.b);
        g_blink.lit = true;
        xTimerChangePeriod(g_blinkTimer, pdMS_TO_TICKS(g_blink.on), 0);
    }
}

void vibCb(TimerHandle_t) {
    if (g_vibActive) {
        expander().set(Exp::VIB_EN, false);
        g_vibActive = false;
        if (--g_vibLeft == 0) return;
        xTimerChangePeriod(g_vibTimer, pdMS_TO_TICKS(g_vibOff), 0);
    } else {
        expander().set(Exp::VIB_EN, true);
        g_vibActive = true;
        xTimerChangePeriod(g_vibTimer, pdMS_TO_TICKS(g_vibOn), 0);
    }
}
}  // namespace

InitResult init(bool full) {
    InitResult r;
    // INT_N: ingresso con pull-up esterno, interruzione sul fronte di discesa.
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_INT_N;
    io.mode = GPIO_MODE_INPUT;
    io.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&io);

    r.expander = expander().begin();
    r.batteria = batteria().begin();
    g_blinkTimer = xTimerCreate("led", pdMS_TO_TICKS(100), pdFALSE, nullptr, blinkCb);
    g_vibTimer = xTimerCreate("vib", pdMS_TO_TICKS(100), pdFALSE, nullptr, vibCb);
    if (!full) return r;

    r.tastiera = tastiera().begin();
    xTaskCreatePinnedToCore(inputTask, "input", 4096, nullptr, 12, &g_inputTask, 1);
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    gpio_isr_handler_add(PIN_INT_N, onIntN, nullptr);

    r.display = display().begin();
    r.audio = audio().begin();
    r.gnss = gnss().begin();
    ESP_LOGI(TAG, "expander %d, tastiera %d, batteria %d, display %d, audio %d, gnss %d", r.expander, r.tastiera,
             r.batteria, r.display, r.audio, r.gnss);
    return r;
}

void vibrate(uint16_t ms) { vibratePattern(1, ms, 0); }

void vibratePattern(uint8_t times, uint16_t onMs, uint16_t offMs) {
    if (!g_vibTimer || times == 0) return;
    xTimerStop(g_vibTimer, 0);
    g_vibLeft = times;
    g_vibOn = onMs;
    g_vibOff = offMs;
    g_vibActive = true;
    expander().set(Exp::VIB_EN, true);
    xTimerChangePeriod(g_vibTimer, pdMS_TO_TICKS(onMs), 0);
}

void ledBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t onMs, uint16_t offMs, uint8_t count) {
    if (!g_blinkTimer) return;
    xTimerStop(g_blinkTimer, 0);
    g_blink = {r, g, b, onMs, offMs, count, count == 0, true};
    expander().rgb(r, g, b);
    xTimerChangePeriod(g_blinkTimer, pdMS_TO_TICKS(onMs), 0);
}

void ledOff() {
    if (g_blinkTimer) xTimerStop(g_blinkTimer, 0);
    expander().rgb(0, 0, 0);
}

void keyboardLight(uint8_t level) { expander().led(Exp::KB_BL, level); }

bool wokeFromSleep() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0; }

void powerOff() {
    ESP_LOGI(TAG, "spegnimento");
    display().backlight(0);
    keyboardLight(0);
    ledOff();
    audio().stop();
    gnss().power(false);
    expander().set(Exp::AMP_SD, false);
    expander().set(Exp::LORA_RST_N, false);  // SX1262 in reset: consuma pochissimo
    // Aspetta che PWR sia rilasciato, altrimenti si risveglia subito.
    for (int i = 0; i < 100 && (expander().readButtons() & Expander::BTN_PWR); i++) vTaskDelay(pdMS_TO_TICKS(20));
    // Solo il tasto PWR sveglia: tastiera zitta, interruzione dell'expander solo su PWR.
    tastiera().sleep();
    expander().onlyPowerWakes();
    rtc_gpio_pullup_dis(PIN_INT_N);
    rtc_gpio_pulldown_dis(PIN_INT_N);
    esp_sleep_enable_ext0_wakeup(PIN_INT_N, 0);
    esp_deep_sleep_start();
}

}  // namespace scheda
