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

// Acceso dal deep sleep: si accende davvero solo se PWR resta premuto per un secondo.
void checkPowerButton() {
    if (!scheda::wokeFromSleep()) return;
    for (int i = 0; i < 50; i++) {
        if (!(scheda::expander().readButtons() & scheda::Expander::BTN_PWR)) {
            ESP_LOGI(TAG, "PWR rilasciato troppo presto: si torna a dormire");
            scheda::powerOff();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    scheda::vibrate(120);
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
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
}  // namespace

extern "C" void app_main() {
    ESP_LOGI(TAG, "Pippaphone %s", app::firmwareVersion());
    app::memoriaInit();
    app::dati().load();
    app::eventsInit();

    scheda::init(true);
    checkPowerButton();
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
