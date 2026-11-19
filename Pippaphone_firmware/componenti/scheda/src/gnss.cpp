#include "scheda/gnss.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "scheda/pins.h"
#if !CONFIG_SCHEDA_TDECK_PLUS
#include "scheda/expander.h"
#else
#include "scheda/scheda.h"
#include "tdeck/gnss_pilota.h"
#endif

namespace scheda {

namespace {
constexpr uart_port_t PORT = UART_NUM_1;
SemaphoreHandle_t g_lock = nullptr;
Fix g_fix;
const char* g_modulo = "?";            // ricevitore riconosciuto (diagnosi)
std::atomic<int64_t> g_ecoFino{0};     // ms dall'avvio: fino a quando ripetere le frasi nel registro

#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck Plus: GPS u-blox M10Q (38400 baud) oppure Quectel L76K (9600), sempre alimentato. Lo guida il
// pilota di tdeck/gnss_pilota.cpp: riconosce la velocita', mette l'u-blox in standby (UBX-RXM-PMREQ) e
// lo risveglia (un byte sulla seriale) controllando che lo faccia davvero, lo cerca di nuovo se tace,
// tiene la storia e fa le prove al banco. Qui c'e' solo la seriale. L'L76K resta acceso.
const char* TAG_GNSS = "gnss";
// Stack del compito del GPS: era 4096 byte. Con il pilota i suoi frame sono piccoli (-fstack-usage, ramo
// piu' profondo giro 176 + passo 48 + sveglia 64 + evento 208 + ricerca 224 byte: meno di 1 kB), ma ogni
// evento passa dal registro di ESP-IDF (vprintf, 1,5-2 kB): 6144 lasciano circa 3 kB di margine. Quanti
// byte non sono mai stati usati lo dice @GPS? ("stack"), misurato sul telefono.
constexpr uint32_t STACK_GNSS = 6144;

// La UART1 del GPS per il pilota. Le frasi lette finiscono anche nel registro finche' c'e' l'eco
// (@NMEA!).
class PortaUart : public gps::Porta {
public:
    uint64_t ms() override { return uint64_t(esp_timer_get_time() / 1000); }
    int leggi(uint8_t* buf, size_t n, uint32_t attesaMs) override {
        const int r = uart_read_bytes(PORT, buf, n, pdMS_TO_TICKS(attesaMs));
        if (r <= 0) return 0;
        eco(buf, r);
        return r;
    }
    void scrivi(const uint8_t* p, size_t n) override {
        uart_write_bytes(PORT, p, n);                 // il frame intero in una volta
        uart_wait_tx_done(PORT, pdMS_TO_TICKS(100));  // 30 byte a 9600 baud: 31 ms
    }
    void velocita(uint32_t baud) override { uart_set_baudrate(PORT, baud); }
    void blocca() override {
        if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY);
    }
    void sblocca() override {
        if (g_lock) xSemaphoreGive(g_lock);
    }
    void registro(bool avviso, const char* testo) override {
        if (avviso) {
            ESP_LOGW(TAG_GNSS, "%s", testo);
        } else {
            ESP_LOGI(TAG_GNSS, "%s", testo);
        }
    }

private:
    // Diagnosi: le frasi cosi' come arrivano, nel registro (i byte non stampabili dei frame UBX come '.').
    void eco(const uint8_t* b, int n) {
        if (int64_t(ms()) >= g_ecoFino.load()) {
            ecoLen_ = 0;
            return;
        }
        for (int i = 0; i < n; i++) {
            const char c = char(b[i]);
            if (c == '\r' || c == '\n') {
                if (ecoLen_) {
                    eco_[ecoLen_] = 0;
                    ESP_LOGI("nmea", "%s", eco_);
                }
                ecoLen_ = 0;
            } else if (ecoLen_ < int(sizeof(eco_)) - 1) {
                eco_[ecoLen_++] = (b[i] >= 32 && b[i] < 127) ? c : '.';
            }
        }
    }
    char eco_[100];
    int ecoLen_ = 0;
};

PortaUart g_porta;
gps::Pilota g_pilota(g_porta, g_fix);
TaskHandle_t g_compito = nullptr;

void gnssTask(void*) {
    g_pilota.avvio();  // riconosce il ricevitore (nel compito, per non rallentare l'avvio)
    for (;;) {
        g_pilota.giro();
        g_modulo = g_pilota.modulo();
        // Risparmio: finche' il GPS parla, o lo si vuole, o lo si sta cercando, svegliando o addormentando, il
        // chip non va in light sleep (la UART perderebbe i caratteri). Quando dorme (STANDBY) o non c'e' (MUTO)
        // e non lo si vuole, la seriale si guarda ogni mezzo secondo invece che ogni 50 ms (power() e le altre
        // richieste svegliano subito il compito). L'L76K non si addormenta: se non lo si vuole, le sue frasi
        // si possono perdere.
        const Gnss::Stato st = g_pilota.stato();
        const bool fermo = st == Gnss::Stato::STANDBY || st == Gnss::Stato::MUTO;
        const bool serve = g_pilota.voluto() || (!fermo && st != Gnss::Stato::ACCESO);
        tieni(Tieni::GPS, serve);
        if (!serve && fermo) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
    }
}

// Una richiesta per il compito del GPS: se sta aspettando a riposo, che guardi subito.
void sveglia() {
    if (g_compito) xTaskNotifyGive(g_compito);
}
#else
int hexVal(char c) { return (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1; }

// Divide la frase in campi separati da virgole (modifica la stringa).
int split(char* s, char** f, int max) {
    int n = 0;
    f[n++] = s;
    for (char* p = s; *p && n < max; p++)
        if (*p == ',') {
            *p = 0;
            f[n++] = p + 1;
        }
    return n;
}

double coord(const char* v, const char* hemi) {
    if (!*v) return 0;
    double x = std::atof(v);
    int deg = int(x / 100);
    double d = deg + (x - deg * 100) / 60.0;
    if (*hemi == 'S' || *hemi == 'W') d = -d;
    return d;
}

// Giorni dal 1970 (algoritmo di Howard Hinnant, senza dipendere dal fuso orario).
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}

void gnssTask(void*) {
    uint8_t buf[128];
    NmeaParser parser;
    char eco[100];
    int ecoLen = 0;
    for (;;) {
        int n = uart_read_bytes(PORT, buf, sizeof(buf), pdMS_TO_TICKS(500));
        if (n <= 0) continue;
        const uint64_t now = uint64_t(esp_timer_get_time() / 1000);
        xSemaphoreTake(g_lock, portMAX_DELAY);
        for (int i = 0; i < n; i++) parser.feed(char(buf[i]), g_fix, now);
        xSemaphoreGive(g_lock);
        // Diagnosi: le frasi cosi' come arrivano, nel registro.
        if (int64_t(now) < g_ecoFino.load()) {
            for (int i = 0; i < n; i++) {
                const char c = char(buf[i]);
                if (c == '\r' || c == '\n') {
                    if (ecoLen) {
                        eco[ecoLen] = 0;
                        ESP_LOGI("nmea", "%s", eco);
                    }
                    ecoLen = 0;
                } else if (ecoLen < int(sizeof(eco)) - 1) {
                    eco[ecoLen++] = c;
                }
            }
        } else {
            ecoLen = 0;
        }
    }
}
#endif
}  // namespace

