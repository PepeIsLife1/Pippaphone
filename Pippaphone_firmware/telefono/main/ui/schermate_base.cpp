// Schermata principale, menu, blocco con PIN, primo avvio (assegnazione del numero).
#include <cstdio>

#include "app/dati.h"
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
        lv_obj_set_size(col, 480, 412);
        pad(col, 16, 20, 12, 20);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_t* top = column(col, 0);
        lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
        clock_ = label(top, &mono_104, BIANCO(), "");
        date_ = label(top, &font_22, GRIGIO3(), "");
        cards_ = column(col, 8);
        lv_obj_set_size(cards_, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_t* foot = row(col, 0);
        lv_obj_set_size(foot, lv_pct(100), LV_SIZE_CONTENT);
        me_ = label(foot, &font_18, GRIGIO2(), "");
        grow(me_);
        sig_ = label(foot, &font_18, GRIGIO2(), "");
        refresh();
    }
    void refresh() {
        const uint32_t t = app::rete().unixTime();
        lv_label_set_text(clock_, t ? timeHM(t).c_str() : "--:--");
        lv_label_set_text(date_, dateLong(t).c_str());
        lv_label_set_text(me_, ("Il mio numero " + std::to_string(app::rete().number())).c_str());
        const auto s = app::rete().signal();
        char b[48];
        if (s.heard) snprintf(b, sizeof(b), "Ripetitore R%u · %d dBm", unsigned(s.repeater - 0xF000), s.rssi);
        else snprintf(b, sizeof(b), "Nessun ripetitore");
        lv_label_set_text(sig_, b);
        // Notifiche: conversazioni non lette (al massimo 3) e SOS in corso.
        lv_obj_clean(cards_);
        cardChats_.clear();
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
        lv_obj_t* c = row(cards_, 12);
        lv_obj_set_size(c, lv_pct(100), 56);
        pad(c, 0, 14, 0, 14);
        fill(c, GRIGIO1());
        radius(c, 10);
        label(c, &icone_22, color, icon);
        lv_obj_t* l = label(c, &font_22, BIANCO(), text.c_str());
        grow(l);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        label(c, &mono_16, GRIGIO3(), when.c_str());
        cardChats_.push_back(id);
    }
    void focusCard() {
        cardObjs_.clear();
        const uint32_t n = lv_obj_get_child_count(cards_);
        for (uint32_t i = 0; i < n; i++) cardObjs_.push_back(lv_obj_get_child(cards_, int32_t(i)));
        if (sel_ >= int(cardObjs_.size())) sel_ = -1;
        for (int i = 0; i < int(cardObjs_.size()); i++) {
            lv_obj_set_style_border_width(cardObjs_[i], i == sel_ ? 3 : 0, 0);
            lv_obj_set_style_border_color(cardObjs_[i], AMBRA(), 0);
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
    std::vector<lv_obj_t*> cardObjs_;
    std::vector<app::ChatId> cardChats_;
    int sel_ = -1;
    int ticks_ = 0;
};

Screen* makeHome() { return new Home(); }

// ================================================================ menu

class Menu : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* grid = box(root);
        lv_obj_set_size(grid, 480, 412);
        pad(grid, 12, 12, 12, 12);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_style_pad_row(grid, 8, 0);
        lv_obj_set_style_pad_column(grid, 8, 0);
        struct Item {
            const char* icon;
            const char* text;
            bool red;
        };
        static const Item items[9] = {{IC_BUSTA, "Messaggi", false},     {IC_MICROFONO, "Walkie-talkie", false},
                                      {IC_PERSONA, "Contatti", false},  {IC_GRUPPO, "Gruppi", false},
                                      {IC_GLOBO, "Canale globale", false}, {IC_PUNTO, "Posizione", false},
                                      {IC_ATTENZIONE, "SOS", true},     {IC_IMPOSTAZIONI, "Impostazioni", false},
                                      {IC_TORRE, "Segnale", false}};
        tiles_.clear();
        for (const auto& it : items) {
            lv_obj_t* t = column(grid, 10);
            lv_obj_set_size(t, 146, 124);
            lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            radius(t, 12);
            lv_obj_set_style_border_width(t, 2, 0);
            lv_obj_set_style_border_color(t, GRIGIO1(), 0);
            lv_obj_t* i = label(t, &icone_44, it.red ? ROSSO() : BIANCO(), it.icon);
            lv_obj_t* l = label(t, &font_20, it.red ? ROSSO() : BIANCO(), it.text);
            (void)i;
            (void)l;
            tiles_.push_back(t);
        }
        focus();
    }
    void focus() {
        for (int i = 0; i < 9; i++) {
            const bool on = i == sel_;
            lv_obj_t* t = tiles_[i];
            fill(t, on ? AMBRA() : NERO());
            lv_obj_set_style_border_width(t, on ? 0 : 2, 0);
            const bool red = i == 6;
            for (int c = 0; c < 2; c++)
                lv_obj_set_style_text_color(lv_obj_get_child(t, c), on ? NERO() : (red ? ROSSO() : BIANCO()), 0);
        }
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        switch (k) {
            case Key::LEFT: sel_ = (sel_ + 8) % 9; break;
            case Key::RIGHT: sel_ = (sel_ + 1) % 9; break;
            case Key::UP: sel_ = (sel_ + 6) % 9; break;
            case Key::DOWN: sel_ = (sel_ + 3) % 9; break;
            case Key::OK: open(); return true;
            case Key::CALL: push(makeChats()); return true;
            default: return false;
        }
        focus();
        return true;
    }
    void open() {
        switch (sel_) {
            case 0: push(makeChats()); break;
            case 1: push(makeWalkie(0)); break;
            case 2: push(makeContacts()); break;
            case 3: push(makeGroups()); break;
            case 4: push(makeChat({app::ChatKind::GLOBAL, 0})); break;
            case 5: push(makePosition()); break;
            case 6: push(makeSosActive(false)); break;
            case 7: push(makeSettings()); break;
            case 8: push(makeSignal()); break;
        }
    }
    std::vector<Hint> hints() override { return {{"NAV", "muovi"}, {"OK", "apri"}, {"END", "home"}}; }

