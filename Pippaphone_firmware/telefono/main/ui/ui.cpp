#include "ui/ui.h"

#include <ctime>

#include "app/dati.h"
#include "app/eventi.h"
#include "app/rete.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scheda/scheda.h"
#include "ui/schermate.h"

namespace ui {

namespace {
constexpr int W = 480, H = 480, BAR_TOP = 36, BAR_BOTTOM = 32;

lv_display_t* g_disp = nullptr;
lv_obj_t* g_status = nullptr;
lv_obj_t* g_content = nullptr;
lv_obj_t* g_hints = nullptr;
std::vector<std::unique_ptr<Screen>> g_stack;
std::vector<std::unique_ptr<Screen>> g_trash;  // eliminate fuori dal loro onKey
std::string g_hintsKey;

// Barra di stato.
lv_obj_t *g_sbTime, *g_sbUnread, *g_sbSos, *g_sbGps, *g_sbBars[4], *g_sbBatt;

// Notifica e finestra di scelta.
lv_obj_t* g_toast = nullptr;
int64_t g_toastUntil = 0;
struct Ask {
    lv_obj_t* box = nullptr;
    std::vector<lv_obj_t*> rows;
    int sel = 0;
    std::function<void(int)> cb;
} g_ask;

// Schermo.
bool g_on = true;
int64_t g_lastActivity = 0;
int64_t g_pwrDownAt = 0, g_volBothAt = 0;
bool g_volUp = false, g_volDn = false, g_pwrLongDone = false, g_sosDone = false;

int64_t nowMs() { return esp_timer_get_time() / 1000; }

void flushCb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
    scheda::display().draw(a->x1, a->y1, a->x2 + 1, a->y2 + 1, px);
    lv_display_flush_ready(d);
}

uint32_t tickCb() { return uint32_t(esp_timer_get_time() / 1000); }

void buildStatusBar(lv_obj_t* parent) {
    g_status = row(parent, 10);
    lv_obj_set_size(g_status, W, BAR_TOP);
    pad(g_status, 0, 14, 0, 14);
    fill(g_status, NERO());
    lv_obj_set_style_border_side(g_status, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(g_status, GRIGIO1(), 0);
    lv_obj_set_style_border_width(g_status, 1, 0);
    g_sbTime = label(g_status, &mono_20, BIANCO(), "");
    g_sbUnread = label(g_status, &font_18, AMBRA(), "");
    g_sbSos = label(g_status, &font_16, BIANCO(), " SOS ");
    fill(g_sbSos, ROSSO_SCURO());
    radius(g_sbSos, 4);
    lv_obj_t* spacer = box(g_status);
    grow(spacer);
    lv_obj_set_height(spacer, 1);
    g_sbGps = label(g_status, &icone_18, VERDE(), IC_GPS);
    lv_obj_t* bars = box(g_status);
    lv_obj_set_size(bars, 22, 16);
    for (int i = 0; i < 4; i++) {
        g_sbBars[i] = box(bars);
        lv_obj_set_size(g_sbBars[i], 4, 4 + i * 4);
        lv_obj_set_pos(g_sbBars[i], i * 6, 12 - i * 4);
        fill(g_sbBars[i], GRIGIO1());
    }
    g_sbBatt = label(g_status, &font_18, GRIGIO3(), "");
}

void buildHints(const std::vector<Hint>& hints) {
    std::string key;
    for (const auto& h : hints) key += std::string(h.key) + "|" + h.text + "|";
    if (key == g_hintsKey) return;
    g_hintsKey = key;
    lv_obj_clean(g_hints);
    for (const auto& h : hints) {
        lv_obj_t* r = row(g_hints, 6);
        lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_t* k = label(r, &mono_16, BIANCO(), h.key);
        lv_obj_set_style_border_width(k, 1, 0);
        lv_obj_set_style_border_color(k, GRIGIO2(), 0);
        radius(k, 4);
        pad(k, 0, 5, 0, 5);
        label(r, &font_16, GRIGIO3(), h.text.c_str());
    }
}

const char* batteryIcon(uint8_t p) {
    return p > 85 ? IC_BATT_PIENA : p > 60 ? IC_BATT_3 : p > 35 ? IC_BATT_2 : p > 10 ? IC_BATT_1 : IC_BATT_0;
}

void closeAsk() {
    if (g_ask.box) lv_obj_delete(g_ask.box);
    g_ask = Ask();
}

void askFocus() {
    for (size_t i = 0; i < g_ask.rows.size(); i++) {
        const bool on = int(i) == g_ask.sel;
        fill(g_ask.rows[i], on ? AMBRA() : NERO());
        lv_obj_t* l = lv_obj_get_child(g_ask.rows[i], 0);
        lv_obj_set_style_text_color(l, on ? NERO() : BIANCO(), 0);
    }
}

void screenOff() {
    if (!g_on) return;
    g_on = false;
    scheda::display().backlight(0);
    scheda::keyboardLight(0);
}

// Tasti che valgono ovunque. true = consumato.
bool globalKey(Key k, bool pressed) {
    const int64_t now = nowMs();
    if (k == Key::VOLUP) g_volUp = pressed;
    if (k == Key::VOLDN) g_volDn = pressed;
    if (g_volUp && g_volDn) {
        if (!g_volBothAt) g_volBothAt = now;
        return true;
    }
    g_volBothAt = 0;
    g_sosDone = false;
    if (k == Key::PWR) {
        if (pressed) {
            g_pwrDownAt = now;
            g_pwrLongDone = false;
        } else if (!g_pwrLongDone && g_pwrDownAt) {
            g_pwrDownAt = 0;
            if (g_on) {
                screenOff();
                if (!app::dati().settings().pin.empty()) push(makeLock());
            } else {
                wake();
            }
        }
        return true;
    }
    if (!pressed) return false;
    if ((k == Key::VOLUP || k == Key::VOLDN) && !(top() && top()->onKey(k, pressed))) {
        app::Settings s = app::dati().settings();
        if (k == Key::VOLUP && s.volume < 10) s.volume++;
        if (k == Key::VOLDN && s.volume > 0) s.volume--;
        app::dati().setSettings(s);
        scheda::audio().setVolume(s.volume);
        scheda::audio().play(scheda::suoni::CLIC);
        toast("Volume", std::to_string(s.volume) + " su 10", GRIGIO3());
        return true;
    }
    return false;
}

void handleKey(const scheda::KeyEvent& e) {
    g_lastActivity = nowMs();
    if (!g_on) {
        // Il primo tasto accende solo lo schermo (PWR e PTT fanno anche il loro lavoro).
        if (e.pressed && e.key != Key::PWR && e.key != Key::PTT) {
            wake();
            return;
        }
        if (e.key == Key::PTT) wake();
    }
    if (e.pressed && app::dati().settings().keyLight) scheda::keyboardLight(60);
    if (globalKey(e.key, e.pressed)) return;
    // Finestra di scelta aperta.
    if (g_ask.box) {
        if (!e.pressed) return;
        if (e.key == Key::UP && g_ask.sel > 0) g_ask.sel--;
        else if (e.key == Key::DOWN && g_ask.sel + 1 < int(g_ask.rows.size())) g_ask.sel++;
        else if (e.key == Key::OK || e.key == Key::ENTER) {
            auto cb = g_ask.cb;
            int sel = g_ask.sel;
            closeAsk();
            if (cb) cb(sel);
            return;
        } else if (e.key == Key::BACK || e.key == Key::END) {
            auto cb = g_ask.cb;
            closeAsk();
            if (cb) cb(-1);
            return;
        }
        askFocus();
        return;
    }
    Screen* s = top();
    if (!s) return;
    if (s->onKey(e.key, e.pressed)) return;
    if (!e.pressed) return;
    if (e.key == Key::END && !s->capturesEnd()) {
        goHome();
        return;
    }
    if (e.key == Key::BACK && g_stack.size() > 1) pop();
}

void handleEvent(const app::Event& e) {
    using app::Ev;
    auto name = [&](uint16_t n) { return app::dati().nameOf(n); };
    const app::Settings st = app::dati().settings();
    auto alert = [&](const std::vector<scheda::Note>& tune, uint8_t r, uint8_t g, uint8_t b) {
        if (st.ringtone) scheda::audio().play(tune);
        if (st.vibration) scheda::vibratePattern(2, 150, 120);
        scheda::ledBlink(r, g, b, 150, 2500, 0);
    };
    switch (e.type) {
        case Ev::MESSAGE: {
            if (screenOn() && isChatOpen(e.chat)) break;
            if (e.chat.kind == app::ChatKind::GLOBAL) break;  // il canale globale non suona
            app::Chat c = app::dati().chat(e.chat);
            std::string text = c.msgs.empty() ? "" : c.msgs.back().text;
            std::string who = name(e.from);
            if (e.chat.kind == app::ChatKind::GROUP) {
                pn::GroupInfo gi;
                if (app::dati().group(e.chat.id, gi)) who += " · " + gi.name;
            }
            wake();
            toast("Nuovo messaggio · " + who, text);
            alert(st.ringtone == 1 ? scheda::suoni::CLASSICA : scheda::suoni::SQUILLO, 0, 0, 0);
            scheda::ledBlink(40, 30, 0, 150, 2500, 0);
            break;
        }
        case Ev::POS_REQUEST:
            wake();
            alert(scheda::suoni::SQUILLO, 40, 30, 0);
            push(makePositionRequest(e.from, e.id));
            break;
        case Ev::POSITION:
            if (!(screenOn() && isChatOpen(e.chat))) {
                wake();
                toast("Posizione ricevuta", name(e.from));
            }
            break;
        case Ev::SOS:
            wake();
            if (e.a) {
                if (sosShouldAlert(e.from)) {
                    if (g_toast) {  // niente avvisi vecchi sopra l'allarme
                        lv_obj_delete(g_toast);
                        g_toast = nullptr;
                    }
                    scheda::audio().play(scheda::suoni::SOS, true);
                    scheda::vibratePattern(6, 400, 200);
                    scheda::ledBlink(80, 0, 0, 300, 300, 0);
                    push(makeSosReceived(e.from));
                } else {
                    // Ripetizione di un SOS gia' visto: solo un avviso (con la posizione nuova).
                    if (st.vibration) scheda::vibrate(300);
                    toast("SOS ancora attivo", name(e.from) + ": posizione aggiornata", ROSSO());
                }
            } else {
                sosCancelled(e.from);
                scheda::audio().stop();
                scheda::ledOff();
                toast("SOS annullato", name(e.from), VERDE());
            }
            break;
        case Ev::GROUP_INVITE: {
            pn::GroupInfo gi;
            app::dati().group(uint16_t(e.id), gi);
            wake();
            toast("Nuovo gruppo", name(e.from) + " ti ha aggiunto a " + gi.name);
            if (st.vibration) scheda::vibrate(150);
            break;
        }
        case Ev::VOICE_INCOMING:
            wake();
            toast("Walkie-talkie", name(e.from) + " sta parlando...", VERDE());
            break;
        case Ev::VOICE_READY:
            wake();
            if (walkieTakesVoice(e.from)) break;  // lo riproduce il walkie-talkie aperto
            if (walkieRecording() || scheda::audio().recording()) {
                toast("Vocale da " + name(e.from), "Premi CALL dopo per ascoltarlo", VERDE());
                break;
            }
            push(makeVoicePlayer(e.from, e.id, true));
            break;
        default:
            break;
    }
    // La schermata in cima puo' essere cambiata: le si passa comunque l'evento.
    if (top()) top()->onEvent(e);
    refreshBars();
}

void uiTask(void*) {
    int64_t lastTick = 0, lastBars = 0;
    goHome();  // principale, oppure primo avvio se manca il numero
    if (!app::rete().radioOk()) toast("Radio LoRa non trovata", "Controlla il modulo SX1262", ROSSO());
    for (;;) {
        scheda::KeyEvent ke;
        while (xQueueReceive(scheda::tastiera().queue(), &ke, 0) == pdTRUE) handleKey(ke);
        app::Event ev;
        while (app::poll(ev)) handleEvent(ev);
        const int64_t now = nowMs();
        // PWR tenuto premuto: spegnimento.
        if (g_pwrDownAt && !g_pwrLongDone && now - g_pwrDownAt > 1500) {
            g_pwrLongDone = true;
            g_pwrDownAt = 0;
            wake();
            ask("Spegnere il telefono?", "Non riceverai messaggi finché non lo riaccendi (tieni premuto PWR).",
                {"Spegni", "Annulla"}, [](int i) {
                    if (i == 0) scheda::powerOff();
                });
        }
        // VOL+ e VOL- insieme per 3 secondi: SOS.
        if (g_volBothAt && !g_sosDone && now - g_volBothAt > 3000) {
            g_sosDone = true;
            wake();
            scheda::vibrate(600);
            push(makeSosActive(true));
        }
        if (now - lastTick > 250) {
            lastTick = now;
            if (top()) top()->tick();
            if (g_toast && now > g_toastUntil) {
                lv_obj_delete(g_toast);
                g_toast = nullptr;
            }
        }
        if (now - lastBars > 2000) {
            lastBars = now;
            refreshBars();
        }
        // Schermo spento dopo un po' senza tasti (non durante SOS e vocali).
        const uint16_t off = app::dati().settings().screenOffS;
        if (g_on && off && now - g_lastActivity > int64_t(off) * 1000 && !app::rete().sosActive() &&
            !scheda::audio().busy() && !scheda::audio().recording()) {
            screenOff();
            if (!app::dati().settings().pin.empty() && !isLockOpen()) push(makeLock());
        }
        g_trash.clear();
        const uint32_t wait = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(g_on ? std::min<uint32_t>(wait, 20) : 60));
    }
}
}  // namespace

