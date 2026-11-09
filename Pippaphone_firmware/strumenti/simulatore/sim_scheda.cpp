// Simulatore: la scheda finta (display in memoria, audio muto, GPS e batteria fissi).
// La tastiera e' quella vera di tastiera.cpp (nomi dei tasti, lettere, ALT); qui solo il resto.
// Nel browser (SIM_WEB) schermo, luce della tastiera e suoni vanno alla pagina (web/sim_web.cpp)
// e la tastiera del T-Deck e' quella vera di tastiera_tdeck.cpp, con un C3 finto.
#include <algorithm>
#include <cstring>
#include <vector>

#include "scheda/scheda.h"
#include "sim.h"
#ifdef SIM_WEB
#include "web/web.h"
#endif

namespace scheda {

// ---------------------------------------------------------------- melodie (come in audio.cpp)

namespace suoni {
const std::vector<Note> SQUILLO = {{1568, 90}, {0, 40}, {2093, 140}};
const std::vector<Note> CLASSICA = {{1319, 120}, {1175, 120}, {740, 240}, {831, 240}, {1109, 120}, {988, 120},
                                    {587, 240}, {659, 240}, {988, 120}, {880, 120}, {554, 240}, {659, 240},
                                    {880, 480}, {0, 800}};
const std::vector<Note> SOS = {{2000, 150}, {0, 60}, {2000, 150}, {0, 60}, {2000, 150}, {0, 200},
                               {2000, 400}, {0, 60}, {2000, 400}, {0, 60}, {2000, 400}, {0, 200},
                               {2000, 150}, {0, 60}, {2000, 150}, {0, 60}, {2000, 150}, {0, 700}};
const std::vector<Note> CLIC = {{3000, 8}};
const std::vector<Note> INVIO_OK = {{1760, 60}, {2349, 90}};
const std::vector<Note> ERRORE = {{440, 180}, {0, 40}, {330, 260}};
const std::vector<Note> PTT_INIZIO = {{1200, 60}, {1800, 60}};
const std::vector<Note> PTT_FINE = {{1800, 60}, {1200, 60}};
}  // namespace suoni

// ---------------------------------------------------------------- display

Display& display() {
    static Display d;
    return d;
}
bool Display::begin() { return true; }
void Display::draw(int x1, int y1, int x2, int y2, const void* pixels) { sim::draw(x1, y1, x2, y2, pixels); }
void Display::backlight(uint8_t percent) {
    bl_ = percent;
#ifdef SIM_WEB
    web::backlight(percent);
#endif
}
#if CONFIG_SCHEDA_TDECK_PLUS
void Display::sleep() {}
#endif

// ---------------------------------------------------------------- audio

Audio& audio() {
    static Audio a;
    return a;
}
bool Audio::begin() { return true; }
#ifdef SIM_WEB
void Audio::play(const std::vector<Note>& notes, bool loop) { web::play(notes, loop, volume_); }
#else
void Audio::play(const std::vector<Note>&, bool) {}
#endif
void Audio::playPcm(std::vector<int16_t>, std::function<void()> done) {
    if (done) done();
}
void Audio::stop() {
#ifdef SIM_WEB
    web::stopSound();
#endif
}
bool Audio::busy() const { return false; }
void Audio::startRecording(uint32_t, std::function<void(const int16_t*, size_t)>, std::function<void()>) {}
void Audio::stopRecording() {}
bool Audio::recording() const { return sim::state().recording; }

// ---------------------------------------------------------------- GPS

Gnss& gnss() {
    static Gnss g;
    return g;
}
bool Gnss::begin() { return true; }
void Gnss::power(bool on) { on_ = on; }
Fix Gnss::fix() const {
    Fix f;
    f.valid = sim::state().gpsFix;
    f.timeValid = true;
    f.lat = 45.4642;
    f.lon = 9.19;
    f.altM = 122;
    f.hdop = 0.9f;
    f.sats = f.valid ? 9 : 3;
    f.unixTime = sim::unixNow();
    return f;
}
float Gnss::accuracyM() const { return sim::state().gpsFix ? 8 : 999; }

// ---------------------------------------------------------------- batteria

Batteria& batteria() {
    static Batteria b;
    return b;
}
bool Batteria::begin() { return true; }
uint8_t Batteria::percent() { return sim::state().battery; }
float Batteria::volts() { return 3.91f; }
float Batteria::ratePerHour() { return -6; }
bool Batteria::charging() { return sim::state().charging; }
#if !CONFIG_SCHEDA_TDECK_PLUS
bool Batteria::clearAlert() { return false; }
#endif

// ---------------------------------------------------------------- tastiera

#if CONFIG_SCHEDA_TDECK_PLUS
#ifndef SIM_WEB  // nel browser c'e' il codice vero (tastiera_tdeck.cpp) con un C3 finto
bool Tastiera::begin() {
    if (!q_) q_ = xQueueCreate(64, sizeof(KeyEvent));
    mode_ = RAW;  // il simulatore mostra le scorciatoie del modo raw
    return true;
}
void Tastiera::service() {}
void Tastiera::light(uint8_t) {}
#endif
#else
// tastiera.cpp (TCA8418) usa I2C ed expander: qui non rispondono, ma la coda dei tasti c'e'.
i2c_master_bus_handle_t i2cBus() { return nullptr; }
bool I2cDev::attach(i2c_master_bus_handle_t, uint8_t, uint32_t) { return false; }
bool I2cDev::write(const uint8_t*, size_t) { return false; }
bool I2cDev::writeReg(uint8_t, uint8_t) { return false; }
bool I2cDev::readReg(uint8_t, uint8_t*, size_t) { return false; }
uint8_t I2cDev::readReg8(uint8_t, bool* ok) {
    if (ok) *ok = false;
    return 0;
}
Expander& expander() {
    static Expander e;
    return e;
}
uint8_t Expander::readButtons() { return 0; }
#endif

// ---------------------------------------------------------------- servizi della scheda

void vibrate(uint16_t) {}
void vibratePattern(uint8_t, uint16_t, uint16_t) {}
#ifdef SIM_WEB
// Come scheda_tdeck.cpp: niente LED RGB, lampeggia la luce della tastiera, ma solo quando e' spenta.
namespace {
uint8_t g_kbLevel = 0;
bool g_blinking = false;
struct {
    uint8_t level;
    uint16_t on, off;
    uint8_t left;
    bool infinite, lit;
} g_blink = {};
uint32_t g_blinkGen = 0;  // i lampeggi vecchi ancora in programma si ignorano

void applyKeyboardLight() {
    if (g_blinking && g_kbLevel == 0) tastiera().light(g_blink.lit ? g_blink.level : 0);
    else tastiera().light(g_kbLevel);
}

void blinkStep(uint32_t gen) {
    if (!g_blinking || gen != g_blinkGen) return;
    if (g_blink.lit) {
        g_blink.lit = false;
        if (!g_blink.infinite && --g_blink.left == 0) {
            g_blinking = false;
            applyKeyboardLight();
            return;
        }
        web::after(g_blink.off, [gen] { blinkStep(gen); });
    } else {
        g_blink.lit = true;
        web::after(g_blink.on, [gen] { blinkStep(gen); });
    }
    applyKeyboardLight();
}
}  // namespace

void ledBlink(uint8_t r, uint8_t g, uint8_t b, uint16_t onMs, uint16_t offMs, uint8_t count) {
    uint16_t level = std::max(r, std::max(g, b));
    level = std::min<uint16_t>(255, level * 2 + 20);  // la luce della tastiera e' piu' debole del LED
    g_blink = {uint8_t(level), onMs, offMs, count, count == 0, true};
    g_blinking = true;
    const uint32_t gen = ++g_blinkGen;
    applyKeyboardLight();
    web::after(onMs, [gen] { blinkStep(gen); });
}
void ledOff() {
    g_blinking = false;
    ++g_blinkGen;
    applyKeyboardLight();
}
void keyboardLight(uint8_t level) {
    g_kbLevel = level;
    applyKeyboardLight();
}
#else
void ledBlink(uint8_t, uint8_t, uint8_t, uint16_t, uint16_t, uint8_t) {}
void ledOff() {}
void keyboardLight(uint8_t) {}
#endif
void spiLowDrive() {}
void powerOff() {
#ifdef SIM_WEB
    // Sul T-Deck: periferiche spente e sonno profondo; si riaccende con l'interruttore.
    display().sleep();
    ledOff();
    tastiera().light(0);
    audio().stop();
    web::powerOff();
#else
    std::fprintf(stderr, "spegnimento chiesto dall'interfaccia\n");
    std::exit(0);
#endif
}

}  // namespace scheda
