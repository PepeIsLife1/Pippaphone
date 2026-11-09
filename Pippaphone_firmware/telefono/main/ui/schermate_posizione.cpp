// Posizione: la mia, richieste in arrivo (con conferma) e posizione di un altro telefono.
#include <cmath>
#include <cstdio>

#include "app/dati.h"
#include "app/rete.h"
#include "scheda/scheda.h"
#include "ui/schermate.h"
#include "ui/widget.h"
#if TDECK
#include "ui/mappa.h"
#endif

namespace ui {

namespace {
struct PosData {
    bool valid = false;
    int32_t lat = 0, lon = 0;
    int alt = 0;
    unsigned acc = 255, age = 0;
    int battery = -1;
};

// "lat,lon,alt,acc,age[,batteria]" (vedi Rete::onPosition e Rete::onSos).
PosData parse(const std::string& s) {
    PosData p;
    long lat, lon;
    int alt, bat;
    unsigned acc, age;
    const int n = sscanf(s.c_str(), "%ld,%ld,%d,%u,%u,%d", &lat, &lon, &alt, &acc, &age, &bat);
    if (n >= 2) {
        p.valid = true;
        p.lat = int32_t(lat);
        p.lon = int32_t(lon);
    }
    if (n >= 3) p.alt = alt;
    if (n >= 4) p.acc = acc;
    if (n >= 5) p.age = age;
    if (n >= 6) p.battery = bat;
    return p;
}

std::string comma(std::string s) {
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string distanceText(double m) {
    char b[24];
    if (m < 1000) snprintf(b, sizeof(b), "%.0f m", m);
    else if (m < 10000) snprintf(b, sizeof(b), "%.2f km", m / 1000);
    else snprintf(b, sizeof(b), "%.1f km", m / 1000);
    return comma(b);
}

const char* direction(double bearing) {
    static const char* dirs[] = {"nord", "nord-est", "est", "sud-est", "sud", "sud-ovest", "ovest", "nord-ovest"};
    return dirs[int((bearing + 22.5) / 45) % 8];
}

std::string ageText(uint32_t s) {
    if (s < 60) return "adesso";
    if (s < 3600) return std::to_string(s / 60) + " min fa";
    return std::to_string(s / 3600) + " h fa";
}

// Riga "chiave: valore".
void info(lv_obj_t* parent, const char* key, const std::string& value, lv_color_t color = BIANCO()) {
    lv_obj_t* r = row(parent, PX(8, 4));
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    label(r, &font_18, GRIGIO3(), key);
    lv_obj_t* v = label(r, &font_20, color, value.c_str());
    grow(v);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
}

// Ultima posizione ricevuta da un contatto (da risposta o da SOS).
bool lastPositionOf(uint16_t number, app::Message& out) {
    const app::Chat c = app::dati().chat({app::ChatKind::DIRECT, number});
    for (auto it = c.msgs.rbegin(); it != c.msgs.rend(); ++it)
        if ((it->kind == app::MsgKind::POSITION || it->kind == app::MsgKind::SOS) && !it->out &&
            parse(it->text).valid) {
            out = *it;
            return true;
        }
    return false;
}
}  // namespace

// ================================================================ la mia posizione

class MyPosition : public Screen {
public:
    void build(lv_obj_t* root) override {
        app::rete().wantGnss(120);
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_PUNTO, "Posizione");
        card_ = column(col, PX(4, 1));
        lv_obj_set_size(card_, W_SCHERMO, PX(132, 78));
        pad(card_, PX(10, 4), PX(20, 10), PX(10, 4), PX(20, 10));
        underline(card_);
        list_.create(col, W_SCHERMO, H_AREA - H_TITOLO - PX(132, 78));
        lv_obj_t* req = list_.addRow(PX(56, 30));
        List::text(req, &icone_22, AMBRA(), IC_PUNTO);
        List::text(req, &font_22b, AMBRA(), "Chiedi la posizione a...", true);
#if TDECK
        lv_obj_t* mp = list_.addRow(30);
        List::text(mp, &icone_22, AMBRA(), IC_MAPPA);
        List::text(mp, &font_22b, AMBRA(), "Vedi sulla mappa", true);
#endif
        known_.clear();
        for (const auto& c : app::dati().contacts()) {
            app::Message m;
            if (!lastPositionOf(c.number, m)) continue;
            known_.push_back({c.number, m});
        }
        const pn::Position me = app::rete().lastPosition();
        for (const auto& k : known_) {
            lv_obj_t* r = list_.addRow(PX(62, 38));
            avatar(r, k.first, app::dati().nameOf(k.first), PX(40, 26));
            lv_obj_t* mid = column(r, 0);
            grow(mid);
            lv_obj_set_height(mid, LV_SIZE_CONTENT);
            rigaPiena(List::text(mid, &font_22b, BIANCO(), app::dati().nameOf(k.first)));
            const PosData p = parse(k.second.text);
            std::string where = app::formatLatLon(p.lat, p.lon);
            if (me.valid) {
                double m, b;
                app::distanceBearing(me.latE7, me.lonE7, p.lat, p.lon, m, b);
                where = distanceText(m) + " " + direction(b);
            }
            List::text(mid, &font_18, GRIGIO3(), where);
            List::text(r, &mono_16, GRIGIO2(), whenShort(k.second.time));
        }
        list_.focus(sel_);
        refresh();
    }
    void refresh() {
        lv_obj_clean(card_);
        const scheda::Fix f = scheda::gnss().fix();
        const pn::Position p = app::rete().lastPosition();
        lv_obj_t* t = row(card_, PX(10, 6));
        lv_obj_set_size(t, lv_pct(100), LV_SIZE_CONTENT);
        label(t, &icone_22, f.valid ? VERDE() : AMBRA(), IC_GPS);
        label(t, &font_22b, BIANCO(), f.valid ? "La mia posizione" : (p.valid ? "Ultima posizione nota" : "Cerco i satelliti..."));
        if (p.valid) {
            label(card_, &mono_20, BIANCO(), app::formatLatLon(p.latE7, p.lonE7).c_str());
            char b[96];
            snprintf(b, sizeof(b), "quota %d m · precisione ±%u m · %s", p.altM, unsigned(p.accM),
                     ageText(p.ageS).c_str());
            label(card_, &font_18, GRIGIO3(), b);
        } else {
            lv_obj_t* l = label(card_, &font_18, GRIGIO3(),
                                "Stai all'aperto con il cielo libero: la prima posizione può richiedere un minuto.");
            lv_obj_set_width(l, PX(440, 300));
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        }
        label(card_, &font_16, GRIGIO2(),
              (std::to_string(f.sats) + TXT(" satelliti · la posizione la mandi solo a chi la chiede, se accetti",
                                            " satelliti · la mandi solo a chi la chiede"))
                  .c_str());
    }
    void tick() override {
        if (++ticks_ % 8 == 0) {
            app::rete().wantGnss(120);
            refresh();
        }
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (list_.key(k)) {
            sel_ = list_.focused();
            return true;
        }
        sel_ = list_.focused();
#if TDECK
        if (k == Key::OK && sel_ == 1) {
            push(makeMap());
            return true;
        }
#endif
        if ((k == Key::OK && sel_ == 0) || k == Key::CALL) {
            if (k == Key::CALL && sel_ >= FIRST) {
                request(known_[sel_ - FIRST].first);
                return true;
            }
            push(makePickContact("Chiedi la posizione a", [](uint16_t n) {
                app::rete().requestPosition(n);
                toast("Posizione chiesta", app::dati().nameOf(n) + " deve accettare");
            }));
            return true;
        }
        if (k == Key::OK) {
            const auto& kp = known_[sel_ - FIRST];
            push(makePositionView(kp.first, kp.second.text, kp.second.time));
            return true;
        }
        return false;
    }
    void request(uint16_t n) {
        app::rete().requestPosition(n);
        toast("Posizione chiesta", app::dati().nameOf(n) + " deve accettare");
    }
    void onEvent(const app::Event& e) override {
        if (e.type == app::Ev::POSITION || e.type == app::Ev::SOS) rebuild();
    }
    std::vector<Hint> hints() override {
        if (list_.focused() == 0) return {{"OK", "chiedi"}};
#if TDECK
        if (list_.focused() == 1) return {{"OK", "apri la mappa"}};
#endif
        return {{"OK", "mostra"}, {"CALL", "chiedi di nuovo"}};
    }

private:
    static constexpr int FIRST = PX(1, 2);  // prima riga dei contatti (sul T-Deck c'e' anche la mappa)
    lv_obj_t* card_ = nullptr;
    List list_;
    std::vector<std::pair<uint16_t, app::Message>> known_;
    int sel_ = 0;
    int ticks_ = 0;
};

Screen* makePosition() { return new MyPosition(); }

// ================================================================ richiesta in arrivo

class PositionRequest : public Screen {
public:
    PositionRequest(uint16_t from, uint32_t id) : from_(from), id_(id) {}
    void build(lv_obj_t* root) override {
        const std::string name = app::dati().nameOf(from_);
        lv_obj_t* col = column(root, PX(12, 4));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_PUNTO, "Richiesta di posizione");
        lv_obj_t* top = column(col, PX(8, 3));
        lv_obj_set_size(top, W_SCHERMO, LV_SIZE_CONTENT);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        avatar(top, from_, name, PX(90, 36));
        rigaPiena(label(top, PX(&font_32b, &font_26b), BIANCO(), name.c_str()), true);
        lv_obj_t* q = label(top, &font_22, GRIGIO3(),
                            TXT("vuole sapere dove sei. Gli mandi la tua posizione?", "vuole sapere dove sei: gliela mandi?"));
        lv_obj_set_width(q, PX(420, 300));
        lv_obj_set_style_text_align(q, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(q, LV_LABEL_LONG_WRAP);
        if (waiting_) {
            status_ = label(col, &font_22b, AMBRA(), "Cerco il GPS...");
            lv_obj_set_width(status_, W_SCHERMO);
            lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_t* l = label(col, &font_18, GRIGIO3(), "La mando appena c'è una posizione (al massimo 90 secondi).");
            lv_obj_set_width(l, W_SCHERMO);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            return;
        }
        list_.create(col, W_SCHERMO, PX(130, 60));
        lv_obj_t* a = list_.addRow(PX(60, 30));
        List::text(a, &icone_22, VERDE(), IC_OK);
        List::text(a, &font_22b, VERDE(), "Consenti", true);
        lv_obj_t* r = list_.addRow(PX(60, 30));
        List::text(r, &icone_22, ROSSO(), IC_LUCCHETTO);
        List::text(r, &font_22b, ROSSO(), "Rifiuta", true);
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (waiting_) {
            if (k == Key::BACK || k == Key::END) {
                answer(true);  // si manda quello che c'e' (anche "senza GPS")
                return true;
            }
            return true;
        }
        if (list_.key(k)) return true;
        if (k == Key::OK || k == Key::ENTER) {
            if (list_.focused() == 0) accept();
            else answer(false);
            return true;
        }
        if (k == Key::BACK || k == Key::END) {
            answer(false);
            return true;
        }
        return false;
    }
    void accept() {
        app::rete().wantGnss(180);
        const pn::Position p = app::rete().lastPosition();
        if (p.valid && p.ageS < 120) {
            answer(true);
            return;
        }
        waiting_ = true;
        waitStart_ = 0;
        rebuild();
    }
    void tick() override {
        if (!waiting_) return;
        waitStart_++;
        const pn::Position p = app::rete().lastPosition();
        if ((p.valid && p.ageS < 30) || waitStart_ > 90 * 4) answer(true);
        else if (status_)
            lv_label_set_text(status_, ("Cerco il GPS... " + std::to_string(scheda::gnss().fix().sats) + " satelliti").c_str());
    }
    void answer(bool yes) {
        app::rete().answerPosition(from_, id_, yes);
        app::Message m;
        m.id = id_ ^ 0x10000000u;
        m.from = app::rete().number();
        m.out = true;
        m.status = pn::Delivery::SENT;
        m.time = app::rete().unixTime();
        m.kind = app::MsgKind::INFO;
        const pn::Position p = app::rete().lastPosition();
        m.text = !yes ? "Richiesta di posizione rifiutata" : (p.valid ? "Posizione inviata" : "Inviato: GPS senza posizione");
        app::dati().addMessage({app::ChatKind::DIRECT, from_}, m, false);
        toast(yes ? "Posizione inviata" : "Richiesta rifiutata", app::dati().nameOf(from_), yes ? VERDE() : GRIGIO3());
        pop();
    }
    std::vector<Hint> hints() override {
        if (waiting_) return {{"BACK", "manda subito"}};
        return {{"OK", "scegli"}, {"BACK", "rifiuta"}};
    }
    bool capturesEnd() const override { return true; }

private:
    uint16_t from_;
    uint32_t id_;
    List list_;
    bool waiting_ = false;
    int waitStart_ = 0;
    lv_obj_t* status_ = nullptr;
};

Screen* makePositionRequest(uint16_t from, uint32_t requestId) { return new PositionRequest(from, requestId); }

// ================================================================ posizione di un altro

class PositionView : public Screen {
public:
    PositionView(uint16_t from, const std::string& data, uint32_t time) : from_(from), data_(data), time_(time) {}
    void build(lv_obj_t* root) override {
        app::rete().wantGnss(90);
        const PosData p = parse(data_);
        const std::string name = app::dati().nameOf(from_);
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_PUNTO, name, whenShort(time_));
#if TDECK
        // T-Deck: la mappa col contatto (e con me), sotto distanza, direzione, precisione ed eta'.
        {
        map_.create(col, W_SCHERMO, H_AREA - H_TITOLO - 34);
        std::vector<MapMarker> marks;
        if (p.valid) marks.push_back({MapMarker::TARGET, p.lat, p.lon, avatarColor(from_), initialOf(name)});
        const pn::Position me = app::rete().lastPosition();
        if (me.valid && from_ != app::rete().number()) marks.push_back({MapMarker::ME, me.latE7, me.lonE7, BLU(), ""});
        map_.setMarkers(marks);
        map_.fit();
        lv_obj_t* bar = row(col, 8);
        lv_obj_set_size(bar, W_SCHERMO, 34);
        pad(bar, 0, 10, 0, 10);
        dist_ = label(bar, &font_26b, BIANCO(), "");
        dir_ = label(bar, &font_22, AMBRA(), "");
        lv_obj_t* sp = box(bar);
        grow(sp);
        lv_obj_set_height(sp, 1);
        std::string extra = "posizione non valida";
        if (p.valid)
            extra = (p.acc >= 255 ? std::string() : "±" + std::to_string(p.acc) + " m · ") +
                    ageText(p.age + (app::rete().unixTime() > time_ && time_ ? app::rete().unixTime() - time_ : 0));
        label(bar, &font_16, GRIGIO3(), extra.c_str());
        update();
        return;
        }
#endif
        lv_obj_t* body = row(col, PX(16, 8));
        lv_obj_set_size(body, W_SCHERMO, H_AREA - H_TITOLO);
        pad(body, PX(12, 6), PX(16, 8), PX(12, 6), PX(16, 8));
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        // Bussola: la direzione e' rispetto al nord (il telefono non ha la bussola magnetica).
        lv_obj_t* comp = box(body);
        lv_obj_set_size(comp, PX(200, 140), PX(230, 156));
        dial_ = box(comp);
        lv_obj_set_size(dial_, DIAL, DIAL);
        radius(dial_, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(dial_, 2, 0);
        lv_obj_set_style_border_color(dial_, GRIGIO1(), 0);
        const char* card[] = {"N", "E", "S", "O"};
        const int cx[] = {0, PX(84, 58), 0, PX(-84, -58)}, cy[] = {PX(-84, -58), 0, PX(84, 58), 0};
        for (int i = 0; i < 4; i++) {
            lv_obj_t* l = label(dial_, &font_18, i == 0 ? ROSSO() : GRIGIO2(), card[i]);
            lv_obj_align(l, LV_ALIGN_CENTER, cx[i], cy[i]);
        }
        line_ = lv_line_create(dial_);
        lv_obj_set_style_line_width(line_, PX(5, 4), 0);
        lv_obj_set_style_line_color(line_, AMBRA(), 0);
        lv_obj_set_style_line_rounded(line_, true, 0);
        dot_ = box(dial_);
        lv_obj_set_size(dot_, PX(16, 12), PX(16, 12));
        radius(dot_, LV_RADIUS_CIRCLE);
        fill(dot_, AMBRA());
        lv_obj_t* me = box(dial_);
        lv_obj_set_size(me, PX(10, 8), PX(10, 8));
        radius(me, LV_RADIUS_CIRCLE);
        fill(me, BIANCO());
        lv_obj_center(me);
        lv_obj_t* note = label(comp, &font_16, GRIGIO2(), "rispetto al nord");
        lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, 0);
        // Dati.
        lv_obj_t* right = column(body, PX(6, 2));
        grow(right);
        lv_obj_set_height(right, LV_SIZE_CONTENT);
        dist_ = label(right, &font_32b, BIANCO(), "");
        dir_ = label(right, &font_22, AMBRA(), "");
        lv_obj_t* sp = box(right);
        lv_obj_set_size(sp, 1, PX(8, 2));
        if (p.valid) {
            lv_obj_t* c = label(right, &mono_16, BIANCO(), app::formatLatLon(p.lat, p.lon).c_str());
            lv_obj_set_width(c, lv_pct(100));
            lv_label_set_long_mode(c, LV_LABEL_LONG_WRAP);
            info(right, "Quota", std::to_string(p.alt) + " m");
            info(right, "Precisione", p.acc >= 255 ? "?" : "±" + std::to_string(p.acc) + " m");
            info(right, "Presa", ageText(p.age + (app::rete().unixTime() > time_ && time_
                                                      ? app::rete().unixTime() - time_
                                                      : 0)));
            if (p.battery >= 0 && p.battery <= 100) info(right, "Batteria", std::to_string(p.battery) + "%");
        } else {
            label(right, &font_20, ROSSO(), "Posizione non valida");
        }
        update();
    }
    void update() {
        const PosData p = parse(data_);
        const pn::Position me = app::rete().lastPosition();
        if (!p.valid) return;
        if (!me.valid) {
            lv_label_set_text(dist_, "? km");
#if TDECK
            lv_label_set_text(dir_, "GPS?");
#else
            lv_label_set_text(dir_, "la tua posizione non è nota");
            lv_obj_add_flag(line_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(dot_, LV_OBJ_FLAG_HIDDEN);
#endif
            return;
        }
        double m, b;
        app::distanceBearing(me.latE7, me.lonE7, p.lat, p.lon, m, b);
        lv_label_set_text(dist_, distanceText(m).c_str());
        char d[48];
        snprintf(d, sizeof(d), "%s · %.0f°", direction(b), b);
        lv_label_set_text(dir_, d);
#if TDECK
        return;  // niente bussola: c'e' la mappa
#endif
        const double r = b * M_PI / 180.0;
        const int len = PX(70, 48), c = DIAL / 2;
        pts_[0] = {c, c};
        pts_[1] = {lv_value_precise_t(c + std::lround(len * std::sin(r))),
                   lv_value_precise_t(c - std::lround(len * std::cos(r)))};
        lv_line_set_points(line_, pts_, 2);
        lv_obj_set_pos(dot_, int32_t(pts_[1].x) - PX(8, 6), int32_t(pts_[1].y) - PX(8, 6));
        lv_obj_clear_flag(line_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(dot_, LV_OBJ_FLAG_HIDDEN);
    }
    void tick() override {
        if (++ticks_ % 8 == 0) {
            app::rete().wantGnss(90);
            update();
        }
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (k == Key::CALL && from_ != app::rete().number()) {
            app::rete().requestPosition(from_);
            toast("Posizione chiesta di nuovo", app::dati().nameOf(from_) + " deve accettare");
            return true;
        }
#if TDECK
        if (map_.key(k)) return true;
#endif
        return false;
    }
    void onEvent(const app::Event& e) override {
        // Arrivata una posizione piu' nuova dalla stessa persona: la si mostra.
        if ((e.type == app::Ev::POSITION || e.type == app::Ev::SOS) && e.from == from_) {
            app::Message m;
            if (lastPositionOf(from_, m) && m.time >= time_) {
                data_ = m.text;
                time_ = m.time;
                rebuild();
            }
        }
    }
    std::vector<Hint> hints() override {
#if TDECK
        if (from_ == app::rete().number()) return {{"I/O", "zoom"}, {"BACK", "indietro"}};
        return {{"CALL", "aggiorna"}, {"I/O", "zoom"}, {"BACK", "indietro"}};
#else
        if (from_ == app::rete().number()) return {{"BACK", "indietro"}};
        return {{"CALL", "aggiorna"}, {"BACK", "indietro"}};
#endif
    }

private:
#if TDECK
    MapView map_;
#endif
    static constexpr int DIAL = PX(200, 140);  // diametro della bussola
    uint16_t from_;
    std::string data_;
    uint32_t time_;
    lv_obj_t *dial_ = nullptr, *line_ = nullptr, *dot_ = nullptr, *dist_ = nullptr, *dir_ = nullptr;
    lv_point_precise_t pts_[2] = {};
    int ticks_ = 0;
};

Screen* makePositionView(uint16_t from, const std::string& data, uint32_t time) {
    return new PositionView(from, data, time);
}

#if TDECK
bool lastKnownPosition(uint16_t number, int32_t& latE7, int32_t& lonE7, uint32_t& time) {
    app::Message m;
    if (!lastPositionOf(number, m)) return false;
    const PosData p = parse(m.text);
    latE7 = p.lat;
    lonE7 = p.lon;
    time = m.time;
    return p.valid;
}
#endif

}  // namespace ui
