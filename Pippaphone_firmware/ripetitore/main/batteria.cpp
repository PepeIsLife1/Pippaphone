#include "batteria.h"

#include <cmath>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace batteria {

namespace {
const char* TAG = "batteria";
constexpr float PARTITORE = 2.0f;  // 100k / 100k

adc_oneshot_unit_handle_t g_adc = nullptr;
adc_cali_handle_t g_cali = nullptr;
adc_channel_t g_ch = ADC_CHANNEL_0;
SemaphoreHandle_t g_lock = nullptr;

struct Punto {
    int mv;
    float pc;
};
// Tensione -> percentuale (cella al litio con poco carico, la stessa curva del T-Deck).
const Punto CURVA[] = {{4150, 100}, {4050, 90}, {3970, 80}, {3900, 70}, {3840, 60}, {3800, 50},
                       {3760, 40},  {3730, 30}, {3690, 20}, {3620, 10}, {3500, 5},  {3300, 0}};
}  // namespace

bool begin(int pin) {
    g_lock = xSemaphoreCreateMutex();
    adc_unit_t unit = ADC_UNIT_1;
    if (adc_oneshot_io_to_channel(pin, &unit, &g_ch) != ESP_OK) return false;
    adc_oneshot_unit_init_cfg_t u = {};
    u.unit_id = unit;
    if (adc_oneshot_new_unit(&u, &g_adc) != ESP_OK) {
        ESP_LOGE(TAG, "ADC non disponibile");
        g_adc = nullptr;
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
    return true;
}

int millivolt() {
    if (!g_adc) return 0;
    xSemaphoreTake(g_lock, portMAX_DELAY);
    int somma = 0, n = 0;
    for (int i = 0; i < 16; i++) {
        int raw = 0;
        if (adc_oneshot_read(g_adc, g_ch, &raw) != ESP_OK) continue;
        int mv = 0;
        if (g_cali) adc_cali_raw_to_voltage(g_cali, raw, &mv);
        else mv = raw * 3100 / 4095;
        somma += mv;
        n++;
    }
    xSemaphoreGive(g_lock);
    return n ? int(std::lround(float(somma) / float(n) * PARTITORE)) : 0;
}

uint8_t percento(int mv) {
    if (mv >= CURVA[0].mv) return 100;
    const size_t n = sizeof(CURVA) / sizeof(CURVA[0]);
    for (size_t i = 1; i < n; i++)
        if (mv >= CURVA[i].mv)
            return uint8_t(std::lround(CURVA[i].pc + float(mv - CURVA[i].mv) * (CURVA[i - 1].pc - CURVA[i].pc) /
                                                         float(CURVA[i - 1].mv - CURVA[i].mv)));
    return 0;
}

}  // namespace batteria
