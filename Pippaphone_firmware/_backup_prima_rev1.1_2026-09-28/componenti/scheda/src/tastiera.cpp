#include "scheda/tastiera.h"

#include "esp_log.h"
#include "scheda/expander.h"
#include "scheda/pins.h"

namespace scheda {

namespace {
// Registri TCA8418.
constexpr uint8_t REG_CFG = 0x01, REG_INT_STAT = 0x02, REG_KEY_LCK_EC = 0x03, REG_KEY_EVENT_A = 0x04,
                  REG_KP_GPIO1 = 0x1D, REG_KP_GPIO2 = 0x1E, REG_KP_GPIO3 = 0x1F;

// Codice TCA8418 = riga * 10 + colonna + 1. Matrice dalla PCB 2 (qmesh-kbd).
Key keyFromCode(uint8_t code) {
    switch (code) {
        case 1: return Key::Q;   case 11: return Key::W;  case 21: return Key::E;  case 31: return Key::R;
        case 41: return Key::T;  case 51: return Key::Y;  case 61: return Key::U;
        case 2: return Key::I;   case 12: return Key::O;  case 22: return Key::P;  case 32: return Key::A;
        case 42: return Key::S;  case 52: return Key::D;  case 62: return Key::F;
        case 3: return Key::G;   case 13: return Key::H;  case 23: return Key::J;  case 33: return Key::K;
        case 43: return Key::L;  case 53: return Key::BKSP; case 63: return Key::ALT;
        case 4: return Key::Z;   case 14: return Key::X;  case 24: return Key::C;  case 34: return Key::V;
        case 44: return Key::B;  case 54: return Key::N;  case 64: return Key::M;
        case 5: return Key::DOLLAR; case 15: return Key::ENTER; case 25: return Key::LSHIFT;
        case 35: return Key::ZERO_MIC; case 45: return Key::SPACE; case 55: return Key::SYM; case 65: return Key::RSHIFT;
        case 6: return Key::CALL; case 16: return Key::MENU; case 26: return Key::BACK; case 36: return Key::END;
        case 7: return Key::UP;  case 17: return Key::RIGHT; case 27: return Key::DOWN; case 37: return Key::LEFT;
        case 47: return Key::OK;
        default: return Key::NONE;
    }
}
}  // namespace

char letterOf(Key k) {
    if (k >= Key::A && k <= Key::Z) return char('a' + (uint8_t(k) - uint8_t(Key::A)));
    return 0;
}

uint32_t altCharOf(Key k) {
    switch (k) {
        case Key::W: return '1'; case Key::E: return '2'; case Key::R: return '3';
        case Key::S: return '4'; case Key::D: return '5'; case Key::F: return '6';
        case Key::Z: return '7'; case Key::X: return '8'; case Key::C: return '9';
        case Key::ZERO_MIC: return '0';
        case Key::Q: return '#'; case Key::A: return '*'; case Key::T: return '(';
        case Key::Y: return ')'; case Key::U: return '_'; case Key::I: return '-';
        case Key::O: return '+'; case Key::P: return '@'; case Key::G: return '/';
        case Key::H: return ':'; case Key::J: return ';'; case Key::K: return '\'';
        case Key::L: return '"'; case Key::V: return '?'; case Key::B: return '!';
        case Key::N: return ','; case Key::M: return '.'; case Key::DOLLAR: return '$';
        default: return 0;
    }
}

const char* keyName(Key k) {
    static const char* names[] = {"-", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N",
                                  "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "$", "SPACE",
                                  "ENTER", "BKSP", "ALT", "LSHIFT", "RSHIFT", "SYM", "0/MIC", "CALL", "MENU",
                                  "BACK", "END", "SU", "GIU", "SINISTRA", "DESTRA", "OK", "PWR", "PTT", "VOL+",
                                  "VOL-"};
    const size_t i = size_t(k);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "?";
}

Tastiera& tastiera() {
    static Tastiera t;
    return t;
}

bool Tastiera::begin() {
    if (!q_) q_ = xQueueCreate(32, sizeof(KeyEvent));
    side_ = expander().readButtons();
    charging_ = (side_ & Expander::CHARGING) != 0;
    side_ &= 0x0F;
    if (!dev_.attach(i2cBus(), ADDR_TCA8418)) {
        ESP_LOGE("tastiera", "TCA8418 non trovato: il cavo piatto della tastiera e' collegato?");
        return false;
    }
    // Righe 0..6 e colonne 0..6 in modo tastiera; interruzione sugli eventi dei tasti.
    dev_.writeReg(REG_KP_GPIO1, 0x7F);
    dev_.writeReg(REG_KP_GPIO2, 0x7F);
    dev_.writeReg(REG_KP_GPIO3, 0x00);
    dev_.writeReg(REG_CFG, 0x01);
    // Svuota eventuali eventi rimasti.
    for (int i = 0; i < 16 && (dev_.readReg8(REG_KEY_LCK_EC) & 0x0F); i++) dev_.readReg8(REG_KEY_EVENT_A);
    dev_.writeReg(REG_INT_STAT, 0x1F);
    return true;
}

void Tastiera::sleep() {
    if (ok()) {
        dev_.writeReg(REG_CFG, 0x00);
        dev_.writeReg(REG_INT_STAT, 0x1F);
    }
}

void Tastiera::service() {
    // Tastiera.
    if (ok()) {
        for (int i = 0; i < 16; i++) {
            const uint8_t cnt = dev_.readReg8(REG_KEY_LCK_EC) & 0x0F;
            if (!cnt) break;
            const uint8_t ev = dev_.readReg8(REG_KEY_EVENT_A);
            if (!ev) break;
            KeyEvent e{keyFromCode(ev & 0x7F), (ev & 0x80) != 0};
            // Utile per controllare la mappa sulla PCB vera: codice grezzo e tasto riconosciuto.
            if (e.pressed) ESP_LOGI("tastiera", "codice %u -> %s", unsigned(ev & 0x7F), keyName(e.key));
            if (e.key != Key::NONE) xQueueSend(q_, &e, 0);
        }
        dev_.writeReg(REG_INT_STAT, 0x1F);
    }
    // Tasti laterali e stato di carica (expander).
    const uint8_t now = expander().readButtons();
    charging_ = (now & Expander::CHARGING) != 0;
    const uint8_t side = now & 0x0F;
    const uint8_t changed = side ^ side_;
    const struct { uint8_t bit; Key key; } map[] = {{Expander::BTN_PWR, Key::PWR},
                                                   {Expander::BTN_PTT, Key::PTT},
                                                   {Expander::BTN_VOLUP, Key::VOLUP},
                                                   {Expander::BTN_VOLDN, Key::VOLDN}};
    for (const auto& m : map)
        if (changed & m.bit) {
            KeyEvent e{m.key, (side & m.bit) != 0};
            if (e.pressed) ESP_LOGI("tastiera", "laterale -> %s", keyName(e.key));
            xQueueSend(q_, &e, 0);
        }
    side_ = side;
}

}  // namespace scheda
