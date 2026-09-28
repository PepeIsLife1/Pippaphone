// Firmware del telefono Pippaphone.
//
// Compiti:
//   radio  (nucleo 0): SX1262 + nodo PippaNet (pippa_radio/radio_link)
//   ui     (nucleo 1): tasti, eventi, LVGL (ui/ui.cpp)
//   input  (nucleo 1): interruzione INT_N -> tastiera e tasti laterali (scheda)
//   voce, audio_in:    Codec2 e I2S
//   servizi (qui):     GPS, batteria, potenza automatica, ogni 5 secondi
#include "app/aggiorna.h"
#include "app/dati.h"
#include "app/eventi.h"
#include "app/memoria.h"
#include "app/rete.h"
#include "app/voce.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scheda/scheda.h"
#include "ui/ui.h"

namespace {
const char* TAG = "pippaphone";

// Acceso col tasto PWR (o risvegliato, con JP3 chiuso): si accende davvero solo se PWR resta
// premuto per un secondo, poi una vibrazione breve dice che si puo' rilasciare. Dopo un
// riavvio del solo ESP32 (aggiornamento, watchdog, USB, tasto RESET) il telefono era gia'
// acceso: niente controllo.
void checkPowerButton(scheda::Avvio avvio) {
    if (avvio == scheda::Avvio::RIAVVIO) return;
    for (int i = 0; i < 50; i++) {
        if (!(scheda::expander().readButtons() & scheda::Expander::BTN_PWR)) {
            ESP_LOGI(TAG, "PWR rilasciato troppo presto: si spegne");
            scheda::powerOff();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    scheda::expander().set(scheda::Exp::VIB_EN, true);
    vTaskDelay(pdMS_TO_TICKS(120));
    scheda::expander().set(scheda::Exp::VIB_EN, false);
}

// Batteria, ogni 5 secondi dal compito dei servizi:
//  - sotto il 5% un avviso (una volta per scarica: si riarma in carica o sopra il 7%);
//  - fuori carica, sotto il 2% oppure sotto 3,3 V per 30 secondi: avviso e spegnimento, per
//    non scaricare troppo la cella (il TPS63001 funzionerebbe fino a circa 1,8 V).
//    Con un SOS attivo si spegne solo sotto 3,0 V: la cella e' comunque vuota.
void checkBattery() {
    static bool warned = false;
    static int lowCount = 0;
    const uint8_t pc = scheda::batteria().percent();  // 255 = MAX17048 assente
    const float v = scheda::batteria().volts();       // 0 = MAX17048 assente
    if (pc > 100 && v < 0.5f) return;
    const bool chg = scheda::batteria().charging();
    if (chg || (pc <= 100 && pc > 7)) warned = false;
    if (!chg && pc <= 5 && !warned) {
        warned = true;
        ESP_LOGW(TAG, "batteria quasi scarica: %u%%, %.2f V", unsigned(pc), double(v));
        app::post({app::Ev::BATTERY, {}, 0, 0, pc});
    }
    const bool sos = app::rete().sosActive();
    const bool empty = sos ? (v > 0.5f && v < 3.0f) : ((pc <= 2) || (v > 0.5f && v < 3.3f));
    lowCount = (!chg && empty) ? lowCount + 1 : 0;
    if (lowCount < 6) return;
    ESP_LOGW(TAG, "batteria scarica: %u%%, %.2f V: spegnimento", unsigned(pc), double(v));
    app::post({app::Ev::BATTERY, {}, 0, 1, pc});
    vTaskDelay(pdMS_TO_TICKS(5000));  // il tempo di leggere l'avviso
    if (scheda::batteria().charging()) {
        lowCount = 0;  // caricabatterie collegato nel frattempo
        return;
    }
    scheda::powerOff();
}

// GPS: acceso all'avvio finche' non arriva l'ora (al massimo 10 minuti), poi solo quando
// serve (richiesta di posizione, SOS, schermata della posizione).
void services() {
    const int64_t bootMs = esp_timer_get_time() / 1000;
    bool gpsOn = false;
    for (;;) {
        const int64_t now = esp_timer_get_time() / 1000;
        const bool boot = !app::rete().timeValid() && now - bootMs < 10 * 60 * 1000;
        const bool want = boot || app::rete().gnssWanted();
        if (want != gpsOn) {
            gpsOn = want;
            scheda::gnss().power(want);
            ESP_LOGI(TAG, "GPS %s", want ? "acceso" : "spento");
        }
        const uint8_t batt = scheda::batteria().percent();
        app::rete().updateGnss(scheda::gnss().fix(), scheda::gnss().accuracyM(), batt <= 100 ? batt : 0);
        app::rete().service();
        checkBattery();
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
}  // namespace

extern "C" void app_main() {
    // Prima di tutto l'autoritenuta dell'alimentazione (PCB rev. 1.1).
    const scheda::Avvio avvio = scheda::holdPower();
    ESP_LOGI(TAG, "Pippaphone %s", app::firmwareVersion());
    checkPowerButton(avvio);
    app::memoriaInit();
    app::dati().load();
    app::eventsInit();

    scheda::init(true);
    const app::Settings s = app::dati().settings();
    scheda::audio().setVolume(s.volume);

    app::voce().begin();
    if (!app::rete().begin()) ESP_LOGE(TAG, "radio LoRa non trovata");

    ui::init();
    scheda::display().backlight(s.brightness);
    if (s.keyLight) scheda::keyboardLight(60);

    // Arrivati fin qui il firmware funziona: niente ritorno alla versione precedente.
    app::otaConfirm();
    services();
}
