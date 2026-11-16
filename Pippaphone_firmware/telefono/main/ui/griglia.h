// Griglia di caselle per i giochi, disegnata in un oggetto LVGL solo (niente oggetto per casella:
// poca RAM interna e disegno veloce anche con 400 caselle). Il gioco dice com'e' ogni casella.
#pragma once

#include <functional>

#include "lvgl.h"
#include "ui/tema.h"

namespace ui {

struct Casella {
    lv_color_t bg = NERO();
    lv_opa_t bgOpa = LV_OPA_COVER;
    const char* text = nullptr;  // testo fisso (letterale): resta valido mentre si disegna
    lv_color_t fg = BIANCO();
    const lv_font_t* font = nullptr;  // nullptr = quello della griglia
    lv_color_t border = NERO();
    uint8_t borderW = 0;  // bordo (per il cursore)
    uint8_t radius = 0;
    uint8_t inset = 0;    // casella piu' piccola di cosi' per lato (per esempio il cibo del serpente)
    // Solo con le righe (lines()): bordo spostato di cosi' verso l'esterno, sopra le righe tra le
    // caselle (al massimo quanto lo spazio tra le caselle), e disegnato sopra il testo: lascia libera
    // la casella (cursore di Carta).
    uint8_t outset = 0;
};

class Griglia {
public:
    using Fn = std::function<void(int x, int y, Casella& c)>;
    // cols x rows caselle di cell pixel con gap pixel tra l'una e l'altra.
    lv_obj_t* create(lv_obj_t* parent, int cols, int rows, int cell, int gap, const lv_font_t* font, Fn fn);
    // Dopo create() (che le toglie): righe di questo colore tra le caselle e una cornice larga frame
    // pixel attorno (la griglia si allarga), come un foglio a quadretti. Senza, tra le caselle si vede
    // il fondo. Con le righe il testo resta dentro la sua casella (un'icona piu' grande della casella
    // non sporca le righe).
    void lines(lv_color_t color, int frame);
    // Ridisegna tutta la griglia o una casella sola.
    void refresh();
    void refresh(int x, int y);
    lv_obj_t* obj() const { return obj_; }
    int cols() const { return cols_; }
    int rows() const { return rows_; }

private:
    static void drawCb(lv_event_t* e);
    lv_obj_t* obj_ = nullptr;
    int cols_ = 0, rows_ = 0, cell_ = 0, gap_ = 0, frame_ = 0;
    bool righe_ = false;  // lines() chiamata
    const lv_font_t* font_ = nullptr;
    Fn fn_;
};

}  // namespace ui
