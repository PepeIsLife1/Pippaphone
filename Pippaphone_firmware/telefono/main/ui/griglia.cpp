#include "ui/griglia.h"

#include <algorithm>

namespace ui {

lv_obj_t* Griglia::create(lv_obj_t* parent, int cols, int rows, int cell, int gap, const lv_font_t* font, Fn fn) {
    cols_ = cols;
    rows_ = rows;
    cell_ = cell;
    gap_ = gap;
    frame_ = 0;
    righe_ = false;
    font_ = font;
    fn_ = std::move(fn);
    obj_ = box(parent);
    lv_obj_set_size(obj_, cols * cell + (cols - 1) * gap, rows * cell + (rows - 1) * gap);
    lv_obj_add_event_cb(obj_, drawCb, LV_EVENT_DRAW_MAIN, this);
    // L'oggetto si cancella insieme alla schermata: la griglia non deve piu' disegnare.
    lv_obj_add_event_cb(
        obj_, [](lv_event_t* e) { static_cast<Griglia*>(lv_event_get_user_data(e))->obj_ = nullptr; }, LV_EVENT_DELETE,
        this);
    return obj_;
}

void Griglia::lines(lv_color_t color, int frame) {
    if (!obj_) return;
    frame_ = frame;
    righe_ = true;
    lv_obj_set_size(obj_, cols_ * cell_ + (cols_ - 1) * gap_ + 2 * frame, rows_ * cell_ + (rows_ - 1) * gap_ + 2 * frame);
    fill(obj_, color);  // si vede tra le caselle e nella cornice
}

void Griglia::refresh() {
    if (obj_) lv_obj_invalidate(obj_);
}

void Griglia::refresh(int x, int y) {
    if (!obj_ || x < 0 || y < 0 || x >= cols_ || y >= rows_) return;
    lv_area_t a;
    lv_obj_get_coords(obj_, &a);
    lv_area_increase(&a, -frame_, -frame_);
    const int step = cell_ + gap_;
    lv_area_t c = {a.x1 + x * step, a.y1 + y * step, a.x1 + x * step + cell_ - 1, a.y1 + y * step + cell_ - 1};
    // Con le righe anche quelle attorno, dove puo' esserci un bordo spostato fuori (Casella::outset).
    if (righe_) lv_area_increase(&c, gap_, gap_);
    lv_obj_invalidate_area(obj_, &c);
}

void Griglia::drawCb(lv_event_t* e) {
    Griglia* g = static_cast<Griglia*>(lv_event_get_user_data(e));
    lv_layer_t* layer = lv_event_get_layer(e);
    if (!g || !g->obj_ || !layer || !g->fn_) return;
    lv_area_t a;
    lv_obj_get_coords(g->obj_, &a);
    lv_area_increase(&a, -g->frame_, -g->frame_);  // le caselle, senza la cornice
    const int step = g->cell_ + g->gap_;
    // Solo le caselle nella zona da ridisegnare. Con le righe anche quelle subito fuori: il loro bordo
    // spostato fuori puo' stare sulle righe dentro la zona.
    const lv_area_t clip = layer->_clip_area;
    const int m = g->righe_ ? 1 : 0;
    const int x0 = std::max(0, int((clip.x1 - a.x1) / step) - m), x1 = std::min(g->cols_ - 1, int((clip.x2 - a.x1) / step) + m);
    const int y0 = std::max(0, int((clip.y1 - a.y1) / step) - m), y1 = std::min(g->rows_ - 1, int((clip.y2 - a.y1) / step) + m);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            Casella c;
            g->fn_(x, y, c);
            lv_area_t r = {a.x1 + x * step + c.inset, a.y1 + y * step + c.inset, a.x1 + x * step + g->cell_ - 1 - c.inset,
                           a.y1 + y * step + g->cell_ - 1 - c.inset};
            // Bordo spostato fuori: si disegna a parte, dopo il testo.
            const bool fuori = c.borderW && c.outset && g->gap_ && g->righe_;
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_color = c.bg;
            d.bg_opa = c.bgOpa;
            d.radius = c.radius;
            d.border_color = c.border;
            d.border_width = fuori ? 0 : c.borderW;
            d.border_opa = c.borderW && !fuori ? LV_OPA_COVER : LV_OPA_TRANSP;
            lv_draw_rect(layer, &d, &r);
            if (c.text) {
                const lv_font_t* f = c.font ? c.font : g->font_;
                lv_draw_label_dsc_t l;
                lv_draw_label_dsc_init(&l);
                l.text = c.text;
                l.font = f;
                l.color = c.fg;
                l.align = LV_TEXT_ALIGN_CENTER;
                const int h = lv_font_get_line_height(f);
                lv_area_t t = r;
                t.y1 = r.y1 + (lv_area_get_height(&r) - h) / 2;
                t.y2 = t.y1 + h - 1;
                if (!g->righe_) {
                    lv_draw_label(layer, &l, &t);
                } else {
                    // Con le righe il testo resta nella casella (il disegno prende il ritaglio del livello).
                    const lv_area_t dentro = {std::max(clip.x1, r.x1), std::max(clip.y1, r.y1), std::min(clip.x2, r.x2),
                                              std::min(clip.y2, r.y2)};
                    if (dentro.x1 <= dentro.x2 && dentro.y1 <= dentro.y2) {
                        layer->_clip_area = dentro;
                        lv_draw_label(layer, &l, &t);
                        layer->_clip_area = clip;
                    }
                }
            }
            if (fuori) {
                lv_draw_rect_dsc_t b;
                lv_draw_rect_dsc_init(&b);
                b.bg_opa = LV_OPA_TRANSP;
                b.radius = c.radius;
                b.border_color = c.border;
                b.border_width = c.borderW;
                b.border_opa = LV_OPA_COVER;
                lv_area_t o = r;
                const int px = std::min<int>(c.outset, g->gap_);
                lv_area_increase(&o, px, px);
                lv_draw_rect(layer, &b, &o);
            }
        }
}

}  // namespace ui
