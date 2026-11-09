// Pippaphone: firmware del ripetitore.
//
// Ripete i pacchetti della rete PippaNet (vedi componenti/pippanet/repeater.h). Si configura
// dalla console USB: numero del ripetitore, potenza, stato. Gira sulla PCB del telefono, sulla
// scheda E22 proposta, sulla Heltec WiFi LoRa 32 V3 e sulla LilyGO T3-S3 (vedi HARDWARE.md).
#include <cstdio>
#include <cstring>
#include <string>

#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pippa_radio/radio_link.h"
#include "pippanet/repeater.h"
#include "sdkconfig.h"

#if CONFIG_RIPETITORE_SCHEDA_TELEFONO
#include "scheda/scheda.h"
#endif

// Schede con lo schermo OLED 128x64 e il tasto (Heltec V3, LilyGO T3-S3).
#if CONFIG_RIPETITORE_SCHEDA_HELTEC_V3 || CONFIG_RIPETITORE_SCHEDA_T3S3
#define RIPETITORE_OLED 1
#include <algorithm>
#include <cstdlib>

#include "oled.h"
#include "schermo.h"
#else
#define RIPETITORE_OLED 0
#endif

#if CONFIG_RIPETITORE_SCHEDA_T3S3
#include "batteria.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#endif

#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#endif

#if CONFIG_RIPETITORE_SCHEDA_HELTEC_V3 && !CONFIG_ESP_CONSOLE_UART
#error "Heltec V3: la USB-C e' collegata al CP2102, cioe' alla UART0. Compila con sdkconfig.defaults.heltec (vedi HARDWARE.md)"
#endif
#if CONFIG_RIPETITORE_SCHEDA_T3S3 && !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#error "LilyGO T3-S3: la USB-C e' quella interna dell'ESP32-S3, serve la console USB Serial/JTAG"
#endif

