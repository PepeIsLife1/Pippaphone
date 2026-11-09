// Rubrica (contatti) e gruppi.
#include <algorithm>
#include <cctype>

#include "app/dati.h"
#include "app/rete.h"
#include "scheda/scheda.h"
#include "ui/schermate.h"
#include "ui/widget.h"

namespace ui {

// ================================================================ elenco contatti

class Contacts : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        items_ = app::dati().contacts();
        header(col, IC_PERSONA, "Contatti", std::to_string(items_.size()));
        list_.create(col, W_SCHERMO, H_AREA - H_TITOLO);
        lv_obj_t* nw = list_.addRow(PX(56, 34));
        List::text(nw, &icone_22, AMBRA(), IC_AGGIUNGI_PERSONA);
        List::text(nw, &font_22b, AMBRA(), "Nuovo contatto", true);
        for (const auto& c : items_) {
            lv_obj_t* r = list_.addRow(PX(64, 34));
            avatar(r, c.number, c.name, PX(44, 26));
            List::text(r, &font_22b, BIANCO(), c.name, true);
            List::text(r, &mono_16, GRIGIO3(), std::to_string(c.number));
        }
        list_.focus(sel_);
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return k == Key::PTT;
        if (list_.key(k)) {
            sel_ = list_.focused();
            return true;
        }
        sel_ = list_.focused();
        const app::Contact* c = sel_ > 0 ? &items_[sel_ - 1] : nullptr;
        switch (k) {
            case Key::OK:
                push(c ? makeContact(c->number) : makeEditContact(0));
                return true;
            case Key::CALL:
                if (c) push(makeChat({app::ChatKind::DIRECT, c->number}));
                return true;
            case Key::PTT:
                if (c) push(makeWalkie(c->number, true));
                return true;
            case Key::MENU:
                if (c) push(makeEditContact(c->number));
                return true;
            default: break;
        }
        // Una lettera salta al primo contatto che inizia cosi'.
        if (const char l = scheda::letterOf(k)) {
            for (size_t i = 0; i < items_.size(); i++) {
                const char f = items_[i].name.empty() ? 0 : char(std::tolower(uint8_t(items_[i].name[0])));
                if (f >= l) {
                    sel_ = int(i) + 1;
                    list_.focus(sel_);
                    break;
                }
            }
            return true;
        }
        return false;
    }
    std::vector<Hint> hints() override {
        if (list_.focused() == 0) return {{"OK", "aggiungi"}, {"A-Z", "cerca"}};
        return {{"OK", "apri"}, {"CALL", "scrivi"}, {"PTT", "parla"}, {"MENU", "modifica"}};
    }

private:
    List list_;
    std::vector<app::Contact> items_;
    int sel_ = 0;
};

Screen* makeContacts() { return new Contacts(); }

// ================================================================ scheda contatto

class ContactCard : public Screen {
public:
    explicit ContactCard(uint16_t n) : number_(n) {}
    void build(lv_obj_t* root) override {
        app::Contact c;
        known_ = app::dati().contact(number_, c);
        name_ = known_ ? c.name : std::to_string(number_);
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        lv_obj_t* top = row(col, PX(16, 10));
        lv_obj_set_size(top, W_SCHERMO, PX(110, 70));
        pad(top, PX(12, 6), PX(20, 10), PX(12, 6), PX(20, 10));
        avatar(top, number_, name_, PX(80, 50));
        lv_obj_t* txt = column(top, PX(4, 1));
        grow(txt);
        lv_obj_set_height(txt, LV_SIZE_CONTENT);
        lv_obj_t* nl = label(txt, PX(&font_32b, &font_26b), BIANCO(), name_.c_str());
        unaRiga(nl);
        lv_obj_set_width(nl, lv_pct(100));
        label(txt, &mono_20, GRIGIO3(), std::to_string(number_).c_str());
        const std::string code = app::rete().codeOf(number_);
        label(txt, &font_16, code.empty() ? GRIGIO2() : VERDE(),
              code.empty() ? "chiave non ancora ricevuta" : (std::string(IC_SCUDO " ") + code).c_str());
        underline(top);
        list_.create(col, W_SCHERMO, H_AREA - PX(110, 70));
        struct Item {
            const char* icon;
            const char* text;
        };
        const Item items[] = {{IC_BUSTA, "Scrivi un messaggio"},
                              {IC_MICROFONO, "Walkie-talkie"},
                              {IC_PUNTO, "Chiedi la posizione"},
                              {IC_MATITA, known_ ? "Modifica" : "Aggiungi alla rubrica"},
                              {IC_CESTINO, "Elimina"}};
        const int n = known_ ? 5 : 4;
        for (int i = 0; i < n; i++) {
            lv_obj_t* r = list_.addRow(PX(56, 32));
            List::text(r, &icone_22, i == 4 ? ROSSO() : AMBRA(), items[i].icon);
            List::text(r, &font_22, i == 4 ? ROSSO() : BIANCO(), items[i].text, true);
        }
        list_.focus(sel_);
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return k == Key::PTT;
        if (list_.key(k)) {
            sel_ = list_.focused();
            return true;
        }
        if (k == Key::CALL) return push(makeChat({app::ChatKind::DIRECT, number_})), true;
        if (k == Key::PTT) return push(makeWalkie(number_, true)), true;
        if (k != Key::OK) return false;
        sel_ = list_.focused();
        switch (sel_) {
            case 0: push(makeChat({app::ChatKind::DIRECT, number_})); break;
            case 1: push(makeWalkie(number_)); break;
            case 2:
                app::rete().requestPosition(number_);
                toast("Posizione chiesta", name_ + " deve accettare");
                break;
            case 3: push(makeEditContact(number_)); break;
            case 4: {
                const uint16_t n = number_;
                ask("Eliminare " + name_ + "?", "La conversazione resta.", {"Elimina", "Annulla"}, [n](int i) {
                    if (i != 0) return;
                    app::dati().removeContact(n);
                    pop();
                });
                break;
            }
        }
        return true;
    }
    std::vector<Hint> hints() override { return {{"OK", "scegli"}, {"CALL", "scrivi"}, {"PTT", "parla"}}; }

private:
    uint16_t number_;
    bool known_ = false;
    std::string name_;
    List list_;
    int sel_ = 0;
};

