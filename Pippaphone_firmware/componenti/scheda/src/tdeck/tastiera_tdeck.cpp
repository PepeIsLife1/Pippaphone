// Tastiera e trackball del LilyGO T-Deck Plus.
//
// La tastiera e' un ESP32-C3 (indirizzo I2C 0x55, solo 100 kHz) che scandisce la stessa matrice
// BlackBerry Q10 della PCB 2 del Pippaphone, ogni ~35 ms. Non ha un'interruzione verso l'ESP32-S3:
// si interroga ogni 12 ms. Due modi:
//  - raw (firmware del C3 dal giugno 2025, comando 0x03): 5 byte, uno per colonna, coi tasti
//    tenuti premuti. Da qui si ricavano premuto/rilasciato di ogni tasto come col TCA8418;
//  - caratteri (firmware vecchio): un carattere gia' composto per ogni pressione, niente
//    rilasci. Si traducono di nuovo nei tasti della PCB (ALT + tasto per cifre e simboli, che sul
//    T-Deck sono gli stessi stampati sui tasti).
// Il modo si capisce da solo: in raw ogni byte ha 7 bit e un tasto resta visibile per almeno due
// letture (il C3 aggiorna lo stato una volta per scansione), mentre un carattere arriva in una
// lettura sola.
//
// Trackball: ogni scatto cambia il livello di uno dei quattro pin (interruzione su entrambi i
// fronti); il clic e' il GPIO 0, letto con un filtro dei rimbalzi.
#include <atomic>
#include <cstring>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "scheda/pins.h"
#include "scheda/tastiera.h"

namespace scheda {

namespace {
const char* TAG = "tastiera";
constexpr uint8_t CMD_LIGHT = 0x01, CMD_RAW = 0x03;
constexpr int TB_EDGES_PER_STEP = 2;      // fronti per uno scatto del trackball (da provare)
constexpr int TB_MAX_STEPS = 3;           // al massimo tre passi per lettura
constexpr int64_t CLICK_DEBOUNCE_MS = 25;
constexpr int64_t RAW_REFRESH_MS = 5000;  // il comando raw si ripete (se il C3 si e' riavviato)

// Matrice del C3: MATRIX[colonna][riga] (da Keyboard_ESP32C3.ino di LilyGO).
const Key MATRIX[5][7] = {
    {Key::Q, Key::W, Key::SYM, Key::A, Key::ALT, Key::SPACE, Key::ZERO_MIC},
    {Key::E, Key::S, Key::D, Key::P, Key::X, Key::Z, Key::LSHIFT},
    {Key::R, Key::G, Key::T, Key::RSHIFT, Key::V, Key::C, Key::F},
    {Key::U, Key::H, Key::Y, Key::ENTER, Key::B, Key::N, Key::J},
    {Key::O, Key::L, Key::I, Key::BKSP, Key::DOLLAR, Key::M, Key::K},
};

// Impulsi del trackball contati nelle interruzioni: su, giu', sinistra, destra.
std::atomic<int32_t> g_tb[4] = {};
const gpio_num_t TB_PINS[4] = {PIN_TB_UP, PIN_TB_DOWN, PIN_TB_LEFT, PIN_TB_RIGHT};
const Key TB_KEYS[4] = {Key::UP, Key::DOWN, Key::LEFT, Key::RIGHT};

void IRAM_ATTR tbIsr(void* arg) { g_tb[reinterpret_cast<intptr_t>(arg)].fetch_add(1, std::memory_order_relaxed); }

int64_t nowMs() { return esp_timer_get_time() / 1000; }

bool anyBit(const uint8_t* c) {
    for (int i = 0; i < 5; i++)
        if (c[i]) return true;
    return false;
}

bool highBit(const uint8_t* c, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (c[i] & 0x80) return true;
    return false;
}

// Tasto della tastiera che con ALT da' il carattere c (NONE = nessuno).
Key keyForAltChar(uint8_t c) {
    for (int col = 0; col < 5; col++)
        for (int row = 0; row < 7; row++)
            if (altCharOf(MATRIX[col][row]) == c) return MATRIX[col][row];
    return Key::NONE;
}

uint8_t g_blips = 0;     // caratteri visti in una lettura sola mentre il modo e' da capire
uint8_t g_firstBlip = 0;
int64_t g_lastRaw = 0;
}  // namespace

bool Tastiera::begin() {
    if (!q_) q_ = xQueueCreate(32, sizeof(KeyEvent));

    // Trackball: pull-up interne, interruzione su entrambi i fronti.
    gpio_config_t io = {};
    for (int i = 0; i < 4; i++) io.pin_bit_mask |= 1ULL << TB_PINS[i];
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.intr_type = GPIO_INTR_ANYEDGE;
    gpio_config(&io);
    // Dopo il risveglio col trackball il pin e' ancora dell'RTC: torna un GPIO normale.
    rtc_gpio_deinit(PIN_TB_CLICK);
    gpio_config_t click = {};
    click.pin_bit_mask = 1ULL << PIN_TB_CLICK;
    click.mode = GPIO_MODE_INPUT;
    click.pull_up_en = GPIO_PULLUP_ENABLE;
    click.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&click);
    // Il clic che ha riacceso il telefono e' ancora premuto: non conta come OK.
    clickDown_ = clickIgnora_ = gpio_get_level(PIN_TB_CLICK) == 0;
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);  // se e' gia' installato l'errore e' innocuo
    for (int i = 0; i < 4; i++) gpio_isr_handler_add(TB_PINS[i], tbIsr, reinterpret_cast<void*>(intptr_t(i)));

    // Il C3 parte insieme alle periferiche (POWERON): gli si lascia un po' di tempo.
    for (int i = 0; i < 20 && !dev_.present(); i++) {
        if (dev_.attach(i2cBus(), ADDR_TASTIERA, 100000)) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (!dev_.present()) {
        ESP_LOGE(TAG, "tastiera (ESP32-C3) non risponde: resta solo il trackball");
        return false;
    }
    const uint8_t raw = CMD_RAW;
    dev_.write(&raw, 1);
    mode_ = UNKNOWN;
    ESP_LOGI(TAG, "tastiera pronta, chiesto il modo raw");
    return true;
}