private:
    std::vector<lv_obj_t*> tiles_;
    int sel_ = 0;
};

Screen* makeMenu() { return new Menu(); }

// ================================================================ blocco

static bool g_lockOpen = false;

class Lock : public Screen {
public:
    Lock() { g_lockOpen = true; }
    ~Lock() override { g_lockOpen = false; }
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 18);
        lv_obj_set_size(col, 480, 480);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        label(col, &icone_44, AMBRA(), IC_LUCCHETTO);
        label(col, &mono_104, BIANCO(), timeHM(app::rete().unixTime()).c_str());
        label(col, &font_22, GRIGIO3(), "Scrivi il PIN e premi OK");
        in_.create(col, 260, 56, false, 8, "PIN");
        in_.setDigitsOnly(true);
        in_.setPassword(true);
        in_.setFocused(true);
        msg_ = label(col, &font_20, ROSSO(), "");
        const uint16_t u = app::dati().unreadTotal();
        if (u) label(col, &font_20, AMBRA(), (std::to_string(u) + " messaggi non letti").c_str());
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

private:
    TextInput in_;
    lv_obj_t* msg_ = nullptr;
};

Screen* makeLock() { return new Lock(); }
bool isLockOpen() { return g_lockOpen; }

// ================================================================ primo avvio

class FirstBoot : public Screen {
public:
    FirstBoot() { app::rete().setProvisioning(true); }
    ~FirstBoot() override { app::rete().setProvisioning(false); }
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 14);
        lv_obj_set_size(col, 480, 412);
        pad(col, 18, 22, 12, 22);
        label(col, &font_32b, BIANCO(), "Benvenuto");
        pn::Cert cert;
        std::string admin;
        if (app::rete().offer(cert, admin)) {
            // L'amministratore ha mandato un numero: si confrontano i codici.
            lv_obj_t* t = label(col, &font_22, GRIGIO3(), "L'amministratore ti assegna il numero");
            lv_obj_set_width(t, 436);
            lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
            label(col, &font_48b, AMBRA(), std::to_string(cert.number).c_str());
            lv_obj_t* b = column(col, 4);
            lv_obj_set_size(b, 436, LV_SIZE_CONTENT);
            fill(b, GRIGIO1());
            radius(b, 10);
            pad(b, 10, 14, 12, 14);
            label(b, &font_16, GRIGIO3(), "Codice dell'amministratore: deve essere uguale al suo schermo");
            label(b, &mono_28, BIANCO(), admin.c_str());
            offer_ = true;
            return;
        }
        offer_ = false;
        lv_obj_t* t = label(col, &font_22, GRIGIO3(),
                            "Questo telefono non ha ancora un numero. Chiedilo all'amministratore:");
        lv_obj_set_width(t, 436);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        const char* steps[] = {"avvicina il suo telefono a questo",
                               "lui sceglie Impostazioni → Amministrazione",
                               "controllate che il codice sia uguale"};
        for (int i = 0; i < 3; i++) {
            lv_obj_t* r = row(col, 10);
            lv_obj_set_size(r, 436, LV_SIZE_CONTENT);
            label(r, &mono_20, AMBRA(), std::to_string(i + 1).c_str());
            label(r, &font_20, BIANCO(), steps[i]);
        }
        lv_obj_t* b = column(col, 4);
        lv_obj_set_size(b, 436, LV_SIZE_CONTENT);
        fill(b, GRIGIO1());
        radius(b, 10);
        pad(b, 10, 14, 12, 14);
        label(b, &font_16, GRIGIO3(), "Codice di questo telefono");
        label(b, &mono_28, AMBRA(), app::rete().myCode().c_str());
        label(col, &font_18, GRIGIO2(), "In attesa del numero...");
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
                "Questo telefono diventa l'amministratore: sara' lui ad assegnare i numeri agli altri.",
                {"Si', crea la rete", "Annulla"}, [this](int i) {
                    if (i == 0) chooseNumber();
                });
            return true;
        }
        return false;
    }
    void chooseNumber() {
        push(makePickContact("Il tuo numero", [](uint16_t n) {
            if (n && app::rete().createNetwork(n)) {
                goHome();
                toast("Rete creata", "Sei l'amministratore. Il tuo numero e' " + std::to_string(n), VERDE());
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
};

Screen* makeFirstBoot() { return new FirstBoot(); }

}  // namespace ui
