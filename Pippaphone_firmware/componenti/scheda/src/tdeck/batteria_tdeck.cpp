// Batteria del LilyGO T-Deck Plus: solo la tensione della cella (partitore 1:2 sul GPIO 4).
//
// La percentuale viene dalla curva di scarica tipica di una LiPo, letta con un po' di carico
// (radio in ricezione, display): e' una stima. "In carica" si ricava dalla tensione: un salto
// verso l'alto quando si collega il caricabatterie, poi la percentuale che sale; in piu' la USB
// collegata a un computer. Da controllare sull'hardware vicino allo 0% e al 100%.
#include <algorithm>
#include <cmath>

#include "driver/usb_serial_jtag.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "scheda/batteria.h"
#include "scheda/pins.h"

namespace scheda {

namespace {
const char* TAG = "batteria";
adc_oneshot_unit_handle_t g_adc = nullptr;
adc_cali_handle_t g_cali = nullptr;
adc_channel_t g_ch = ADC_CHANNEL_3;
SemaphoreHandle_t g_lock = nullptr;

constexpr float DIVIDER = 2.0f;
constexpr int64_t TREND_MS = 5 * 60 * 1000;  // finestra per la tendenza in %/h
constexpr int64_t STEP_MS = 60 * 1000;       // finestra per i salti (caricabatterie collegato o tolto)
constexpr int64_t HOLD_MS = 15 * 1000;       // un salto conta se dura almeno 15 s (non la trasmissione)
constexpr float STEP_V = 0.05f;

struct Point {
    float v;
    float pc;
};
// Tensione -> percentuale (cella LiPo con poco carico).
const Point CURVE[] = {{4.15f, 100}, {4.05f, 90}, {3.97f, 80}, {3.90f, 70}, {3.84f, 60}, {3.80f, 50},
                       {3.76f, 40},  {3.73f, 30}, {3.69f, 20}, {3.62f, 10}, {3.50f, 5},  {3.30f, 0}};

float percentOf(float v) {
    if (v >= CURVE[0].v) return 100;
    const size_t n = sizeof(CURVE) / sizeof(CURVE[0]);
    for (size_t i = 1; i < n; i++)
        if (v >= CURVE[i].v)
            return CURVE[i].pc + (v - CURVE[i].v) * (CURVE[i - 1].pc - CURVE[i].pc) / (CURVE[i - 1].v - CURVE[i].v);
    return 0;
}

int64_t nowMs() { return esp_timer_get_time() / 1000; }

// Tensione filtrata dell'ultimo minuto (un campione ogni ~5 s). Un salto e' "tutti i campioni
// degli ultimi 15 s" sopra (o sotto) "tutti quelli prima": cosi' il calo durante una trasmissione
// o un vocale ad alto volume, e la risalita subito dopo, non contano.
struct Window {
    float v[16] = {};
    int64_t t[16] = {};
    int n = 0, head = 0;
    void add(float x, int64_t at) {
        v[head] = x;
        t[head] = at;
        head = (head + 1) % 16;
        if (n < 16) n++;
    }
    // +1 salto in su, -1 salto in giu', 0 niente.
    int step(int64_t now) const {
        float oldLo = 99, oldHi = 0, newLo = 99, newHi = 0;
        int olds = 0, news = 0;
        for (int i = 0; i < n; i++) {
            const int64_t age = now - t[i];
            if (age > STEP_MS) continue;
            if (age <= HOLD_MS) {
                newLo = std::min(newLo, v[i]);
                newHi = std::max(newHi, v[i]);
                news++;
            } else {
                oldLo = std::min(oldLo, v[i]);
                oldHi = std::max(oldHi, v[i]);
                olds++;
            }
        }
        if (olds < 2 || news < 2) return 0;
        if (newLo - oldHi > STEP_V) return 1;
        if (oldLo - newHi > STEP_V) return -1;
        return 0;
    }
} g_win;
}  // namespace

Batteria& batteria() {
    static Batteria b;
    return b;
}

bool Batteria::begin() {
    g_lock = xSemaphoreCreateMutex();
    adc_unit_t unit = ADC_UNIT_1;
    if (adc_oneshot_io_to_channel(PIN_BATT_ADC, &unit, &g_ch) != ESP_OK) return false;
    adc_oneshot_unit_init_cfg_t u = {};
    u.unit_id = unit;
    if (adc_oneshot_new_unit(&u, &g_adc) != ESP_OK) {
        ESP_LOGE(TAG, "ADC non disponibile");
        return false;
    }
    adc_oneshot_chan_cfg_t c = {};
    c.atten = ADC_ATTEN_DB_12;
    c.bitwidth = ADC_BITWIDTH_DEFAULT;
    adc_oneshot_config_channel(g_adc, g_ch, &c);
    adc_cali_curve_fitting_config_t cc = {};
    cc.unit_id = unit;
    cc.chan = g_ch;
    cc.atten = ADC_ATTEN_DB_12;
    cc.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&cc, &g_cali) != ESP_OK) {
        ESP_LOGW(TAG, "calibrazione dell'ADC assente: tensione meno precisa");
        g_cali = nullptr;
    }
    ok_ = true;
    const int64_t now = nowMs();
    filtered_ = measure();
    ref_ = filtered_;
    refAtMs_ = lastMs_ = now;
    g_win.add(filtered_, now);
    ESP_LOGI(TAG, "%.2f V, circa %.0f%%", double(filtered_), double(percentOf(filtered_)));
    return true;
}

