// SOS: attivazione (tenendo premuto), SOS in corso, SOS ricevuto da un contatto.
#include <cstdio>
#include <set>

#include "app/dati.h"
#include "app/rete.h"
#include "esp_timer.h"
#include "scheda/scheda.h"
#include "ui/schermate.h"
#include "ui/widget.h"

namespace ui {

namespace {
constexpr int64_t HOLD_MS = 3000;
int64_t nowMs() { return esp_timer_get_time() / 1000; }

// Barra del tasto tenuto premuto. Carta: bianca col bordo nero e l'arancio dentro il bordo.
lv_obj_t* holdBar(lv_obj_t* parent) {
    lv_obj_t* b = lv_bar_create(parent);
    lv_obj_remove_style_all(b);
    const bool carta = temaChiaro();
    const int h = carta ? PX(16, 10) : PX(12, 8);
    lv_obj_set_size(b, PX(400, 260), h);
    lv_obj_set_style_bg_color(b, carta ? NERO() : GRIGIO1(), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, h / 2, 0);
    if (carta) {
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_border_color(b, INCHIOSTRO(), 0);
        lv_obj_set_style_pad_all(b, 2, 0);
    }
    lv_obj_set_style_bg_color(b, ROSSO(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, carta ? h / 2 - 2 : PX(6, 4), LV_PART_INDICATOR);
    lv_bar_set_range(b, 0, int32_t(HOLD_MS));
    return b;
}

lv_obj_t* wrapped(lv_obj_t* parent, const lv_font_t* f, lv_color_t c, const std::string& s, int w = PX(440, 300)) {
    lv_obj_t* l = label(parent, f, c, s.c_str());
    lv_obj_set_width(l, w);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}
#if TDECK
// Col firmware vecchio della tastiera non si puo' tenere premuto END (SYM+H): si annulla tenendo
// premuto il clic del trackball, come per attivare.
bool rawKeys() { return scheda::tastiera().rawMode(); }

// Dov'e' chi ha mandato l'SOS, come in SosReceived::build(): "a 857 m verso sud-ovest  (±12 m)" con la mia
// posizione, se no le sue coordinate. acc = 255: precisione non nota.
std::string whereSos(int32_t lat, int32_t lon, unsigned acc) {
    std::string where;
    const pn::Position me = app::rete().lastPosition();
    if (me.valid) {
        double d, b;
        app::distanceBearing(me.latE7, me.lonE7, lat, lon, d, b);
        static const char* dirs[] = {"nord", "nord-est", "est", "sud-est", "sud", "sud-ovest", "ovest", "nord-ovest"};
        char buf[64];
        if (d < 1000) snprintf(buf, sizeof(buf), "a %.0f m verso %s", d, dirs[int((b + 22.5) / 45) % 8]);
        else snprintf(buf, sizeof(buf), "a %.1f km verso %s", d / 1000, dirs[int((b + 22.5) / 45) % 8]);
        where = buf;
        for (auto& c : where)
            if (c == '.') c = ',';
    } else {
        where = app::formatLatLon(lat, lon);
    }
    if (acc < 255) where += "  (±" + std::to_string(acc) + " m)";
    return where;
}
#endif
}  // namespace

// ================================================================ il mio SOS

class SosActive : public Screen {
public:
    explicit SosActive(bool start) {
        if (start && !app::rete().sosActive()) activate();
    }
    void activate() {
        app::rete().startSos();
        app::rete().wantGnss(600);
        scheda::vibratePattern(3, 200, 100);
        scheda::ledBlink(80, 0, 0, 200, 800, 0);
    }
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, PX(10, 4));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        pad(col, PX(12, 4), PX(20, 10), PX(10, 4), PX(20, 10));
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        const size_t nContacts = app::dati().contacts().size();
        if (!app::rete().sosActive()) {
            // Spiegazione e attivazione tenendo premuto OK.
            lv_obj_t* c = box(col);
            lv_obj_set_size(c, PX(120, 50), PX(120, 50));
            radius(c, LV_RADIUS_CIRCLE);
            fill(c, ROSSO_SCURO());
            lv_obj_center(label(c, &font_32b, SU_COLORE(), "SOS"));
            wrapped(col, PX(&font_22, &font_20), BIANCO(),
                    "Manda un allarme con la tua posizione a tutti i contatti della rubrica (" +
                        std::to_string(nContacts) + "). Si ripete ogni 2 minuti finché non lo annulli.");
            if (!nContacts) wrapped(col, &font_20, ROSSO(), "La rubrica è vuota: nessuno riceverà l'allarme!");
            // Carta: arancio come la barra che si riempie (l'arancio e' per le cose urgenti).
            wrapped(col, &font_22b, colore(0xFFB000, 0xC04000),
                    TXT("Tieni premuto OK per 3 secondi", "Tieni premuto il trackball per 3 secondi"));
            bar_ = holdBar(col);
            wrapped(col, &font_16, colore(0x808080, 0x404040),
                    TXT("Scorciatoia da qualsiasi schermata: VOL+ e VOL- insieme per 3 secondi",
                        rawKeys() ? "Scorciatoia da qualsiasi schermata: i due SHIFT insieme per 3 secondi"
                                  : "Con questa tastiera l'SOS si attiva solo da qui"));
            return;
        }
        // SOS in corso.
        lv_obj_t* banner = row(col, PX(12, 8));
        lv_obj_set_size(banner, PX(440, 300), PX(70, 44));
        fill(banner, ROSSO_SCURO());
        radius(banner, PX(12, 8));
        pad(banner, 0, PX(16, 10), 0, PX(16, 10));
        label(banner, &icone_44, SU_COLORE(), IC_ATTENZIONE);
        lv_obj_t* bt = column(banner, 0);
        grow(bt);
        lv_obj_set_height(bt, LV_SIZE_CONTENT);
        label(bt, &font_26b, SU_COLORE(), "SOS ATTIVO");
        label(bt, &font_18, SU_COLORE(), "si ripete ogni 2 minuti");
        pos_ = label(col, &font_18, GRIGIO3(), "");
        updatePos();
        // Contatti che hanno ricevuto.
        const auto contacts = app::rete().sosContacts();
        const auto acked = app::rete().sosAcked();
        lv_obj_t* lst = column(col, PX(4, 1));
        lv_obj_set_size(lst, PX(440, 300), PX(160, 54));
        lv_obj_add_flag(lst, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(lst, LV_SCROLLBAR_MODE_OFF);
        // E' l'unico segno che l'allarme e' arrivato: in Carta grigio scuro, non il grigio chiaro.
        label(lst, &font_16, colore(0x808080, 0x404040),
              ("Ricevuto da " + std::to_string(acked.size()) + " su " + std::to_string(contacts.size())).c_str());
        for (auto n : contacts) {
            lv_obj_t* r = row(lst, PX(10, 6));
            lv_obj_set_size(r, lv_pct(100), PX(30, 18));
            const bool ok = acked.count(n);
            label(r, &icone_18, ok ? VERDE() : colore(0x808080, 0x404040), ok ? IC_OK : IC_OROLOGIO);
            lv_obj_t* l = label(r, &font_20, ok ? BIANCO() : GRIGIO3(), app::dati().nameOf(n).c_str());
            grow(l);
        }
        label(col, &font_20, colore(0xFFB000, 0xC04000),
              TXT("Per annullare tieni premuto END per 3 secondi",
                  rawKeys() ? "Per annullare tieni SYM+H per 3 secondi"
                            : "Per annullare tieni il trackball 3 secondi"));
        bar_ = holdBar(col);
    }
    void updatePos() {
        if (!pos_) return;
        const pn::Position p = app::rete().lastPosition();
        const scheda::Fix f = scheda::gnss().fix();
        if (p.valid)
            lv_label_set_text(pos_, (std::string(IC_GPS " ") + app::formatLatLon(p.latE7, p.lonE7) + "  ±" +
                                     std::to_string(p.accM) + " m" + (f.valid ? "" : " (ultima nota)"))
                                        .c_str());
        else
#if TDECK
            // T-Deck: quanti satelliti sente (gli usati sono 0 per forza finche' non c'e' la posizione).
            lv_label_set_text(pos_, faseGps() == FaseGps::MUTO
                                        ? IC_GPS " il GPS non risponde"
                                        : (std::string(IC_GPS " cerco i satelliti... ne sento ") +
                                           std::to_string(f.conSegnale))
                                              .c_str());
#else
            lv_label_set_text(pos_, (std::string(IC_GPS " cerco il GPS... ") + std::to_string(f.sats) + " satelliti").c_str());
#endif
    }
    void tick() override {
        if (holdAt_ && bar_) {
            const int64_t held = nowMs() - holdAt_;
            lv_bar_set_value(bar_, int32_t(std::min<int64_t>(held, HOLD_MS)), LV_ANIM_OFF);
            if (held >= HOLD_MS) {
                holdAt_ = 0;
                if (app::rete().sosActive()) {
                    app::rete().stopSos();
                    scheda::ledOff();
                    scheda::vibrate(300);
                    toast("SOS annullato", "I contatti ricevono l'annullamento", VERDE());
                    pop();
                    return;
                }
                activate();
                rebuild();
                return;
            }
        }
        if (++ticks_ % 8 == 0) updatePos();
    }
    bool onKey(Key k, bool pressed) override {
        const bool active = app::rete().sosActive();
        const Key hold = active ? TXT(Key::END, rawKeys() ? Key::END : Key::OK) : Key::OK;
        if (k == hold) {
            holdAt_ = pressed ? nowMs() : 0;
            if (!pressed && bar_) lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
            return true;
        }
        return false;
    }
    void onEvent(const app::Event& e) override {
        if (e.type == app::Ev::SOS_ACK) {
            scheda::vibrate(100);
            rebuild();
        }
    }
    std::vector<Hint> hints() override {
        if (app::rete().sosActive()) return {{TXT("END", rawKeys() ? "END" : "OK"), "tieni premuto: annulla"}, {"BACK", "indietro"}};
        return {{"OK", "tieni premuto: attiva"}, {"BACK", "indietro"}};
    }
    bool capturesEnd() const override { return app::rete().sosActive(); }
#if TDECK
    bool vuoleGps() const override { return app::rete().sosActive(); }
#endif

private:
    lv_obj_t* bar_ = nullptr;
    lv_obj_t* pos_ = nullptr;
    int64_t holdAt_ = 0;
    int ticks_ = 0;
};

Screen* makeSosActive(bool start) { return new SosActive(start); }

// ================================================================ SOS ricevuto

namespace {
std::set<uint16_t> g_sosSeen;   // SOS gia' visti (niente allarme a ogni ripetizione)
std::set<uint16_t> g_sosOpen;   // schermate aperte
int g_sosSounding = 0;           // schermate aperte la cui sirena non e' ancora stata zittita
}  // namespace

bool sosShouldAlert(uint16_t from) { return !g_sosSeen.count(from) && !g_sosOpen.count(from); }
void sosCancelled(uint16_t from) { g_sosSeen.erase(from); }
// Solo finche' la sirena suona davvero: se finisce da sola con la schermata ancora aperta, la
// sveglia non deve restare ferma per sempre.
bool sosAlerting() { return g_sosSounding > 0 && scheda::audio().busy(); }

class SosReceived : public Screen {
public:
    // La sirena parte appena prima di aprire la schermata (ui.cpp, Ev::SOS).
    explicit SosReceived(uint16_t from) : from_(from) {
        g_sosOpen.insert(from);
        g_sosSounding++;
    }
    ~SosReceived() override {
        g_sosOpen.erase(from_);
        g_sosSeen.insert(from_);
        silence();
    }
    void silence() {
        if (sounding_) {
            sounding_ = false;
            g_sosSounding--;
        }
        scheda::audio().stop();
        scheda::ledOff();
    }
    bool lastSos(app::Message& out) {
        const app::Chat c = app::dati().chat({app::ChatKind::DIRECT, from_});
        for (auto it = c.msgs.rbegin(); it != c.msgs.rend(); ++it)
            if (it->kind == app::MsgKind::SOS && !it->out) {
                out = *it;
                return true;
            }
        return false;
    }
    void build(lv_obj_t* root) override {
        app::rete().wantGnss(300);  // per calcolare distanza e direzione
        lv_obj_t* col = column(root, PX(10, 2));
        lv_obj_set_size(col, W_SCHERMO, H_SCHERMO);
        fill(col, ROSSO_SCURO());
        pad(col, PX(24, 6), PX(20, 10), PX(16, 4), PX(20, 10));
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        // Tutto bianco sul rosso, in tutti e due i temi.
        label(col, &icone_44, SU_COLORE(), IC_ATTENZIONE);
        label(col, &font_48b, SU_COLORE(), "SOS");
        const std::string name = app::dati().nameOf(from_);
#if TDECK
        wrapped(col, &font_32b, SU_COLORE(), name);  // il nome si legge tutto, anche su due righe
#else
        label(col, &font_32b, SU_COLORE(), name.c_str());
#endif
        if (name != std::to_string(from_)) label(col, &mono_20, SU_COLORE(), std::to_string(from_).c_str());
        app::Message m;
        hasPos_ = false;
#if TDECK
        where_ = nullptr;
#endif
        if (lastSos(m)) {
            data_ = m.text;
            time_ = m.time;
            long lat, lon;
            int alt;
            unsigned acc, age, bat = 255;
            const int n = sscanf(m.text.c_str(), "%ld,%ld,%d,%u,%u,%u", &lat, &lon, &alt, &acc, &age, &bat);
            std::string where;
            if (n >= 2) {
                hasPos_ = true;
                const pn::Position me = app::rete().lastPosition();
                if (me.valid) {
                    double d, b;
                    app::distanceBearing(me.latE7, me.lonE7, int32_t(lat), int32_t(lon), d, b);
                    static const char* dirs[] = {"nord", "nord-est", "est", "sud-est", "sud", "sud-ovest", "ovest",
                                                 "nord-ovest"};
                    char buf[64];
                    if (d < 1000) snprintf(buf, sizeof(buf), "a %.0f m verso %s", d, dirs[int((b + 22.5) / 45) % 8]);
                    else snprintf(buf, sizeof(buf), "a %.1f km verso %s", d / 1000, dirs[int((b + 22.5) / 45) % 8]);
                    where = buf;
                    for (auto& c : where)
                        if (c == '.') c = ',';
                } else {
                    where = app::formatLatLon(int32_t(lat), int32_t(lon));
                }
                if (n >= 4 && acc < 255) where += "  (±" + std::to_string(acc) + " m)";
            } else {
                sscanf(m.text.c_str(), "-,%u", &bat);
                where = "posizione non disponibile (GPS senza segnale)";
            }
            wrapped(col, &font_22b, SU_COLORE(), where);
#if TDECK
            // T-Deck: distanza e direzione seguono la mia posizione, anche quando arriva dopo (tick).
            if (n >= 2) {
                where_ = lv_obj_get_child(col, -1);
                lat_ = int32_t(lat);
                lon_ = int32_t(lon);
                acc_ = n >= 4 ? acc : 255;
            }
#endif
            std::string meta = "ultimo aggiornamento " + timeHM(m.time);
            if (bat <= 100) meta += " · batteria " + std::to_string(bat) + "%";
            label(col, &font_20, SU_COLORE(), meta.c_str());
        }
        lv_obj_t* sp = box(col);
        grow(sp);
        lv_obj_set_width(sp, 10);
        wrapped(col, &font_20, SU_COLORE(),
                std::string(hasPos_ ? "OK: silenzia e mostra la posizione\n" : "OK: silenzia\n") +
                    TXT("CALL: scrivi · PTT: parla · BACK: chiudi",
                        std::string(tasto("CALL")) + ": scrivi · " + tasto("PTT") + ": parla · " + tasto("BACK") +
                            ": chiudi"));
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return k == Key::PTT;
        switch (k) {
            case Key::OK:
                silence();
                if (hasPos_) push(makePositionView(from_, data_, time_));
                return true;
            case Key::CALL:
                silence();
                replace(makeChat({app::ChatKind::DIRECT, from_}));
                return true;
            case Key::PTT:
                silence();
                replace(makeWalkie(from_, true));
                return true;
            case Key::BACK:
            case Key::END:
                pop();
                return true;
            default:
                silence();  // qualsiasi tasto ferma la sirena
                return true;
        }
    }
    void onEvent(const app::Event& e) override {
        if (e.type == app::Ev::SOS && e.from == from_) {
            if (!e.a) pop();  // annullato
            else rebuild();   // posizione aggiornata
        }
    }
    bool fullscreen() const override { return true; }
    bool capturesEnd() const override { return true; }
#if TDECK
    // Ogni 2 secondi distanza e direzione con la mia posizione di adesso (e quando arriva la prima).
    void tick() override {
        if (!where_ || ++ticks_ % 8) return;
        const std::string w = whereSos(lat_, lon_, acc_);
        if (w != lv_label_get_text(where_)) lv_label_set_text(where_, w.c_str());
    }
    bool vuoleGps() const override { return true; }
#endif

private:
    uint16_t from_;
    bool sounding_ = true;  // conta in g_sosSounding finche' non la si zittisce
    bool hasPos_ = false;
    std::string data_;
    uint32_t time_ = 0;
#if TDECK
    lv_obj_t* where_ = nullptr;  // dov'e' (distanza e direzione, o le coordinate)
    int32_t lat_ = 0, lon_ = 0;
    unsigned acc_ = 255;
    int ticks_ = 0;
#endif
};

Screen* makeSosReceived(uint16_t from) { return new SosReceived(from); }

}  // namespace ui
