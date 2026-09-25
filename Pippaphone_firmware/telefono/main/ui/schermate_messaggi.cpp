// Conversazioni, chat (1:1, gruppo, canale globale) e scelta di un contatto.
#include <cmath>
#include <cstdio>

#include "app/dati.h"
#include "app/rete.h"
#include "app/voce.h"
#include "scheda/scheda.h"
#include "ui/schermate.h"
#include "ui/widget.h"

namespace ui {

namespace {
// Icona e colore dello stato di un messaggio inviato.
void statusIcon(lv_obj_t* parent, pn::Delivery d) {
    switch (d) {
        case pn::Delivery::PENDING: label(parent, &icone_18, GRIGIO2(), IC_OROLOGIO); break;
        case pn::Delivery::SENT: label(parent, &icone_18, GRIGIO3(), IC_OK); break;
        case pn::Delivery::DELIVERED: label(parent, &icone_18, BIANCO(), IC_OK); break;
        case pn::Delivery::READ: label(parent, &icone_18, BLU(), IC_DOPPIO_OK); break;
        case pn::Delivery::UNREACHABLE: label(parent, &icone_18, ROSSO(), IC_ATTENZIONE); break;
    }
}

// Descrizione breve di una posizione ricevuta ("1,8 km nord-est").
std::string positionSummary(const std::string& data) {
    long lat, lon;
    int alt;
    unsigned acc, age;
    if (sscanf(data.c_str(), "%ld,%ld,%d,%u,%u", &lat, &lon, &alt, &acc, &age) < 2) return data;
    pn::Position me = app::rete().lastPosition();
    if (!me.valid) return app::formatLatLon(int32_t(lat), int32_t(lon));
    double m, b;
    app::distanceBearing(me.latE7, me.lonE7, int32_t(lat), int32_t(lon), m, b);
    static const char* dirs[] = {"nord", "nord-est", "est", "sud-est", "sud", "sud-ovest", "ovest", "nord-ovest"};
    char buf[64];
    if (m < 1000) snprintf(buf, sizeof(buf), "%.0f m %s", m, dirs[int((b + 22.5) / 45) % 8]);
    else snprintf(buf, sizeof(buf), "%.1f km %s", m / 1000, dirs[int((b + 22.5) / 45) % 8]);
    std::string s = buf;
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string preview(const app::Message& m) {
    switch (m.kind) {
        case app::MsgKind::VOICE: return "Vocale · " + durationMS(m.voiceMs);
        case app::MsgKind::POSITION: return "Posizione: " + positionSummary(m.text);
        case app::MsgKind::SOS: return "SOS";
        default: return m.text;
    }
}
}  // namespace

// ================================================================ scelta contatto

class PickContact : public Screen {
public:
    PickContact(const std::string& title, std::function<void(uint16_t)> cb) : title_(title), cb_(std::move(cb)) {}
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, 480, 412);
        header(col, IC_PERSONA, title_);
        lv_obj_t* r = row(col, 10);
        lv_obj_set_size(r, 480, 70);
        pad(r, 8, 14, 8, 14);
        label(r, &font_20, GRIGIO3(), "Numero");
        num_.create(r, 330, 54, false, 5, "scrivi o scegli sotto");
        num_.setDigitsOnly(true);
        num_.setFocused(true);
        contacts_ = app::dati().contacts();
        list_.create(col, 480, 412 - 48 - 70);
        for (const auto& c : contacts_) {
            lv_obj_t* row2 = list_.addRow(58);
            avatar(row2, c.number, c.name, 40);
            List::text(row2, &font_22b, BIANCO(), c.name, true);
            List::text(row2, &mono_16, GRIGIO3(), std::to_string(c.number));
        }
        if (contacts_.empty()) label(list_.obj(), &font_20, GRIGIO2(), "  La rubrica è vuota");
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (k == Key::UP || k == Key::DOWN) return list_.key(k);
        if ((k == Key::ENTER || k == Key::OK) && !num_.empty()) {
            const int n = atoi(num_.text().c_str());
            if (n >= 1 && n <= int(pn::ADDR_PHONE_MAX)) done(uint16_t(n));
            return true;
        }
        if (k == Key::OK && !contacts_.empty()) {
            done(contacts_[list_.focused()].number);
            return true;
        }
        if (k == Key::BACK && !num_.empty()) {
            num_.clear();
            return true;
        }
        return num_.key(k, pressed) != TextInput::NONE;
    }
    void done(uint16_t n) {
        auto cb = cb_;
        pop();
        cb(n);
    }
    std::vector<Hint> hints() override { return {{"OK", "scegli"}, {"BACK", "annulla"}}; }

private:
    std::string title_;
    std::function<void(uint16_t)> cb_;
    TextInput num_;
    List list_;
    std::vector<app::Contact> contacts_;
};

