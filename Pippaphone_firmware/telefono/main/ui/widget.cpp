#include "ui/widget.h"

#include "esp_timer.h"
#include "pippanet/charset.h"

namespace ui {

namespace {
int64_t nowMs() { return esp_timer_get_time() / 1000; }

void setCheckedDeep(lv_obj_t* o, bool on) {
    if (on) lv_obj_add_state(o, LV_STATE_CHECKED);
    else lv_obj_clear_state(o, LV_STATE_CHECKED);
    const uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) setCheckedDeep(lv_obj_get_child(o, int32_t(i)), on);
}

// Simboli del tasto SYM: accenti e punteggiatura (tutti nell'alfabeto a 7 bit).
const char* const SYMS[] = {"à", "è", "é", "ì", "ò", "ù", "€", "£", "$", "@", "#", "%", "&", "*", "(", ")",
                            "-", "_", "+", "=", "/", ":", ";", "'", "\"", "!", "?", ",", ".", "<", ">", "°",
                            "À", "È", "É", "Ì", "Ò", "Ù", "«", "»"};
constexpr int SYM_COLS = 8;
constexpr int SYM_COUNT = sizeof(SYMS) / sizeof(SYMS[0]);
}  // namespace

// ---------------------------------------------------------------- List

void List::create(lv_obj_t* parent, int w, int h) {
    cont_ = lv_obj_create(parent);
    lv_obj_remove_style_all(cont_);
    lv_obj_set_size(cont_, w, h);
    lv_obj_set_flex_flow(cont_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(cont_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cont_, LV_OBJ_FLAG_CLICKABLE);
    rows_.clear();
    sel_ = 0;
}

void List::clear() {
    lv_obj_clean(cont_);
    rows_.clear();
    sel_ = 0;
}

lv_obj_t* List::addRow(int height) {
    lv_obj_t* r = row(cont_, 12);
    lv_obj_set_size(r, lv_pct(100), height);
    pad(r, 0, 16, 0, 16);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(r, AMBRA(), LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_STATE_CHECKED);
    underline(r);
    rows_.push_back(r);
    if (rows_.size() == 1) setCheckedDeep(r, true);
    return r;
}

lv_obj_t* List::text(lv_obj_t* r, const lv_font_t* font, lv_color_t color, const std::string& s, bool expand) {
    lv_obj_t* l = label(r, font, color, s.c_str());
    lv_obj_set_style_text_color(l, NERO(), LV_STATE_CHECKED);
    if (expand) {
        grow(l);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
    return l;
}

void List::focus(int i) {
    if (rows_.empty()) return;
    if (i < 0) i = 0;
    if (i >= int(rows_.size())) i = int(rows_.size()) - 1;
    setCheckedDeep(rows_[sel_], false);
    sel_ = i;
    setCheckedDeep(rows_[sel_], true);
    lv_obj_scroll_to_view(rows_[sel_], LV_ANIM_OFF);
}

bool List::key(Key k) {
    if (rows_.empty()) return false;
    switch (k) {
        case Key::UP: focus(sel_ - 1); return true;
        case Key::DOWN: focus(sel_ + 1); return true;
        case Key::LEFT: focus(sel_ - 5); return true;
        case Key::RIGHT: focus(sel_ + 5); return true;
        default: return false;
    }
}

// ---------------------------------------------------------------- TextInput

void TextInput::create(lv_obj_t* parent, int w, int h, bool multiline, size_t maxChars, const char* placeholder) {
    max_ = maxChars;
    wrap_ = row(parent, 8);
    lv_obj_set_size(wrap_, w, h);
    pad(wrap_, 4, 12, 4, 12);
    radius(wrap_, 12);
    lv_obj_set_style_border_width(wrap_, 2, 0);
    lv_obj_set_style_border_color(wrap_, GRIGIO1(), 0);
    ta_ = lv_textarea_create(wrap_);
    lv_obj_remove_style_all(ta_);
    grow(ta_);
    lv_obj_set_height(ta_, lv_pct(100));
    lv_obj_set_style_text_font(ta_, &font_22, 0);
    lv_obj_set_style_text_color(ta_, BIANCO(), 0);
    lv_obj_set_style_text_color(ta_, GRIGIO2(), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(ta_, AMBRA(), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta_, LV_OPA_COVER, uint32_t(LV_PART_CURSOR) | LV_STATE_FOCUSED);
    lv_obj_set_style_width(ta_, 2, LV_PART_CURSOR);
    lv_obj_set_style_anim_duration(ta_, 500, uint32_t(LV_PART_CURSOR) | LV_STATE_FOCUSED);
    lv_textarea_set_one_line(ta_, !multiline);
    lv_textarea_set_placeholder_text(ta_, placeholder);
    lv_obj_add_state(ta_, LV_STATE_FOCUSED);
    lv_obj_set_scrollbar_mode(ta_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t* side = column(wrap_, 2);
    lv_obj_set_size(side, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    mode_ = label(side, &mono_16, AMBRA(), "");
    counter_ = label(side, &mono_16, GRIGIO2(), "");
    updateCounter();
}

void TextInput::setFocused(bool f) {
    lv_obj_set_style_border_color(wrap_, f ? AMBRA() : GRIGIO1(), 0);
    if (f) lv_obj_add_state(ta_, LV_STATE_FOCUSED);
    else lv_obj_clear_state(ta_, LV_STATE_FOCUSED);
}

void TextInput::setPassword(bool p) { lv_textarea_set_password_mode(ta_, p); }

std::string TextInput::text() const { return ta_ ? lv_textarea_get_text(ta_) : ""; }

size_t TextInput::length() const { return pn::textLength(text()); }

void TextInput::setText(const std::string& s) {
    lv_textarea_set_text(ta_, s.c_str());
    updateCounter();
}

void TextInput::updateCounter() {
    if (max_ >= 40) lv_label_set_text(counter_, (std::to_string(length()) + "/" + std::to_string(max_)).c_str());
    else lv_label_set_text(counter_, "");
    std::string m;
    if (altLock_) m = "123";
    else if (alt_) m = "alt";
    else if (caps_) m = "ABC";
    else if (shift_) m = "Abc";
    lv_label_set_text(mode_, m.c_str());
}

void TextInput::insert(const char* utf8) {
    if (length() >= max_) return;
    lv_textarea_add_text(ta_, utf8);
}

TextInput::Result TextInput::key(Key k, bool pressed) {
    if (!pressed) return NONE;
    if (sym_) {
        switch (k) {
            case Key::UP: symSel_ = std::max(0, symSel_ - SYM_COLS); break;
            case Key::DOWN: symSel_ = std::min(SYM_COUNT - 1, symSel_ + SYM_COLS); break;
            case Key::LEFT: symSel_ = std::max(0, symSel_ - 1); break;
            case Key::RIGHT: symSel_ = std::min(SYM_COUNT - 1, symSel_ + 1); break;
            case Key::OK:
            case Key::ENTER:
                insert(SYMS[symSel_]);
                closeSym();
                updateCounter();
                return CHANGED;
            case Key::BACK:
            case Key::SYM:
            case Key::END: closeSym(); return NONE;
            default: break;
        }
        symFocus();
        return NONE;
    }
    const int64_t now = nowMs();
    const bool altActive = alt_ || altLock_ || digits_;
    uint32_t ch = 0;
    const char* str = nullptr;
    char buf[8] = {};
    switch (k) {
        case Key::LSHIFT:
        case Key::RSHIFT:
            if (caps_) {
                caps_ = shift_ = false;
            } else if (shift_ && now - lastShift_ < 500) {
                caps_ = true;
                shift_ = false;
            } else {
                shift_ = !shift_;
            }
            lastShift_ = now;
            updateCounter();
            return NONE;
        case Key::ALT:
            if (altLock_) {
                altLock_ = alt_ = false;
            } else if (alt_ && now - lastAlt_ < 500) {
                altLock_ = true;
                alt_ = false;
            } else {
                alt_ = !alt_;
            }
            lastAlt_ = now;
            updateCounter();
            return NONE;
        case Key::SYM:
            if (!digits_) openSym();
            return NONE;
        case Key::BKSP:
            lv_textarea_delete_char(ta_);
            updateCounter();
            return CHANGED;
        case Key::ENTER:
            return SUBMIT;
        case Key::SPACE:
            if (!digits_) str = " ";
            break;
        case Key::LEFT:
            lv_textarea_cursor_left(ta_);
            return NONE;
        case Key::RIGHT:
            lv_textarea_cursor_right(ta_);
            return NONE;
        case Key::DOLLAR:
            str = altActive ? "$" : "€";
            break;
        case Key::ZERO_MIC:
            if (altActive) ch = '0';
            break;
        default: {
            if (altActive) {
                ch = scheda::altCharOf(k);
                if (digits_ && (ch < '0' || ch > '9')) ch = 0;
            } else {
                char c = scheda::letterOf(k);
                if (c) {
                    // Maiuscola automatica a inizio frase.
                    const std::string t = text();
                    const bool sentence = t.empty() || (t.size() >= 2 && t.back() == ' ' &&
                                                         (t[t.size() - 2] == '.' || t[t.size() - 2] == '!' ||
                                                          t[t.size() - 2] == '?'));
                    const bool upper = caps_ || shift_ || (sentence && max_ >= 40);
                    ch = uint32_t(upper ? c - 32 : c);
                }
            }
            break;
        }
    }
    if (ch) {
        buf[0] = char(ch);
        str = buf;
    }
    if (!str) return NONE;
    insert(str);
    if (shift_) shift_ = false;
    if (alt_) alt_ = false;
    updateCounter();
    return CHANGED;
}

void TextInput::openSym() {
    sym_ = column(lv_layer_top(), 8);
    lv_obj_set_size(sym_, 452, LV_SIZE_CONTENT);
    lv_obj_align(sym_, LV_ALIGN_BOTTOM_MID, 0, -110);
    fill(sym_, GRIGIO1());
    radius(sym_, 12);
    pad(sym_, 10, 12, 12, 12);
    lv_obj_t* head = row(sym_, 0);
    lv_obj_set_width(head, lv_pct(100));
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_t* t = label(head, &font_18, BIANCO(), "Simboli");
    grow(t);
    label(head, &font_16, GRIGIO3(), "set ridotto a 7 bit, come gli SMS");
    lv_obj_t* grid = box(sym_);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 6, 0);
    lv_obj_set_style_pad_column(grid, 6, 0);
    symCells_.clear();
    for (int i = 0; i < SYM_COUNT; i++) {
        lv_obj_t* c = box(grid);
        lv_obj_set_size(c, 47, 38);
        radius(c, 8);
        lv_obj_t* l = label(c, &font_22b, BIANCO(), SYMS[i]);
        lv_obj_center(l);
        symCells_.push_back(c);
    }
    symSel_ = 0;
    symFocus();
}

void TextInput::symFocus() {
    for (int i = 0; i < int(symCells_.size()); i++) {
        const bool on = i == symSel_;
        fill(symCells_[i], on ? AMBRA() : NERO());
        lv_obj_set_style_text_color(lv_obj_get_child(symCells_[i], 0), on ? NERO() : BIANCO(), 0);
    }
}

void TextInput::closeSym() {
    if (sym_) lv_obj_delete(sym_);
    sym_ = nullptr;
    symCells_.clear();
}

// ---------------------------------------------------------------- vari

lv_obj_t* header(lv_obj_t* parent, const char* icon, const std::string& title, const std::string& right,
                 lv_color_t iconColor) {
    lv_obj_t* h = row(parent, 10);
    lv_obj_set_size(h, lv_pct(100), 48);
    pad(h, 0, 14, 0, 14);
    underline(h);
    if (icon) label(h, &icone_22, iconColor, icon);
    lv_obj_t* t = label(h, &font_26b, BIANCO(), title.c_str());
    grow(t);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    if (!right.empty()) label(h, &font_16, GRIGIO2(), right.c_str());
    return h;
}

lv_obj_t* avatar(lv_obj_t* parent, uint16_t number, const std::string& name, int size) {
    lv_obj_t* a = box(parent);
    lv_obj_set_size(a, size, size);
    radius(a, LV_RADIUS_CIRCLE);
    fill(a, avatarColor(number));
    std::string ini;
    if (!name.empty() && !(name[0] >= '0' && name[0] <= '9')) {
        // Prima lettera (anche accentata: fino al prossimo carattere UTF-8).
        size_t n = 1;
        while (n < name.size() && (uint8_t(name[n]) & 0xC0) == 0x80) n++;
        ini = name.substr(0, n);
    } else {
        ini = "#";
    }
    lv_obj_t* l = label(a, size >= 70 ? &font_32b : &font_22b, BIANCO(), ini.c_str());
    lv_obj_center(l);
    return a;
}

void underline(lv_obj_t* o) {
    lv_obj_set_style_border_side(o, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(o, GRIGIO1(), 0);
    lv_obj_set_style_border_width(o, 1, 0);
}

}  // namespace ui