Screen* makeContact(uint16_t number) { return new ContactCard(number); }

// ================================================================ nuovo / modifica contatto

class EditContact : public Screen {
public:
    explicit EditContact(uint16_t n) : orig_(n) {
        app::Contact c;
        if (n && app::dati().contact(n, c)) name0_ = c.name;
    }
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, PX(10, 4));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, orig_ ? IC_MATITA : IC_AGGIUNGI_PERSONA, orig_ ? "Modifica contatto" : "Nuovo contatto");
        lv_obj_t* body = column(col, PX(8, 3));
        lv_obj_set_size(body, W_SCHERMO, LV_SIZE_CONTENT);
        pad(body, PX(8, 4), PX(20, 10), 0, PX(20, 10));
        label(body, &font_18, GRIGIO3(), "Nome");
        name_.create(body, PX(440, 300), PX(56, 32), false, 24, "Nome e cognome");
        name_.setText(nameText_.empty() ? name0_ : nameText_);
        label(body, &font_18, GRIGIO3(), "Numero (1 - 59999)");
        num_.create(body, PX(440, 300), PX(56, 32), false, 5, "Numero");
        num_.setDigitsOnly(true);
        num_.setText(numText_.empty() ? (orig_ ? std::to_string(orig_) : "") : numText_);
        msg_ = label(body, &font_18, ROSSO(), "");
        focus(field_);
    }
    void focus(int f) {
        field_ = f;
        name_.setFocused(f == 0);
        num_.setFocused(f == 1);
    }
    bool onKey(Key k, bool pressed) override {
        TextInput& in = field_ == 0 ? name_ : num_;
        if (!pressed) return in.key(k, pressed) != TextInput::NONE;
        if (in.symOpen()) return in.key(k, pressed), true;
        if (k == Key::UP || k == Key::DOWN) {
            focus(k == Key::UP ? 0 : 1);
            return true;
        }
        if (k == Key::OK || k == Key::ENTER || k == Key::CALL) {
            if (field_ == 0 && k != Key::CALL) {
                focus(1);
                return true;
            }
            save();
            return true;
        }
        if (k == Key::BACK) return false;
        const auto r = in.key(k, pressed);
        remember();
        return r != TextInput::NONE;
    }
    void remember() {
        nameText_ = name_.text();
        numText_ = num_.text();
    }
    void save() {
        std::string name = name_.text();
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        const int n = atoi(num_.text().c_str());
        if (name.empty()) {
            lv_label_set_text(msg_, "Scrivi il nome");
            focus(0);
            return;
        }
        if (n < 1 || n > int(pn::ADDR_PHONE_MAX)) {
            lv_label_set_text(msg_, "Numero non valido");
            focus(1);
            return;
        }
        if (n == app::rete().number()) {
            lv_label_set_text(msg_, "È il tuo numero");
            focus(1);
            return;
        }
        app::Contact other;
        if (n != orig_ && app::dati().contact(uint16_t(n), other)) {
            lv_label_set_text(msg_, ("Numero già in rubrica: " + other.name).c_str());
            focus(1);
            return;
        }
        if (orig_ && orig_ != n) app::dati().removeContact(orig_);
        app::dati().setContact({uint16_t(n), name});
        scheda::audio().play(scheda::suoni::CLIC);
        toast("Contatto salvato", name, VERDE());
        pop();
    }
    std::vector<Hint> hints() override {
        if (name_.symOpen() || num_.symOpen()) return {{"NAV", "scegli"}, {"OK", "inserisci"}};
        return {{"SU/GIÙ", "campo"}, {"CALL", "salva"}, {"BACK", "annulla"}};
    }
    bool capturesEnd() const override { return false; }
