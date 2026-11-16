// Tris via radio con un contatto: chi sfida e' la X e comincia. Frecce per scegliere la casella,
// OK per giocare; BACK esce (la partita resta, si riapre da Giochi), MENU per abbandonare.
#include "app/dati.h"
#include "app/giochi.h"
#include "ui/giochi.h"
#include "ui/griglia.h"
#include "ui/schermate.h"
#include "ui/widget.h"

namespace ui {

class Tris : public Screen {
public:
    Tris() : id_(app::giochi().sfida().id) { challengeScreen(this, true, id_); }
    ~Tris() override { challengeScreen(this, false); }
    // La partita di questa schermata. Se intanto ne e' cominciata un'altra (la schermata e' rimasta
    // sotto), qui non c'e' piu' niente: la schermata vecchia non deve giocare quella nuova.
    app::Sfida partita() const {
        app::Sfida s = app::giochi().sfida();
        if (s.id != id_ || s.gioco != app::Gioco::TRIS) s = app::Sfida();
        return s;
    }
    void build(lv_obj_t* root) override {
        s_ = partita();
        lv_obj_t* col = column(root, PX(10, 4));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        header(col, IC_TRIS, "Tris con " + app::dati().nameOf(s_.avversario), s_.invitante ? "tu sei X" : "tu sei O");
        grid_.create(col, 3, 3, PX(90, 38), PX(8, 4), &font_48b, [this](int x, int y, Casella& c) { cell(x, y, c); });
        if (temaChiaro()) grid_.lines(INCHIOSTRO(), 0);  // Carta: il cancelletto nero, come sulla carta
        status_ = label(col, PX(&font_22, &font_20), BIANCO(), "");
        lv_obj_set_width(status_, PX(440, 300));
        lv_label_set_long_mode(status_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
        update();
    }
    void cell(int x, int y, Casella& c) {
        const int i = y * 3 + x;
        const uint8_t v = s_.tris[i];
        c.bg = GRIGIO1();
        c.radius = PX(10, 6);
        if (temaChiaro()) {  // Carta: caselle bianche tra le righe nere
            c.bg = NERO();
            c.radius = 0;
        }
        if (v) {
            const bool mia = v == 1;
            const bool x_ = mia == s_.invitante;  // chi ha invitato e' la X
            c.text = x_ ? "X" : "O";
            c.fg = mia ? AMBRA() : colore(0x40A0FF, 0x000000);  // le sue: blu nello scuro, nere in Carta
        }
        if (i == cur_ && s_.stato == app::Sfida::IN_CORSO) {
            if (temaChiaro()) {  // Carta: la casella scelta piena di blu
                c.bg = AMBRA();
                c.fg = SU_COLORE();
            } else {
                c.border = AMBRA();
                c.borderW = PX(4, 2);
            }
        }
    }
    void update() {
        s_ = partita();
        std::string t;
        lv_color_t col = BIANCO();
        const std::string nome = app::dati().nameOf(s_.avversario);
        switch (s_.stato) {
            case app::Sfida::INVITATO:
                if (s_.nonArrivata) t = "L'invito non è arrivato: OK per rimandarlo", col = ROSSO();
                else t = "Aspetto che " + nome + " accetti la sfida...", col = GRIGIO3();
                break;
            case app::Sfida::INVITO_ARRIVATO: t = nome + " ti sfida: OK per giocare"; col = AMBRA(); break;
            case app::Sfida::IN_CORSO:
                if (s_.nonArrivata) t = "La mossa non è arrivata: OK per rimandarla", col = ROSSO();
                else if (s_.inViaggio) t = "Mossa in viaggio...", col = GRIGIO3();
                else if (s_.tocca) t = "Tocca a te", col = AMBRA();
                else t = "Tocca a " + nome + "...", col = GRIGIO3();
                break;
            case app::Sfida::FINITA:
                // La mia mossa che ha chiuso la partita non e' arrivata: l'altro la aspetta ancora.
                if (s_.nonArrivata) t = s_.esito + " - la mossa non è arrivata: OK per rimandarla", col = ROSSO();
                else if (s_.inViaggio) t = s_.esito + " - mossa in viaggio...", col = s_.vinto ? VERDE() : AMBRA();
                else t = s_.esito, col = s_.vinto ? VERDE() : AMBRA();
                break;
            default: t = "Nessuna partita"; break;
        }
        lv_label_set_text(status_, t.c_str());
        lv_obj_set_style_text_color(status_, col, 0);
        grid_.refresh();
        refreshBars();
    }
    void onEvent(const app::Event& e) override {
        if (e.type == app::Ev::GAME) update();
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        switch (k) {
            case Key::UP: cur_ = (cur_ + 6) % 9; break;
            case Key::DOWN: cur_ = (cur_ + 3) % 9; break;
            case Key::LEFT: cur_ = cur_ % 3 ? cur_ - 1 : cur_ + 2; break;
            case Key::RIGHT: cur_ = cur_ % 3 == 2 ? cur_ - 2 : cur_ + 1; break;
            case Key::OK:
            case Key::ENTER:
                if (s_.stato == app::Sfida::INVITO_ARRIVATO) app::giochi().accetta();
                // Prima della fine: la mossa che ha chiuso la partita puo' non essere arrivata.
                else if (s_.nonArrivata) app::giochi().rimanda();
                else if (s_.stato == app::Sfida::FINITA || s_.stato == app::Sfida::NESSUNA) {
                    if (s_.stato == app::Sfida::FINITA) app::giochi().chiudi();
                    pop();
                    return true;
                } else app::giochi().gioca(cur_);
                break;
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
        update();
        return true;
    }
    std::vector<Hint> hints() override {
        if (s_.stato == app::Sfida::FINITA || s_.stato == app::Sfida::NESSUNA)
            return {{"OK", s_.nonArrivata ? "rimanda" : "fine"}, {"BACK", "indietro"}};
        return {{"OK", "gioca"}, {"MENU", "abbandona"}, {"BACK", "dopo"}};
    }

private:
    uint16_t id_;  // partita di questa schermata
    app::Sfida s_;
    Griglia grid_;
    lv_obj_t* status_ = nullptr;
    int cur_ = 4;
};

Screen* makeTris() { return new Tris(); }

}  // namespace ui
