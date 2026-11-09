// Simulatore del Pippaphone nel browser (WebAssembly con Emscripten), versione LilyGO T-Deck Plus.
//
// Gira l'interfaccia vera (telefono/main/ui), coi dati veri (app/dati.cpp) e il codice vero della
// tastiera e del trackball del T-Deck (scheda/src/tdeck/tastiera_tdeck.cpp). Al posto dell'hardware:
//  - un ESP32-C3 finto risponde sull'I2C coi 5 byte del modo raw: i tasti tenuti premuti nella pagina;
//  - il trackball manda i fronti alle stesse interruzioni del firmware, il clic e' il GPIO 0;
//  - schermo, retroilluminazione, luce della tastiera e suoni vanno alla pagina (shell.html);
//  - la rete e' finta (rete_web.cpp) e risponde ai messaggi.
// Il compito dell'interfaccia gira come sul telefono: vTaskDelay cede il controllo al browser
// (ASYNCIFY) e intanto qui si fa il lavoro degli altri compiti (tastiera ogni 12 ms, rete finta).
#include <emscripten.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <map>

#include "app/dati.h"
#include "app/eventi.h"
#include "app/memoria.h"
#include "scheda/pins.h"
#include "scheda/scheda.h"
#include "sim.h"
#include "ui/ui.h"
#include "web/web.h"

// Coda dei tasti vista senza toglierne gli elementi (sim_rtos.cpp).
size_t simQueueSize(QueueHandle_t q);
bool simQueuePeek(QueueHandle_t q, size_t i, void* item);

// ---------------------------------------------------------------- verso la pagina (PippaSim in shell.html)

EM_JS(void, js_frame, (const uint16_t* px, int w, int h), {
    PippaSim.frame(HEAPU16.subarray(px >> 1, (px >> 1) + w * h), w, h);
});
EM_JS(void, js_backlight, (int percent), { PippaSim.backlight(percent); });
EM_JS(void, js_keylight, (int level), { PippaSim.keyLight(level); });
EM_JS(void, js_key, (const char* name, int pressed), { PippaSim.keyEvent(UTF8ToString(name), !!pressed); });
EM_JS(void, js_play, (const uint16_t* notes, int count, int loop, int volume), {
    const n = [];
    for (let i = 0; i < count; i++) n.push([HEAPU16[(notes >> 1) + 2 * i], HEAPU16[(notes >> 1) + 2 * i + 1]]);
    PippaSim.play(n, !!loop, volume);
});
EM_JS(void, js_stop_sound, (), { PippaSim.stopSound(); });
EM_JS(void, js_power_off, (), { PippaSim.powerOff(); });
EM_JS(void, js_restart, (), { PippaSim.restart(); });
EM_JS(void, js_ready, (), { PippaSim.ready(); });

namespace {

using scheda::Key;

// Tasti tenuti premuti, come li manda il C3 in modo raw: un byte per colonna, 7 righe. Il C3 vero
// cambia stato una volta per scansione (~35 ms) e il firmware lo legge ogni 12 ms: ogni stato si
// vede almeno due volte, quanto serve al filtro dei rimbalzi. Qui gli stati chiesti dalla pagina
// aspettano in fila e ognuno si legge due volte, cosi' anche i tasti battuti in fretta arrivano tutti.
using Matrix = std::array<uint8_t, 5>;
Matrix g_matrix = {};                    // ultimo stato chiesto dalla pagina
std::deque<Matrix> g_states = {Matrix{}};
int g_reads = 2;                         // letture dello stato in testa alla fila

void pushMatrix() { g_states.push_back(g_matrix); }

Matrix readMatrix() {
    if (g_states.size() > 1 && g_reads >= 2) {
        g_states.pop_front();
        g_reads = 0;
    }
    g_reads++;
    return g_states.front();
}

// Interruzioni del trackball (gpio_isr_handler_add) e clic (GPIO 0, basso = premuto).
struct Isr {
    gpio_isr_t fn = nullptr;
    void* arg = nullptr;
};
Isr g_isr[49];
bool g_click = false;

struct Cmd {
    int cmd, arg;
};
std::deque<Cmd> g_cmds;
std::multimap<int64_t, std::function<void()>> g_timers;
bool g_dirty = false;
int64_t g_lastInput = -1000;
double g_t0 = 0;

int64_t nowMs() { return web::nowUs() / 1000; }

// Il lavoro degli altri compiti del firmware, fatto mentre l'interfaccia aspetta.
void services() {
    const int64_t now = nowMs();
    // Tastiera e trackball: come il compito "input" di scheda_tdeck.cpp, ogni 12 ms. I tasti che il
    // codice vero della tastiera mette in coda si mostrano anche nella pagina.
    if (now - g_lastInput >= 12) {
        g_lastInput = now;
        QueueHandle_t q = scheda::tastiera().queue();
        const size_t before = simQueueSize(q);
        scheda::tastiera().service();
        scheda::KeyEvent e;
        for (size_t i = before; i < simQueueSize(q); i++)
            if (simQueuePeek(q, i, &e)) js_key(scheda::keyName(e.key), e.pressed);
    }
    // Lavori in programma (rete finta, lampeggi, fine dei vocali).
    while (!g_timers.empty() && g_timers.begin()->first <= nowMs()) {
        std::function<void()> fn = std::move(g_timers.begin()->second);
        g_timers.erase(g_timers.begin());
        fn();
    }
    // Comandi della pagina ("fai succedere qualcosa").
    while (!g_cmds.empty()) {
        const Cmd c = g_cmds.front();
        g_cmds.pop_front();
        web::command(c.cmd, c.arg);
    }
    if (g_dirty) {
        g_dirty = false;
        js_frame(sim::pixels(), sim::width(), sim::height());
    }
}

}  // namespace

