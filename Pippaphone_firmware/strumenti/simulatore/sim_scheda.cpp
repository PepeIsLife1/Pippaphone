// Simulatore: la scheda finta (display in memoria, audio muto, GPS e batteria fissi).
// La tastiera e' quella vera di tastiera.cpp (nomi dei tasti, lettere, ALT); qui solo il resto.
#include <cstring>
#include <vector>

#include "scheda/scheda.h"
#include "sim.h"

namespace scheda {

// ---------------------------------------------------------------- melodie (come in audio.cpp)

namespace suoni {
const std::vector<Note> SQUILLO = {{1568, 90}};
const std::vector<Note> CLASSICA = {{1319, 120}};
const std::vector<Note> SOS = {{2000, 150}};
const std::vector<Note> CLIC = {{3000, 8}};
const std::vector<Note> INVIO_OK = {{1760, 60}};
const std::vector<Note> ERRORE = {{440, 180}};
const std::vector<Note> PTT_INIZIO = {{1200, 60}};
const std::vector<Note> PTT_FINE = {{1800, 60}};
}  // namespace suoni

// ---------------------------------------------------------------- display

Display& display() {
    static Display d;
    return d;
}
bool Display::begin() { return true; }
void Display::draw(int x1, int y1, int x2, int y2, const void* pixels) { sim::draw(x1, y1, x2, y2, pixels); }
void Display::backlight(uint8_t percent) { bl_ = percent; }
#if CONFIG_SCHEDA_TDECK_PLUS
void Display::sleep() {}
#endif

// ---------------------------------------------------------------- audio

Audio& audio() {
    static Audio a;
    return a;
}
bool Audio::begin() { return true; }
void Audio::play(const std::vector<Note>&, bool) {}
void Audio::playPcm(std::vector<int16_t>, std::function<void()> done) {
    if (done) done();
}
void Audio::stop() {}
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
bool Tastiera::begin() {
    if (!q_) q_ = xQueueCreate(64, sizeof(KeyEvent));
    mode_ = RAW;  // il simulatore mostra le scorciatoie del modo raw
    return true;
}
void Tastiera::service() {}
void Tastiera::light(uint8_t) {}
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
void ledBlink(uint8_t, uint8_t, uint8_t, uint16_t, uint16_t, uint8_t) {}
void ledOff() {}
void keyboardLight(uint8_t) {}
void spiLowDrive() {}
void powerOff() {
    std::fprintf(stderr, "spegnimento chiesto dall'interfaccia\n");
    std::exit(0);
}

}  // namespace scheda