// ---------------------------------------------------------------- Screen

void Screen::rebuild() {
    if (!root_) return;
    lv_obj_clean(root_);
    build(root_);
    buildHints(hints());
}

void showTop() {
    Screen* s = top();
    // Una domanda aperta riguarda la schermata di prima: si chiude senza risposta.
    closeAsk();
    lv_obj_clean(g_content);
    if (!s) return;
    const bool full = s->fullscreen();
    if (full) {
        lv_obj_add_flag(g_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_hints, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(g_content, W, H);
    } else {
        lv_obj_clear_flag(g_status, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_hints, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(g_content, W, H - BAR_TOP - BAR_BOTTOM);
    }
    s->root_ = g_content;
    s->build(g_content);
    g_hintsKey.clear();
    buildHints(s->hints());
    refreshBars();
}

void init() {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    lv_init();
    lv_tick_set_cb(tickCb);
    g_disp = lv_display_create(W, H);
    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
    const size_t lines = 48;
    const size_t bytes = W * lines * 2;
    void* b1 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void* b2 = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    lv_display_set_buffers(g_disp, b1, b2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(g_disp, flushCb);

    lv_obj_t* scr = lv_screen_active();
    fill(scr, NERO());
    lv_obj_set_style_text_color(scr, BIANCO(), 0);
    lv_obj_set_style_text_font(scr, &font_22, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    // Il tema mette spazi tra i figli dello schermo: le tre fasce devono stare attaccate.
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);
    buildStatusBar(scr);
    g_content = box(scr);
    lv_obj_set_size(g_content, W, H - BAR_TOP - BAR_BOTTOM);
    g_hints = row(scr, 16);
    lv_obj_set_size(g_hints, W, BAR_BOTTOM);
    pad(g_hints, 0, 14, 0, 14);
    lv_obj_set_style_border_side(g_hints, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(g_hints, GRIGIO1(), 0);
    lv_obj_set_style_border_width(g_hints, 1, 0);

    g_lastActivity = nowMs();
    xTaskCreatePinnedToCore(uiTask, "ui", 16384, nullptr, 5, nullptr, 1);
}

void push(Screen* s) {
    g_stack.emplace_back(s);
    showTop();
}

void pop() {
    if (g_stack.size() <= 1) return;
    g_trash.push_back(std::move(g_stack.back()));
    g_stack.pop_back();
    showTop();
}

void replace(Screen* s) {
    if (!g_stack.empty()) {
        g_trash.push_back(std::move(g_stack.back()));
        g_stack.pop_back();
    }
    push(s);
}

void goHome() {
    while (g_stack.size() > 1) {
        g_trash.push_back(std::move(g_stack.back()));
        g_stack.pop_back();
    }
    if (g_stack.empty()) g_stack.emplace_back(app::rete().hasNumber() ? makeHome() : makeFirstBoot());
    showTop();
}

Screen* top() { return g_stack.empty() ? nullptr : g_stack.back().get(); }

void refreshBars() {
    if (!g_status) return;
    Screen* s = top();
    const uint32_t t = app::rete().unixTime();
    lv_label_set_text(g_sbTime, (s && !s->showClock()) || !t ? "" : timeHM(t).c_str());
    const uint16_t unread = app::dati().unreadTotal();
    lv_label_set_text(g_sbUnread, unread ? (std::string(IC_BUSTA " ") + std::to_string(unread)).c_str() : "");
    if (app::rete().sosActive()) lv_obj_clear_flag(g_sbSos, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g_sbSos, LV_OBJ_FLAG_HIDDEN);
    const scheda::Fix f = scheda::gnss().fix();
    lv_obj_set_style_text_color(g_sbGps, f.valid ? VERDE() : GRIGIO1(), 0);
    const app::Rete::Signal sig = app::rete().signal();
    for (int i = 0; i < 4; i++) fill(g_sbBars[i], i < sig.bars ? BIANCO() : GRIGIO1());
    const uint8_t b = scheda::batteria().percent();
    const bool chg = scheda::tastiera().charging();
    std::string bt = b <= 100 ? std::to_string(b) + "% " : "";
    bt += chg ? IC_CARICA : batteryIcon(b <= 100 ? b : 50);
    lv_label_set_text(g_sbBatt, bt.c_str());
    if (s) buildHints(s->hints());
}

void toast(const std::string& title, const std::string& text, lv_color_t accent) {
    if (g_toast) lv_obj_delete(g_toast);
    g_toast = column(lv_layer_top(), 4);
    lv_obj_set_width(g_toast, 452);
    lv_obj_set_height(g_toast, LV_SIZE_CONTENT);
    lv_obj_align(g_toast, LV_ALIGN_TOP_MID, 0, 42);
    fill(g_toast, GRIGIO1());
    radius(g_toast, 12);
    lv_obj_set_style_border_width(g_toast, 2, 0);
    lv_obj_set_style_border_color(g_toast, accent, 0);
    pad(g_toast, 10, 14, 12, 14);
    label(g_toast, &font_18, accent, title.c_str());
    if (!text.empty()) {
        lv_obj_t* l = label(g_toast, &font_22, BIANCO(), text.c_str());
        lv_obj_set_width(l, 420);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_height(l, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(l, 60, 0);
    }
    g_toastUntil = nowMs() + 4000;
}

void ask(const std::string& title, const std::string& text, const std::vector<std::string>& options,
         std::function<void(int)> cb) {
    closeAsk();
    g_ask.box = column(lv_layer_top(), 10);
    lv_obj_set_size(g_ask.box, 440, LV_SIZE_CONTENT);
    lv_obj_center(g_ask.box);
    fill(g_ask.box, GRIGIO1());
    radius(g_ask.box, 16);
    lv_obj_set_style_border_width(g_ask.box, 2, 0);
    lv_obj_set_style_border_color(g_ask.box, AMBRA(), 0);
    pad(g_ask.box, 18, 18, 18, 18);
    label(g_ask.box, &font_26b, BIANCO(), title.c_str());
    if (!text.empty()) {
        lv_obj_t* l = label(g_ask.box, &font_20, GRIGIO3(), text.c_str());
        lv_obj_set_width(l, 400);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
    for (const auto& o : options) {
        lv_obj_t* r = row(g_ask.box, 0);
        lv_obj_set_size(r, 400, 48);
        radius(r, 24);
        pad(r, 0, 18, 0, 18);
        label(r, &font_22b, BIANCO(), o.c_str());
        g_ask.rows.push_back(r);
    }
    g_ask.sel = 0;
    g_ask.cb = std::move(cb);
    askFocus();
}

void wake() {
    g_lastActivity = nowMs();
    if (g_on) return;
    g_on = true;
    scheda::display().backlight(app::dati().settings().brightness);
    if (app::dati().settings().keyLight) scheda::keyboardLight(60);
    lv_obj_invalidate(lv_screen_active());
}

bool screenOn() { return g_on; }

// ---------------------------------------------------------------- formattazione

static std::tm local(uint32_t unixSec) {
    time_t t = time_t(unixSec);
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm;
}

std::string timeHM(uint32_t unixSec) {
    if (!unixSec) return "--:--";
    std::tm tm = local(unixSec);
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return b;
}

std::string dateLong(uint32_t unixSec) {
    if (!unixSec) return "ora non ancora sincronizzata";
    static const char* days[] = {"domenica", "lunedì", "martedì", "mercoledì", "giovedì", "venerdì", "sabato"};
    static const char* months[] = {"gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno",
                                   "luglio", "agosto", "settembre", "ottobre", "novembre", "dicembre"};
    std::tm tm = local(unixSec);
    return std::string(days[tm.tm_wday]) + " " + std::to_string(tm.tm_mday) + " " + months[tm.tm_mon];
}

std::string whenShort(uint32_t unixSec) {
    if (!unixSec) return "";
    const uint32_t now = app::rete().unixTime();
    std::tm a = local(unixSec), b = local(now ? now : unixSec);
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) return timeHM(unixSec);
    if (a.tm_year == b.tm_year && a.tm_yday + 1 == b.tm_yday) return "ieri";
    char s[16];
    snprintf(s, sizeof(s), "%02d/%02d", a.tm_mday, a.tm_mon + 1);
    return s;
}

std::string durationMS(uint32_t ms) {
    char b[12];
    snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(ms / 60000), (unsigned long)((ms / 1000) % 60));
    return b;
}

}  // namespace ui
