// Serpente: si muove da solo, le frecce (trackball, W A S D, navigatore) lo girano; ogni mela lo
// allunga e lo fa andare piu' veloce. Si perde toccando il bordo o se stessi.
#include <algorithm>
#include <cstdio>
#include <deque>

#include "app/giochi.h"
#include "pippanet/node.h"
#include "scheda/scheda.h"
#include "ui/giochi.h"
#include "ui/griglia.h"
#include "ui/widget.h"

namespace ui {

namespace {
constexpr int CELL = PX(16, 10);
constexpr int COLS = PX(29, 31), ROWS = PX(23, 17);
constexpr uint32_t PASSO_MS = 200, PASSO_MIN_MS = 90;
}  // namespace

class Snake : public Screen {
public:
    Snake() { reset(); }
    ~Snake() override {
        if (timer_) lv_timer_delete(timer_);
    }
    void reset() {
        body_.clear();
        for (int i = 0; i < 4; i++) body_.push_back({COLS / 2 - i, ROWS / 2});  // testa davanti
        dir_ = {1, 0};
        turns_.clear();
        points_ = 0;
        step_ = PASSO_MS;
        state_ = PRONTO;
        food();
    }
    void build(lv_obj_t* root) override {
        // Carta, T-Deck: due pixel in meno tra i punti e il campo, cosi' tra la cornice nera del campo e
        // la riga nera della barra dei tasti restano tre pixel bianchi (con uno solo sembrano una riga doppia).
        lv_obj_t* col = column(root, temaChiaro() ? PX(6, 1) : PX(6, 3));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        pad(col, PX(6, 3), 0, 0, 0);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        info_ = label(col, &font_20, GRIGIO3(), "");
        lv_obj_t* frame = box(col);
        lv_obj_set_size(frame, COLS * CELL + 2, ROWS * CELL + 2);
        lv_obj_set_style_border_width(frame, 1, 0);
        lv_obj_set_style_border_color(frame, GRIGIO1(), 0);
        if (temaChiaro()) {
            // Carta: cornice nera da 2 pixel disegnata sopra la griglia (copre solo il margine vuoto
            // delle caselle sul bordo): il campo resta grande uguale.
            lv_obj_set_style_border_width(frame, 2, 0);
            lv_obj_set_style_border_color(frame, INCHIOSTRO(), 0);
            lv_obj_set_style_border_post(frame, true, 0);
        }
        grid_.create(frame, COLS, ROWS, CELL, 0, &font_16, [this](int x, int y, Casella& c) { cell(x, y, c); });
        // La posizione parte da dentro il bordo; in Carta la griglia comincia a 1 pixel dal bordo esterno
        // e la cornice le copre un pixel per lato.
        lv_obj_set_pos(grid_.obj(), temaChiaro() ? -1 : 1, temaChiaro() ? -1 : 1);
        // Messaggio sopra la griglia (inizio, pausa, fine).
        msg_ = label(frame, &font_26b, AMBRA(), "");
        lv_obj_center(msg_);
        fill(msg_, NERO());
        pad(msg_, PX(6, 3), PX(12, 6), PX(6, 3), PX(12, 6));
        radius(msg_, PX(8, 5));
        if (temaChiaro()) {  // Carta: cartellino bianco col bordo nero
            lv_obj_set_style_border_width(msg_, 2, 0);
            lv_obj_set_style_border_color(msg_, INCHIOSTRO(), 0);
        }
        if (!timer_) timer_ = lv_timer_create([](lv_timer_t* t) { static_cast<Snake*>(lv_timer_get_user_data(t))->advance(); }, step_, this);
        update();
    }
    void cell(int x, int y, Casella& c) {
        c.bg = NERO();
        if (x == food_.x && y == food_.y) {
            c.bg = ROSSO();
            c.radius = CELL / 2;
            c.inset = 1;
            return;
        }
        for (size_t i = 0; i < body_.size(); i++)
            if (body_[i].x == x && body_[i].y == y) {
                // Strisce secondo la casella e non secondo il pezzo: a ogni passo si ridisegnano solo
                // testa, collo e coda, e i colori del resto devono restare giusti. Testa: nello scuro
                // piu' chiara del corpo, in Carta piu' scura.
                c.bg = i == 0 ? colore(0x40FF80, 0x004000) : (((x + y) & 1) ? VERDE() : lv_color_hex(0x00A040));
                c.radius = PX(3, 2);
                c.inset = 1;
                return;
            }
    }
    void update() {
        char b[64];
        snprintf(b, sizeof(b), "Punti %lu · record %lu", (unsigned long)points_,
                 (unsigned long)std::max(points_, app::giochi().recordSerpente()));
        lv_label_set_text(info_, b);
        const char* m = state_ == PRONTO ? "Premi una freccia" : state_ == PAUSA ? "Pausa" : state_ == FINE ? end_.c_str() : "";
        lv_label_set_text(msg_, m);
        if (*m) lv_obj_clear_flag(msg_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(msg_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(msg_);
    }
    void food() {
        for (int tent = 0; tent < 2000; tent++) {
            const P p{int(pn::randomBelow(COLS)), int(pn::randomBelow(ROWS))};
            bool free = true;
            for (const auto& s : body_)
                if (s.x == p.x && s.y == p.y) free = false;
            if (free) {
                food_ = p;
                return;
            }
        }
    }
    void advance() {
        if (state_ != GIOCA) return;
        // Un'altra schermata sopra (le etichette non ci sono piu'): pausa, si vede al ritorno.
        if (top() != this) {
            state_ = PAUSA;
            return;
        }
        // Schermo spento, o una finestra di scelta aperta sopra (i tasti vanno a lei, il serpente non
        // si potrebbe girare): pausa.
        if (!screenOn() || askOpen()) {
            state_ = PAUSA;
            update();
            return;
        }
        if (!turns_.empty()) {
            dir_ = turns_.front();
            turns_.pop_front();
        }
        const P head{body_.front().x + dir_.x, body_.front().y + dir_.y};
        bool hit = head.x < 0 || head.y < 0 || head.x >= COLS || head.y >= ROWS;
        for (size_t i = 0; i + 1 < body_.size() && !hit; i++)  // la coda si sposta: lei no
            if (body_[i].x == head.x && body_[i].y == head.y) hit = true;
        if (hit) {
            gameOver();
            return;
        }
        body_.push_front(head);
        grid_.refresh(head.x, head.y);
        grid_.refresh(body_[1].x, body_[1].y);
        if (head.x == food_.x && head.y == food_.y) {
            points_++;
            step_ = std::max<uint32_t>(PASSO_MIN_MS, step_ - 5);
            lv_timer_set_period(timer_, step_);
            scheda::audio().play(scheda::suoni::CLIC);
            food();
            grid_.refresh(food_.x, food_.y);
            update();
        } else {
            const P tail = body_.back();
            body_.pop_back();
            grid_.refresh(tail.x, tail.y);
        }
    }
    void gameOver() {
        state_ = FINE;
        const bool rec = app::giochi().nuovoRecordSerpente(points_);
        end_ = rec ? "Nuovo record: " + std::to_string(points_) + "!" : "Fine: " + std::to_string(points_) + " punti";
        scheda::audio().play(rec ? scheda::suoni::INVIO_OK : scheda::suoni::ERRORE);
        scheda::vibrate(200);
        update();
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        P d{0, 0};
        if (k == Key::UP) d = {0, -1};
        else if (k == Key::DOWN) d = {0, 1};
        else if (k == Key::LEFT) d = {-1, 0};
        else if (k == Key::RIGHT) d = {1, 0};
        if (d.x || d.y) {
            if (state_ == FINE) return true;
            const P last = turns_.empty() ? dir_ : turns_.back();
            // Non si torna indietro su se stessi; al massimo due svolte in coda (trackball veloce).
            if ((d.x != -last.x || d.y != -last.y) && (d.x != last.x || d.y != last.y) && turns_.size() < 2) turns_.push_back(d);
            if (state_ != GIOCA) {
                state_ = GIOCA;
                update();
            }
            return true;
        }
        if (k == Key::OK || k == Key::ENTER || k == Key::SPACE) {
            if (state_ == FINE) {
                reset();
                grid_.refresh();
                lv_timer_set_period(timer_, step_);
            } else {
                state_ = state_ == GIOCA ? PAUSA : GIOCA;
            }
            update();
            return true;
        }
        return false;
    }
    std::vector<Hint> hints() override {
        if (state_ == FINE) return {{"OK", "un'altra"}, {"BACK", "esci"}};
        return {{"NAV", "gira"}, {"OK", state_ == GIOCA ? "pausa" : "via"}, {"BACK", "esci"}};
    }

private:
    struct P {
        int x, y;
    };
    enum Stato { PRONTO, GIOCA, PAUSA, FINE };
    std::deque<P> body_;
    std::deque<P> turns_;
    P dir_{1, 0}, food_{0, 0};
    uint32_t points_ = 0, step_ = PASSO_MS;
    Stato state_ = PRONTO;
    std::string end_;
    Griglia grid_;
    lv_obj_t *info_ = nullptr, *msg_ = nullptr;
    lv_timer_t* timer_ = nullptr;
};

Screen* makeSnake() { return new Snake(); }

}  // namespace ui
