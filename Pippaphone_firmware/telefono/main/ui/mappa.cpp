// Mappa offline del T-Deck Plus: proiezione di Mercatore come OpenStreetMap, tile PNG 256x256
// letti dalla microSD e decodificati in RGB565 (PSRAM) con una piccola cache.
#include "ui/mappa.h"

#if TDECK
#include <dirent.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app/memoria.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#define LODEPNG_NO_COMPILE_CPP  // solo l'interfaccia C di lodepng (quella C++ non serve)
#include "src/libs/lodepng/lodepng.h"
#include "ui/widget.h"

namespace ui {

namespace {
const char* TAG = "mappa";
constexpr double TILE = 256.0;
constexpr int MIN_ZOOM = 2, MAX_ZOOM = 18;
constexpr size_t CACHE_TILES = 12;  // 12 x 128 kB in PSRAM

// ---------------------------------------------------------------- proiezione

double world(int z) { return TILE * double(1u << z); }
double lonToX(double lon, int z) { return (lon + 180.0) / 360.0 * world(z); }
double latToY(double lat, int z) {
    lat = std::max(-85.0511, std::min(85.0511, lat));
    const double r = lat * M_PI / 180.0;
    return (1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / M_PI) / 2.0 * world(z);
}
double xToLon(double x, int z) { return x / world(z) * 360.0 - 180.0; }
double yToLat(double y, int z) { return std::atan(std::sinh(M_PI * (1.0 - 2.0 * y / world(z)))) * 180.0 / M_PI; }

// ---------------------------------------------------------------- tile

struct Tile {
    int z = 0, x = 0, y = 0;
    uint16_t* px = nullptr;
    lv_image_dsc_t dsc{};
    bool missing = false;
    uint32_t used = 0;
};
std::vector<Tile*> g_cache;
uint32_t g_frame = 0;

void freeTile(Tile* t) {
    if (t->px) {
        lv_image_cache_drop(&t->dsc);
        heap_caps_free(t->px);
    }
    delete t;
}

// Tile z/x/y dalla microSD (nullptr se non c'e'). I tile mancanti si ricordano come tali.
const lv_image_dsc_t* tile(int z, int x, int y) {
    for (Tile* t : g_cache)
        if (t->z == z && t->x == x && t->y == y) {
            t->used = g_frame;
            return t->missing ? nullptr : &t->dsc;
        }
    if (g_cache.size() >= CACHE_TILES) {
        auto old = std::min_element(g_cache.begin(), g_cache.end(), [](Tile* a, Tile* b) { return a->used < b->used; });
        if ((*old)->used != g_frame) {  // mai uno che si sta mostrando adesso
            freeTile(*old);
            g_cache.erase(old);
        }
    }
    Tile* t = new Tile;
    t->z = z;
    t->x = x;
    t->y = y;
    t->used = g_frame;
    g_cache.push_back(t);
    char path[80];
    snprintf(path, sizeof(path), "%s/%d/%d/%d.png", app::MAPPE, z, x, y);
    std::vector<uint8_t> file;
    // Il lodepng di LVGL 9 non restituisce i pixel ma un lv_draw_buf_t (dati RGBA a 8 bit).
    unsigned char* out = nullptr;
    unsigned w = 0, h = 0;
    const bool ok = app::readFile(path, file) && !file.empty() &&
                    lodepng_decode32(&out, &w, &h, file.data(), file.size()) == 0 && out;
    lv_draw_buf_t* buf = reinterpret_cast<lv_draw_buf_t*>(out);
    if (!ok || w != 256 || h != 256) {
        if (buf) lv_draw_buf_destroy(buf);
        if (!file.empty()) ESP_LOGW(TAG, "tile non valido: %s", path);
        t->missing = true;
        return nullptr;
    }
    t->px = static_cast<uint16_t*>(heap_caps_malloc(256 * 256 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!t->px) t->px = static_cast<uint16_t*>(heap_caps_malloc(256 * 256 * 2, MALLOC_CAP_DEFAULT));
    if (!t->px) {
        lv_draw_buf_destroy(buf);
        t->missing = true;
        return nullptr;
    }
    for (uint32_t y = 0; y < 256; y++) {
        const uint8_t* row = buf->data + size_t(y) * buf->header.stride;
        for (uint32_t x = 0; x < 256; x++) {
            const uint8_t* p = row + 4 * x;  // R, G, B, A
            t->px[y * 256 + x] = uint16_t(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
        }
    }
    lv_draw_buf_destroy(buf);
    t->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    t->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    t->dsc.header.w = 256;
    t->dsc.header.h = 256;
    t->dsc.header.stride = 512;
    t->dsc.data_size = 256 * 256 * 2;
    t->dsc.data = reinterpret_cast<const uint8_t*>(t->px);
    return &t->dsc;
}

// Distanza "tonda" per la scala, tra 40 e 100 pixel.
void scaleFor(double metersPerPx, int& px, std::string& text) {
    static const int STEPS[] = {5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000};
    int m = STEPS[0];
    for (int s : STEPS) {
        m = s;
        if (s / metersPerPx >= 40) break;
    }
    px = std::max(1, int(std::lround(m / metersPerPx)));
    text = m >= 1000 ? std::to_string(m / 1000) + " km" : std::to_string(m) + " m";
}
}  // namespace

const std::vector<int>& mapZooms() {
    static std::vector<int> zooms;
    static bool scanned = false;
    if (scanned) return zooms;
    scanned = true;
    DIR* d = opendir(app::MAPPE);
    if (!d) return zooms;
    while (dirent* e = readdir(d)) {
        char* end = nullptr;
        const long v = std::strtol(e->d_name, &end, 10);
        if (end != e->d_name && *end == 0 && v >= 0 && v <= 20) zooms.push_back(int(v));
    }
    closedir(d);
    std::sort(zooms.begin(), zooms.end());
    ESP_LOGI(TAG, "%u livelli di zoom sulla microSD", unsigned(zooms.size()));
    return zooms;
}

void MapView::create(lv_obj_t* parent, int w, int h) {
    w_ = w;
    h_ = h;
    cont_ = box(parent);
    lv_obj_set_size(cont_, w, h);
    fill(cont_, lv_color_hex(0x202020));
    layer_ = box(cont_);
    lv_obj_set_size(layer_, w, h);
    tiles_.clear();
    marks_.clear();
    // Scala in basso a sinistra, attribuzione in basso a destra (su fondo scuro, per leggerle
    // anche sopra la mappa), avviso in alto.
    lv_obj_t* scale = column(cont_, 2);
    lv_obj_set_size(scale, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(scale, NERO(), 0);
    lv_obj_set_style_bg_opa(scale, LV_OPA_60, 0);
    pad(scale, 1, 4, 3, 4);
    lv_obj_align(scale, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    scaleText_ = label(scale, &font_16, BIANCO(), "");
    scaleLine_ = box(scale);
    fill(scaleLine_, BIANCO());
    lv_obj_t* osm = label(cont_, &font_16, GRIGIO3(), "© OpenStreetMap");
    lv_obj_set_style_bg_color(osm, NERO(), 0);
    lv_obj_set_style_bg_opa(osm, LV_OPA_60, 0);
    pad(osm, 0, 3, 0, 3);
    lv_obj_align(osm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    msg_ = label(cont_, &font_16, GRIGIO3(), "");
    lv_obj_set_width(msg_, w - 40);
    lv_obj_set_style_text_align(msg_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(msg_, LV_LABEL_LONG_WRAP);
    lv_obj_align(msg_, LV_ALIGN_TOP_MID, 0, 6);
}

void MapView::setMarkers(const std::vector<MapMarker>& m) {
    for (lv_obj_t* o : marks_) lv_obj_delete(o);
    marks_.clear();
    markers_ = m;
    for (const MapMarker& k : markers_) {
        const int s = k.kind == MapMarker::ME ? 12 : k.kind == MapMarker::TARGET ? 22 : 18;
        lv_obj_t* o = box(layer_);
        lv_obj_set_size(o, s, s);
        radius(o, LV_RADIUS_CIRCLE);
        fill(o, k.kind == MapMarker::ME ? BLU() : k.color);
        lv_obj_set_style_border_width(o, k.kind == MapMarker::PERSON ? 1 : 2, 0);
        lv_obj_set_style_border_color(o, k.kind == MapMarker::TARGET ? AMBRA() : (k.kind == MapMarker::ME ? BIANCO() : NERO()), 0);
        if (!k.text.empty()) lv_obj_center(label(o, &font_16, BIANCO(), k.text.c_str()));
        marks_.push_back(o);
    }
}

void MapView::fit() {
    const std::vector<int>& av = mapZooms();
    if (markers_.empty()) {
        lat_ = 42.5;  // Italia
        lon_ = 12.5;
        zoom_ = av.empty() ? 5 : av.front();
        redraw();
        return;
    }
    double minX = 1e300, maxX = -1e300, minY = 1e300, maxY = -1e300;
    for (const MapMarker& k : markers_) {
        const double x = lonToX(k.lonE7 / 1e7, 0), y = latToY(k.latE7 / 1e7, 0);
        minX = std::min(minX, x);
        maxX = std::max(maxX, x);
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
    }
    lon_ = xToLon((minX + maxX) / 2, 0);
    lat_ = yToLat((minY + maxY) / 2, 0);
    // Lo zoom piu' vicino in cui ci stanno tutti (con un po' di margine); un punto solo: 15.
    std::vector<int> cands = av;
    if (cands.empty())
        for (int z = MIN_ZOOM; z <= MAX_ZOOM; z++) cands.push_back(z);
    zoom_ = cands.front();
    for (int z : cands) {
        const double k = double(1u << z);
        if ((maxX - minX) * k <= w_ - 40 && (maxY - minY) * k <= h_ - 40 && z <= 16) zoom_ = z;
    }
    redraw();
}

bool MapView::key(scheda::Key k) {
    using scheda::Key;
    const std::vector<int>& av = mapZooms();
    const double cx = lonToX(lon_, zoom_), cy = latToY(lat_, zoom_);
    switch (k) {
        case Key::UP: lat_ = yToLat(cy - h_ / 4.0, zoom_); break;
        case Key::DOWN: lat_ = yToLat(cy + h_ / 4.0, zoom_); break;
        case Key::LEFT: lon_ = xToLon(cx - w_ / 4.0, zoom_); break;
        case Key::RIGHT: lon_ = xToLon(cx + w_ / 4.0, zoom_); break;
        case Key::OK:
        case Key::I: {
            int next = std::min(MAX_ZOOM, zoom_ + 1);
            if (!av.empty()) {
                auto it = std::upper_bound(av.begin(), av.end(), zoom_);
                next = it == av.end() ? zoom_ : *it;
            }
            zoom_ = next;
            break;
        }
        case Key::O: {
            int prev = std::max(MIN_ZOOM, zoom_ - 1);
            if (!av.empty()) {
                auto it = std::lower_bound(av.begin(), av.end(), zoom_);
                prev = it == av.begin() ? zoom_ : *(it - 1);
            }
            zoom_ = prev;
            break;
        }
        case Key::C: fit(); return true;
        default: return false;
    }
    redraw();
    return true;
}

void MapView::redraw() {
    if (!cont_) return;
    ++g_frame;
    const double cx = lonToX(lon_, zoom_), cy = latToY(lat_, zoom_);
    const double left = cx - w_ / 2.0, top = cy - h_ / 2.0;
    const int n = 1 << zoom_;
    const bool maps = !mapZooms().empty();
    const int tx0 = int(std::floor(left / TILE)), tx1 = int(std::floor((left + w_ - 1) / TILE));
    const int ty0 = int(std::floor(top / TILE)), ty1 = int(std::floor((top + h_ - 1) / TILE));
    size_t i = 0;
    bool any = false;
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
            if (i >= tiles_.size()) {
                lv_obj_t* img = lv_image_create(layer_);
                lv_obj_move_background(img);  // sotto i segnaposto
                tiles_.push_back(img);
            }
            lv_obj_t* img = tiles_[i++];
            const lv_image_dsc_t* d = maps && ty >= 0 && ty < n ? tile(zoom_, ((tx % n) + n) % n, ty) : nullptr;
            if (!d) {
                lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            lv_image_set_src(img, d);
            lv_obj_set_pos(img, int32_t(std::lround(tx * TILE - left)), int32_t(std::lround(ty * TILE - top)));
            lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);
            any = true;
        }
    for (; i < tiles_.size(); i++) lv_obj_add_flag(tiles_[i], LV_OBJ_FLAG_HIDDEN);

    for (size_t k = 0; k < markers_.size(); k++) {
        const double x = lonToX(markers_[k].lonE7 / 1e7, zoom_) - left, y = latToY(markers_[k].latE7 / 1e7, zoom_) - top;
        lv_obj_t* o = marks_[k];
        const int s = markers_[k].kind == MapMarker::ME ? 12 : markers_[k].kind == MapMarker::TARGET ? 22 : 18;
        const bool in = x >= 0 && y >= 0 && x < w_ && y < h_;
        if (in) {
            lv_obj_set_pos(o, int32_t(std::lround(x)) - s / 2, int32_t(std::lround(y)) - s / 2);
            lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (any) lv_obj_add_flag(msg_, LV_OBJ_FLAG_HIDDEN);
    else {
        lv_label_set_text(msg_, maps ? "Nessuna mappa di questa zona a questo zoom"
                                     : "Nessuna mappa sulla microSD: copia i tile nella cartella /mappe");
        lv_obj_clear_flag(msg_, LV_OBJ_FLAG_HIDDEN);
    }
    // Scala: metri per pixel al centro.
    const double mpp = 156543.03392 * std::cos(lat_ * M_PI / 180.0) / double(n);
    int px;
    std::string text;
    scaleFor(mpp, px, text);
    lv_label_set_text(scaleText_, (text + " · zoom " + std::to_string(zoom_)).c_str());
    lv_obj_set_size(scaleLine_, px, 2);
}

}  // namespace ui
#endif
