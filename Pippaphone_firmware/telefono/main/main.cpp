// Firmware del telefono Pippaphone.
//
// Compiti:
//   radio  (nucleo 0): SX1262 + nodo PippaNet (pippa_radio/radio_link)
//   ui     (nucleo 1): tasti, eventi, LVGL (ui/ui.cpp)
//   input  (nucleo 1): interruzione INT_N -> tastiera e tasti laterali (scheda)
//   voce, audio_in:    Codec2 e I2S
//   servizi (qui):     GPS, batteria, potenza automatica, ogni 5 secondi (sul T-Deck il GPS ogni secondo)
#include "app/aggiorna.h"
#include "app/dati.h"
#include "app/eventi.h"
#include "app/giochi.h"
#include "app/memoria.h"
#include "app/orologio.h"
#include "app/rete.h"
#include "app/usb_mappe.h"
#include "app/voce.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scheda/scheda.h"
#include "ui/ui.h"
#if CONFIG_SCHEDA_TDECK_PLUS
#include <ctime>
#include <string>

#include "app/cuffie.h"
#include "app/gps_politica.h"
#include "esp_heap_caps.h"
#include "pippanet/airtime.h"
#endif

namespace {
const char* TAG = "pippaphone";

// Acceso col tasto PWR (o risvegliato, con JP3 chiuso): si accende davvero solo se PWR resta
// premuto per un secondo, poi una vibrazione breve dice che si puo' rilasciare. Dopo un
// riavvio del solo ESP32 (aggiornamento, watchdog, USB, tasto RESET) il telefono era gia'
// acceso: niente controllo.
void checkPowerButton(scheda::Avvio avvio) {
#if CONFIG_SCHEDA_TDECK_PLUS
    (void)avvio;  // T-Deck Plus: si accende con l'interruttore, non c'e' un tasto da controllare
#else
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
#endif
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

#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck Plus: informazioni del dispositivo in /pippaphone/info.txt sulla microSD (si leggono
// sul computer), riscritte all'avvio e ogni 10 minuti.
void deviceInfo() {
    if (!app::sdPresente()) return;
    char b[640];
    const uint32_t t = app::rete().unixTime();
    char when[40] = "ora non ancora sincronizzata";
    if (t) {
        const time_t tt = time_t(t);
        std::tm tm{};
        localtime_r(&tt, &tm);
        strftime(when, sizeof(when), "%d/%m/%Y %H:%M", &tm);
    }
    const uint8_t pc = scheda::batteria().percent();
    const pn::RadioParams& m = pn::paramsFor(pn::Channel::MAIN);
    const pn::RadioParams& v = pn::paramsFor(pn::Channel::VOICE);
    const uint16_t n = app::rete().number();
    snprintf(b, sizeof(b),
             "Pippaphone - informazioni del dispositivo\n"
             "(le scrive il telefono all'accensione e ogni 10 minuti)\n\n"
             "aggiornato:      %s\n"
             "firmware:        %s\n"
             "hardware:        LilyGO T-Deck Plus\n"
             "numero:          %s%s\n"
             "codice:          %s\n"
             "amministratore:  %s\n"
             "radio:           %s\n"
             "messaggi:        %.3f MHz, %.0f kHz, SF%d\n"
             "vocali:          %.3f MHz, %.0f kHz, SF%d\n"
             "tastiera:        %s\n"
             "batteria:        %s (%.2f V)\n"
             "dati:            microSD, cartella /pippaphone\n"
             "mappe:           microSD, cartella /mappe (tile OpenStreetMap z/x/y.png)\n",
             when, app::firmwareVersion(), n ? std::to_string(n).c_str() : "nessuno",
             app::rete().isAdmin() ? " (amministratore)" : "", app::rete().myCode().c_str(),
             n ? app::rete().adminCode().c_str() : "-", app::rete().radioOk() ? "ok" : "non risponde",
             double(m.freqMHz), double(m.bwKHz), m.sf, double(v.freqMHz), double(v.bwKHz), v.sf,
             scheda::tastiera().c3Pippaphone() ? "firmware del C3 del Pippaphone (modo raw, C3_INT, sonno)"
             : !scheda::tastiera().modeKnown()  ? "da capire al primo tasto"
             : scheda::tastiera().rawMode()     ? "tasti premuti e rilasciati (modo raw)"
                                                : "solo caratteri (firmware vecchio del C3: ripiego)",
             pc <= 100 ? (std::to_string(pc) + "%").c_str() : "?", double(scheda::batteria().volts()));
    app::writeDeviceInfo(b);
}
#endif

#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck: il GPS lo decide app::PoliticaGps (app/gps_politica.h), un giro al secondo: acceso all'avvio fino
// alla prima posizione (piu' 3 minuti, al massimo 20), ricerche periodiche finche' mancano l'ora o una
// posizione recente (piu' rade al chiuso), le schermate che lo usano finche' sono aperte (ui.cpp), le
// richieste (wantGnss: posizione, SOS, comandi USB) e dopo 5 minuti di grazia, se non c'e' una posizione da 4
// ore. Ogni accensione e spegnimento una riga nel registro, col motivo. La posizione passa alla rete ogni
// secondo finche' il GPS ne ha una (il mio punto sulla mappa la segue), se no ogni 5 secondi; batteria,
// potenza automatica e info.txt ogni 5 secondi, come sulla PCB.
void services() {
    using Ora = app::PoliticaGps::Ora;
    app::PoliticaGps politica;
    bool gpsOn = false;
    int giro = 0, infoTicks = 0;
    uint8_t batt = 0;
    for (;;) {
        const bool cinque = giro == 0;  // un giro su cinque: ogni 5 secondi
        giro = (giro + 1) % 5;
        if (cinque && infoTicks-- <= 0) {
            infoTicks = 120;  // 10 minuti
            deviceInfo();
        }
        const scheda::Fix f = scheda::gnss().fix();
        const app::Rete::TimeSource src = app::rete().timeSource();
        app::PoliticaGps::Ingressi in;
        in.ms = esp_timer_get_time() / 1000;
        if (src == app::Rete::TimeSource::NONE) in.ora = Ora::NESSUNA;
        else if (src == app::Rete::TimeSource::LOCAL || src == app::Rete::TimeSource::RESTORED) in.ora = Ora::DA_CORREGGERE;
        else in.ora = Ora::BUONA;
        in.fix = f.valid;
        in.snrMax = f.snrMax;
        in.schermata = app::rete().gnssSchermataAperta();
        in.richiesta = app::rete().gnssWanted();
        const bool want = politica.decidi(in);
        if (want != gpsOn) {
            gpsOn = want;
            scheda::gnss().power(want, politica.motivo());
            const int64_t tra = politica.prossimaRicercaTra(in.ms);
            if (want) ESP_LOGI(TAG, "GPS acceso: %s", politica.motivo());
            else if (tra >= 0)
                ESP_LOGI(TAG, "GPS spento: %s (prossima ricerca tra %d min)", politica.motivo(), int((tra + 59999) / 60000));
            else ESP_LOGI(TAG, "GPS spento: %s", politica.motivo());
        }
        if (cinque) {
            const uint8_t pc = scheda::batteria().percent();
            batt = pc <= 100 ? pc : 0;
        }
        // Con la posizione ogni secondo; senza, ogni 5 secondi (updateGnss conta l'eta' a passi di 5 s).
        if (cinque || f.valid) app::rete().updateGnss(f, scheda::gnss().accuracyM(), batt);
        if (cinque) {
            app::rete().service();
            checkBattery();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#else
// GPS: acceso all'avvio finche' non arriva l'ora (10 minuti; senza nessuna ora 20, poi 3 minuti
// ogni quarto d'ora finche' arriva), poi solo quando serve (richiesta di posizione, SOS,
// schermata della posizione).
void services() {
    const int64_t bootMs = esp_timer_get_time() / 1000;
    bool gpsOn = false;
    for (;;) {
        const int64_t now = esp_timer_get_time() / 1000;
        const int64_t dall = now - bootMs;
        constexpr int64_t MIN = 60 * 1000;
        // Anche un'ora "locale" (messa a mano o dall'orologio del chip) si corregge col GPS: i primi
        // 10 minuti. Senza ora (acceso con l'interruttore o dopo la batteria scarica: il GPS riparte
        // da freddo e al chiuso puo' metterci molto) i primi 20 minuti, poi 3 minuti ogni quarto
        // d'ora finche' non arriva: prima dopo 10 minuti si spegneva e non riprovava piu', e uscendo
        // all'aperto l'ora non arrivava.
        const app::Rete::TimeSource src = app::rete().timeSource();
        const bool senzaOra = src == app::Rete::TimeSource::NONE;
        const bool daCorreggere = src == app::Rete::TimeSource::LOCAL || src == app::Rete::TimeSource::RESTORED;
        const bool boot = (daCorreggere && dall < 10 * MIN) ||
                          (senzaOra && (dall < 20 * MIN || (dall - 20 * MIN) % (15 * MIN) < 3 * MIN));
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
#endif
}  // namespace

extern "C" void app_main() {
    // Prima di tutto l'autoritenuta dell'alimentazione (PCB rev. 1.1).
    const scheda::Avvio avvio = scheda::holdPower();
    ESP_LOGI(TAG, "Pippaphone %s", app::firmwareVersion());
    checkPowerButton(avvio);
    app::memoriaInit();
    app::dati().load();
    app::orologio().load();
    app::giochi().load();
    app::eventsInit();

    scheda::init(true);
    const app::Settings s = app::dati().settings();
    scheda::audio().setVolume(s.volume);
#if CONFIG_SCHEDA_TDECK_PLUS
    // Cuffie USB-C: pronte ma spente (a ogni avvio la porta USB-C e' del computer).
    app::cuffie().avvia();
    app::cuffie().setVolume(s.volumeCuffie);
#endif

    app::voce().begin();
    if (!app::rete().begin()) ESP_LOGE(TAG, "radio LoRa non trovata");

    ui::init();
#if CONFIG_SCHEDA_TDECK_PLUS
    app::usbMappeAvvia();  // la pagina del flasher puo' mandare le mappe per la microSD
#endif
    scheda::display().backlight(s.brightness);
    if (s.keyLight) scheda::keyboardLight(60);

    // Arrivati fin qui il firmware funziona: niente ritorno alla versione precedente.
    app::otaConfirm();
#if CONFIG_SCHEDA_TDECK_PLUS
    ESP_LOGI(TAG, "stack del task main: %u byte mai usati", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    // Il Wi-Fi (aggiornamento) ha bisogno di RAM interna: quanta ne resta dopo l'avvio.
    ESP_LOGI(TAG, "RAM interna libera %u byte (blocco piu' grande %u, minimo dall'avvio %u), PSRAM libera %u kB",
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
             unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
#endif
    services();
}
