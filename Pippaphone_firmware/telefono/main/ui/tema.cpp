#include "ui/tema.h"

namespace ui {

namespace {
bool g_chiaro = true;   // Carta
int g_scuroFisso = 0;   // TemaScuroFisso attivi
}  // namespace

bool temaChiaro() { return g_chiaro && g_scuroFisso == 0; }
void setTemaChiaro(bool chiaro) { g_chiaro = chiaro; }
TemaScuroFisso::TemaScuroFisso() { g_scuroFisso++; }
TemaScuroFisso::~TemaScuroFisso() { g_scuroFisso--; }

void superficie(lv_obj_t* o) {
    fill(o, SUPERFICIE());
    if (temaChiaro()) {
        lv_obj_set_style_border_width(o, 2, 0);
        lv_obj_set_style_border_color(o, INCHIOSTRO(), 0);
    }
}

lv_color_t avatarColor(uint16_t number) {
    static const uint32_t colors[] = {0x004080, 0x408040, 0x804080, 0x806000, 0x008080, 0x800040};
    return lv_color_hex(colors[number % (sizeof(colors) / sizeof(colors[0]))]);
}

void initStyles() {}

lv_obj_t* box(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t* column(lv_obj_t* parent, int gap) {
    lv_obj_t* o = box(parent);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o, gap, 0);
    return o;
}

lv_obj_t* row(lv_obj_t* parent, int gap) {
    lv_obj_t* o = box(parent);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, gap, 0);
    return o;
}

void fill(lv_obj_t* o, lv_color_t color) {
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

void radius(lv_obj_t* o, int r) { lv_obj_set_style_radius(o, r, 0); }

void pad(lv_obj_t* o, int top, int right, int bottom, int left) {
    lv_obj_set_style_pad_top(o, top, 0);
    lv_obj_set_style_pad_right(o, right, 0);
    lv_obj_set_style_pad_bottom(o, bottom, 0);
    lv_obj_set_style_pad_left(o, left, 0);
}

void grow(lv_obj_t* o) { lv_obj_set_flex_grow(o, 1); }

}  // namespace ui