Screen* makePickContact(const std::string& title, std::function<void(uint16_t)> cb) {
    return new PickContact(title, std::move(cb));
}

// ================================================================ conversazioni

class Chats : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, 480, 412);
        const uint16_t unread = app::dati().unreadTotal();
        header(col, nullptr, "Messaggi", unread ? std::to_string(unread) + " non letti" : "");
        list_.create(col, 480, 412 - 48);
        items_ = app::dati().summaries();
        lv_obj_t* nw = list_.addRow(56);
        List::text(nw, &icone_22, AMBRA(), IC_PIU);
        List::text(nw, &font_22b, AMBRA(), "Nuovo messaggio", true);
        for (const auto& s : items_) {
            lv_obj_t* r = list_.addRow(74);
            const bool group = s.id.kind == app::ChatKind::GROUP;
            if (group) {
                lv_obj_t* a = box(r);
                lv_obj_set_size(a, 46, 46);
                radius(a, 12);
                fill(a, avatarColor(s.id.id));
                lv_obj_center(label(a, &icone_22, BIANCO(), IC_GRUPPO));
            } else {
                avatar(r, s.id.id, s.title, 46);
            }
            lv_obj_t* mid = column(r, 2);
            grow(mid);
            lv_obj_set_height(mid, LV_SIZE_CONTENT);
            std::string t = s.title;
            if (!group && s.title != std::to_string(s.id.id)) t += "  " + std::to_string(s.id.id);
            List::text(mid, &font_22b, BIANCO(), t);
            lv_obj_t* pv = row(mid, 6);
            lv_obj_set_size(pv, lv_pct(100), LV_SIZE_CONTENT);
            if (s.last.out) statusIcon(pv, s.last.status);
            std::string p = preview(s.last);
            if (group && !s.last.out && s.last.kind == app::MsgKind::TEXT) p = app::dati().nameOf(s.last.from) + ": " + p;
            lv_obj_t* pl = List::text(pv, &font_18,
                                      s.last.out && s.last.status == pn::Delivery::UNREACHABLE ? ROSSO() : GRIGIO3(),
                                      s.last.out && s.last.status == pn::Delivery::UNREACHABLE ? "Non raggiungibile" : p,
                                      true);
            (void)pl;
            lv_obj_t* right = column(r, 6);
            lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
            List::text(right, &mono_16, GRIGIO2(), whenShort(s.last.time));
            if (s.unread) {
                lv_obj_t* b = label(right, &font_16, NERO(), std::to_string(s.unread).c_str());
                fill(b, AMBRA());
                radius(b, 12);
                pad(b, 1, 8, 1, 8);
            }
        }
        list_.focus(sel_);
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (list_.key(k)) {
            sel_ = list_.focused();
            return true;
        }
        if (k == Key::OK) {
            sel_ = list_.focused();
            if (sel_ == 0) newMessage();
            else push(makeChat(items_[sel_ - 1].id));
            return true;
        }
        if (k == Key::CALL) {
            newMessage();
            return true;
        }
        if (k == Key::MENU && list_.focused() > 0) {
            const app::ChatId id = items_[list_.focused() - 1].id;
            ask("Cancellare la conversazione?", items_[list_.focused() - 1].title, {"Cancella", "Annulla"},
                [this, id](int i) {
                    if (i == 0) {
                        app::dati().deleteChat(id);
                        rebuild();
                    }
                });
            return true;
        }
        return false;
    }
    void newMessage() {
        push(makePickContact("Nuovo messaggio", [](uint16_t n) { push(makeChat({app::ChatKind::DIRECT, n})); }));
    }
    void onEvent(const app::Event& e) override {
        if (e.type == app::Ev::MESSAGE || e.type == app::Ev::DELIVERY || e.type == app::Ev::VOICE_READY) rebuild();
    }
    std::vector<Hint> hints() override { return {{"OK", "apri"}, {"CALL", "nuovo"}, {"MENU", "cancella"}}; }

