// Pezzi riusabili: elenco con selezione, campo di testo per la tastiera QWERTY, intestazioni.
#pragma once

#include <string>
#include <vector>

#include "lvgl.h"
#include "scheda/tastiera.h"
#include "ui/tema.h"

namespace ui {

using scheda::Key;

// Elenco verticale; la riga selezionata e' ambra con testo nero.
class List {
public:
    void create(lv_obj_t* parent, int w, int h);
    // Nuova riga (flex orizzontale). Le etichette create con text() cambiano colore da sole.
    lv_obj_t* addRow(int height = 64);
    static lv_obj_t* text(lv_obj_t* row, const lv_font_t* font, lv_color_t color, const std::string& s,
                          bool expand = false);
    void focus(int i);
    int focused() const { return sel_; }
    int size() const { return int(rows_.size()); }
    // Su/giu' (e pagine con sinistra/destra); true se usato.
    bool key(Key k);
    lv_obj_t* obj() const { return cont_; }
    void clear();

private:
    lv_obj_t* cont_ = nullptr;
    std::vector<lv_obj_t*> rows_;
    int sel_ = 0;
};

// Campo di testo con la tastiera del telefono (maiuscole, ALT, SYM, accenti).
class TextInput {
public:
    enum Result { NONE, CHANGED, SUBMIT };
    TextInput() = default;
    TextInput(const TextInput&) = delete;
    TextInput& operator=(const TextInput&) = delete;
    ~TextInput() { closeSym(); }
    void create(lv_obj_t* parent, int w, int h, bool multiline, size_t maxChars, const char* placeholder);
    Result key(Key k, bool pressed);
    std::string text() const;
    void setText(const std::string& s);
    void clear() { setText(""); }
    bool empty() const { return text().empty(); }
    size_t length() const;
    bool symOpen() const { return sym_ != nullptr; }
    void setDigitsOnly(bool d) { digits_ = d; }
    void setPassword(bool p);
    void setFocused(bool f);
    lv_obj_t* obj() const { return wrap_; }
#if TDECK
    // Il prossimo tasto 0/microfono e' la cifra 0 (ALT premuto o campo numerico).
    bool wantsDigit() const { return alt_ || altLock_ || digits_; }
#endif

private:
    void insert(const char* utf8);
    void updateCounter();
    void openSym();
    void closeSym();
    void symFocus();

    lv_obj_t* wrap_ = nullptr;
    lv_obj_t* ta_ = nullptr;
    lv_obj_t* counter_ = nullptr;
    lv_obj_t* mode_ = nullptr;
    size_t max_ = 160;
    bool digits_ = false;
    bool shift_ = false, caps_ = false, alt_ = false, altLock_ = false;
    int64_t lastShift_ = 0, lastAlt_ = 0;
    lv_obj_t* sym_ = nullptr;
    std::vector<lv_obj_t*> symCells_;
    int symSel_ = 0;
};

// Intestazione di schermata: icona, titolo, testo a destra.
lv_obj_t* header(lv_obj_t* parent, const char* icon, const std::string& title, const std::string& right = "",
                 lv_color_t iconColor = AMBRA());
// Cerchio con le iniziali.
lv_obj_t* avatar(lv_obj_t* parent, uint16_t number, const std::string& name, int size);
// Divisore sottile.
void underline(lv_obj_t* o);

#if TDECK
// Il mio GPS (T-Deck), come lo dicono la barra di stato e le schermate: spento (non serve, o lo si sta
// accendendo), cerca i satelliti, non risponde (voluto acceso ma muto da piu' di 5 s), posizione di adesso.
enum class FaseGps : uint8_t { SPENTO, CERCA, MUTO, FIX };
FaseGps faseGps();
// "ne sento 5 su 14 in vista" (con segnale, cioe' C/N0 > 0, su quelli in vista), con max anche
// " · max 31 dB"; "nessun satellite in vista" se non ne elenca nessuno.
std::string sentoGps(bool max);
#endif

}  // namespace ui
