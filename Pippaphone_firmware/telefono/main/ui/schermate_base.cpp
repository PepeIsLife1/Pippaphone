// Schermata principale, menu, blocco con PIN, primo avvio (assegnazione del numero).
#include <algorithm>
#include <cstdio>

#include "app/dati.h"
#include "app/orologio.h"
#include "app/rete.h"
#include "scheda/scheda.h"
#include "ui/schermate.h"
#include "ui/widget.h"

namespace ui {

// ================================================================ principale

class Home : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        pad(col, PX(16, 6), PX(20, 10), PX(12, 4), PX(20, 10));
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_t* top = column(col, 0);
        lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
        clock_ = label(top, &mono_104, BIANCO(), "");
        // Carta: accanto alla data la prossima sveglia, come nello sketch (lo scuro resta com'era).
        lv_obj_t* dateRow = top;
        if (temaChiaro()) {
            dateRow = row(top, PX(10, 6));
            lv_obj_set_size(dateRow, lv_pct(100), LV_SIZE_CONTENT);
        }
        date_ = label(dateRow, &font_22, GRIGIO3(), "");
        alarm_ = nullptr;
        if (temaChiaro()) {
            grow(date_);
            alarm_ = label(dateRow, &font_22b, AMBRA(), "");
        }
        cards_ = column(col, PX(8, 5));
        lv_obj_set_size(cards_, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_t* foot = row(col, 0);
        lv_obj_set_size(foot, lv_pct(100), LV_SIZE_CONTENT);
        me_ = label(foot, PX(&font_18, &font_16), GRIGIO2(), "");
        grow(me_);
        sig_ = label(foot, PX(&font_18, &font_16), GRIGIO2(), "");
        refresh();
    }
    void refresh() {
        const uint32_t t = app::rete().unixTime();
        lv_label_set_text(clock_, t ? timeHM(t).c_str() : "--:--");
        lv_label_set_text(date_, dateLong(t).c_str());
        if (alarm_) {
            uint32_t quando = 0;
            int indice = -1;
            const bool presto = t && app::orologio().prossimaDi(t, quando, indice) && quando - t < 24 * 3600;
            lv_label_set_text(alarm_, presto ? ("sveglia " + timeHM(quando)).c_str() : "");
        }
        lv_label_set_text(me_, ("Il mio numero " + std::to_string(app::rete().number())).c_str());
        const auto s = app::rete().signal();
        char b[48];
        if (s.heard) snprintf(b, sizeof(b), "Ripetitore R%u · %d dBm", unsigned(s.repeater - 0xF000), s.rssi);
        else snprintf(b, sizeof(b), "Nessun ripetitore");
        lv_label_set_text(sig_, b);
        // Notifiche: conversazioni non lette (al massimo 3) e SOS in corso.
        lv_obj_clean(cards_);
        cardChats_.clear();
        cardColors_.clear();
        if (app::rete().sosActive()) card(IC_ATTENZIONE, ROSSO(), "SOS attivo: premi OK", "", app::ChatId{});
        for (const auto& s2 : app::dati().summaries()) {
            if (!s2.unread || cardChats_.size() >= 3) continue;
            std::string what = s2.last.kind == app::MsgKind::VOICE ? "Vocale da " : "Messaggi da ";
            if (s2.unread == 1 && s2.last.kind == app::MsgKind::TEXT) what = "1 messaggio da ";
            else if (s2.last.kind == app::MsgKind::TEXT) what = std::to_string(s2.unread) + " messaggi da ";
            card(s2.last.kind == app::MsgKind::VOICE ? IC_MICROFONO : IC_BUSTA, AMBRA(), what + s2.title,
                 whenShort(s2.last.time), s2.id);
        }
        focusCard();
    }
    void card(const char* icon, lv_color_t color, const std::string& text, const std::string& when,
              const app::ChatId& id) {
        lv_obj_t* c = row(cards_, PX(12, 8));
        lv_obj_set_size(c, lv_pct(100), PX(56, 30));
        pad(c, 0, PX(14, 8), 0, PX(14, 8));
        superficie(c);
        radius(c, PX(10, 6));
        label(c, &icone_22, color, icon);
        lv_obj_t* l = label(c, &font_22, BIANCO(), text.c_str());
        grow(l);
        unaRiga(l);
        label(c, &mono_16, GRIGIO3(), when.c_str());
        cardChats_.push_back(id);
        cardColors_.push_back(color);
    }
    void focusCard() {
        cardObjs_.clear();
        const uint32_t n = lv_obj_get_child_count(cards_);
        for (uint32_t i = 0; i < n; i++) cardObjs_.push_back(lv_obj_get_child(cards_, int32_t(i)));
        if (sel_ >= int(cardObjs_.size())) sel_ = -1;
        for (int i = 0; i < int(cardObjs_.size()); i++) {
            lv_obj_t* c = cardObjs_[i];
            if (temaChiaro()) {
                // Carta: la notizia scelta e' una barra blu piena, le altre bianche col bordo nero.
                const bool on = i == sel_;
                fill(c, on ? AMBRA() : SUPERFICIE());
                lv_obj_set_style_text_color(lv_obj_get_child(c, 0), on ? NERO() : cardColors_[size_t(i)], 0);
                lv_obj_set_style_text_color(lv_obj_get_child(c, 1), on ? NERO() : BIANCO(), 0);
                lv_obj_set_style_text_color(lv_obj_get_child(c, 2), on ? NERO() : GRIGIO3(), 0);
                continue;
            }
            lv_obj_set_style_border_width(c, i == sel_ ? PX(3, 2) : 0, 0);
            lv_obj_set_style_border_color(c, AMBRA(), 0);
        }
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return k == Key::PTT;
        switch (k) {
            case Key::MENU:
            case Key::OK:
                if (k == Key::OK && sel_ >= 0 && sel_ < int(cardChats_.size())) {
                    if (app::rete().sosActive() && sel_ == 0) push(makeSosActive(false));
                    else push(makeChat(cardChats_[sel_]));
                    return true;
                }
                push(makeMenu());
                return true;
            case Key::CALL: push(makeChats()); return true;
            case Key::PTT: push(makeWalkie(0)); return true;
            case Key::DOWN:
                if (sel_ + 1 < int(cardObjs_.size())) sel_++;
                focusCard();
                return true;
            case Key::UP:
                if (sel_ >= 0) sel_--;
                focusCard();
                return true;
            default:
                // Una lettera sulla schermata principale apre la rubrica.
                if (scheda::letterOf(k)) {
                    push(makeContacts());
                    return true;
                }
                return false;
        }
    }
    void onEvent(const app::Event&) override { refresh(); }
    void tick() override {
        if (++ticks_ % 8 == 0) refresh();
    }
    std::vector<Hint> hints() override { return {{"MENU", "menu"}, {"CALL", "messaggi"}, {"PTT", "walkie-talkie"}}; }
    bool showClock() const override { return false; }

private:
    lv_obj_t *clock_, *date_, *cards_, *me_, *sig_;
    lv_obj_t* alarm_ = nullptr;
    std::vector<lv_obj_t*> cardObjs_;
    std::vector<lv_color_t> cardColors_;
    std::vector<app::ChatId> cardChats_;
    int sel_ = -1;
    int ticks_ = 0;
};