void Tastiera::light(uint8_t level) {
    if (!ok()) return;
    const uint8_t b[2] = {CMD_LIGHT, level};
    dev_.write(b, 2);
}

void Tastiera::push(Key k, bool pressed) {
    if (k == Key::NONE) return;
    KeyEvent e{k, pressed};
    xQueueSend(q_, &e, 0);
}

void Tastiera::tap(Key k) {
    push(k, true);
    push(k, false);
}

void Tastiera::service() {
    serviceTrackball();
    if (!ok()) return;
    const int64_t now = nowMs();
    uint8_t buf[5] = {};
    const size_t want = mode_ == CHARS ? 1 : 5;
    if (!dev_.read(buf, want)) {
        // Col firmware vecchio una lettura di 5 byte potrebbe non andare a buon fine.
        if (mode_ == UNKNOWN && ++readErrors_ >= 3) {
            mode_ = CHARS;
            ESP_LOGW(TAG, "il C3 non risponde a 5 byte: ripiego coi caratteri");
        }
        return;
    }
    readErrors_ = 0;

    switch (mode_) {
        case CHARS:
            if (buf[0]) serviceChar(buf[0]);
            return;
        case RAW:
            if (now - g_lastRaw > RAW_REFRESH_MS || highBit(buf, 5)) {
                // Ogni tanto (o se il C3 si e' riavviato ed e' tornato ai caratteri) si ripete.
                g_lastRaw = now;
                const uint8_t raw = CMD_RAW;
                dev_.write(&raw, 1);
                if (highBit(buf, 5)) return;
            }
            serviceRaw(buf);
            return;
        case UNKNOWN:
            break;
    }

    // Modo ancora da capire.
    if (highBit(buf, 5)) {  // in raw ogni byte ha solo 7 bit: e' il firmware vecchio
        mode_ = CHARS;
        ESP_LOGW(TAG, "firmware della tastiera senza modo raw: ripiego coi caratteri");
        if (buf[0]) serviceChar(buf[0]);
        return;
    }
    if (!anyBit(buf)) {
        if (pendingCount_ == 1) {
            // Visto in una lettura sola: un carattere (o un disturbo). Al secondo si decide.
            if (++g_blips == 1) {
                g_firstBlip = last_[0];
                const uint8_t raw = CMD_RAW;
                dev_.write(&raw, 1);
            } else {
                mode_ = CHARS;
                ESP_LOGW(TAG, "firmware della tastiera senza modo raw: ripiego coi caratteri");
                if (g_firstBlip) serviceChar(g_firstBlip);
                serviceChar(last_[0]);
            }
        }
        pendingCount_ = 0;
        std::memset(last_, 0, sizeof(last_));
        return;
    }
    if (pendingCount_ && std::memcmp(buf, last_, 5) == 0) {
        mode_ = RAW;
        g_lastRaw = now;
        ESP_LOGI(TAG, "tastiera in modo raw");
        std::memset(last_, 0, sizeof(last_));
        serviceRaw(buf);
        serviceRaw(buf);  // due letture uguali: accettata subito
        return;
    }
    std::memcpy(last_, buf, 5);
    pendingCount_ = 1;
}

// Modo raw: un cambiamento si accetta quando due letture di fila sono uguali (rimbalzi).
void Tastiera::serviceRaw(const uint8_t* cols) {
    if (std::memcmp(cols, last_, 5) != 0) {
        std::memcpy(last_, cols, 5);
        return;
    }
    for (int col = 0; col < 5; col++) {
        const uint8_t diff = uint8_t((cols[col] ^ stable_[col]) & 0x7F);
        for (int row = 0; row < 7; row++)
            if (diff & (1 << row)) onRawChange(col, row, (cols[col] & (1 << row)) != 0);
        stable_[col] = cols[col];
    }
}

