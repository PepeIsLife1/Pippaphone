// Interfaccia: pila di schermate, barra di stato, barra dei tasti, notifiche.
//
// Niente touch: ogni schermata riceve i tasti (navigatore, CALL, END, MENU, BACK, tastiera,
// tasti laterali) e disegna con LVGL dentro l'area centrale (480 x 412), oppure a schermo
// intero.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/eventi.h"
#include "lvgl.h"
#include "scheda/tastiera.h"
#include "ui/tema.h"

namespace ui {

using scheda::Key;

struct Hint {
    const char* key;
    std::string text;
};

class Screen {
public:
    virtual ~Screen() = default;
    // Crea gli oggetti dentro root (gia' vuoto, 480 x 412 oppure 480 x 480).
    virtual void build(lv_obj_t* root) = 0;
    // Tasto premuto (pressed) o rilasciato; true se usato.
    virtual bool onKey(Key k, bool pressed) { (void)k; (void)pressed; return false; }
    virtual void onEvent(const app::Event& e) { (void)e; }
    // Ogni 250 ms circa.
    virtual void tick() {}
    virtual std::vector<Hint> hints() { return {}; }
    virtual bool fullscreen() const { return false; }
    virtual bool showClock() const { return true; }
    // Durante la scrittura END e BACK non chiudono subito la schermata.
    virtual bool capturesEnd() const { return false; }
    // Ricostruisce la schermata (dati cambiati).
    void rebuild();

protected:
    lv_obj_t* root_ = nullptr;
    friend void showTop();
};

void init();                 // LVGL, display, compito dell'interfaccia
void push(Screen* s);        // la pila ne prende possesso
void pop();
void replace(Screen* s);     // sostituisce quella in cima
void goHome();               // torna alla schermata principale
Screen* top();
void refreshBars();

// Notifica in alto sopra qualsiasi schermata (sparisce da sola).
void toast(const std::string& title, const std::string& text, lv_color_t accent = AMBRA());
// Finestra di scelta modale (Si'/No...). La risposta arriva a cb con l'indice scelto (-1 = BACK).
void ask(const std::string& title, const std::string& text, const std::vector<std::string>& options,
         std::function<void(int)> cb);

// Schermo acceso/spento.
void wake();
bool screenOn();

// Formattazione.
std::string timeHM(uint32_t unixSec);          // "14:32"
std::string dateLong(uint32_t unixSec);        // "giovedi' 25 settembre"
std::string whenShort(uint32_t unixSec);       // "14:32" oggi, "ieri", "23/09"
std::string durationMS(uint32_t ms);        // "0:07"

// Fabbrica della schermata principale (definita in schermate).
Screen* makeHome();
Screen* makeFirstBoot();

}  // namespace ui