Screen* makeHome() { return new Home(); }

// ================================================================ menu
//
// Le voci stanno in pagine, come le icone dei telefoni: sulla PCB 3 x 3 col nome sotto l'icona,
// sul T-Deck 5 x 2 solo icone col nome della voce scelta sotto. Andando oltre il bordo destro o
// sinistro di una pagina si passa alla pagina accanto con uno scorrimento breve; i pallini in basso
// dicono in che pagina si e'. La prima pagina e' quella di sempre, le voci nuove stanno nella
// seconda. Con Carta le icone sono nere su tessere bianche col bordo nero, la scelta piena di blu.

namespace {
enum class Voce : uint8_t {
    MESSAGGI, WALKIE, CONTATTI, GRUPPI, GLOBALE, POSIZIONE, MAPPA, SOS, IMPOSTAZIONI, SEGNALE,
    OROLOGIO, GIOCHI, MUSICA,
};

struct VoceMenu {
    Voce id;
    const char* icon;
    const char* text;
    bool red;
};

constexpr VoceMenu VOCI[] = {
    {Voce::MESSAGGI, IC_BUSTA, "Messaggi", false},
    {Voce::WALKIE, IC_MICROFONO, "Walkie-talkie", false},
    {Voce::CONTATTI, IC_PERSONA, "Contatti", false},
    {Voce::GRUPPI, IC_GRUPPO, "Gruppi", false},
    {Voce::GLOBALE, IC_GLOBO, "Canale globale", false},
    {Voce::POSIZIONE, IC_PUNTO, "Posizione", false},
#if TDECK
    {Voce::MAPPA, IC_MAPPA, "Mappa", false},
#endif
    {Voce::SOS, IC_ATTENZIONE, "SOS", true},
    {Voce::IMPOSTAZIONI, IC_IMPOSTAZIONI, "Impostazioni", false},
    {Voce::SEGNALE, IC_TORRE, "Segnale", false},
    // seconda pagina
    {Voce::OROLOGIO, IC_OROLOGIO, "Orologio", false},
    {Voce::GIOCHI, IC_GIOCHI, "Giochi", false},
    {Voce::MUSICA, IC_MUSICA, "Musica", false},
};
constexpr int N_VOCI = int(sizeof(VOCI) / sizeof(VOCI[0]));
constexpr int COLONNE = PX(3, 5), RIGHE = PX(3, 2), PER_PAGINA = COLONNE * RIGHE;
constexpr int PAGINE = (N_VOCI + PER_PAGINA - 1) / PER_PAGINA;
constexpr int TILE_W = PX(146, 56), TILE_H = PX(118, 66), GAP_X = PX(8, 5), GAP_Y = PX(8, 6);
constexpr int GRID_H = RIGHE * TILE_H + (RIGHE - 1) * GAP_Y;
constexpr int SCORRIMENTO_MS = 220;

constexpr int indexOf(Voce v) {
    for (int i = 0; i < N_VOCI; i++)
        if (VOCI[i].id == v) return i;
    return -1;
}
// La prima pagina resta piena e uguale a prima: le voci nuove cominciano dalla seconda.
static_assert(indexOf(Voce::OROLOGIO) == PER_PAGINA, "la prima pagina del menu deve restare piena");

int vociIn(int page) { return std::min(PER_PAGINA, N_VOCI - page * PER_PAGINA); }

// Voce alla riga r e colonna c di una pagina, o la piu' vicina che c'e' (l'ultima pagina puo'
// essere a meta').
int cell(int page, int r, int c) {
    const int n = vociIn(page);
    int i = r * COLONNE + c;
    while (i >= n && r > 0) i = --r * COLONNE + c;
    if (i >= n) i = n - 1;
    return page * PER_PAGINA + i;
}
}  // namespace

