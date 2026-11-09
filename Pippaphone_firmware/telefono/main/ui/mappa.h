// Mappa offline (solo T-Deck Plus): tile OpenStreetMap PNG 256x256 dalla microSD in
// /mappe/<zoom>/<x>/<y>.png (lo schema "slippy map" che usano quasi tutti i programmi per
// scaricare le mappe), con i segnaposto: io, i contatti, il punto da mostrare.
//
// Si muove col trackball, OK e I avvicinano, O allontana, C torna a far vedere tutti i segnaposto.
// Senza tile (niente microSD, o zona non scaricata) i segnaposto si vedono lo stesso su uno
// sfondo a quadretti, con la scala: la distanza tra le persone resta leggibile.
#pragma once

#include "ui/tema.h"

#if TDECK
#include <string>
#include <vector>

#include "lvgl.h"
#include "scheda/tastiera.h"

namespace ui {

struct MapMarker {
    enum Kind : uint8_t { ME, PERSON, TARGET };
    Kind kind = PERSON;
    int32_t latE7 = 0, lonE7 = 0;
    lv_color_t color = lv_color_hex(0x808080);
    std::string text;  // iniziale del contatto
};

class MapView {
public:
    void create(lv_obj_t* parent, int w, int h);
    void setMarkers(const std::vector<MapMarker>& m);
    // Zoom e centro per vedere tutti i segnaposto.
    void fit();
    // Tasti della mappa; true se usato.
    bool key(scheda::Key k);
    lv_obj_t* obj() const { return cont_; }

private:
    void redraw();
    lv_obj_t* cont_ = nullptr;
    lv_obj_t* layer_ = nullptr;  // tile e segnaposto (sotto la scala e le scritte)
    lv_obj_t* msg_ = nullptr;
    lv_obj_t* scaleLine_ = nullptr;
    lv_obj_t* scaleText_ = nullptr;
    std::vector<lv_obj_t*> tiles_;
    std::vector<lv_obj_t*> marks_;
    std::vector<MapMarker> markers_;
    int w_ = 0, h_ = 0;
    int zoom_ = 14;
    double lat_ = 0, lon_ = 0;  // centro
};

// Zoom presenti sulla microSD (cartelle numeriche in /mappe); vuoto = nessuna mappa.
const std::vector<int>& mapZooms();

}  // namespace ui
#endif
