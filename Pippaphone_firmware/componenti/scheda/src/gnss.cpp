#include "scheda/gnss.h"

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
#endif

namespace scheda {

namespace {
constexpr uart_port_t PORT = UART_NUM_1;
SemaphoreHandle_t g_lock = nullptr;
Fix g_fix;

#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck Plus: GPS u-blox M10Q (38400 baud) oppure Quectel L76K (9600), sempre alimentato. La
// velocita' si riconosce dalle frasi NMEA valide; lo standby si fa solo sull'u-blox (comando
// UBX-RXM-PMREQ, si risveglia con un byte sulla seriale). L'L76K resta acceso.
const char* TAG_GNSS = "gnss";
bool g_ublox = false;
bool g_standby = false;

// Aspetta una frase NMEA con il checksum giusto per al massimo ms millisecondi.
bool nmeaWithin(int ms) {
    NmeaParser p;
    Fix f;
    uint8_t b[64];
    const int64_t end = esp_timer_get_time() / 1000 + ms;
    while (esp_timer_get_time() / 1000 < end) {
        const int n = uart_read_bytes(PORT, b, sizeof(b), pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++)
            if (p.feed(char(b[i]), f, 0)) return true;
    }
    return false;
}

void ubx(uint8_t cls, uint8_t id, const uint8_t* payload, uint16_t len) {
    uint8_t head[6] = {0xB5, 0x62, cls, id, uint8_t(len), uint8_t(len >> 8)};
    uint8_t ca = 0, cb = 0;
    for (int i = 2; i < 6; i++) {
        ca += head[i];
        cb += ca;
    }
    for (uint16_t i = 0; i < len; i++) {
        ca += payload[i];
        cb += ca;
    }
    const uint8_t tail[2] = {ca, cb};
    uart_write_bytes(PORT, head, sizeof(head));
    if (len) uart_write_bytes(PORT, payload, len);
    uart_write_bytes(PORT, tail, sizeof(tail));
}

// Prima 38400 (u-blox M10Q), poi 9600 (L76K); nel compito del GPS, per non rallentare l'avvio.
// Un u-blox lasciato in standby da un giro precedente si sveglia col primo byte.
void detectBaud() {
    for (int round = 0; round < 3; round++) {
        uart_set_baudrate(PORT, 38400);
        const uint8_t wake = 0xFF;
        uart_write_bytes(PORT, &wake, 1);
        if (nmeaWithin(1500)) {
            g_ublox = true;
            ESP_LOGI(TAG_GNSS, "GPS u-blox (38400 baud)");
            return;
        }
        uart_set_baudrate(PORT, 9600);
        if (nmeaWithin(1500)) {
            ESP_LOGI(TAG_GNSS, "GPS L76K (9600 baud)");
            return;
        }
    }
    ESP_LOGW(TAG_GNSS, "GPS muto: nessuna frase NMEA a 38400 o 9600 baud (resta a 9600)");
}
#endif

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
#if CONFIG_SCHEDA_TDECK_PLUS
    detectBaud();
#endif
    for (;;) {
        int n = uart_read_bytes(PORT, buf, sizeof(buf), pdMS_TO_TICKS(500));
        if (n <= 0) continue;
        const uint64_t now = uint64_t(esp_timer_get_time() / 1000);
        xSemaphoreTake(g_lock, portMAX_DELAY);
        for (int i = 0; i < n; i++) parser.feed(char(buf[i]), g_fix, now);
        xSemaphoreGive(g_lock);
    }
}
}  // namespace

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
            if (yy >= 24) {  // il ricevitore appena acceso puo' dare date del 1980/2080
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
    c.source_clk = UART_SCLK_DEFAULT;
    if (uart_driver_install(PORT, 2048, 0, 0, nullptr, 0) != ESP_OK) return false;
    uart_param_config(PORT, &c);
    uart_set_pin(PORT, PIN_GNSS_TX, PIN_GNSS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    xTaskCreatePinnedToCore(gnssTask, "gnss", 4096, nullptr, 4, nullptr, 0);  // riconosce la velocita'
    on_ = true;
    return true;
}

void Gnss::power(bool on) {
    on_ = on;
    if (!g_ublox || on == !g_standby) return;
    if (on) {
        const uint8_t wake = 0xFF;  // un fronte sulla seriale lo sveglia
        uart_write_bytes(PORT, &wake, 1);
        g_standby = false;
    } else {
        // UBX-RXM-PMREQ: durata 0 = finche' non arriva un byte, flag backup, sveglia da UART RX.
        const uint8_t pm[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0x02, 0, 0, 0, 0x08, 0, 0, 0};
        ubx(0x02, 0x41, pm, sizeof(pm));
        g_standby = true;
    }
}
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

}  // namespace scheda