class Menu : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, PX(8, 4));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        pad(col, PX(12, 8), 0, 0, 0);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        // Finestra larga quanto lo schermo con dentro le pagine: si vede solo quella scelta (le altre
        // sono nascoste); scorrendo, la vecchia esce da un lato e la nuova entra dall'altro.
        view_ = box(col);
        lv_obj_set_size(view_, W_SCHERMO, GRID_H);
        tiles_.clear();
        pages_.clear();
        const int side = (W_SCHERMO - COLONNE * TILE_W - (COLONNE - 1) * GAP_X) / 2;
        for (int p = 0; p < PAGINE; p++) {
            lv_obj_t* grid = box(view_);
            lv_obj_set_size(grid, W_SCHERMO, GRID_H);
            lv_obj_set_pos(grid, 0, 0);
            pad(grid, 0, side, 0, side);
            lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
            lv_obj_set_style_pad_row(grid, GAP_Y, 0);
            lv_obj_set_style_pad_column(grid, GAP_X, 0);
            for (int i = p * PER_PAGINA; i < p * PER_PAGINA + vociIn(p); i++) tiles_.push_back(tile(grid, VOCI[i]));
            pages_.push_back(grid);
        }
#if TDECK
        name_ = label(col, &font_26b, BIANCO(), "");
        lv_obj_set_width(name_, lv_pct(100));
        lv_obj_set_style_text_align(name_, LV_TEXT_ALIGN_CENTER, 0);