#if !CONFIG_SCHEDA_TDECK_PLUS
bool NmeaParser::feed(char c, Fix& fix, uint64_t nowMs) {
    if (c == '$') {
        len_ = 0;
        line_[len_++] = c;
        return false;
    }
    if (len_ == 0) return false;
    if (c == '\r' || c == '\n') {
        line_[len_] = 0;
        bool ok = parse(fix, nowMs);
        len_ = 0;
        return ok;
    }
    if (len_ < int(sizeof(line_)) - 1) line_[len_++] = c;
    else len_ = 0;
    return false;
}

bool NmeaParser::parse(Fix& fix, uint64_t nowMs) {
    // Controllo del checksum.
    char* star = std::strchr(line_, '*');
    if (!star || star[1] == 0 || star[2] == 0) return false;
    uint8_t sum = 0;
    for (char* p = line_ + 1; p < star; p++) sum ^= uint8_t(*p);
    const int hi = hexVal(star[1]), lo = hexVal(star[2]);
    if (hi < 0 || lo < 0 || sum != uint8_t(hi * 16 + lo)) return false;
    *star = 0;
    char* f[20];
    const int n = split(line_ + 1, f, 20);
    if (n < 1 || std::strlen(f[0]) < 5) return false;
    const char* type = f[0] + 2;  // GPRMC, GNRMC, BDRMC...
    if (std::strcmp(type, "RMC") == 0 && n >= 10) {
        // hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,v,c,ddmmyy
        const bool valid = f[2][0] == 'A';
        if (std::strlen(f[1]) >= 6 && std::strlen(f[9]) == 6) {
            auto two = [](const char* s) { return (s[0] - '0') * 10 + (s[1] - '0'); };
            const int hh = two(f[1]), mi = two(f[1] + 2), ss = two(f[1] + 4);
            const int dd = two(f[9]), mo = two(f[9] + 2), yy = two(f[9] + 4);
            // Il ricevitore appena acceso puo' dare date del 1980 o 2080 ("..80"): un anno cosi' preso per
            // buono passerebbe anche ai ripetitori, come ora del GPS. Si accettano solo 2024-2079.
            if (yy >= 24 && yy < 80 && mo >= 1 && mo <= 12 && dd >= 1 && dd <= 31 && hh < 24 && mi < 60 && ss < 61) {
                fix.unixTime = uint32_t(daysFromCivil(2000 + yy, unsigned(mo), unsigned(dd)) * 86400 + hh * 3600 +
                                        mi * 60 + ss);
                fix.timeValid = true;
                fix.timeAtMs = nowMs;
            }
        }
        if (valid) {
            fix.lat = coord(f[3], f[4]);
            fix.lon = coord(f[5], f[6]);
            fix.valid = true;
            fix.atMs = nowMs;
        } else {
            fix.valid = false;
        }
        return true;
    }
    if (std::strcmp(type, "GSV") == 0 && n >= 4) {
        // Satelliti in vista: ogni costellazione ha le sue frasi; si somma l'ultimo totale di ognuna.
        static const char* const SISTEMI[] = {"GP", "GL", "GA", "GB", "BD", "GQ"};
        for (int i = 0; i < 6; i++)
            if (!std::strncmp(f[0], SISTEMI[i], 2)) inView_[i] = uint8_t(std::min(99, std::atoi(f[3])));
        int tot = 0;
        for (const uint8_t v : inView_) tot += v;
        fix.inVista = uint8_t(std::min(255, tot));
        return true;
    }
    if (std::strcmp(type, "GGA") == 0 && n >= 10) {
        // hhmmss,lat,N,lon,E,qualita',satelliti,hdop,altitudine,M
        const int q = std::atoi(f[6]);
        fix.sats = uint8_t(std::atoi(f[7]));
        fix.hdop = f[8][0] ? float(std::atof(f[8])) : 99.0f;
        if (q > 0 && f[9][0]) fix.altM = float(std::atof(f[9]));
        return true;
    }
    return false;
}
#endif

