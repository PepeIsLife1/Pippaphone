// Campo minato: frecce per muoversi, OK scopre (su un numero con le bandierine giuste attorno
// scopre i vicini), F o spazio mette e toglie la bandierina. La prima casella non e' mai una mina.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "app/giochi.h"
#include "esp_timer.h"
#include "pippanet/node.h"
#include "scheda/scheda.h"
#include "ui/giochi.h"
#include "ui/griglia.h"
#include "ui/widget.h"

namespace ui {

namespace {
struct Livello {
    int cols, rows, mine, cell;
    const lv_font_t* font;
};
const Livello LIVELLI[3] = {
    {9, 9, 10, PX(38, 18), &font_26b},
    {16, 12, 30, PX(26, 13), &font_22b},
    {24, 14, 60, PX(19, 11), &font_16},
};
// Colori dei numeri (solo livelli che il pannello della PCB fa bene). Carta: quelli classici del
// campo minato, scuri sul bianco.
lv_color_t numColor(int n) {
    static const uint32_t C[9] = {0, 0x40A0FF, 0x00C040, 0xFF4040, 0x8080FF, 0xC00000, 0x00C0C0, 0xFFFFFF, 0xC0C0C0};
    static const uint32_t CARTA[9] = {0, 0x0040C0, 0x008000, 0xC00000, 0x000080, 0x800000, 0x008080, 0x000000, 0x808080};
    const int i = std::max(0, std::min(8, n));
    return colore(C[i], CARTA[i]);
}
// Casella ancora coperta: nello scuro grigio, in Carta grigio chiaro (le scoperte sono bianche).
lv_color_t COPERTA() { return colore(0x404040, 0xC0C0C0); }
int64_t nowMs() { return esp_timer_get_time() / 1000; }
}  // namespace

class Mines : public Screen {
public:
    explicit Mines(int livello) : lv_(std::max(0, std::min(2, livello))), L_(LIVELLI[lv_]) { reset(); }
    void reset() {
        const size_t n = size_t(L_.cols * L_.rows);
        mine_.assign(n, false);
        open_.assign(n, false);
        flag_.assign(n, false);
        near_.assign(n, 0);
        cx_ = L_.cols / 2;
        cy_ = L_.rows / 2;
        state_ = PRONTO;
        startMs_ = 0;
        endS_ = 0;
        flags_ = 0;
        record_ = false;
    }
    void build(lv_obj_t* root) override {
        // Carta: righe nere tra le caselle e una cornice attorno. Il difficile della PCB e' largo quanto
        // lo schermo: le caselle diventano di un pixel piu' piccole, cosi' la cornice ci sta (si
        // ricalcola a ogni build, se nel frattempo e' cambiato il tema).
        L_.cell = LIVELLI[lv_].cell;
        if (temaChiaro() && L_.cols * (L_.cell + 1) + 1 > W_SCHERMO) L_.cell--;
        lv_obj_t* col = column(root, PX(6, 2));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        pad(col, PX(4, 2), 0, 0, 0);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        info_ = label(col, &font_20, GRIGIO3(), "");
        grid_.create(col, L_.cols, L_.rows, L_.cell, 1, L_.font, [this](int x, int y, Casella& c) { cell(x, y, c); });
        if (temaChiaro()) grid_.lines(INCHIOSTRO(), 1);
        update();
    }
    int at(int x, int y) const { return y * L_.cols + x; }
    void cell(int x, int y, Casella& c) {
        const int i = at(x, y);
        const bool finita = state_ == PERSO || state_ == VINTO;
        // L'icona entra nella casella (facile e medio della PCB, facile del T-Deck)? Nelle caselle piu'
        // piccole l'icona e' grande quasi quanto la casella e ne coprirebbe tutto il colore.
        const bool entra = L_.cell >= PX(26, 18);
        const lv_font_t* icona = entra ? &icone_22 : &icone_18;
        c.radius = temaChiaro() ? 0 : 2;  // Carta: quadretti
        if (open_[size_t(i)]) {
            c.bg = mine_[size_t(i)] ? ROSSO_SCURO() : NERO();
            if (mine_[size_t(i)]) {
                c.text = IC_BOMBA;
                c.font = icona;
                c.fg = SU_COLORE();
                // Carta, casella piccola: la mina scoppiata e' rossa sul bianco (le altre nere sul grigio).
                if (temaChiaro() && !entra) {
                    c.bg = NERO();
                    c.fg = ROSSO_SCURO();
                }
            } else if (near_[size_t(i)]) {
                c.text = digitText(near_[size_t(i)]);
                c.fg = numColor(near_[size_t(i)]);
            }
        } else {
            c.bg = COPERTA();
            if (flag_[size_t(i)]) {
                c.text = IC_BANDIERA;
                c.font = icona;
                const bool sbagliata = finita && !mine_[size_t(i)];
                c.fg = sbagliata ? GRIGIO2() : ROSSO();
                // Carta: casella arancio con la bandierina bianca (si vede da lontano); quella
                // sbagliata resta grigia con la bandierina nera. Nelle caselle piccole la bandierina
                // arancio (o nera) sul grigio.
                if (temaChiaro() && entra) {
                    c.bg = sbagliata ? COPERTA() : ROSSO();
                    c.fg = sbagliata ? INCHIOSTRO() : SU_COLORE();
                } else if (temaChiaro()) {
                    c.fg = sbagliata ? INCHIOSTRO() : ROSSO();
                }
            } else if (finita && mine_[size_t(i)]) {
                c.text = IC_BOMBA;
                c.font = icona;
                // Partita vinta: mine verdi (Carta: verde scuro, sul grigio chiaro si legge).
                c.fg = state_ == VINTO ? colore(0x00C040, 0x004000) : BIANCO();
            }
        }
        if (x == cx_ && y == cy_) {
            c.border = AMBRA();
            c.borderW = 2;
            if (temaChiaro()) c.outset = 1;  // Carta: l'anello sta sopra le righe nere, il numero resta libero
        }
    }
    uint32_t seconds() const {
        if (state_ == PRONTO) return 0;
        if (state_ != GIOCA) return endS_;
        return uint32_t((nowMs() - startMs_) / 1000);
    }
    void update() {
        char b[96];
        const uint32_t s = seconds();
        const uint32_t rec = app::giochi().recordMine(lv_);
        if (state_ == VINTO)
            snprintf(b, sizeof(b), record_ ? "Vinto in %lu:%02lu: nuovo record!" : "Vinto in %lu:%02lu!", (unsigned long)(s / 60), (unsigned long)(s % 60));
        else if (state_ == PERSO) snprintf(b, sizeof(b), "Boom! Mina trovata dopo %lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
        else if (rec)
            snprintf(b, sizeof(b), "Mine %d · tempo %lu:%02lu · record %lu:%02lu", L_.mine - flags_, (unsigned long)(s / 60),
                     (unsigned long)(s % 60), (unsigned long)(rec / 60), (unsigned long)(rec % 60));
        else snprintf(b, sizeof(b), "Mine %d · tempo %lu:%02lu", L_.mine - flags_, (unsigned long)(s / 60), (unsigned long)(s % 60));
        lv_label_set_text(info_, b);
        lv_obj_set_style_text_color(info_, state_ == VINTO ? VERDE() : state_ == PERSO ? ROSSO() : GRIGIO3(), 0);
    }
    // Mine messe dopo la prima casella scoperta: lei e le vicine restano libere.
    void place(int fx, int fy) {
        int left = L_.mine;
        while (left > 0) {
            const int x = int(pn::randomBelow(uint32_t(L_.cols))), y = int(pn::randomBelow(uint32_t(L_.rows)));
            if (std::abs(x - fx) <= 1 && std::abs(y - fy) <= 1) continue;
            if (mine_[size_t(at(x, y))]) continue;
            mine_[size_t(at(x, y))] = true;
            left--;
        }
        for (int y = 0; y < L_.rows; y++)
            for (int x = 0; x < L_.cols; x++) {
                int n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        const int qx = x + dx, qy = y + dy;
                        if ((dx || dy) && qx >= 0 && qy >= 0 && qx < L_.cols && qy < L_.rows && mine_[size_t(at(qx, qy))]) n++;
                    }
                near_[size_t(at(x, y))] = uint8_t(n);
            }
    }
    // Scopre una casella (e, se e' uno zero, tutta la zona attorno).
    void reveal(int x, int y) {
        std::vector<int> todo = {at(x, y)};
        while (!todo.empty()) {
            const int i = todo.back();
            todo.pop_back();
            if (open_[size_t(i)] || flag_[size_t(i)]) continue;
            open_[size_t(i)] = true;
            if (mine_[size_t(i)]) {
                lose();
                return;
            }
            if (near_[size_t(i)]) continue;
            const int px = i % L_.cols, py = i / L_.cols;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const int qx = px + dx, qy = py + dy;
                    if ((dx || dy) && qx >= 0 && qy >= 0 && qx < L_.cols && qy < L_.rows && !open_[size_t(at(qx, qy))])
                        todo.push_back(at(qx, qy));
                }
        }
    }
    // Su un numero gia' scoperto con tante bandierine attorno quante mine: scopre gli altri vicini.
    void chord(int x, int y) {
        int flags = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                const int qx = x + dx, qy = y + dy;
                if ((dx || dy) && qx >= 0 && qy >= 0 && qx < L_.cols && qy < L_.rows && flag_[size_t(at(qx, qy))]) flags++;
            }
        if (flags != near_[size_t(at(x, y))]) return;
        for (int dy = -1; dy <= 1 && state_ == GIOCA; dy++)
            for (int dx = -1; dx <= 1 && state_ == GIOCA; dx++) {
                const int qx = x + dx, qy = y + dy;
                if ((dx || dy) && qx >= 0 && qy >= 0 && qx < L_.cols && qy < L_.rows) reveal(qx, qy);
            }
    }
    void lose() {
        endS_ = seconds();
        state_ = PERSO;
        scheda::audio().play(scheda::suoni::ERRORE);
        scheda::vibrate(300);
    }
    void checkWin() {
        if (state_ != GIOCA) return;
        for (size_t i = 0; i < mine_.size(); i++)
            if (!mine_[i] && !open_[i]) return;
        endS_ = seconds();
        state_ = VINTO;
        record_ = app::giochi().nuovoRecordMine(lv_, std::max<uint32_t>(1, endS_));
        scheda::audio().play(scheda::suoni::INVIO_OK);
    }
    void tick() override {
        if (state_ == GIOCA) update();
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        const int ox = cx_, oy = cy_;
        switch (k) {
            case Key::UP: cy_ = (cy_ + L_.rows - 1) % L_.rows; break;
            case Key::DOWN: cy_ = (cy_ + 1) % L_.rows; break;
            case Key::LEFT: cx_ = (cx_ + L_.cols - 1) % L_.cols; break;
            case Key::RIGHT: cx_ = (cx_ + 1) % L_.cols; break;
            case Key::OK:
            case Key::ENTER:
                if (state_ == PERSO || state_ == VINTO) {
                    reset();
                } else {
                    // OK su una bandierina non fa niente (se no la prima casella "sicura" non si
                    // aprirebbe, e il primo clic vero potrebbe essere una mina).
                    const size_t i = size_t(at(cx_, cy_));
                    if (flag_[i] && !open_[i]) return true;
                    if (state_ == PRONTO) {
                        place(cx_, cy_);
                        state_ = GIOCA;
                        startMs_ = nowMs();
                    }
                    if (open_[size_t(at(cx_, cy_))]) chord(cx_, cy_);
                    else reveal(cx_, cy_);
                    checkWin();
                }
                grid_.refresh();
                update();
                return true;
            case Key::F:
            case Key::SPACE: {
                const size_t i = size_t(at(cx_, cy_));
                if ((state_ == GIOCA || state_ == PRONTO) && !open_[i]) {
                    flag_[i] = !flag_[i];
                    flags_ += flag_[i] ? 1 : -1;
                    grid_.refresh(cx_, cy_);
                    update();
                }
                return true;
            }
            default: return false;
        }
        grid_.refresh(ox, oy);
        grid_.refresh(cx_, cy_);
        return true;
    }
    std::vector<Hint> hints() override {
        if (state_ == PERSO || state_ == VINTO) return {{"OK", "un'altra"}, {"BACK", "esci"}};
        return {{"OK", "scopri"}, {"F", "bandierina"}, {"BACK", "esci"}};
    }

private:
    enum Stato { PRONTO, GIOCA, PERSO, VINTO };
    int lv_;
    Livello L_;
    std::vector<bool> mine_, open_, flag_;
    std::vector<uint8_t> near_;
    int cx_ = 0, cy_ = 0, flags_ = 0;
    Stato state_ = PRONTO;
    int64_t startMs_ = 0;
    uint32_t endS_ = 0;
    bool record_ = false;
    Griglia grid_;
    lv_obj_t* info_ = nullptr;
};

Screen* makeMines(int livello) { return new Mines(livello); }

}  // namespace ui