#endif
        lv_obj_t* dots = row(col, PX(10, 6));
        lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        dots_.clear();
        for (int p = 0; p < PAGINE; p++) {
            lv_obj_t* d = box(dots);
            lv_obj_set_size(d, PX(10, 6), PX(10, 6));
            radius(d, LV_RADIUS_CIRCLE);
            dots_.push_back(d);
        }
        showPages(page(), page());
        focus();
    }
    static lv_obj_t* tile(lv_obj_t* grid, const VoceMenu& v) {
        lv_obj_t* t = column(grid, PX(10, 0));
        lv_obj_set_size(t, TILE_W, TILE_H);
        lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        radius(t, PX(12, 8));
        lv_obj_set_style_border_width(t, 2, 0);
        lv_obj_set_style_border_color(t, temaChiaro() ? INCHIOSTRO() : GRIGIO1(), 0);  // Carta: bordo nero
        label(t, &icone_44, v.red ? ROSSO() : BIANCO(), v.icon);
#if !TDECK
        label(t, &font_20, v.red ? ROSSO() : BIANCO(), v.text);
#endif
        return t;
    }
    int page() const { return sel_ / PER_PAGINA; }
    void focus() {
        for (int i = 0; i < N_VOCI; i++) {
            const bool on = i == sel_;
            lv_obj_t* t = tiles_[i];
            fill(t, on ? AMBRA() : NERO());
            // Carta: anche la tessera scelta tiene il bordo nero; scuro: la scelta senza bordo.
            lv_obj_set_style_border_width(t, on && !temaChiaro() ? 0 : 2, 0);
            const uint32_t n = lv_obj_get_child_count(t);
            for (uint32_t c = 0; c < n; c++)
                lv_obj_set_style_text_color(lv_obj_get_child(t, int32_t(c)),
                                            on ? NERO() : (VOCI[i].red ? ROSSO() : BIANCO()), 0);
        }
#if TDECK
        lv_label_set_text(name_, VOCI[sel_].text);
        lv_obj_set_style_text_color(name_, VOCI[sel_].red ? ROSSO() : BIANCO(), 0);
#endif
        for (int p = 0; p < PAGINE; p++) fill(dots_[p], p == page() ? AMBRA() : GRIGIO1());
    }
    // Si vedono solo le pagine a e b (durante lo scorrimento tutte e due), le altre sono nascoste.
    void showPages(int a, int b) {
        for (int p = 0; p < PAGINE; p++) {
            if (p == a || p == b) lv_obj_clear_flag(pages_[p], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(pages_[p], LV_OBJ_FLAG_HIDDEN);
            if (p != a && p != b) lv_obj_set_x(pages_[p], 0);
        }
    }
    // Scorrimento breve fino alla pagina nuova: shift va da 0 a una larghezza di schermo. Se la
    // schermata cambia a meta', LVGL cancella l'animazione insieme alla finestra (e la fine non arriva).
    void slide(int from, int to) {
        lv_anim_delete(view_, nullptr);
        from_ = from;
        to_ = to;
        showPages(from, to);
        place(0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, view_);
        lv_anim_set_values(&a, 0, W_SCHERMO);
        lv_anim_set_duration(&a, SCORRIMENTO_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_user_data(&a, this);
        lv_anim_set_custom_exec_cb(&a, [](lv_anim_t* an, int32_t shift) {
            static_cast<Menu*>(lv_anim_get_user_data(an))->place(shift);
        });
        lv_anim_set_completed_cb(&a, [](lv_anim_t* an) {
            Menu* m = static_cast<Menu*>(lv_anim_get_user_data(an));
            m->showPages(m->page(), m->page());
            lv_obj_set_x(m->pages_[size_t(m->page())], 0);
        });
        lv_anim_start(&a);
    }
    // Pagina vecchia spostata di shift verso il lato da cui esce, la nuova attaccata a lei.
    void place(int32_t shift) {
        const int dir = to_ > from_ ? 1 : -1;
        lv_obj_set_x(pages_[size_t(from_)], -dir * shift);
        lv_obj_set_x(pages_[size_t(to_)], dir * (W_SCHERMO - shift));
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        const int p = page(), i = sel_ % PER_PAGINA, r = i / COLONNE, c = i % COLONNE, n = vociIn(p);
        switch (k) {
            case Key::RIGHT:
                if (c + 1 < COLONNE && i + 1 < n) sel_++;
                else if (p + 1 < PAGINE) sel_ = cell(p + 1, r, 0);  // oltre il bordo: pagina dopo
                break;
            case Key::LEFT:
                if (c > 0) sel_--;
                else if (p > 0) sel_ = cell(p - 1, r, COLONNE - 1);  // pagina prima
                break;
            case Key::DOWN: sel_ = p * PER_PAGINA + (i + COLONNE < n ? i + COLONNE : c); break;
            case Key::UP: sel_ = p * PER_PAGINA + (r > 0 ? i - COLONNE : (n - 1 - c) / COLONNE * COLONNE + c); break;
            case Key::OK: open(); return true;
            case Key::CALL: push(makeChats()); return true;
            default: return false;
        }
        if (page() != p) slide(p, page());
        focus();
        return true;
    }
    void open() {
        switch (VOCI[sel_].id) {
            case Voce::MESSAGGI: push(makeChats()); break;
            case Voce::WALKIE: push(makeWalkie(0)); break;
            case Voce::CONTATTI: push(makeContacts()); break;
            case Voce::GRUPPI: push(makeGroups()); break;
            case Voce::GLOBALE: push(makeChat({app::ChatKind::GLOBAL, 0})); break;
            case Voce::POSIZIONE: push(makePosition()); break;
            case Voce::MAPPA:
#if TDECK
                push(makeMap());
#endif
                break;
            case Voce::SOS: push(makeSosActive(false)); break;
            case Voce::IMPOSTAZIONI: push(makeSettings()); break;
            case Voce::SEGNALE: push(makeSignal()); break;
            case Voce::OROLOGIO: push(makeClock()); break;
            case Voce::GIOCHI: push(makeGames()); break;
            case Voce::MUSICA: push(makeMusic()); break;
        }
    }
    std::vector<Hint> hints() override { return {{"NAV", "muovi"}, {"OK", "apri"}, {"END", "home"}}; }

private:
    lv_obj_t* view_ = nullptr;
    std::vector<lv_obj_t*> pages_, tiles_, dots_;
    int from_ = 0, to_ = 0;
#if TDECK
    lv_obj_t* name_ = nullptr;
#endif
    int sel_ = 0;
};

Screen* makeMenu() { return new Menu(); }

// ================================================================ blocco

static bool g_lockOpen = false;

class Lock : public Screen {
public:
    Lock() { g_lockOpen = true; }
    ~Lock() override { g_lockOpen = false; }
    bool isLock() const override { return true; }
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, PX(18, 5));
        lv_obj_set_size(col, W_SCHERMO, H_SCHERMO);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        label(col, &icone_44, AMBRA(), IC_LUCCHETTO);
        label(col, &mono_104, BIANCO(), timeHM(app::rete().unixTime()).c_str());
        label(col, &font_22, GRIGIO3(), "Scrivi il PIN e premi OK");
        in_.create(col, PX(260, 180), PX(56, 32), false, 8, "PIN");
        in_.setDigitsOnly(true);
        in_.setPassword(true);
        in_.setFocused(true);
        msg_ = label(col, &font_20, ROSSO(), "");
        const uint16_t u = app::dati().unreadTotal();
        if (u) label(col, &font_20, AMBRA(), u == 1 ? "1 messaggio non letto" : (std::to_string(u) + " messaggi non letti").c_str());
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return true;
        if (k == Key::OK || k == Key::ENTER) {
            if (in_.text() == app::dati().settings().pin) {
                pop();
            } else {
                lv_label_set_text(msg_, "PIN sbagliato");
                in_.clear();
            }
            return true;
        }
        if (k == Key::PTT) return false;
        in_.key(k, pressed);
        return true;  // nient'altro passa finche' e' bloccato
    }
    bool fullscreen() const override { return true; }
    bool capturesEnd() const override { return true; }