namespace {

const char* TAG = "ripetitore";

// ---------------------------------------------------------------- scheda

#if CONFIG_RIPETITORE_SCHEDA_E22
// Scheda ripetitore proposta (HARDWARE.md): ESP32-S3-WROOM-1 + Ebyte E22-900M30S.
constexpr int PIN_SCK = 12, PIN_MOSI = 11, PIN_MISO = 13, PIN_NSS = 10, PIN_BUSY = 9, PIN_DIO1 = 8, PIN_NRST = 7,
              PIN_TXEN = 6, PIN_RXEN = 5, PIN_LED = 2;
#elif CONFIG_RIPETITORE_SCHEDA_HELTEC_V3
// Heltec WiFi LoRa 32 V3 e cloni: ESP32-S3FN8, SX1262 con TCXO, OLED alimentato da Vext (attivo
// basso), LED bianco, tasto PRG. Pin come lo schema Heltec e le varianti di Meshtastic e MeshCore.
constexpr int PIN_SCK = 9, PIN_MOSI = 10, PIN_MISO = 11, PIN_NSS = 8, PIN_BUSY = 13, PIN_DIO1 = 14, PIN_NRST = 12,
              PIN_LED = 35, PIN_TASTO = 0, PIN_OLED_SDA = 17, PIN_OLED_SCL = 18, PIN_OLED_RST = 21, PIN_VEXT = 36;
const char* const NOME_TASTO = "PRG";
#elif CONFIG_RIPETITORE_SCHEDA_T3S3
// LilyGO T3-S3 V1.2 con SX1262: ESP32-S3FH4R2, modulo SX1262 con TCXO, OLED con reset proprio,
// LED verde, tasto BOOT, batteria su GPIO1 (partitore 100k/100k). Pin dallo schema LilyGO.
constexpr int PIN_SCK = 5, PIN_MOSI = 6, PIN_MISO = 3, PIN_NSS = 7, PIN_BUSY = 34, PIN_DIO1 = 33, PIN_NRST = 8,
              PIN_LED = 37, PIN_TASTO = 0, PIN_OLED_SDA = 18, PIN_OLED_SCL = 17, PIN_OLED_RST = -1, PIN_BATTERIA = 1;
const char* const NOME_TASTO = "BOOT";
#endif

void ledSet(bool on) {
#if CONFIG_RIPETITORE_SCHEDA_TELEFONO
    scheda::expander().rgb(0, on ? 40 : 0, 0);
#else
    gpio_set_level(gpio_num_t(PIN_LED), on);
#endif
}

pippa::RadioConfig radioConfig() {
    pippa::RadioConfig c;
#if CONFIG_RIPETITORE_SCHEDA_E22
    c.sck = PIN_SCK;
    c.mosi = PIN_MOSI;
    c.miso = PIN_MISO;
    c.nss = PIN_NSS;
    c.busy = PIN_BUSY;
    c.dio1 = PIN_DIO1;
    c.reset = PIN_NRST;
    c.txEn = PIN_TXEN;
    c.rxEn = PIN_RXEN;
    c.dio2AsRfSwitch = false;
    c.tcxoVoltage = 1.8f;
#elif RIPETITORE_OLED
    // SX1262 con TCXO a 1,8 V su DIO3 e commutatore d'antenna comandato da DIO2.
    c.sck = PIN_SCK;
    c.mosi = PIN_MOSI;
    c.miso = PIN_MISO;
    c.nss = PIN_NSS;
    c.busy = PIN_BUSY;
    c.dio1 = PIN_DIO1;
    c.reset = PIN_NRST;
    c.dio2AsRfSwitch = true;
    c.tcxoVoltage = 1.8f;
#else
    c.sck = scheda::PIN_SPI_SCK;
    c.mosi = scheda::PIN_SPI_MOSI;
    c.miso = scheda::PIN_SPI_MISO;
    c.nss = scheda::PIN_LORA_NSS;
    c.busy = scheda::PIN_LORA_BUSY;
    c.dio1 = scheda::PIN_LORA_DIO1;
    c.resetFn = [](bool level) { scheda::expander().set(scheda::Exp::LORA_RST_N, level); };
    c.dio2AsRfSwitch = true;
    c.tcxoVoltage = 1.8f;
#endif
    c.maxPowerDbm = CONFIG_RIPETITORE_POTENZA_MAX;
    return c;
}

#if CONFIG_RIPETITORE_SCHEDA_T3S3
int g_batteriaMv = 0;  // ultima tensione misurata (mV), aggiornata ogni minuto
#endif

uint8_t batteryPercent() {
#if CONFIG_RIPETITORE_SCHEDA_TELEFONO
    return scheda::batteria().percent();
#elif CONFIG_RIPETITORE_SCHEDA_T3S3
    const int mv = g_batteriaMv;
    return mv > batteria::MV_ASSENTE ? batteria::percento(mv) : 255;
#else
    return 255;
#endif
}

// ---------------------------------------------------------------- memoria

nvs_handle_t g_nvs = 0;

uint16_t loadAddress() {
    uint16_t a = 0;
    if (nvs_get_u16(g_nvs, "id", &a) == ESP_OK && pn::isRepeaterAddr(a)) return a;
    // Primo avvio: numero ricavato dal MAC (0xF001..0xFFFE), poi si cambia da console.
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    a = uint16_t(0xF000 | ((mac[4] << 8 | mac[5]) & 0x0FFF));
    if (a == 0xF000) a = 0xF001;
    if (a == pn::ADDR_BROADCAST) a = 0xFFFE;
    return a;
}

// ---------------------------------------------------------------- stato

SemaphoreHandle_t g_lock;
pn::Repeater* g_node = nullptr;
pippa::RadioLink g_radio;

// ---------------------------------------------------------------- schermo

#if RIPETITORE_OLED
Oled g_oled;                 // lo usa solo compitoSchermo (e l'avvio, prima che parta)
schermo::Framebuffer g_fb;
uint32_t g_schermoSecondi = CONFIG_RIPETITORE_SCHERMO_SECONDI;  // 0 = sempre acceso
bool g_capovolto = false;
volatile bool g_cambioSchermo = false;  // la console ha cambiato le impostazioni
volatile bool g_spegniSchermo = false;  // si va a dormire: spegnilo e fermati
volatile bool g_schermoFermo = false;
TaskHandle_t g_compitoSchermo = nullptr;

// Prima i ripetitori, poi chi si sente meglio.
bool vicinoMigliore(const schermo::Vicino& a, const schermo::Vicino& b) {
    const bool ra = pn::isRepeaterAddr(a.addr), rb = pn::isRepeaterAddr(b.addr);
    if (ra != rb) return ra;
    return a.snr > b.snr;
}

// Copia i numeri da mostrare (col nodo bloccato per poco).
void raccogli(schermo::Stato& s, pn::Millis now) {
    xSemaphoreTakeRecursive(g_lock, portMAX_DELAY);
    s.indirizzo = g_node->address();
    s.radioOk = g_radio.ok();
    s.potenza = g_radio.power();
    const pn::Channel ch = g_node->activeChannel(now);
    s.canaleVoce = ch == pn::Channel::VOICE;
    const pn::RadioParams& p = pn::paramsFor(ch);
    s.mhz = p.freqMHz;
    s.khz = p.bwKHz;
    s.sf = p.sf;
    s.ripetuti = g_node->relayed;
    s.ricevuti = g_node->rxCount;
    s.trasmessi = g_node->txCount;
    s.soppressi = g_node->suppressed;
    s.recuperati = g_node->rescued;
    s.scartati = g_node->rxDropped;
    s.erroriRadio = g_radio.rxErrors();
    s.secondiInAria = uint32_t(g_node->txAirtimeMs / 1000);
    s.rssi = g_radio.lastRssi();
    s.snr = g_radio.lastSnr();
    for (const auto& r : g_node->routes()) {
        if (now - r.second.seen > g_node->routeTtlMs) continue;
        s.strade++;
        if (r.second.hops != 0 || r.second.next != r.first) continue;  // non e' un vicino diretto
        s.nVicini++;
        const schermo::Vicino v{r.first, r.second.snr, uint32_t((now - r.second.seen) / 1000)};
        int i = s.nElenco;
        while (i > 0 && vicinoMigliore(v, s.vicini[i - 1])) i--;
        if (i >= schermo::MAX_VICINI) continue;
        for (int k = std::min(s.nElenco, schermo::MAX_VICINI - 1); k > i; k--) s.vicini[k] = s.vicini[k - 1];
        s.vicini[i] = v;
        if (s.nElenco < schermo::MAX_VICINI) s.nElenco++;
    }
    xSemaphoreGiveRecursive(g_lock);
    s.accesoSecondi = uint32_t(now / 1000);
#if CONFIG_RIPETITORE_SCHEDA_T3S3
    const int mv = g_batteriaMv;
    if (mv > batteria::MV_ASSENTE) {
        s.batteriaMv = mv;
        s.batteriaPercento = batteria::percento(mv);
    }
#endif
}

// Schermo: acceso all'avvio e quando si preme il tasto, si spegne da solo dopo
// g_schermoSecondi (l'OLED acceso sempre si consuma e resta il segno della scritta). Con lo
// schermo acceso il tasto passa alla pagina dopo: stato, vicini diretti, radio.
void compitoSchermo(void*) {
    if (!g_oled.begin(PIN_OLED_SDA, PIN_OLED_SCL, PIN_OLED_RST)) {
        ESP_LOGW(TAG, "schermo non disponibile");
        g_compitoSchermo = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    g_oled.capovolto(g_capovolto);
    schermo::Stato s;
    int pagina = 0;
    bool premuto = true;  // il tasto tenuto premuto all'avvio non conta
    bool vuoleAcceso = true;
    bool haUltimo = false;
    uint32_t ricevuti = 0;
    pn::Millis ultimoRx = 0, prossimo = 0;
    pn::Millis spegniA = pippa::RadioLink::nowMs() + pn::Millis(g_schermoSecondi) * 1000;
    for (;;) {
        const pn::Millis now = pippa::RadioLink::nowMs();
        if (g_spegniSchermo) {
            g_oled.accendi(false);
            g_schermoFermo = true;
            vTaskSuspend(nullptr);
        }
        bool sveglia = false;
        if (g_cambioSchermo) {
            g_cambioSchermo = false;
            g_oled.capovolto(g_capovolto);
            sveglia = true;
        }
        const bool giu = gpio_get_level(gpio_num_t(PIN_TASTO)) == 0;
        if (giu && !premuto) {
            pagina = vuoleAcceso ? (pagina + 1) % schermo::PAGINE : 0;
            sveglia = true;
        }
        premuto = giu;
        if (sveglia) {
            vuoleAcceso = true;
            spegniA = now + pn::Millis(g_schermoSecondi) * 1000;
            prossimo = 0;
        } else if (vuoleAcceso && g_schermoSecondi > 0 && now >= spegniA) {
            vuoleAcceso = false;
            g_oled.accendi(false);
        }
        if (now >= prossimo) {
            prossimo = now + 1000;
            s = schermo::Stato();
            raccogli(s, now);
            if (s.ricevuti != ricevuti) {
                ricevuti = s.ricevuti;
                ultimoRx = now;
                haUltimo = true;
            }
            s.haUltimo = haUltimo;
            s.ultimoSecondi = uint32_t((now - ultimoRx) / 1000);
            if (vuoleAcceso) {
                schermo::disegna(g_fb, s, pagina);
                g_oled.mostra(g_fb.buf);
                if (!g_oled.acceso()) g_oled.accendi(true);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
#endif

// ---------------------------------------------------------------- batteria (T3-S3)

#if CONFIG_RIPETITORE_SCHEDA_T3S3
// Batteria quasi scarica: radio, schermo e LED spenti e ESP32 nel sonno profondo. Si risveglia
// ogni CONFIG_RIPETITORE_SONNO_MINUTI (o col tasto BOOT) e riparte solo quando il pannello ha
// ricaricato la batteria sopra CONFIG_RIPETITORE_BATTERIA_RIPARTE_MV: cosi' non si accende e si
// spegne di continuo, e la cella non scende mai troppo.
RTC_DATA_ATTR bool g_dormivaPerBatteria = false;

// Collegato a un computer (la USB riceve i pacchetti del PC): non si va a dormire.
bool usbCollegata() { return usb_serial_jtag_is_connected(); }

// Mette l'SX1262 nel sonno piu' profondo (circa 1 uA) con due comandi SPI: SetStandby e SetSleep
// (avvio a freddo, senza timer). Si chiama tenendo g_lock, senza piu' lasciarlo: il compito della
// radio usa l'SPI solo col lock, quindi da li' in poi non tocca piu' la radio (ogni accesso la
// sveglierebbe). Il bus e' quello aperto da pippa_radio; NSS lo comandiamo noi, come RadioLib.
void radioDorme() {
    spi_device_interface_config_t dc = {};
    dc.mode = 0;
    dc.clock_speed_hz = 1000000;
    dc.spics_io_num = -1;
    dc.queue_size = 1;
    spi_device_handle_t dev = nullptr;
    if (spi_bus_add_device(SPI2_HOST, &dc, &dev) != ESP_OK) return;
    static const uint8_t COMANDI[2][2] = {{0x80, 0x00}, {0x84, 0x00}};
    for (const auto& c : COMANDI) {
        for (int i = 0; i < 100 && gpio_get_level(gpio_num_t(PIN_BUSY)); i++) vTaskDelay(pdMS_TO_TICKS(1));
        spi_transaction_t t = {};
        t.length = 8 * sizeof(c);
        t.tx_buffer = c;
        gpio_set_level(gpio_num_t(PIN_NSS), 0);
        spi_device_polling_transmit(dev, &t);
        gpio_set_level(gpio_num_t(PIN_NSS), 1);
    }
    vTaskDelay(pdMS_TO_TICKS(2));
    spi_bus_remove_device(dev);
}

[[noreturn]] void dormi(int mv) {
    ESP_LOGW(TAG, "batteria a %d mV: dormo, riprovo fra %d minuti (o col tasto %s)", mv,
             CONFIG_RIPETITORE_SONNO_MINUTI, NOME_TASTO);
    if (g_compitoSchermo) {
        g_spegniSchermo = true;  // l'I2C lo usa solo il suo compito
        for (int i = 0; i < 40 && !g_schermoFermo; i++) vTaskDelay(pdMS_TO_TICKS(25));
    } else {
        g_oled.begin(PIN_OLED_SDA, PIN_OLED_SCL, PIN_OLED_RST);  // lo lascia spento
    }
    xSemaphoreTakeRecursive(g_lock, portMAX_DELAY);  // e non si lascia piu' (vedi radioDorme)
    radioDorme();
    ledSet(false);
    // NSS e reset alti per tutto il sonno, se no la radio si sveglia da sola.
    gpio_hold_en(gpio_num_t(PIN_NSS));
    gpio_hold_en(gpio_num_t(PIN_NRST));
    g_dormivaPerBatteria = true;
    esp_sleep_enable_timer_wakeup(uint64_t(CONFIG_RIPETITORE_SONNO_MINUTI) * 60ULL * 1000000ULL);
    esp_sleep_enable_ext0_wakeup(gpio_num_t(PIN_TASTO), 0);
    esp_deep_sleep_start();
}

// All'avvio, prima di accendere radio e schermo.
void controllaBatteriaAvvio() {
    const bool dormiva = g_dormivaPerBatteria;
    g_dormivaPerBatteria = false;
    // Qualche misura distanziata, tenendo la piu' alta: senza batteria l'uscita del
    // caricabatterie oscilla e non deve sembrare una batteria scarica.
    int mv = 0;
    for (int i = 0; i < 5; i++) {
        if (i) vTaskDelay(pdMS_TO_TICKS(40));
        mv = std::max(mv, batteria::millivolt());
    }
    g_batteriaMv = mv;
    if (mv <= batteria::MV_ASSENTE) {
        ESP_LOGI(TAG, "batteria non collegata");
        return;
    }
    const int soglia = dormiva ? CONFIG_RIPETITORE_BATTERIA_RIPARTE_MV : CONFIG_RIPETITORE_BATTERIA_MIN_MV;
    ESP_LOGI(TAG, "batteria %d mV (%u%%)%s", mv, batteria::percento(mv), dormiva ? ", dopo il sonno" : "");
    if (mv >= soglia) return;
    // La USB di un computer si fa vedere qualche centinaio di ms dopo l'avvio.
    for (int i = 0; i < 30 && !usbCollegata(); i++) vTaskDelay(pdMS_TO_TICKS(50));
    if (usbCollegata()) {
        ESP_LOGW(TAG, "batteria sotto %d mV, ma la USB e' collegata a un computer: resto acceso", soglia);
        return;
    }
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 &&
        g_oled.begin(PIN_OLED_SDA, PIN_OLED_SCL, PIN_OLED_RST)) {
        // Svegliato col tasto: fa vedere perche' non riparte.
        char r1[24], r2[24];
        snprintf(r1, sizeof r1, "scarica: %d,%02d V", mv / 1000, mv % 1000 / 10);
        snprintf(r2, sizeof r2, "riparte sopra %d,%02d V", soglia / 1000, soglia % 1000 / 10);
        schermo::avviso(g_fb, "Batteria", r1, r2);
        g_oled.capovolto(g_capovolto);
        g_oled.mostra(g_fb.buf);
        g_oled.accendi(true);
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
    dormi(mv);
}

// Ogni minuto: tensione per il beacon e lo schermo; tre misure di fila sotto la soglia
// (circa tre minuti, non un calo durante una trasmissione) mandano a dormire.
void compitoBatteria(void*) {
    for (int sotto = 0;;) {
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
        const int mv = batteria::millivolt();
        g_batteriaMv = mv;
        if (mv <= batteria::MV_ASSENTE || mv >= CONFIG_RIPETITORE_BATTERIA_MIN_MV) {
            sotto = 0;
            continue;
        }
        if (++sotto < 3) continue;
        if (usbCollegata()) {
            ESP_LOGW(TAG, "batteria a %d mV, ma la USB e' collegata a un computer: resto acceso", mv);
            continue;
        }
        dormi(mv);
    }
}
#endif

// ---------------------------------------------------------------- console

void printInfo() {
    xSemaphoreTakeRecursive(g_lock, portMAX_DELAY);
    const pn::Millis now = pippa::RadioLink::nowMs();
    printf("Ripetitore R%u (0x%04X)\n", unsigned(g_node->address() - 0xF000), g_node->address());
    printf("  radio: %s, potenza %d dBm\n", g_radio.ok() ? "ok" : "NON RISPONDE", g_radio.power());
    printf("  canale: %s\n", g_node->activeChannel(now) == pn::Channel::VOICE ? "voce" : "messaggi");
    printf("  ricevuti %lu (scartati %lu, errori radio %lu), trasmessi %lu, ripetuti %lu, soppressi %lu, "
           "recuperati %lu\n",
           (unsigned long)g_node->rxCount, (unsigned long)g_node->rxDropped, (unsigned long)g_radio.rxErrors(),
           (unsigned long)g_node->txCount, (unsigned long)g_node->relayed, (unsigned long)g_node->suppressed,
           (unsigned long)g_node->rescued);
    printf("  tempo in aria totale: %llu s\n", (unsigned long long)(g_node->txAirtimeMs / 1000));
    printf("  ultimo segnale: %d dBm, SNR %.1f dB\n", g_radio.lastRssi(), double(g_radio.lastSnr()));
    if (g_node->timeValid()) printf("  ora: %lu (Unix)\n", (unsigned long)g_node->unixTime(now));
    const uint8_t b = batteryPercent();
#if CONFIG_RIPETITORE_SCHEDA_T3S3
    if (b <= 100)
        printf("  batteria: %d mV (%u%%), dorme sotto %d mV\n", g_batteriaMv, b, CONFIG_RIPETITORE_BATTERIA_MIN_MV);
    else
        printf("  batteria: non collegata\n");
#else
    if (b <= 100) printf("  batteria: %u%%\n", b);
#endif
#if RIPETITORE_OLED
    if (!g_oled.ok())
        printf("  schermo: non trovato\n");
    else if (g_schermoSecondi)
        printf("  schermo: si spegne dopo %lu s, tasto %s per riaccenderlo\n", (unsigned long)g_schermoSecondi,
               NOME_TASTO);
    else
        printf("  schermo: sempre acceso\n");
#endif
    xSemaphoreGiveRecursive(g_lock);
}

void printRoutes() {
    xSemaphoreTakeRecursive(g_lock, portMAX_DELAY);
    const pn::Millis now = pippa::RadioLink::nowMs();
    printf("destinazione  via      salti  SNR    eta'\n");
    for (const auto& r : g_node->routes()) {
        if (now - r.second.seen > g_node->routeTtlMs) continue;
        printf("%-12u  %-7u  %-5u  %-5.1f  %lus\n", r.first, r.second.next, r.second.hops, double(r.second.snr),
               (unsigned long)((now - r.second.seen) / 1000));
    }
    xSemaphoreGiveRecursive(g_lock);
}

#if RIPETITORE_OLED
// schermo <secondi> | schermo gira
void comandoSchermo(const char* line) {
    char a[16] = {};
    if (sscanf(line, "%*s %15s", a) != 1) {
        if (g_schermoSecondi) printf("lo schermo si spegne dopo %lu s\n", (unsigned long)g_schermoSecondi);
        else printf("schermo sempre acceso\n");
        return;
    }
    if (!strcmp(a, "gira")) {
        g_capovolto = !g_capovolto;
        nvs_set_u8(g_nvs, "capovolto", g_capovolto ? 1 : 0);
        nvs_commit(g_nvs);
        g_cambioSchermo = true;
        printf("schermo %s\n", g_capovolto ? "capovolto" : "diritto");
        return;
    }
    char* fine = nullptr;
    const long sec = strtol(a, &fine, 10);
    if (fine == a || *fine || sec < 0 || sec > 3600) {
        printf("uso: schermo <secondi 0..3600> (0 = sempre acceso), oppure schermo gira\n");
        return;
    }
    g_schermoSecondi = uint32_t(sec);
    nvs_set_u32(g_nvs, "schermo", g_schermoSecondi);
    nvs_commit(g_nvs);
    g_cambioSchermo = true;
    if (sec) printf("lo schermo si spegne dopo %ld s\n", sec);
    else printf("schermo sempre acceso (l'OLED si consuma: meglio lasciarlo spegnere)\n");
}
#endif

void console() {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&cfg);
    usb_serial_jtag_vfs_use_driver();
#else
    // Console sulla UART (Heltec V3: la USB-C arriva alla UART0 attraverso il CP2102).
    uart_driver_install(uart_port_t(CONFIG_ESP_CONSOLE_UART_NUM), 256, 0, 0, nullptr, 0);
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#endif
    setvbuf(stdin, nullptr, _IONBF, 0);
#if RIPETITORE_OLED
    printf("\nComandi: info, strade, numero <1..4094>, potenza <dBm>, schermo <secondi>|gira, riavvia\n");
#else
    printf("\nComandi: info, strade, numero <1..4094>, potenza <dBm>, riavvia\n");
#endif
    char line[80];
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        char* nl = strpbrk(line, "\r\n");
        if (nl) *nl = 0;
        char cmd[16] = {};
        int arg = 0;
        const int n = sscanf(line, "%15s %d", cmd, &arg);
        if (n < 1) continue;
        if (!strcmp(cmd, "info")) {
            printInfo();
        } else if (!strcmp(cmd, "strade")) {
            printRoutes();
        } else if (!strcmp(cmd, "numero") && n == 2 && arg >= 1 && arg <= 4094) {
            const uint16_t a = uint16_t(0xF000 + arg);
            nvs_set_u16(g_nvs, "id", a);
            nvs_commit(g_nvs);
            xSemaphoreTakeRecursive(g_lock, portMAX_DELAY);
            g_node->setAddress(a);
            xSemaphoreGiveRecursive(g_lock);
            printf("ora sono R%d\n", arg);
        } else if (!strcmp(cmd, "potenza") && n == 2) {
            g_radio.setPower(int8_t(arg));
            nvs_set_i8(g_nvs, "potenza", g_radio.power());
            nvs_commit(g_nvs);
            printf("potenza %d dBm\n", g_radio.power());
        } else if (!strcmp(cmd, "riavvia")) {
            esp_restart();
#if RIPETITORE_OLED
        } else if (!strcmp(cmd, "schermo")) {
            comandoSchermo(line);
#endif
        } else {
            printf("comando sconosciuto\n");
        }
    }
}

}  // namespace

extern "C" void app_main() {
#if CONFIG_RIPETITORE_SCHEDA_TELEFONO
    // PCB del telefono rev. 1.1: prima di tutto l'autoritenuta, cosi' resta acceso anche dopo
    // aver rilasciato PWR.
    scheda::holdPower();
#endif
#if CONFIG_RIPETITORE_SCHEDA_T3S3
    // Dopo il sonno per batteria scarica NSS e reset della radio sono ancora bloccati alti: si
    // sbloccano gia' alti, senza impulsi che sveglierebbero la radio.
    for (int pin : {PIN_NSS, PIN_NRST}) {
        gpio_set_direction(gpio_num_t(pin), GPIO_MODE_OUTPUT);
        gpio_set_level(gpio_num_t(pin), 1);
        gpio_hold_dis(gpio_num_t(pin));
    }
#endif
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    nvs_open("ripetitore", NVS_READWRITE, &g_nvs);

#if CONFIG_RIPETITORE_SCHEDA_E22
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_LED;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
#elif RIPETITORE_OLED
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << PIN_LED;
#if CONFIG_RIPETITORE_SCHEDA_HELTEC_V3
    io.pin_bit_mask |= 1ULL << PIN_VEXT;
#endif
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
#if CONFIG_RIPETITORE_SCHEDA_HELTEC_V3
    gpio_set_level(gpio_num_t(PIN_VEXT), 0);  // Vext (attivo basso) alimenta l'OLED
#endif
    io = {};
    io.pin_bit_mask = 1ULL << PIN_TASTO;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);
    uint32_t secondi = 0;
    if (nvs_get_u32(g_nvs, "schermo", &secondi) == ESP_OK && secondi <= 3600) g_schermoSecondi = secondi;
    uint8_t capovolto = 0;
    if (nvs_get_u8(g_nvs, "capovolto", &capovolto) == ESP_OK) g_capovolto = capovolto != 0;
#if CONFIG_RIPETITORE_SCHEDA_T3S3
    batteria::begin(PIN_BATTERIA);
#endif
#else
    scheda::init(false);
    // Sulla scheda del telefono: niente GNSS ne' amplificatore, solo la radio.
    scheda::expander().set(scheda::Exp::GNSS_EN, false);
#endif

    g_lock = xSemaphoreCreateRecursiveMutex();
    g_node = new pn::Repeater(loadAddress());
    uint32_t seq = 1;
    nvs_get_u32(g_nvs, "seq", &seq);
    g_node->setSeq(seq);
    g_node->onSeqReserve = [](uint32_t next) {
        nvs_set_u32(g_nvs, "seq", next);
        nvs_commit(g_nvs);
    };
    g_node->battery = batteryPercent();

    const bool radioOk = g_radio.begin(radioConfig(), g_node, g_lock);
#if CONFIG_RIPETITORE_SCHEDA_TELEFONO
    scheda::spiLowDrive();  // dopo l'inizializzazione del bus SPI
#endif
#if CONFIG_RIPETITORE_SCHEDA_T3S3
    // Batteria quasi scarica: si torna a dormire prima di accendere radio e schermo.
    controllaBatteriaAvvio();
    g_node->battery = batteryPercent();
#endif
    if (!radioOk) {
        ESP_LOGE(TAG, "radio non disponibile: controlla l'SX1262");
    } else {
        int8_t p = CONFIG_RIPETITORE_POTENZA_MAX;
        nvs_get_i8(g_nvs, "potenza", &p);
        g_radio.setPower(p);
        g_radio.start(0, 10);
    }
    ESP_LOGI(TAG, "ripetitore R%u pronto", unsigned(g_node->address() - 0xF000));

    // Lampeggio di "vivo" e aggiornamento della batteria.
    xTaskCreate(
        [](void*) {
            for (int i = 0;; i++) {
                ledSet(true);
                vTaskDelay(pdMS_TO_TICKS(g_radio.ok() ? 40 : 400));
                ledSet(false);
                vTaskDelay(pdMS_TO_TICKS(g_radio.ok() ? 4960 : 600));
                if (i % 12 == 0) {
                    const uint8_t b = batteryPercent();
                    xSemaphoreTakeRecursive(g_lock, portMAX_DELAY);
                    g_node->battery = b;
                    xSemaphoreGiveRecursive(g_lock);
                }
            }
        },
        "led", 3072, nullptr, 2, nullptr);

#if RIPETITORE_OLED
    xTaskCreate(compitoSchermo, "schermo", 4096, nullptr, 2, &g_compitoSchermo);
#endif
#if CONFIG_RIPETITORE_SCHEDA_T3S3
    xTaskCreate(compitoBatteria, "batteria", 4096, nullptr, 3, nullptr);
#endif

#if CONFIG_RIPETITORE_SCHEDA_TELEFONO
    // PWR tenuto premuto 3 secondi: spegnimento.
    xTaskCreate(
        [](void*) {
            // Prima aspetta il rilascio del tasto usato per accendere.
            while (scheda::expander().readButtons() & scheda::Expander::BTN_PWR) vTaskDelay(pdMS_TO_TICKS(100));
            for (int held = 0;;) {
                vTaskDelay(pdMS_TO_TICKS(100));
                held = (scheda::expander().readButtons() & scheda::Expander::BTN_PWR) ? held + 1 : 0;
                if (held >= 30) {
                    ESP_LOGI(TAG, "PWR tenuto premuto: spegnimento");
                    scheda::powerOff();
                }
            }
        },
        "pwr", 3072, nullptr, 3, nullptr);
#endif

    console();
}