#if TDECK
    const TextInput* input() const override { return field_ == 0 ? &name_ : &num_; }
#endif

private:
    uint16_t orig_;
    std::string name0_, nameText_, numText_;
    TextInput name_, num_;
    lv_obj_t* msg_ = nullptr;
    int field_ = 0;
};

Screen* makeEditContact(uint16_t number) { return new EditContact(number); }

// ================================================================ gruppi

class Groups : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        items_ = app::dati().groups();
        header(col, IC_GRUPPO, "Gruppi", std::to_string(items_.size()));
        list_.create(col, W_SCHERMO, H_AREA - H_TITOLO);
        lv_obj_t* nw = list_.addRow(PX(56, 34));
        List::text(nw, &icone_22, AMBRA(), IC_PIU);
        List::text(nw, &font_22b, AMBRA(), "Nuovo gruppo", true);
        for (const auto& g : items_) {
            lv_obj_t* r = list_.addRow(PX(70, 42));
            lv_obj_t* a = box(r);
            lv_obj_set_size(a, PX(44, 28), PX(44, 28));
            radius(a, PX(12, 8));
            fill(a, avatarColor(g.id));
            lv_obj_center(label(a, &icone_22, BIANCO(), IC_GRUPPO));
            lv_obj_t* mid = column(r, 2);
            grow(mid);
            lv_obj_set_height(mid, LV_SIZE_CONTENT);
            rigaPiena(List::text(mid, &font_22b, BIANCO(), g.name));
            std::string names;
            for (auto m : g.members) {
                if (m == app::rete().number()) continue;
                names += (names.empty() ? "" : ", ") + app::dati().nameOf(m);
            }
            lv_obj_t* l = List::text(mid, &font_18, GRIGIO3(), std::to_string(g.members.size()) + " membri · " + names);
            lv_obj_set_width(l, lv_pct(100));
            unaRiga(l);
        }
        if (items_.empty()) {
            lv_obj_t* t = label(list_.obj(), &font_20, GRIGIO2(),
                                "Nessun gruppo. Chiunque può creare un gruppo fino a 16 persone.");
            lv_obj_set_width(t, PX(440, 300));
            lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
            pad(t, PX(16, 8), PX(20, 10), 0, PX(20, 10));
        }
        list_.focus(sel_);
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (list_.key(k)) {
            sel_ = list_.focused();
            return true;
        }
        sel_ = list_.focused();
        if (k == Key::OK) {
            if (sel_ == 0) push(makeNewGroup());
            else push(makeChat({app::ChatKind::GROUP, items_[sel_ - 1].id}));
            return true;
        }
        if (k == Key::MENU && sel_ > 0) {
            const pn::GroupInfo g = items_[sel_ - 1];
            ask("Uscire da " + g.name + "?",
                "Il gruppo e i suoi messaggi spariscono da questo telefono. Per rientrare qualcuno deve "
                "aggiungerti di nuovo.",
                {"Esci dal gruppo", "Annulla"}, [this, g](int i) {
                    if (i != 0) return;
                    app::dati().removeGroup(g.id);
                    app::dati().deleteChat({app::ChatKind::GROUP, g.id});
                    sel_ = 0;
                    rebuild();
                });
            return true;
        }
        return false;
    }
    void onEvent(const app::Event& e) override {
        if (e.type == app::Ev::GROUP_INVITE) rebuild();
    }
    std::vector<Hint> hints() override {
        if (list_.focused() == 0) return {{"OK", "crea"}};
        return {{"OK", "apri"}, {"MENU", "esci dal gruppo"}};
    }

private:
    List list_;
    std::vector<pn::GroupInfo> items_;
    int sel_ = 0;
};

Screen* makeGroups() { return new Groups(); }

// ================================================================ nuovo gruppo