#if TDECK
    const TextInput* input() const override { return &in_; }
#endif

private:
    TextInput in_;
    lv_obj_t* msg_ = nullptr;
};

Screen* makeLock() { return new Lock(); }
bool isLockOpen() { return g_lockOpen; }

// ================================================================ primo avvio

namespace {
// Riquadro dei codici: scuro grigio pieno; Carta bianco col bordo nero, che prende il posto di
// 2 pixel di margine (resta grande uguale).
lv_obj_t* riquadroCodice(lv_obj_t* col) {
    lv_obj_t* b = column(col, PX(4, 2));
    lv_obj_set_size(b, PX(436, 300), LV_SIZE_CONTENT);
    superficie(b);
    radius(b, PX(10, 6));
    const int k = temaChiaro() ? 2 : 0;
    pad(b, PX(10, 5) - k, PX(14, 8) - k, PX(12, 5) - k, PX(14, 8) - k);
    return b;
}
}  // namespace

class FirstBoot : public Screen {
public:
    FirstBoot() { app::rete().setProvisioning(true); }
    ~FirstBoot() override { app::rete().setProvisioning(false); }
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, PX(14, 4));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        pad(col, PX(18, 6), PX(22, 10), PX(12, 4), PX(22, 10));
#if TDECK
        // Sul T-Deck "in attesa" sta accanto al titolo: non c'e' spazio per una riga in piu'.
        lv_obj_t* tr = row(col, 8);
        lv_obj_set_size(tr, lv_pct(100), LV_SIZE_CONTENT);
        label(tr, &font_26b, BIANCO(), "Benvenuto");
        lv_obj_t* trs = box(tr);
        grow(trs);
        lv_obj_set_height(trs, 1);
        waitLbl_ = label(tr, &font_16, GRIGIO2(), "");
