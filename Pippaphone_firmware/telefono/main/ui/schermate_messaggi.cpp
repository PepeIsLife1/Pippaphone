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
// Su che fondo sta una scritta: il bianco della pagina (o la nuvoletta dell'altro), la mia nuvoletta,
// quella rossa dell'SOS.
enum class Fondo { PAGINA, MIA, SOS };

// Colore di una scritta secondo il fondo. Nello scuro e sulla pagina e' sempre quello del tema; in
// Carta sul nero della mia nuvoletta ci vuole la versione chiara (suNero), sul rosso il bianco.
lv_color_t suFondo(lv_color_t tema, uint32_t suNero, Fondo f) {
    if (!temaChiaro() || f == Fondo::PAGINA) return tema;
    return lv_color_hex(f == Fondo::SOS ? 0xFFFFFF : suNero);
}

// Testo poco importante (orari, avvisi, elenco vuoto): nello scuro GRIGIO2 come prima, in Carta il
// grigio scuro dei testi secondari (il #808080 sul bianco al sole si legge male).
lv_color_t tenue() { return colore(0x808080, 0x404040); }

// Icona e colore dello stato di un messaggio inviato.
void statusIcon(lv_obj_t* parent, pn::Delivery d, Fondo f = Fondo::PAGINA) {
    const char* icon = nullptr;
    lv_color_t c = GRIGIO2();
    switch (d) {
        case pn::Delivery::PENDING: icon = IC_OROLOGIO; c = suFondo(GRIGIO2(), 0x808080, f); break;
        // Carta: spunta grigia per trasmesso, nera per consegnato (scure tutte e due non si distinguono);
        // sul nero della mia nuvoletta grigia contro bianca, e il grigio non e' quello dell'ora accanto.
        case pn::Delivery::SENT: icon = IC_OK; c = suFondo(colore(0xC0C0C0, 0x808080), 0x808080, f); break;
        case pn::Delivery::DELIVERED: icon = IC_OK; c = suFondo(BIANCO(), 0xFFFFFF, f); break;
        case pn::Delivery::READ: icon = IC_DOPPIO_OK; c = suFondo(BLU(), 0x40A0FF, f); break;
        case pn::Delivery::UNREACHABLE: icon = IC_ATTENZIONE; c = suFondo(ROSSO(), 0xFF8000, f); break;
        case pn::Delivery::RETRYING: icon = IC_OROLOGIO; c = suFondo(AMBRA(), 0x40A0FF, f); break;
    }
    if (!icon) return;
    lv_obj_t* l = label(parent, &icone_18, c, icon);
    // Carta, sulla riga scelta (blu) dell'elenco: bianca, se no l'icona blu sparisce.
    if (temaChiaro()) lv_obj_set_style_text_color(l, SU_COLORE(), LV_STATE_CHECKED);
}

// Stato CHECKED su un oggetto e su tutto quello che ha dentro (in Carta e' la nuvoletta scelta).
void scelta(lv_obj_t* o, bool on) {
    if (on) lv_obj_add_state(o, LV_STATE_CHECKED);
    else lv_obj_clear_state(o, LV_STATE_CHECKED);
    const uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) scelta(lv_obj_get_child(o, int32_t(i)), on);
}

void scritteBianche(lv_obj_t* o) {
    if (lv_obj_check_type(o, &lv_label_class)) lv_obj_set_style_text_color(o, SU_COLORE(), LV_STATE_CHECKED);
    const uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) scritteBianche(lv_obj_get_child(o, int32_t(i)));
}