private:
    List list_;
    std::vector<app::Dati::Summary> items_;
    int sel_ = 0;
};

Screen* makeChats() { return new Chats(); }

// ================================================================ chat

static app::ChatId g_openChat{app::ChatKind::DIRECT, 0xFFFF};
static Screen* g_chatScreen = nullptr;  // schermata della chat aperta

class ChatScreen : public Screen {
public:
    explicit ChatScreen(const app::ChatId& id) : id_(id) {}
    ~ChatScreen() override {
        if (g_chatScreen == this) {
            g_chatScreen = nullptr;
            g_openChat = {app::ChatKind::DIRECT, 0xFFFF};
        }
    }

    void build(lv_obj_t* root) override {
        g_openChat = id_;
        g_chatScreen = this;
        app::rete().markRead(id_);
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, 480, 412);
        if (id_.kind == app::ChatKind::GLOBAL) {
            header(col, IC_GLOBO, "Canale globale", "lo leggono tutti");
        } else if (id_.kind == app::ChatKind::GROUP) {
            pn::GroupInfo g;
            app::dati().group(id_.id, g);
            header(col, IC_GRUPPO, g.name.empty() ? "Gruppo" : g.name, std::to_string(g.members.size()) + " membri");
        } else {
            const std::string name = app::dati().nameOf(id_.id);
            header(col, nullptr, name, name == std::to_string(id_.id) ? "cifrato" : std::to_string(id_.id) + " · cifrato");
        }
        msgsBox_ = column(col, 8);
        lv_obj_set_size(msgsBox_, 480, 412 - 48 - 64);
        pad(msgsBox_, 8, 12, 8, 12);
        lv_obj_add_flag(msgsBox_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(msgsBox_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_t* comp = row(col, 0);
        lv_obj_set_size(comp, 480, 64);
        pad(comp, 5, 12, 7, 12);
        in_.create(comp, 456, 52, false, pn::MAX_TEXT_CHARS,
                   id_.kind == app::ChatKind::GLOBAL ? "Scrivi a tutti..." : "Scrivi un messaggio...");
        in_.setFocused(true);
        in_.setText(draft_);
        fillMessages();
    }

    void fillMessages() {
        lv_obj_clean(msgsBox_);
        bubbles_.clear();
        chat_ = app::dati().chat(id_);
        const size_t from = chat_.msgs.size() > 40 ? chat_.msgs.size() - 40 : 0;
        for (size_t i = from; i < chat_.msgs.size(); i++) bubbles_.push_back({bubble(chat_.msgs[i]), i});
        if (chat_.msgs.empty())
            label(msgsBox_, &font_20, GRIGIO2(),
                  id_.kind == app::ChatKind::DIRECT ? "Nessun messaggio. Scrivi qualcosa!" : "Nessun messaggio.");
        sel_ = -1;
        lv_obj_update_layout(msgsBox_);
        lv_obj_scroll_to_y(msgsBox_, LV_COORD_MAX, LV_ANIM_OFF);
    }

    // Riga larga quanto la chat: la nuvoletta va a destra (mia), a sinistra o al centro.
    lv_obj_t* line(lv_flex_align_t where) {
        lv_obj_t* r = row(msgsBox_, 0);
        lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_align(r, where, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        return r;
    }

    lv_obj_t* bubble(const app::Message& m) {
        if (m.kind == app::MsgKind::INFO)
            return label(line(LV_FLEX_ALIGN_CENTER), &font_16, GRIGIO2(), (m.text + "  " + whenShort(m.time)).c_str());
        const bool sos = m.kind == app::MsgKind::SOS;
        lv_obj_t* b = column(line(m.out ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START), 2);
        lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(b, 340, 0);
        pad(b, 8, 12, 6, 12);
        fill(b, sos ? ROSSO_SCURO() : (m.out ? AMBRA_SCURO() : GRIGIO1()));
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_border_color(b, AMBRA(), 0);
        if (!m.out && id_.kind != app::ChatKind::DIRECT)
            label(b, &font_16, AMBRA(), app::dati().nameOf(m.from).c_str());
        lv_obj_t* body = nullptr;
        switch (m.kind) {
            case app::MsgKind::VOICE:
                body = label(b, &font_20, BIANCO(), (std::string(IC_MICROFONO "  Vocale  ") + durationMS(m.voiceMs) +
                                                    "   " IC_PLAY).c_str());
                break;
            case app::MsgKind::POSITION:
                body = label(b, &font_20, BIANCO(),
                             (std::string(IC_PUNTO "  ") + (m.text.find(',') != std::string::npos
                                                                ? positionSummary(m.text)
                                                                : "Posizione " + m.text))
                                 .c_str());
                break;
            case app::MsgKind::SOS:
                body = label(b, &font_20, BIANCO(),
                             (std::string(IC_ATTENZIONE "  SOS  ") +
                              (m.text.find(',') != std::string::npos && m.text[0] != '-' ? positionSummary(m.text) : m.text))
                                 .c_str());
                break;
            default:
                body = label(b, &font_20, BIANCO(), m.text.c_str());
                break;
        }
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(body, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(body, 316, 0);
        lv_obj_t* meta = row(b, 4);
        lv_obj_set_size(meta, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        label(meta, &mono_16, m.out ? BIANCO() : GRIGIO3(), whenShort(m.time).c_str());
        if (m.out && id_.kind == app::ChatKind::DIRECT) statusIcon(meta, m.status);
        if (m.out && m.status == pn::Delivery::UNREACHABLE)
            label(b, &font_16, ROSSO(), "Non raggiungibile");
        return b;
    }

    void select(int s) {
        if (sel_ >= 0 && sel_ < int(bubbles_.size())) lv_obj_set_style_border_width(bubbles_[sel_].first, 0, 0);
        sel_ = s;
        if (sel_ >= 0 && sel_ < int(bubbles_.size())) {
            lv_obj_set_style_border_width(bubbles_[sel_].first, 3, 0);
            lv_obj_scroll_to_view(bubbles_[sel_].first, LV_ANIM_OFF);
        } else {
            sel_ = -1;
            lv_obj_scroll_to_y(msgsBox_, LV_COORD_MAX, LV_ANIM_OFF);
        }
        refreshBars();
    }

    void openSelected() {
        if (sel_ < 0) return;
        const app::Message& m = chat_.msgs[bubbles_[sel_].second];
        if (m.kind == app::MsgKind::VOICE && m.voiceFile) push(makeVoicePlayer(m.from, m.voiceFile, true));
        else if ((m.kind == app::MsgKind::POSITION || m.kind == app::MsgKind::SOS) && m.text.find(',') != std::string::npos &&
                 m.text[0] != '-')
            push(makePositionView(m.from, m.text, m.time));
    }

    bool onKey(Key k, bool pressed) override {
        if (!pressed) return k == Key::PTT;
        if (k == Key::UP) {
            if (bubbles_.empty()) return true;
            select(sel_ < 0 ? int(bubbles_.size()) - 1 : std::max(0, sel_ - 1));
            return true;
        }
        if (k == Key::DOWN) {
            select(sel_ < 0 ? -1 : (sel_ + 1 < int(bubbles_.size()) ? sel_ + 1 : -1));
            return true;
        }
        if (k == Key::OK && sel_ >= 0) {
            openSelected();
            return true;
        }
        if (k == Key::PTT && id_.kind == app::ChatKind::DIRECT) {
            push(makeWalkie(id_.id, true));
            return true;
        }
        if (k == Key::MENU) {
            menu();
            return true;
        }
        if ((k == Key::BACK || k == Key::END) && in_.symOpen()) return in_.key(k, pressed), true;
        // Tasti che non riguardano il testo: li gestisce l'interfaccia (indietro, home, volume).
        if (k == Key::BACK || k == Key::END || k == Key::VOLUP || k == Key::VOLDN || k == Key::CALL ||
            k == Key::PTT || k == Key::PWR)
            return false;
        const TextInput::Result r = in_.key(k, pressed);
        if (r == TextInput::SUBMIT || (k == Key::OK && sel_ < 0)) {
            send();
            return true;
        }
        if (r == TextInput::CHANGED) {
            draft_ = in_.text();
            if (sel_ >= 0) select(-1);
        }
        return true;  // gli altri tasti sono del campo di testo
    }

    void send() {
        const std::string t = in_.text();
        if (t.empty()) return;
        if (!app::rete().sendText(id_, t)) {
            toast("Non inviato", "Controlla che il numero esista", ROSSO());
            return;
        }
        in_.clear();
        draft_.clear();
        fillMessages();
    }

    void menu() {
        std::vector<std::string> opts;
        std::vector<int> acts;
        if (id_.kind == app::ChatKind::DIRECT) {
            opts = {"Chiedi la posizione", "Walkie-talkie", "Scheda contatto", "Cancella conversazione"};
            acts = {0, 1, 2, 3};
        } else if (id_.kind == app::ChatKind::GROUP) {
            opts = {"Membri del gruppo", "Cancella conversazione"};
            acts = {4, 3};
        } else {
            opts = {"Cancella i messaggi"};
            acts = {3};
        }
        ask("Opzioni", "", opts, [this, acts](int i) {
            if (i < 0) return;
            switch (acts[i]) {
                case 0:
                    app::rete().requestPosition(id_.id);
                    toast("Posizione chiesta", app::dati().nameOf(id_.id) + " deve accettare");
                    fillMessages();
                    break;
                case 1: push(makeWalkie(id_.id)); break;
                case 2: push(makeContact(id_.id)); break;
                case 3:
                    app::dati().deleteChat(id_);
                    fillMessages();
                    break;
                case 4: {
                    pn::GroupInfo g;
                    app::dati().group(id_.id, g);
                    std::string names;
                    for (auto m : g.members) names += (names.empty() ? "" : ", ") + app::dati().nameOf(m);
                    ask(g.name, names, {"OK"}, nullptr);
                    break;
                }
            }
        });
    }

    void onEvent(const app::Event& e) override {
        if (!(e.chat == id_)) return;
        if (e.type == app::Ev::MESSAGE || e.type == app::Ev::DELIVERY || e.type == app::Ev::POSITION ||
            e.type == app::Ev::VOICE_READY || e.type == app::Ev::VOICE_STATE || e.type == app::Ev::SOS) {
            if (e.type == app::Ev::MESSAGE) {
                app::rete().markRead(id_);
                if (app::dati().settings().vibration) scheda::vibrate(60);
            }
            fillMessages();
        }
    }

    std::vector<Hint> hints() override {
        if (in_.symOpen()) return {{"NAV", "scegli"}, {"OK", "inserisci"}, {"BACK", "chiudi"}};
        if (sel_ >= 0) return {{"OK", "apri"}, {"GIÙ", "scrivi"}, {"MENU", "opzioni"}};
        if (id_.kind == app::ChatKind::DIRECT) return {{"ENTER", "invia"}, {"SYM", "accenti"}, {"PTT", "vocale"}};
        return {{"ENTER", "invia"}, {"SYM", "accenti"}, {"MENU", "opzioni"}};
    }

    const app::ChatId& id() const { return id_; }

private:
    app::ChatId id_;
    app::Chat chat_;
    lv_obj_t* msgsBox_ = nullptr;
    std::vector<std::pair<lv_obj_t*, size_t>> bubbles_;
    TextInput in_;
    std::string draft_;
    int sel_ = -1;
};

Screen* makeChat(const app::ChatId& id) { return new ChatScreen(id); }

bool isChatOpen(const app::ChatId& id) {
    return g_chatScreen && top() == g_chatScreen && g_openChat == id;
}

}  // namespace ui