// ---------------------------------------------------------------- servizi per il resto del simulatore

namespace web {

int64_t nowUs() { return int64_t((emscripten_get_now() - g_t0) * 1000.0); }

void sleep(uint32_t ms) {
    const int64_t end = nowMs() + ms;
    for (;;) {
        services();
        const int64_t left = end - nowMs();
        if (left <= 0) break;
        emscripten_sleep(uint32_t(std::min<int64_t>(left, 12)));
    }
}

void after(uint32_t ms, std::function<void()> fn) { g_timers.emplace(nowMs() + ms, std::move(fn)); }

void frameChanged() { g_dirty = true; }

void backlight(uint8_t percent) { js_backlight(percent); }

void keyboardLight(uint8_t level) { js_keylight(level); }

void play(const std::vector<scheda::Note>& notes, bool loop, uint8_t volume) {
    std::vector<uint16_t> n;
    for (const auto& note : notes) {
        n.push_back(note.hz);
        n.push_back(note.ms);
    }
    if (!n.empty()) js_play(n.data(), int(notes.size()), loop, volume);
}

void stopSound() { js_stop_sound(); }

void powerOff() {
    js_power_off();
    for (;;) emscripten_sleep(60000);  // sonno profondo: si riaccende con l'interruttore (la pagina riparte)
}

void restart() {
    js_restart();
    for (;;) emscripten_sleep(60000);
}

}  // namespace web

// ---------------------------------------------------------------- dalla pagina

extern "C" {

// Tasto della matrice del C3 (colonna 0-4, riga 0-6) premuto o rilasciato.
EMSCRIPTEN_KEEPALIVE void web_matrix(int col, int row, int pressed) {
    if (col < 0 || col > 4 || row < 0 || row > 6) return;
    if (pressed) g_matrix[col] = uint8_t(g_matrix[col] | (1 << row));
    else g_matrix[col] = uint8_t(g_matrix[col] & ~(1 << row));
    pushMatrix();
}

// Trackball: dir 0 su, 1 giu', 2 sinistra, 3 destra. Ogni scatto sono due fronti sul pin
// (TB_EDGES_PER_STEP in tastiera_tdeck.cpp).
EMSCRIPTEN_KEEPALIVE void web_trackball(int dir, int steps) {
    static const gpio_num_t pins[4] = {scheda::PIN_TB_UP, scheda::PIN_TB_DOWN, scheda::PIN_TB_LEFT,
                                       scheda::PIN_TB_RIGHT};
    if (dir < 0 || dir > 3) return;
    const Isr& isr = g_isr[pins[dir]];
    for (int i = 0; i < steps * 2 && isr.fn; i++) isr.fn(isr.arg);
}

EMSCRIPTEN_KEEPALIVE void web_click(int down) { g_click = down != 0; }

// Comandi della rete finta (codici in rete_web.cpp e in shell.html).
EMSCRIPTEN_KEEPALIVE void web_cmd(int cmd, int arg) { g_cmds.push_back({cmd, arg}); }

}  // extern "C"

// ---------------------------------------------------------------- hardware finto per tastiera_tdeck.cpp

int gpio_config(const gpio_config_t*) { return 0; }
int gpio_install_isr_service(int) { return 0; }
int gpio_isr_handler_add(gpio_num_t pin, gpio_isr_t fn, void* arg) {
    if (pin >= 0 && pin < 49) g_isr[pin] = {fn, arg};
    return 0;
}
int gpio_get_level(gpio_num_t pin) { return pin == scheda::PIN_TB_CLICK && g_click ? 0 : 1; }

namespace scheda {

// Sul bus I2C risponde solo il C3 della tastiera.
i2c_master_bus_handle_t i2cBus() {
    static int bus;
    return reinterpret_cast<i2c_master_bus_handle_t>(&bus);
}

bool I2cDev::attach(i2c_master_bus_handle_t, uint8_t addr, uint32_t) {
    static int c3;
    if (addr != ADDR_TASTIERA) return false;
    dev_ = reinterpret_cast<i2c_master_dev_handle_t>(&c3);
    return true;
}

bool I2cDev::write(const uint8_t* data, size_t len) {
    if (!dev_ || !len) return false;
    if (data[0] == 0x01 && len >= 2) web::keyboardLight(data[1]);  // luce della tastiera
    return true;                                                     // 0x03: modo raw (sempre attivo)
}

bool I2cDev::read(uint8_t* out, size_t len) {
    if (!dev_) return false;
    const Matrix m = readMatrix();
    for (size_t i = 0; i < len; i++) out[i] = i < 5 ? m[i] : 0;
    return true;
}

bool I2cDev::writeReg(uint8_t, uint8_t) { return false; }
bool I2cDev::readReg(uint8_t, uint8_t*, size_t) { return false; }
uint8_t I2cDev::readReg8(uint8_t, bool* ok) {
    if (ok) *ok = false;
    return 0;
}
bool I2cDev::readReg16be(uint8_t, uint16_t&) { return false; }

}  // namespace scheda

// ---------------------------------------------------------------- avvio (come telefono/main/main.cpp)

int main() {
    g_t0 = emscripten_get_now();
    sim::setSize(scheda::Display::WIDTH, scheda::Display::HEIGHT);
    app::memoriaInit();
    app::eventsInit();
    scheda::tastiera().begin();  // come sul T-Deck: il modo della tastiera si capisce al primo tasto
    sim::makeTiles(false);
    sim::fillData();
    const app::Settings s = app::dati().settings();
    scheda::audio().setVolume(s.volume);
    scheda::display().backlight(s.brightness);
    if (s.keyLight) scheda::keyboardLight(60);
    js_ready();
    ui::init();  // non torna: il compito dell'interfaccia gira per sempre
    return 0;
}