class NewGroup : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        contacts_ = app::dati().contacts();
        count_ = header(col, IC_GRUPPO, "Nuovo gruppo", "1/16");
        lv_obj_t* r = row(col, PX(10, 6));
        lv_obj_set_size(r, W_SCHERMO, PX(70, 40));
        pad(r, PX(8, 4), PX(14, 8), PX(8, 4), PX(14, 8));
        label(r, &font_20, GRIGIO3(), "Nome");
        name_.create(r, PX(360, 254), PX(54, 32), false, 20, "nome del gruppo");
        name_.setText(nameText_);
        list_.create(col, W_SCHERMO, H_AREA - H_TITOLO - PX(70, 40));
        checks_.clear();
        for (const auto& c : contacts_) {
            lv_obj_t* row2 = list_.addRow(PX(58, 34));
            lv_obj_t* ck = List::text(row2, &icone_22, AMBRA(), chosen(c.number) ? IC_OK : "");
            lv_obj_set_width(ck, PX(26, 18));
            checks_.push_back(ck);
            avatar(row2, c.number, c.name, PX(40, 26));
            List::text(row2, &font_22b, BIANCO(), c.name, true);
            List::text(row2, &mono_16, GRIGIO3(), std::to_string(c.number));
        }
        if (contacts_.empty()) label(list_.obj(), &font_20, GRIGIO2(), "  Prima aggiungi dei contatti in rubrica");
        list_.focus(sel_);
        focusName(inName_);
        updateCount();
    }
    bool chosen(uint16_t n) const { return std::find(members_.begin(), members_.end(), n) != members_.end(); }
    void focusName(bool on) {
        inName_ = on;
        name_.setFocused(on);
        lv_obj_set_style_opa(list_.obj(), on ? LV_OPA_60 : LV_OPA_COVER, 0);
    }
    void updateCount() {
        // Il testo a destra dell'intestazione e' l'ultimo figlio.
        lv_obj_t* right = lv_obj_get_child(count_, -1);
        if (right)
            lv_label_set_text(right,
                              (std::to_string(members_.size() + 1) + "/" + std::to_string(pn::MAX_GROUP_MEMBERS)).c_str());
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return inName_ && name_.key(k, pressed) != TextInput::NONE;
        if (inName_ && name_.symOpen()) return name_.key(k, pressed), true;
        if (k == Key::CALL) {
            create();
            return true;
        }
        if (inName_) {
            if (k == Key::DOWN || k == Key::OK || k == Key::ENTER) {
                if (!contacts_.empty()) focusName(false);
                return true;
            }
            if (k == Key::BACK) return false;
            if (k == Key::UP) return true;
            const auto r = name_.key(k, pressed);
            nameText_ = name_.text();
            return r != TextInput::NONE;
        }
        if (k == Key::UP && list_.focused() == 0) {
            focusName(true);
            return true;
        }
        if (list_.key(k)) {
            sel_ = list_.focused();
            return true;
        }
        if (k == Key::OK && !contacts_.empty()) {
            sel_ = list_.focused();
            const uint16_t n = contacts_[sel_].number;
            auto it = std::find(members_.begin(), members_.end(), n);
            if (it != members_.end()) {
                members_.erase(it);
            } else if (members_.size() + 1 >= pn::MAX_GROUP_MEMBERS) {
                toast("Gruppo pieno", "Al massimo " + std::to_string(pn::MAX_GROUP_MEMBERS) + " persone, tu compreso",
                      ROSSO());
                return true;
            } else {
                members_.push_back(n);
            }
            lv_label_set_text(checks_[sel_], chosen(n) ? IC_OK : "");
            updateCount();
            return true;
        }
        return false;
    }
    void create() {
        std::string name = name_.text();
        while (!name.empty() && name.back() == ' ') name.pop_back();
        if (name.empty()) {
            toast("Manca il nome", "Scrivi il nome del gruppo", ROSSO());
            focusName(true);
            return;
        }
        if (members_.empty()) {
            toast("Nessun membro", "Scegli almeno un contatto con OK", ROSSO());
            return;
        }
        const pn::GroupInfo g = app::rete().createGroup(name, members_);
        if (!g.id) {
            toast("Gruppo non creato", "Serve un numero assegnato", ROSSO());
            return;
        }
        toast("Gruppo creato", "Invito mandato a " + std::to_string(members_.size()) + " persone", VERDE());
        replace(makeChat({app::ChatKind::GROUP, g.id}));
    }
    std::vector<Hint> hints() override {
        if (inName_) return {{"GIÙ", "membri"}, {"CALL", "crea"}};
        return {{"OK", "aggiungi/togli"}, {"CALL", "crea"}};
    }
#if TDECK
    const TextInput* input() const override { return inName_ ? &name_ : nullptr; }
#endif

private:
    TextInput name_;
    std::string nameText_;
    List list_;
    std::vector<app::Contact> contacts_;
    std::vector<uint16_t> members_;
    std::vector<lv_obj_t*> checks_;
    lv_obj_t* count_ = nullptr;
    bool inName_ = true;
    int sel_ = 0;
};

Screen* makeNewGroup() { return new NewGroup(); }

}  // namespace ui