#else
        label(col, &font_32b, BIANCO(), "Benvenuto");
#endif
        pn::Cert cert;
        std::string admin;
        if (app::rete().offer(cert, admin)) {
            // L'amministratore ha mandato un numero: si confrontano i codici.
            lv_obj_t* t = label(col, &font_22, GRIGIO3(), "L'amministratore ti assegna il numero");
            lv_obj_set_width(t, PX(436, 300));
            lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
            label(col, &font_48b, AMBRA(), std::to_string(cert.number).c_str());
            lv_obj_t* b = riquadroCodice(col);
            lv_obj_t* hint = label(b, &font_16, GRIGIO3(), "Codice dell'amministratore: deve essere uguale a quello sul suo schermo");
            lv_obj_set_width(hint, PX(408, 284));
            lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
            label(b, &mono_28, BIANCO(), admin.c_str());
            offer_ = true;
            return;
        }
        offer_ = false;
        lv_obj_t* t = label(col, PX(&font_22, &font_20), GRIGIO3(),
                            TXT("Questo telefono non ha ancora un numero. Chiedilo all'amministratore:",
                                "Non hai ancora un numero: chiedilo all'amministratore."));
        lv_obj_set_width(t, PX(436, 300));
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        const char* steps[] = {"avvicina il suo telefono a questo",
                               "lui sceglie Impostazioni > Amministrazione",
                               "controllate che il codice sia uguale"};
        for (int i = 0; i < 3; i++) {
            lv_obj_t* r = row(col, PX(10, 6));
            lv_obj_set_size(r, PX(436, 300), LV_SIZE_CONTENT);
            label(r, &mono_20, AMBRA(), std::to_string(i + 1).c_str());
            label(r, &font_20, BIANCO(), steps[i]);
        }
        lv_obj_t* b = riquadroCodice(col);
        label(b, &font_16, GRIGIO3(), "Codice di questo telefono");
        label(b, &mono_28, AMBRA(), app::rete().myCode().c_str());
#if TDECK
        lv_label_set_text(waitLbl_, "in attesa del numero...");
#else
        label(col, &font_18, GRIGIO2(), "In attesa del numero...");
#endif
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (offer_ && (k == Key::OK || k == Key::ENTER)) {
            app::rete().acceptOffer();
            scheda::audio().play(scheda::suoni::INVIO_OK);
            replace(makeHome());
            return true;
        }
        if (offer_ && k == Key::BACK) {
            app::rete().rejectOffer();
            rebuild();
            return true;
        }
        if (k == Key::MENU) {
            // Il primo telefono della rete diventa l'amministratore.
            ask("Creare una rete nuova?",
                "Questo telefono diventa l'amministratore: sarà lui ad assegnare i numeri agli altri.",
                {"Sì, crea la rete", "Annulla"}, [this](int i) {
                    if (i == 0) chooseNumber();
                });
            return true;
        }
        return false;
    }
    void chooseNumber() {
        push(makePickContact("Il tuo numero", [](uint16_t n) {
            if (n && app::rete().createNetwork(n)) {
                // In fondo alla pila c'e' ancora questa schermata di benvenuto: goHome() la terrebbe
                // (crea la principale solo a pila vuota), quindi la si sostituisce con la principale.
                goHome();
                replace(makeHome());
                toast("Rete creata", "Sei l'amministratore. Il tuo numero è " + std::to_string(n), VERDE());
            }
        }));
    }
    void onEvent(const app::Event& e) override {
        if (e.type == app::Ev::PROV_OFFER) {
            scheda::audio().play(scheda::suoni::SQUILLO);
            rebuild();
        }
    }
    std::vector<Hint> hints() override {
        if (offer_) return {{"OK", "accetta"}, {"BACK", "rifiuta"}};
        return {{"MENU", "crea una rete nuova"}};
    }
    bool capturesEnd() const override { return true; }

private:
    bool offer_ = false;
#if TDECK
    lv_obj_t* waitLbl_ = nullptr;
#endif
};

Screen* makeFirstBoot() { return new FirstBoot(); }

}  // namespace ui