Gnss& gnss() {
    static Gnss g;
    return g;
}

#if CONFIG_SCHEDA_TDECK_PLUS
bool Gnss::begin() {
    g_lock = xSemaphoreCreateMutex();
    uart_config_t c = {};
    c.baud_rate = 38400;
    c.data_bits = UART_DATA_8_BITS;
    c.parity = UART_PARITY_DISABLE;
    c.stop_bits = UART_STOP_BITS_1;
    c.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    // Il quarzo e non l'APB (quello normale dell'S3): col risparmio di energia l'APB cambia frequenza quando
    // la CPU scende a 40 MHz, e le frasi NMEA arriverebbero rovinate (la UART in ricezione non tiene il
    // blocco del risparmio).
    c.source_clk = UART_SCLK_XTAL;
    if (uart_driver_install(PORT, 2048, 0, 0, nullptr, 0) != ESP_OK) return false;
    uart_param_config(PORT, &c);
    uart_set_pin(PORT, PIN_GNSS_TX, PIN_GNSS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    on_ = true;
    g_pilota.vuoi(true, "avvio");  // all'accensione il ricevitore e' sveglio
    xTaskCreatePinnedToCore(gnssTask, "gnss", STACK_GNSS, nullptr, 4, &g_compito, 0);  // riconosce la velocita'
    return true;
}

// Solo una richiesta: il PMREQ e la sveglia li manda il compito del GPS (l'unico che scrive sulla
// seriale) entro un decimo di secondo circa; in powerOff() i 300 ms di attesa bastano per il PMREQ.
void Gnss::power(bool on) { power(on, nullptr); }
#else
bool Gnss::begin() {
    g_lock = xSemaphoreCreateMutex();
    uart_config_t c = {};
    c.baud_rate = 9600;
    c.data_bits = UART_DATA_8_BITS;
    c.parity = UART_PARITY_DISABLE;
    c.stop_bits = UART_STOP_BITS_1;
    c.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    c.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(PORT, 2048, 0, 0, nullptr, 0) != ESP_OK) return false;
    uart_param_config(PORT, &c);
    uart_set_pin(PORT, UART_PIN_NO_CHANGE, PIN_GNSS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    g_modulo = "AT6558R";
    xTaskCreatePinnedToCore(gnssTask, "gnss", 4096, nullptr, 4, nullptr, 0);
    power(true);
    return true;
}

void Gnss::power(bool on) {
    on_ = on;
    expander().set(Exp::GNSS_EN, on);
}
#endif

Fix Gnss::fix() const {
    if (!g_lock) return Fix();
    xSemaphoreTake(g_lock, portMAX_DELAY);
    Fix f = g_fix;
    xSemaphoreGive(g_lock);
    return f;
}

float Gnss::accuracyM() const {
    Fix f = fix();
    return f.valid ? f.hdop * 2.5f : 999.0f;
}

const char* Gnss::modulo() const { return g_modulo; }

uint8_t Gnss::satInVista() const { return fix().inVista; }

void Gnss::eco(uint32_t secondi) { g_ecoFino = esp_timer_get_time() / 1000 + int64_t(secondi) * 1000; }

void Gnss::freddo() {
#if CONFIG_SCHEDA_TDECK_PLUS
    g_pilota.freddo();  // la fa il compito del GPS appena il ricevitore e' sveglio (CFG-RST o PCAS10,2)
    sveglia();
#endif
}

#if CONFIG_SCHEDA_TDECK_PLUS
void Gnss::power(bool on, const char* perche) {
    on_ = on;
    g_pilota.vuoi(on, perche);
    sveglia();
}

Gnss::Stato Gnss::stato() const { return g_pilota.stato(); }

bool Gnss::risponde() const { return g_pilota.risponde(); }

bool Gnss::inStandby() const { return g_pilota.inStandby(); }

uint32_t Gnss::silenzioMs() const { return g_pilota.silenzioMs(); }

Gnss::Diagnosi Gnss::diagnosi() const {
    Diagnosi d = g_pilota.diagnosi();
    if (g_compito) d.stackLibero = uint32_t(uxTaskGetStackHighWaterMark(g_compito));  // byte mai usati
    return d;
}

int Gnss::storia(Evento* out, int max) const { return g_pilota.storia(out, max); }

void Gnss::chiediVersione() {
    g_pilota.chiediVersione();
    sveglia();
}

Gnss::Versione Gnss::versione() const { return g_pilota.versione(); }

void Gnss::prova(int cicli, bool forza) {
    g_pilota.prova(cicli, forza);
    sveglia();
}

Gnss::Prova Gnss::provaStato() const { return g_pilota.provaStato(); }
#endif

}  // namespace scheda