void Tastiera::onRawChange(int col, int row, bool pressed) {
    const Key k = MATRIX[col][row];
    if (pressed) ESP_LOGI(TAG, "colonna %d riga %d -> %s", col, row, keyName(k));

    // SYM: da solo apre accenti e simboli (al rilascio); tenuto, fa le combinazioni.
    if (k == Key::SYM) {
        symDown_ = pressed;
        if (pressed) symUsed_ = false;
        else if (!symUsed_) tap(Key::SYM);
        return;
    }

    // SHIFT: da solo e' la maiuscola (al rilascio), tenuto vale per ogni lettera, i due insieme
    // sono l'SOS (premuto finche' non se ne rilascia uno).
    if (k == Key::LSHIFT || k == Key::RSHIFT) {
        (k == Key::LSHIFT ? lshDown_ : rshDown_) = pressed;
        const bool both = lshDown_ && rshDown_, none = !lshDown_ && !rshDown_;
        if (pressed) {
            if (both && !sosSent_) {
                sosSent_ = true;
                push(Key::SOS, true);
            }
            return;
        }
        if (sosSent_) {
            if (!both && !sosReleased_) {
                push(Key::SOS, false);
                sosReleased_ = true;
            }
            if (none) sosSent_ = sosReleased_ = shiftSent_ = false;
            return;
        }
        if (none) {
            if (!shiftSent_) tap(Key::LSHIFT);
            shiftSent_ = false;
        }
        return;
    }

    if (!pressed) {
        push(chord_[col][row], false);
        chord_[col][row] = Key::NONE;
        return;
    }
    Key send = k;
    if (symDown_) {
        symUsed_ = true;
        switch (k) {
            case Key::M: send = Key::MENU; break;
            case Key::C: send = Key::CALL; break;
            case Key::H: send = Key::END; break;
            case Key::SPACE: send = Key::PWR; break;
            default:
                // SYM + altro tasto: il simbolo stampato sul tasto, come col firmware di fabbrica
                // (= ALT + tasto del Pippaphone).
                tap(Key::ALT);
                break;
        }
    } else if ((lshDown_ || rshDown_) && !sosSent_) {
        tap(Key::LSHIFT);  // SHIFT tenuto: maiuscola anche per le lettere successive
        shiftSent_ = true;
    }
    chord_[col][row] = send;
    push(send, true);
}

// Ripiego: caratteri gia' composti dal C3, una pressione alla volta.
void Tastiera::serviceChar(uint8_t c) {
    ESP_LOGI(TAG, "carattere 0x%02x", c);
    if (c >= 'a' && c <= 'z') {
        tap(Key(uint8_t(Key::A) + (c - 'a')));
        return;
    }
    if (c >= 'A' && c <= 'Z') {
        tap(Key::LSHIFT);
        tap(Key(uint8_t(Key::A) + (c - 'A')));
        return;
    }
    switch (c) {
        case ' ': tap(Key::SPACE); return;
        case 0x0D: tap(Key::ENTER); return;
        case 0x08: tap(Key::BKSP); return;
        case '$': tap(Key::DOLLAR); return;
        case 0x0C: tap(Key::CALL); return;  // ALT+C
        case 0x04: tap(Key::MENU); return;  // SHIFT+$ ('$' - 32)
        case 0x1A: tap(Key::END); return;   // SYM+SHIFT+H (':' - 32)
        case 0xE0:                          // SHIFT+microfono (0 - 32): PTT, premi e ripremi
            pttToggle_ = !pttToggle_;
            push(Key::PTT, pttToggle_);
            return;
        default: break;
    }
    const Key k = keyForAltChar(c);
    if (k != Key::NONE) {
        tap(Key::ALT);
        tap(k);
    }
}

void Tastiera::serviceTrackball() {
    const int64_t now = nowMs();
    // Clic (GPIO 0, basso = premuto) con filtro dei rimbalzi.
    const bool down = gpio_get_level(PIN_TB_CLICK) == 0;
    if (down != clickDown_ && now - clickChangeMs_ >= CLICK_DEBOUNCE_MS) {
        clickDown_ = down;
        clickChangeMs_ = now;
        if (clickIgnora_) clickIgnora_ = down;  // si riprende dal clic dopo
        else push(Key::OK, down);
    }
    for (int i = 0; i < 4; i++) {
        const int32_t edges = g_tb[i].load(std::memory_order_relaxed);
        if (edges < TB_EDGES_PER_STEP) continue;
        int steps = edges / TB_EDGES_PER_STEP;
        g_tb[i].fetch_sub(steps * TB_EDGES_PER_STEP, std::memory_order_relaxed);
        if (steps > TB_MAX_STEPS) steps = TB_MAX_STEPS;
        Key k = TB_KEYS[i];
        if (symDown_ && (k == Key::UP || k == Key::DOWN)) {
            symUsed_ = true;
            k = k == Key::UP ? Key::VOLUP : Key::VOLDN;
            steps = 1;
        }
        for (int s = 0; s < steps; s++) tap(k);
    }
}

}  // namespace scheda
