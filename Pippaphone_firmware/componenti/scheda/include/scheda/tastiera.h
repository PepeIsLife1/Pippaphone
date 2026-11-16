// Tastiera QWERTY (TCA8418 sulla PCB 2), navigatore a 5 vie e tasti laterali (expander).
#pragma once

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "scheda/i2c.h"
#include "sdkconfig.h"

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
#if CONFIG_SCHEDA_TDECK_PLUS
    // T-Deck Plus: i due SHIFT tenuti insieme (premuto/rilasciato), al posto di VOL+ e VOL-.
    SOS,
#endif
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

#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck Plus: tastiera con un ESP32-C3 (I2C 0x55, stessa matrice BlackBerry Q10 della PCB 2) e
// trackball. Col firmware del C3 dal giugno 2025 si leggono i tasti tenuti premuti (modo "raw"):
// allora tutto funziona come sulla PCB, piu' le combinazioni col tasto SYM:
//   SYM+M = MENU, SYM+C = CALL, SYM+H = END, SYM+spazio = PWR, SYM+trackball su/giu' = VOL+/VOL-,
//   i due SHIFT insieme = SOS; il tasto 0/microfono lo traduce l'interfaccia (PTT o cifra 0).
// Col firmware vecchio arrivano solo i caratteri gia' composti (niente rilasci): ripiego con
//   ALT+C = CALL, SHIFT+$ = MENU, SHIFT+0/microfono = PTT (premi e ripremi), SYM+SHIFT+H = END.
class Tastiera {
public:
    bool begin();
    bool ok() const { return dev_.present(); }
    // Legge tastiera e trackball (lo chiama il compito "input" ogni pochi millisecondi).
    void service();
    QueueHandle_t queue() const { return q_; }
    void sleep() {}
    // true = modo raw (tasti premuti e rilasciati), false = ripiego coi caratteri. Il modo si capisce al
    // primo tasto premuto: fino ad allora si suppone raw (il firmware del C3 di oggi), cosi' la barra in
    // basso mostra subito le combinazioni giuste.
    bool rawMode() const { return mode_ != CHARS; }
    // false finche' non e' arrivato il primo tasto.
    bool modeKnown() const { return mode_ != UNKNOWN; }
    // Luce della tastiera 0..255 (comando 0x01 del C3).
    void light(uint8_t level);

private:
    enum Mode : uint8_t { UNKNOWN, RAW, CHARS };
    void push(Key k, bool pressed);
    void tap(Key k);
    void serviceRaw(const uint8_t* cols);
    void serviceChar(uint8_t c);
    void onRawChange(int col, int row, bool pressed);
    void serviceTrackball();
    I2cDev dev_;
    QueueHandle_t q_ = nullptr;
    Mode mode_ = UNKNOWN;
    uint8_t stable_[5] = {};      // stato accettato (dopo il filtro dei rimbalzi)
    uint8_t last_[5] = {};        // ultima lettura
    uint8_t pendingCount_ = 0;    // letture diverse da zero in attesa di decidere il modo
    uint8_t readErrors_ = 0;
    bool symDown_ = false, symUsed_ = false;
    bool lshDown_ = false, rshDown_ = false, shiftSent_ = false;
    bool sosSent_ = false, sosReleased_ = false;
    Key chord_[5][7] = {};        // tasto logico mandato al posto di quello fisico (combinazioni)
    bool pttToggle_ = false;      // ripiego: PTT premuto col primo SHIFT+microfono
    bool clickDown_ = false;
    bool clickIgnora_ = false;    // clic tenuto all'accensione (risveglio col trackball): non e' un OK
    int64_t clickChangeMs_ = 0;
};
#else
class Tastiera {
public:
    // Configura il TCA8418; gli eventi finiscono nella coda restituita da queue().
    bool begin();
    bool ok() const { return dev_.present(); }
    // Da chiamare quando INT_N scende: legge tastiera e tasti laterali.
    void service();
    QueueHandle_t queue() const { return q_; }
    // Smette di generare interruzioni (prima dello spegnimento).
    void sleep();

private:
    I2cDev dev_;
    QueueHandle_t q_ = nullptr;
    uint8_t side_ = 0;
};
#endif

Tastiera& tastiera();

}  // namespace scheda
