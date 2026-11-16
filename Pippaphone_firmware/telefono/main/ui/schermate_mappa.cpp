// Mappa offline (solo T-Deck Plus): la mia posizione e le ultime posizioni note dei contatti, sui
// tile OpenStreetMap della microSD.
#include "ui/schermate.h"
#include "ui/widget.h"

#if TDECK
#include "app/dati.h"
#include "app/rete.h"
#include "ui/mappa.h"

namespace ui {

// Prima lettera del nome (anche accentata) per il segnaposto.
std::string initialOf(const std::string& name) {
    if (name.empty() || (name[0] >= '0' && name[0] <= '9')) return "#";
    size_t n = 1;
    while (n < name.size() && (uint8_t(name[n]) & 0xC0) == 0x80) n++;
    return name.substr(0, n);
}

// La mappa tiene acceso il GPS finche' e' aperta (vuoleGps) e ogni 2 secondi rilegge la mia posizione e
// quelle dei contatti: se qualcosa e' cambiato sposta i segnaposto, senza toccare centro e zoom. La prima
// volta che compaio si rifa' l'inquadratura (con me dentro), ma solo se la mappa non e' stata spostata o
// ingrandita a mano: se no per qualche secondo "Ti ho trovato · C per vedere tutti". Lo stesso quando arriva
// la prima posizione di adesso e c'era solo una vecchia (rimasta da prima: il punto nuovo puo' essere
// lontano, fuori dalla mappa). Finche' non c'e' una posizione, in alto a sinistra quanti satelliti sente il
// GPS, o che non risponde.
class MapScreen : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        int people = 0;
        marks_ = marks(people, fresca_);
        lv_obj_t* h = header(col, IC_MAPPA, "Mappa", peopleText(people));
        people_ = lv_obj_get_child(h, -1);  // il testo a destra
        map_.create(col, W_SCHERMO, H_AREA - H_TITOLO);
        map_.setMarkers(marks_);
        map_.fit();
        foundTicks_ = 0;
        refreshGps();
    }
    void tick() override {
        if (++ticks_ % 8) return;  // ogni 2 secondi
        if (foundTicks_ > 0) foundTicks_--;
        int people = 0;
        bool fresca = false;
        const std::vector<MapMarker> m = marks(people, fresca);
        // Compaio adesso, o arriva la prima posizione di adesso quando era disegnata solo una vecchia.
        const bool found = hasMe(m) && (!hasMe(marks_) || (fresca && !fresca_));
        if (fresca) fresca_ = true;
        if (m != marks_ || found) {
            marks_ = m;
            lv_label_set_text(people_, peopleText(people).c_str());
            if (found && !map_.moved()) {
                map_.setMarkers(m);
                map_.fit();
            } else {
                map_.update(m);
                if (found) foundTicks_ = 3;  // 6 secondi
            }
        }
        refreshGps();
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed || !map_.key(k)) return false;
        if (k == Key::C) {
            foundTicks_ = 0;
            refreshGps();
        }
        return true;
    }
    std::vector<Hint> hints() override { return {{"NAV", "sposta"}, {"I/O", "zoom"}, {"C", "tutti"}}; }
    bool vuoleGps() const override { return true; }

private:
    static std::string peopleText(int n) { return n == 1 ? "1 persona" : std::to_string(n) + " persone"; }
    // I contatti con una posizione nota e io; fresca: la mia e' di adesso.
    std::vector<MapMarker> marks(int& people, bool& fresca) const {
        std::vector<MapMarker> m;
        people = 0;
        for (const auto& c : app::dati().contacts()) {
            int32_t lat, lon;
            uint32_t t;
            if (!lastKnownPosition(c.number, lat, lon, t)) continue;
            m.push_back({MapMarker::PERSON, lat, lon, avatarColor(c.number), initialOf(c.name)});
            people++;
        }
        const pn::Position me = app::rete().lastPosition();
        if (me.valid) m.push_back({MapMarker::ME, me.latE7, me.lonE7, BLU(), ""});
        fresca = me.valid && me.ageS < MIA_FRESCA_S;
        return m;
    }
    // Riquadro del GPS: cerca i satelliti (quanti ne sente) o non risponde; niente se e' spento o c'e' la
    // posizione, tranne "Ti ho trovato" quando compaio con la mappa spostata.
    void refreshGps() {
        std::string t;
        const FaseGps g = faseGps();
        if (foundTicks_ > 0) t = "Ti ho trovato · C per vedere tutti";
        else if (g == FaseGps::CERCA)
            t = "Cerco i satelliti · ne sento " + std::to_string(scheda::gnss().fix().conSegnale);
        else if (g == FaseGps::MUTO) t = "Il GPS non risponde";
        map_.setGps(t);
    }

    MapView map_;
    std::vector<MapMarker> marks_;  // quelli disegnati
    lv_obj_t* people_ = nullptr;
    int ticks_ = 0;
    int foundTicks_ = 0;  // giri da 2 secondi in cui dire "Ti ho trovato"
    bool fresca_ = false;  // e' gia' stata disegnata una mia posizione di adesso
};

Screen* makeMap() { return new MapScreen(); }

}  // namespace ui
#endif
