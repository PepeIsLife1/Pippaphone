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

class MapScreen : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        std::vector<MapMarker> m;
        int people = 0;
        for (const auto& c : app::dati().contacts()) {
            int32_t lat, lon;
            uint32_t t;
            if (!lastKnownPosition(c.number, lat, lon, t)) continue;
            m.push_back({MapMarker::PERSON, lat, lon, avatarColor(c.number), initialOf(c.name)});
            people++;
        }
        const pn::Position me = app::rete().lastPosition();
        if (me.valid) m.push_back({MapMarker::ME, me.latE7, me.lonE7, BLU(), ""});
        header(col, IC_MAPPA, "Mappa", people == 1 ? "1 persona" : std::to_string(people) + " persone");
        map_.create(col, W_SCHERMO, H_AREA - H_TITOLO);
        map_.setMarkers(m);
        map_.fit();
    }
    bool onKey(Key k, bool pressed) override { return pressed && map_.key(k); }
    std::vector<Hint> hints() override { return {{"NAV", "sposta"}, {"I/O", "zoom"}, {"C", "tutti"}}; }

private:
    MapView map_;
};

Screen* makeMap() { return new MapScreen(); }

}  // namespace ui
#endif