float Batteria::measure() {
    int sum = 0, count = 0;
    for (int i = 0; i < 16; i++) {
        int raw = 0;
        if (adc_oneshot_read(g_adc, g_ch, &raw) != ESP_OK) continue;
        int mv = 0;
        if (g_cali) adc_cali_raw_to_voltage(g_cali, raw, &mv);
        else mv = raw * 3100 / 4095;
        sum += mv;
        count++;
    }
    return count ? float(sum) / count / 1000.0f * DIVIDER : 0;
}

float Batteria::volts() {
    if (!ok_) return 0;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    const int64_t now = nowMs();
    if (now - lastMs_ >= 1000) {  // al massimo una misura al secondo, chiunque la chieda
        const float v = measure();
        filtered_ += (v - filtered_) * 0.3f;
        lastMs_ = now;
        static int64_t lastWin = 0;
        if (now - lastWin >= 5000) {
            lastWin = now;
            g_win.add(filtered_, now);
        }
        if (now - refAtMs_ >= TREND_MS) {
            rate_ = (percentOf(filtered_) - percentOf(ref_)) * 3600000.0f / float(now - refAtMs_);
            ref_ = filtered_;
            refAtMs_ = now;
        }
    }
    const float r = filtered_;
    xSemaphoreGive(g_lock);
    return r;
}

uint8_t Batteria::percent() {
    if (!ok_) return 255;
    return uint8_t(std::lround(percentOf(volts())));
}

float Batteria::ratePerHour() {
    volts();
    return rate_;
}

bool Batteria::charging() {
    if (!ok_) return false;
    const float v = volts();
    xSemaphoreTake(g_lock, portMAX_DELAY);
    const int step = g_win.step(nowMs());
    xSemaphoreGive(g_lock);
    if (usb_serial_jtag_is_connected()) charging_ = true;  // collegato a un computer
    else if (step > 0) charging_ = true;                   // salto in su: caricabatterie collegato
    else if (step < 0) charging_ = false;                  // salto in giu': tolto
    else if (rate_ > 4.0f) charging_ = true;               // la percentuale sale
    else if (rate_ < 1.0f) charging_ = charging_ && v >= 4.1f;  // fermo: in carica solo se piena
    return charging_;
}

}  // namespace scheda
