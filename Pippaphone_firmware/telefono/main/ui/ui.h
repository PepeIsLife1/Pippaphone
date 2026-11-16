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

#if TDECK
class TextInput;
// Nome di un tasto della PCB ("MENU", "CALL", "PTT"...) come si preme sul T-Deck ("sym+M"...).
const char* tasto(const char* pcbKey);
#endif

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
    // La schermata del PIN: goHome() (END) non la toglie mai dalla pila, cosi' quello che si apre
    // sopra di lei (chiamata, vocale, sfida) non sblocca il telefono.
    virtual bool isLock() const { return false; }
#if TDECK
    // Campo di testo in cui si sta scrivendo (nullptr = nessuno). Sul T-Deck decide se il tasto
    // 0/microfono e' il PTT o la cifra 0, e se il tasto cancella e' BACK.
    virtual const TextInput* input() const { return nullptr; }
    // Schermata che prende le cifre dai tasti (W E R S D F Z X C = 1-9, microfono = 0) senza un
    // campo di testo (per esempio data e ora): W A S D restano cifre e il microfono e' lo 0.
    virtual bool typesDigits() const { return false; }
    // Schermata che usa le lettere come comandi (per esempio la rubrica, che salta all'iniziale):
    // W A S D restano lettere e non diventano frecce.
    virtual bool lettersAreKeys() const { return false; }
    // Schermata che usa il GPS (posizione, mappa, SOS...): finche' e' nella pila, anche sotto altre o sotto
    // il PIN, il GPS resta acceso (ui.cpp lo chiede ogni 2 secondi, la politica e' in main.cpp).
    virtual bool vuoleGps() const { return false; }
#endif
    // Ricostruisce la schermata (dati cambiati).
    void rebuild();

protected:
    lv_obj_t* root_ = nullptr;
    friend void showTop();
};

void init();                 // LVGL, display, compito dell'interfaccia
void push(Screen* s);        // la pila ne prende possesso
void pushUnder(Screen* s);   // sotto quella in cima: compare quando l'altra si chiude
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
// La finestra di scelta e' aperta (i tasti vanno a lei).
bool askOpen();
// Tema cambiato nelle impostazioni (Carta o Scuro): colori del fondo e ricostruisce la schermata.
void applicaTema();

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
