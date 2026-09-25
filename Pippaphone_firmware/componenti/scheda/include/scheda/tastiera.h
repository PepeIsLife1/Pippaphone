// Tastiera QWERTY (TCA8418 sulla PCB 2), navigatore a 5 vie e tasti laterali (expander).
#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "scheda/i2c.h"

namespace scheda {

enum class Key : uint8_t {
    NONE = 0,
    // lettere
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    // altri tasti della tastiera
    DOLLAR, SPACE, ENTER, BKSP, ALT, LSHIFT, RSHIFT, SYM, ZERO_MIC,
    // barra di navigazione
    CALL, MENU, BACK, END, UP, DOWN, LEFT, RIGHT, OK,
    // tasti laterali
    PWR, PTT, VOLUP, VOLDN,
};

struct KeyEvent {
    Key key;
    bool pressed;
};

// Lettera minuscola del tasto (0 se non e' una lettera).
char letterOf(Key k);
// Carattere con ALT premuto, come sul BlackBerry (0 se non c'e').
uint32_t altCharOf(Key k);
const char* keyName(Key k);

class Tastiera {
public:
    // Configura il TCA8418; gli eventi finiscono nella coda restituita da queue().
    bool begin();
    bool ok() const { return dev_.present(); }
    // Da chiamare quando INT_N scende: legge tastiera, tasti laterali e carica.
    void service();
    QueueHandle_t queue() const { return q_; }
    // Smette di generare interruzioni (prima dello spegnimento).
    void sleep();
    bool charging() const { return charging_; }

private:
    I2cDev dev_;
    QueueHandle_t q_ = nullptr;
    uint8_t side_ = 0;
    bool charging_ = false;
};

Tastiera& tastiera();

}  // namespace scheda