// Carta: la nuvoletta scelta e' piena di blu e tutte le scritte dentro diventano bianche, come le
// righe scelte degli elenchi.
void coloriScelta(lv_obj_t* b) {
    lv_obj_set_style_bg_color(b, AMBRA(), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(b, AMBRA(), LV_STATE_CHECKED);
    scritteBianche(b);
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
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_PERSONA, title_);
        lv_obj_t* r = row(col, PX(10, 6));
        lv_obj_set_size(r, W_SCHERMO, PX(70, 40));
        pad(r, PX(8, 4), PX(14, 8), PX(8, 4), PX(14, 8));
        label(r, &font_20, GRIGIO3(), "Numero");
        num_.create(r, PX(330, 236), PX(54, 32), false, 5, "scrivi o scegli sotto");
        if (temaChiaro()) grow(num_.obj());  // Carta: il campo arriva al margine, dritto con l'elenco sotto
        num_.setDigitsOnly(true);
        num_.setFocused(true);
        contacts_ = app::dati().contacts();
        list_.create(col, W_SCHERMO, H_AREA - H_TITOLO - PX(70, 40));
        for (const auto& c : contacts_) {
            lv_obj_t* row2 = list_.addRow(PX(58, 36));
            avatar(row2, c.number, c.name, PX(40, 26));
            List::text(row2, &font_22b, BIANCO(), c.name, true);
            List::text(row2, &mono_16, GRIGIO3(), std::to_string(c.number));
        }
        if (contacts_.empty()) label(list_.obj(), &font_20, tenue(), "  La rubrica è vuota");
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
#if TDECK
    const TextInput* input() const override { return &num_; }
#endif

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
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        const uint16_t unread = app::dati().unreadTotal();
        header(col, nullptr, "Messaggi", unread ? std::to_string(unread) + " non letti" : "");
        list_.create(col, W_SCHERMO, H_AREA - H_TITOLO);
        items_ = app::dati().summaries();
        lv_obj_t* nw = list_.addRow(PX(56, 34));
        List::text(nw, &icone_22, AMBRA(), IC_PIU);
        List::text(nw, &font_22b, AMBRA(), "Nuovo messaggio", true);
        for (const auto& s : items_) {
            lv_obj_t* r = list_.addRow(PX(74, 42));
            const bool group = s.id.kind == app::ChatKind::GROUP;
            if (group) {
                lv_obj_t* a = box(r);
                lv_obj_set_size(a, PX(46, 30), PX(46, 30));
                radius(a, PX(12, 8));
                fill(a, avatarColor(s.id.id));
                if (temaChiaro()) {  // Carta, riga scelta (blu): anello bianco, se no un #004080 sparisce nel blu
                    lv_obj_set_style_border_width(a, 2, LV_STATE_CHECKED);
                    lv_obj_set_style_border_color(a, SU_COLORE(), LV_STATE_CHECKED);
                }
                lv_obj_center(label(a, &icone_22, SU_COLORE(), IC_GRUPPO));
            } else {
                avatar(r, s.id.id, s.title, PX(46, 30));
            }
            lv_obj_t* mid = column(r, 2);
            grow(mid);
            lv_obj_set_height(mid, LV_SIZE_CONTENT);
            std::string t = s.title;
            if (!group && s.title != std::to_string(s.id.id)) t += "  " + std::to_string(s.id.id);
            rigaPiena(List::text(mid, &font_22b, BIANCO(), t));
            lv_obj_t* pv = row(mid, PX(6, 4));
            lv_obj_set_size(pv, lv_pct(100), LV_SIZE_CONTENT);
            if (s.last.out) statusIcon(pv, s.last.status);
            std::string p = preview(s.last);
            if (group && !s.last.out && s.last.kind == app::MsgKind::TEXT) p = app::dati().nameOf(s.last.from) + ": " + p;
            const bool fail = s.last.out && s.last.status == pn::Delivery::UNREACHABLE;
            const bool retry = s.last.out && s.last.status == pn::Delivery::RETRYING;
            lv_obj_t* pl = List::text(pv, &font_18, fail ? ROSSO() : retry ? AMBRA() : GRIGIO3(),
                                      fail ? "Non raggiungibile" : retry ? "Non raggiungibile: riprovo" : p, true);
            (void)pl;
            lv_obj_t* right = column(r, PX(6, 3));
            lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
            List::text(right, &mono_16, tenue(), whenShort(s.last.time));
            if (s.unread) {
                lv_obj_t* b = label(right, &font_16, NERO(), std::to_string(s.unread).c_str());
                fill(b, AMBRA());
                radius(b, PX(12, 8));
                pad(b, 1, PX(8, 5), 1, PX(8, 5));
                if (temaChiaro()) {  // Carta, sulla riga scelta (blu): bianco col numero blu
                    lv_obj_set_style_bg_color(b, SU_COLORE(), LV_STATE_CHECKED);
                    lv_obj_set_style_text_color(b, AMBRA(), LV_STATE_CHECKED);
                }
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
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
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
        msgsBox_ = column(col, PX(8, 5));
        lv_obj_set_size(msgsBox_, W_SCHERMO, H_AREA - H_TITOLO - PX(64, 40));
        pad(msgsBox_, PX(8, 4), PX(12, 6), PX(8, 4), PX(12, 6));
        lv_obj_add_flag(msgsBox_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(msgsBox_, LV_SCROLLBAR_MODE_OFF);
        lv_obj_t* comp = row(col, 0);
        lv_obj_set_size(comp, W_SCHERMO, PX(64, 40));
        pad(comp, PX(5, 3), PX(12, 6), PX(7, 3), PX(12, 6));
        in_.create(comp, PX(456, 308), PX(52, 34), false, pn::MAX_TEXT_CHARS,
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
            label(msgsBox_, &font_20, tenue(),
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
        const bool carta = temaChiaro();
        if (m.kind == app::MsgKind::INFO) {
            lv_obj_t* l =
                label(line(LV_FLEX_ALIGN_CENTER), &font_16, tenue(), (m.text + "  " + whenShort(m.time)).c_str());
            if (carta) {  // scelta: una pillola blu
                pad(l, 1, PX(8, 5), 1, PX(8, 5));
                radius(l, PX(12, 8));
                coloriScelta(l);
            }
            return l;
        }
        const bool sos = m.kind == app::MsgKind::SOS;
        // Scuro: la mia ambra scura, la sua grigia. Carta: la mia piena di nero col testo bianco, la sua
        // bianca col bordo nero. L'SOS rossa in tutti e due.
        const Fondo fondo = sos ? Fondo::SOS : (m.out ? Fondo::MIA : Fondo::PAGINA);
        const lv_color_t pieno = sos ? ROSSO_SCURO() : (m.out ? AMBRA_SCURO() : SUPERFICIE());
        // Carta: bordo sempre di 2 pixel (nero per la sua, del colore del fondo per le altre), cosi'
        // quando una nuvoletta e' scelta cambiano solo i colori e niente si sposta. La nuvoletta e'
        // larga 4 pixel in piu': il testo va a capo come nello scuro.
        const int bordo = carta ? 2 : 0;
        lv_obj_t* b = column(line(m.out ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START), 2);
        lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(b, PX(340, 236) + 2 * bordo, 0);
        pad(b, PX(8, 4), PX(12, 8), PX(6, 3), PX(12, 8));
        fill(b, pieno);
        lv_obj_set_style_radius(b, PX(14, 9), 0);
        lv_obj_set_style_border_color(b, AMBRA(), 0);
        if (carta) {
            lv_obj_set_style_border_width(b, bordo, 0);
            lv_obj_set_style_border_color(b, fondo == Fondo::PAGINA ? INCHIOSTRO() : pieno, 0);
        }
        if (!m.out && id_.kind != app::ChatKind::DIRECT)
            label(b, &font_16, suFondo(AMBRA(), 0x40A0FF, fondo), app::dati().nameOf(m.from).c_str());
        const lv_color_t testo = suFondo(BIANCO(), 0xFFFFFF, fondo);
        lv_obj_t* body = nullptr;
        switch (m.kind) {
            case app::MsgKind::VOICE:
                body = label(b, &font_20, testo, (std::string(IC_MICROFONO "  Vocale  ") + durationMS(m.voiceMs) +
                                                 "   " IC_PLAY).c_str());
                break;
            case app::MsgKind::POSITION:
                body = label(b, &font_20, testo,
                             (std::string(IC_PUNTO "  ") + (m.text.find(',') != std::string::npos
                                                                ? positionSummary(m.text)
                                                                : "Posizione " + m.text))
                                 .c_str());
                break;
            case app::MsgKind::SOS:
                body = label(b, &font_20, testo,
                             (std::string(IC_ATTENZIONE "  SOS  ") +
                              (m.text.find(',') != std::string::npos && m.text[0] != '-' ? positionSummary(m.text) : m.text))
                                 .c_str());
                break;
            default:
                body = label(b, &font_20, testo, m.text.c_str());
                break;
        }
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(body, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(body, PX(316, 220), 0);
        lv_obj_t* meta = row(b, PX(4, 3));
        lv_obj_set_size(meta, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        // Carta: l'ora grigio chiaro sul nero della mia nuvoletta, grigio scuro sul bianco della sua.
        label(meta, &mono_16, suFondo(m.out ? BIANCO() : GRIGIO3(), 0xC0C0C0, fondo), whenShort(m.time).c_str());
        if (m.out && id_.kind == app::ChatKind::DIRECT) statusIcon(meta, m.status, fondo);
        if (m.out && m.status == pn::Delivery::UNREACHABLE)
            label(b, &font_16, suFondo(ROSSO(), 0xFF8000, fondo), "Non raggiungibile");
        if (m.out && m.status == pn::Delivery::RETRYING)  // fra 3,5 minuti
            label(b, &font_16, suFondo(AMBRA(), 0x40A0FF, fondo), "Non raggiungibile: riprovo");
        if (carta) coloriScelta(b);
        return b;
    }

    // Nuvoletta scelta con su/giu': nello scuro col bordo ambra (com'era), in Carta piena di blu.
    void mark(int i, bool on) {
        if (i < 0 || i >= int(bubbles_.size())) return;
        if (temaChiaro()) scelta(bubbles_[i].first, on);
        else lv_obj_set_style_border_width(bubbles_[i].first, on ? PX(3, 2) : 0, 0);
    }

    void select(int s) {
        mark(sel_, false);
        sel_ = s;
        if (sel_ >= 0 && sel_ < int(bubbles_.size())) {
            mark(sel_, true);
            // Si fa vedere la riga della nuvoletta, che e' lei a stare nella chat che scorre. Chiesto
            // alla nuvoletta non succedeva niente (la sua riga non scorre): la chat restava ferma in
            // fondo e una nuvoletta vecchia scelta restava fuori dallo schermo.
            lv_obj_scroll_to_view(lv_obj_get_parent(bubbles_[sel_].first), LV_ANIM_OFF);
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
        // Griglia dei simboli aperta: frecce, OK, BACK ed END sono suoi. OK inserisce il simbolo e
        // basta (prima mandava anche il messaggio, e su/giu' sceglievano i messaggi sopra).
        if (in_.symOpen() && k != Key::VOLUP && k != Key::VOLDN && k != Key::PWR && k != Key::PTT) {
            if (in_.key(k, pressed) == TextInput::CHANGED) {
                draft_ = in_.text();
                if (sel_ >= 0) select(-1);
            }
            return true;
        }
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
#if TDECK
    const TextInput* input() const override { return &in_; }
#endif

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
