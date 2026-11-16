// Battaglia navale via radio con un contatto: 10 x 10, flotta di 10 navi (1 da 4, 2 da 3, 3 da 2,
// 4 da 1) che non si toccano, messe a caso (su/giu' per rimescolarle). Un colpo a testa; comincia
// chi ha sfidato. A sinistra il suo mare coi miei colpi, a destra le mie navi coi suoi.
#include "app/dati.h"
#include "app/giochi.h"
#include "ui/giochi.h"
#include "ui/griglia.h"
#include "ui/schermate.h"
#include "ui/widget.h"

namespace ui {

namespace {
// Nello scuro il mare e' blu. In Carta e' un foglio a quadretti: caselle bianche tra righe nere,
// colpi arancio, acqua grigia col pallino, le mie navi nere.
lv_color_t MARE() { return colore(0x004080, 0xFFFFFF); }
lv_color_t MARE_ACQUA() { return colore(0x0080C0, 0xC0C0C0); }  // colpo finito in acqua
lv_color_t COLPITA() { return colore(0xFF4040, 0xFF8000); }     // nello scuro ROSSO
lv_color_t AFFONDATA() { return colore(0xC00000, 0xC04000); }   // nello scuro ROSSO_SCURO
lv_color_t NAVE() { return colore(0xC0C0C0, 0x000000); }        // nello scuro GRIGIO3
}  // namespace

class Navale : public Screen {
public:
    Navale() : id_(app::giochi().sfida().id) { challengeScreen(this, true, id_); }
    ~Navale() override { challengeScreen(this, false); }
    // La partita di questa schermata. Se intanto ne e' cominciata un'altra (la schermata e' rimasta
    // sotto), qui non c'e' piu' niente: la schermata vecchia non deve giocare quella nuova.
    app::Sfida partita() const {
        app::Sfida s = app::giochi().sfida();
        if (s.id != id_ || s.gioco != app::Gioco::NAVALE) s = app::Sfida();
        return s;
    }
    void build(lv_obj_t* root) override {
        s_ = partita();
        lv_obj_t* col = column(root, PX(8, 0));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
#if !TDECK
        header(col, IC_NAVE, "Battaglia navale con " + app::dati().nameOf(s_.avversario));
#endif
        // Carta: la cornice della griglia grande (2 pixel per lato, 1 sul T-Deck) si prende dallo
        // spazio tra le due colonne, cosi' quella di destra resta larga come nello scuro (i testi vanno
        // a capo negli stessi punti).
        lv_obj_t* r = row(col, temaChiaro() ? PX(10, 6) : PX(14, 8));
        lv_obj_set_size(r, W_SCHERMO, LV_SIZE_CONTENT);
        pad(r, PX(4, 4), PX(8, 4), 0, PX(8, 4));
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        const bool prepara = s_.stato == app::Sfida::PREPARA;
        // Sinistra: il suo mare (o, mentre si mettono le navi, le mie in grande).
        lv_obj_t* left = column(r, PX(4, 2));
        lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        label(left, &font_16, GRIGIO3(), prepara ? "Le tue navi" : "Il suo mare");
        big_.create(left, 10, 10, PX(28, 14), PX(2, 1), &font_16, [this, prepara](int x, int y, Casella& c) {
            if (prepara) own(x, y, c);
            else target(x, y, c);
        });
        // Carta: righe nere sul suo mare, grigie sotto le mie navi (nere: con le righe nere sembrerebbe
        // un cruciverba).
        if (temaChiaro()) big_.lines(prepara ? GRIGIO2() : INCHIOSTRO(), PX(2, 1));
        // Destra: le mie navi in piccolo e lo stato.
        lv_obj_t* right = column(r, PX(6, 3));
        grow(right);
        lv_obj_set_height(right, LV_SIZE_CONTENT);
        if (!prepara) {
            label(right, &font_16, GRIGIO3(), "Le tue navi");
            small_.create(right, 10, 10, PX(13, 10), 1, &font_16, [this](int x, int y, Casella& c) { own(x, y, c); });
            if (temaChiaro()) small_.lines(GRIGIO2(), 1);  // righe grigie: le navi nere spiccano
        } else {
            lv_obj_t* f = label(right, &font_16, colore(0x808080, 0x404040),
                                "Flotta: 1 da 4, 2 da 3, 3 da 2, 4 da 1. Le navi non si toccano.");
            lv_obj_set_width(f, lv_pct(100));
            lv_label_set_long_mode(f, LV_LABEL_LONG_WRAP);
        }
        // Carta: com'e' andato il mio ultimo colpo, a parte e grande (arancio se ha colpito).
        esito_ = nullptr;
        if (temaChiaro() && !prepara) esito_ = rigaPiena(label(right, &font_22b, ROSSO(), ""));
        status_ = label(right, PX(&font_20, &font_16), BIANCO(), "");
        lv_obj_set_width(status_, lv_pct(100));
        lv_label_set_long_mode(status_, LV_LABEL_LONG_WRAP);
        update();
    }
    void target(int x, int y, Casella& c) {
        const int i = y * 10 + x;
        const uint8_t v = s_.miei[i];
        c.bg = v == 0 ? MARE() : v == 1 ? MARE_ACQUA() : v == 2 ? COLPITA() : AFFONDATA();
        c.radius = temaChiaro() ? 0 : 2;
        if (v >= 2) c.text = "×";
        else if (v == 1 && temaChiaro()) c.text = "•";  // Carta: acqua col pallino
        if (i == cur_ && (s_.stato == app::Sfida::IN_CORSO || s_.stato == app::Sfida::INVITATO)) {
            c.border = AMBRA();
            c.borderW = 2;
        } else if (i == s_.ultimoMio) {
            c.border = BIANCO();
            c.borderW = temaChiaro() ? 2 : 1;  // Carta: accanto alle righe nere un pixel non si vede
        }
    }
    void own(int x, int y, Casella& c) {
        const int i = y * 10 + x;
        const bool nave = s_.mare[i] != 0;
        const uint8_t colpo = s_.suoi[i];
        c.bg = nave ? (colpo == 2 ? COLPITA() : NAVE()) : (colpo ? MARE_ACQUA() : MARE());
        c.radius = temaChiaro() ? 0 : 1;
        if (i == s_.ultimoSuo) {
            c.border = AMBRA();
            c.borderW = 1;
        }
    }
    void update() {
        s_ = partita();
        const std::string nome = app::dati().nameOf(s_.avversario);
        std::string t;
        lv_color_t col = BIANCO();
        // Com'e' andato il mio ultimo colpo (in Carta nell'etichetta a parte).
        std::string mio;
        uint8_t v = 0;
        if (s_.ultimoMio >= 0) {
            v = s_.miei[size_t(s_.ultimoMio)];
            if (!esito_) mio = v == 1 ? "Acqua. " : v == 2 ? "Colpito! " : v == 3 ? "Affondata! " : "";
        }
        bool conEsito = false;
        switch (s_.stato) {
            case app::Sfida::PREPARA:
                t = TXT("Su o giù per rimescolare le navi, OK quando sei pronto.", "Su/giù: rimescola. OK: pronto.");
                col = AMBRA();
                break;
            case app::Sfida::INVITATO:
                if (s_.nonArrivata) t = "L'invito non è arrivato: OK per rimandarlo", col = ROSSO();
                else t = "Aspetto che " + nome + " accetti e metta le navi...", col = GRIGIO3();
                break;
            case app::Sfida::INVITO_ARRIVATO: t = nome + " ti sfida: OK per giocare"; col = AMBRA(); break;
            case app::Sfida::IN_CORSO:
                if (s_.nonArrivata) t = "Il colpo non è arrivato: OK per rimandarlo", col = ROSSO();
                else if (s_.inViaggio) t = "Colpo in viaggio...", col = GRIGIO3();
                else if (s_.tocca) t = mio + "Tocca a te: scegli dove sparare", col = AMBRA(), conEsito = true;
                else t = mio + "Tocca a " + nome + "...", col = GRIGIO3(), conEsito = true;
                break;
            case app::Sfida::FINITA: t = s_.esito; col = s_.vinto ? VERDE() : AMBRA(); break;
            default: t = "Nessuna partita"; break;
        }
        lv_label_set_text(status_, t.c_str());
        lv_obj_set_style_text_color(status_, col, 0);
        if (esito_) {
            const char* e = !conEsito ? "" : v == 1 ? "ACQUA" : v == 2 ? "COLPITO!" : v == 3 ? "AFFONDATA!" : "";
            lv_label_set_text(esito_, e);
            lv_obj_set_style_text_color(esito_, v >= 2 ? ROSSO() : GRIGIO3(), 0);
            if (*e) lv_obj_clear_flag(esito_, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(esito_, LV_OBJ_FLAG_HIDDEN);
        }
        big_.refresh();
        small_.refresh();
        refreshBars();
    }
    void onEvent(const app::Event& e) override {
        if (e.type != app::Ev::GAME) return;
        // Da PREPARA si passa ai due mari (o viceversa, dopo aver accettato): si ricostruisce.
        const bool was = s_.stato == app::Sfida::PREPARA;
        s_ = partita();
        if (was != (s_.stato == app::Sfida::PREPARA)) rebuild();
        else update();
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        auto& g = app::giochi();
        if (s_.stato == app::Sfida::PREPARA) {
            if (k == Key::UP || k == Key::DOWN) g.rimescola();
            else if (k == Key::OK || k == Key::ENTER) {
                g.pronto();
                rebuild();
                return true;
            } else if (k == Key::MENU) {
                g.abbandona();
                pop();
                return true;
            } else return false;
            update();
            return true;
        }
        const int ox = cur_;
        switch (k) {
            case Key::UP: cur_ = (cur_ + 90) % 100; break;
            case Key::DOWN: cur_ = (cur_ + 10) % 100; break;
            case Key::LEFT: cur_ = cur_ % 10 ? cur_ - 1 : cur_ + 9; break;
            case Key::RIGHT: cur_ = cur_ % 10 == 9 ? cur_ - 9 : cur_ + 1; break;
            case Key::OK:
            case Key::ENTER:
                if (s_.stato == app::Sfida::FINITA || s_.stato == app::Sfida::NESSUNA) {
                    if (s_.stato == app::Sfida::FINITA) g.chiudi();
                    pop();
                    return true;
                }
                if (s_.stato == app::Sfida::INVITO_ARRIVATO) {
                    g.accetta();
                    rebuild();
                    return true;
                }
                if (s_.nonArrivata) g.rimanda();
                else g.gioca(cur_);
                update();
                return true;
            case Key::MENU:
                if (s_.stato == app::Sfida::FINITA || s_.stato == app::Sfida::NESSUNA) return true;
                // this resta valido: la finestra si chiude se cambia la schermata in cima.
                ask("Abbandonare la partita?", "Vince " + app::dati().nameOf(s_.avversario) + ".", {"Continua", "Abbandona"},
                    [this](int i) {
                        if (i != 1) return;
                        app::giochi().abbandona();
                        update();  // l'abbandono non manda eventi: si aggiorna da sola
                    });
                return true;
            default: return false;
        }
        big_.refresh(ox % 10, ox / 10);
        big_.refresh(cur_ % 10, cur_ / 10);
        return true;
    }
    std::vector<Hint> hints() override {
        if (s_.stato == app::Sfida::PREPARA) return {{"OK", "pronto"}, {"SU/GIÙ", "rimescola"}, {"MENU", "annulla"}};
        if (s_.stato == app::Sfida::FINITA || s_.stato == app::Sfida::NESSUNA) return {{"OK", "fine"}, {"BACK", "indietro"}};
        return {{"OK", "spara"}, {"MENU", "abbandona"}, {"BACK", "dopo"}};
    }

private:
    uint16_t id_;  // partita di questa schermata
    app::Sfida s_;
    Griglia big_, small_;
    lv_obj_t *status_ = nullptr, *esito_ = nullptr;
    int cur_ = 44;
};

Screen* makeNavale() { return new Navale(); }

}  // namespace ui
