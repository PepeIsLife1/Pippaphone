// Orologio: sveglie, timer e cronometro (tre schede: sinistra/destra per cambiare), modifica di una
// sveglia, schermata che suona. clockService() fa suonare sveglie e timer da qualsiasi schermata,
// anche a schermo spento; clockBeforePowerOff() fa risvegliare il T-Deck spento per la sveglia.
#include <algorithm>
#include <cstdio>
#include <ctime>

#include "app/orologio.h"
#include "app/rete.h"
#include "esp_timer.h"
#include "scheda/scheda.h"
#include "ui/schermate.h"
#include "ui/widget.h"

namespace ui {

class Ring;

namespace {
int64_t nowMs() { return esp_timer_get_time() / 1000; }

// Cifra scritta coi tasti: W E R = 1 2 3, S D F = 4 5 6, Z X C = 7 8 9, microfono = 0 (-1 = no).
int digitOf(Key k) {
    const uint32_t c = k == Key::ZERO_MIC ? '0' : scheda::altCharOf(k);
    return c >= '0' && c <= '9' ? int(c - '0') : -1;
}

// "05:00", "1:05:00" (ore solo se servono).
std::string hms(uint32_t s) {
    char b[16];
    if (s >= 3600) snprintf(b, sizeof(b), "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else snprintf(b, sizeof(b), "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    return b;
}

// Cronometro: "01:23.4" (decimi), "1:02:03" oltre l'ora.
std::string chrono(uint32_t ms) {
    char b[16];
    const uint32_t s = ms / 1000;
    if (s >= 3600) return hms(s);
    snprintf(b, sizeof(b), "%02lu:%02lu.%lu", (unsigned long)(s / 60), (unsigned long)(s % 60), (unsigned long)(ms / 100 % 10));
    return b;
}

// "tra 3 h 12 min", "tra 5 min", "tra meno di un minuto".
std::string fraQuanto(uint32_t s) {
    const uint32_t m = (s + 59) / 60;
    if (m <= 1) return "tra meno di un minuto";
    if (m < 60) return "tra " + std::to_string(m) + " min";
    const uint32_t h = m / 60, r = m % 60;
    return "tra " + std::to_string(h) + " h" + (r ? " " + std::to_string(r) + " min" : "");
}

// "oggi alle 07:00", "domani alle 07:00", "lunedì alle 07:00".
std::string quandoText(uint32_t when, uint32_t now) {
    const time_t a = time_t(when), b = time_t(now);
    std::tm ta{}, tb{};
    localtime_r(&a, &ta);
    localtime_r(&b, &tb);
    static const char* days[] = {"domenica", "lunedì", "martedì", "mercoledì", "giovedì", "venerdì", "sabato"};
    std::string d;
    if (ta.tm_year == tb.tm_year && ta.tm_yday == tb.tm_yday) d = "oggi";
    else if (when - now < 2 * 86400 && (ta.tm_wday == (tb.tm_wday + 1) % 7)) d = "domani";
    else d = days[ta.tm_wday];
    return d + " alle " + timeHM(when);
}

// Ripeti: le scelte una dopo l'altra.
const uint8_t RIPETI[] = {0, 0x7F, 0x1F, 0x60, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40};

int g_ringOpen = 0;  // schermate che suonano (contatore: la vecchia si distrugge dopo la nuova)
Ring* g_ring = nullptr;  // l'ultima schermata che suona (per ringOnTop)
bool g_askedAfterWake = false;  // T-Deck acceso dalla sveglia: domanda gia' fatta

// Sveglia senza risposta: dopo 2 minuti si posticipa da sola, al massimo POSTICIPI_AUTO volte di
// fila, poi smette (un T-Deck acceso dalla sveglia e mai usato torna spento).
constexpr int POSTICIPI_AUTO = 3;
int g_posticipiAuto = 0;  // di fila; si azzerano con un tasto o quando non c'e' piu' un posticipo
// Telefono usato dall'avvio: un tasto, una chiamata, un SOS o una richiesta (clockNoteActivity), o
// un'altra schermata o una domanda sopra la sveglia.
bool g_usato = false;

// T-Deck acceso dalla sveglia che torna spento ("Spegni" nella domanda, o sveglia senza risposta): si
// risveglia per la prossima.
void powerOffNow() {
    clockBeforePowerOff();
    scheda::powerOff();
}

// Testo tenue (aiuti, sveglie spente): nello scuro grigio medio come GRIGIO2, in Carta grigio scuro
// (il grigio medio sul bianco al sole si legge male).
lv_color_t TENUE() { return colore(0x808080, 0x404040); }
// Cifre grandi di un timer in pausa o di un cronometro fermo: nello scuro GRIGIO3, in Carta grigio
// medio (il grigio scuro accanto al nero non si distinguerebbe).
lv_color_t FERMO() { return colore(0xC0C0C0, 0x808080); }

// Carta: linguetta di quaderno con gli angoli arrotondati solo in alto (il rettangolo esce dal fondo
// dell'oggetto e il disegno viene tagliato li'). Piena di nero quella scelta, le altre col bordo nero.
void linguetta(lv_obj_t* t, bool on) {
    lv_obj_add_event_cb(
        t,
        [](lv_event_t* e) {
            const int r = PX(8, 5);
            lv_area_t a;
            lv_obj_get_coords(lv_event_get_target_obj(e), &a);
            a.y2 += r;
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.radius = r;
            d.bg_color = INCHIOSTRO();
            d.bg_opa = lv_event_get_user_data(e) ? LV_OPA_COVER : LV_OPA_TRANSP;
            d.border_color = INCHIOSTRO();
            d.border_width = 2;
            d.border_opa = LV_OPA_COVER;
            lv_draw_rect(lv_event_get_layer(e), &d, &a);
        },
        LV_EVENT_DRAW_MAIN, on ? t : nullptr);
}

// Carta: tasto scritto in bianco su una pastiglia nera, come nella barra dei tasti (sulla PCB col
// carattere a spaziatura fissa e piu' marcato, come li').
lv_obj_t* pastiglia(lv_obj_t* parent, const char* text) {
    lv_obj_t* l = label(parent, PX(&mono_20, &font_20), SU_COLORE(), text);
    fill(l, INCHIOSTRO());
    radius(l, PX(6, 4));
    pad(l, PX(1, 0), PX(8, 5), PX(1, 0), PX(8, 5));
    return l;
}
}  // namespace

// ================================================================ suona

class Ring : public Screen {
public:
    // alarm = indice della sveglia; -1 = timer.
    explicit Ring(int alarm) : alarm_(alarm) {
        g_ringOpen++;
        g_ring = this;
        if (alarm_ >= 0) {
            const auto s = app::orologio().sveglie();
            if (alarm_ < int(s.size())) name_ = s[size_t(alarm_)].nome;
        }
        startedAt_ = lastTick_ = nowMs();
        oldVolume_ = scheda::audio().volume();
        // Anche col volume basso o a zero la sveglia si deve sentire (come sui telefoni veri).
        scheda::audio().setVolume(std::max<uint8_t>(oldVolume_, 6));
        sound();
        wake();
    }
    ~Ring() override {
        scheda::audio().stop();
        scheda::audio().setVolume(oldVolume_);
        scheda::ledOff();
        g_ringOpen--;
        if (g_ring == this) g_ring = nullptr;  // la vecchia si distrugge dopo la nuova
    }
    void sound() {
        scheda::audio().play(alarm_ >= 0 ? scheda::suoni::SVEGLIA : scheda::suoni::TIMER, true);
        scheda::vibratePattern(3, 400, 300);
        scheda::ledBlink(60, 40, 0, 300, 300, 0);
        lastBuzz_ = nowMs();
    }
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, PX(14, 6));
        lv_obj_set_size(col, W_SCHERMO, H_SCHERMO);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        fill(col, NERO());
        // Carta: foglio bianco con l'ora nera, il campanello e una cornice spessa arancio (e' la cosa
        // urgente) e i tasti come nella barra dei tasti.
        const bool carta = temaChiaro();
        if (carta) {
            lv_obj_set_style_border_width(col, PX(6, 4), 0);
            lv_obj_set_style_border_color(col, ROSSO(), 0);
        }
        label(col, &icone_44, carta ? ROSSO() : AMBRA(), alarm_ >= 0 ? IC_SVEGLIA : IC_CLESSIDRA);
        clock_ = label(col, &mono_104, BIANCO(), "");
        lv_obj_t* t = label(col, &font_32b, carta ? BIANCO() : AMBRA(),
                            alarm_ >= 0 ? (name_.empty() ? "Sveglia" : name_.c_str()) : "Tempo scaduto");
        rigaPiena(t, true);
        if (carta) {
            lv_obj_t* r = row(col, PX(8, 5));
            lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            pad(r, PX(6, 2), 0, 0, 0);
            pastiglia(r, "OK");
            label(r, &font_20, BIANCO(), alarm_ >= 0 ? "ferma" : "o un altro tasto: ferma");
            if (alarm_ >= 0) {
                lv_obj_set_style_margin_left(pastiglia(r, TXT("un altro tasto", "altro tasto")), PX(12, 8), 0);
                label(r, &font_20, BIANCO(), "tra 5 minuti");
            }
            update();
            return;
        }
        lv_obj_t* h = label(col, &font_20, GRIGIO3(),
                            alarm_ >= 0 ? TXT("OK: ferma · un altro tasto: tra 5 minuti",
                                              "OK: ferma · altro tasto: tra 5 minuti")
                                        : "OK o un altro tasto: ferma");
        lv_obj_set_width(h, PX(440, 300));
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(h, LV_TEXT_ALIGN_CENTER, 0);
        update();
    }
    void update() {
        const uint32_t t = app::rete().unixTime();
        lv_label_set_text(clock_, t ? timeHM(t).c_str() : "--:--");
    }
    void tick() override {
        wake();
        update();
        const int64_t now = nowMs();
        // Torna in cima dopo un'altra schermata (chiamata, SOS...) o c'e' una domanda aperta: qualcuno
        // usa il telefono.
        if (now - lastTick_ > 2000 || askOpen()) g_usato = true;
        lastTick_ = now;
        // Dopo 2 minuti smette: la sveglia si ripresenta tra 5 minuti (POSTICIPI_AUTO volte di fila,
        // poi si ferma), il timer si ferma.
        if (now - startedAt_ > 2 * 60 * 1000) {
            if (alarm_ >= 0 && g_posticipiAuto < POSTICIPI_AUTO) {
                g_posticipiAuto++;
                finish(true);
            } else if (alarm_ >= 0 && scheda::wokeForAlarm() && !g_usato) {
                // T-Deck acceso da spento per la sveglia e mai usato dall'avvio: torna spento (e si
                // risveglia per la prossima sveglia).
                app::orologio().annullaPosticipo();
                powerOffNow();
            } else {
                finish(false);
            }
            return;
        }
        // Ogni 3 secondi: vibrazione e, se un altro suono (un messaggio) ha preso il posto della
        // sveglia, si riparte.
        if (now - lastBuzz_ > 3000) {
            if (!scheda::audio().busy()) scheda::audio().play(alarm_ >= 0 ? scheda::suoni::SVEGLIA : scheda::suoni::TIMER, true);
            scheda::vibratePattern(3, 400, 300);
            lastBuzz_ = now;
        }
    }
    void finish(bool snooze) {
        const uint32_t now = app::rete().unixTime();
        if (alarm_ >= 0 && snooze && now) {
            app::orologio().posticipa(alarm_, now);
            toast("Sveglia posticipata", "Suona di nuovo alle " + timeHM(now + app::Orologio::POSTICIPA_S), AMBRA());
        } else if (alarm_ >= 0) {
            app::orologio().annullaPosticipo();
        }
        pop();
        // T-Deck acceso da spento per la sveglia: si chiede se restare acceso (una volta, dopo una
        // sveglia: il timer non puo' averlo acceso).
        if (alarm_ >= 0 && !snooze && scheda::wokeForAlarm() && !g_askedAfterWake) {
            g_askedAfterWake = true;
            ask("Il telefono era spento", "Si è acceso per la sveglia. Lo lascio acceso?", {"Lascia acceso", "Spegni"},
                [](int i) {
                    if (i == 1) powerOffNow();
                });
        }
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return true;
        g_posticipiAuto = 0;
        g_usato = true;
        // OK ferma; gli altri tasti posticipano, anche PWR breve (l'interfaccia lo passa qui invece
        // di spegnere lo schermo o di mettere il blocco col PIN sopra la sveglia).
        finish(alarm_ >= 0 && k != Key::OK && k != Key::ENTER);
        return true;
    }
    bool fullscreen() const override { return true; }
    bool capturesEnd() const override { return true; }

private:
    int alarm_;
    std::string name_;
    int64_t startedAt_ = 0, lastBuzz_ = 0, lastTick_ = 0;
    uint8_t oldVolume_ = 6;
    lv_obj_t* clock_ = nullptr;
};

// ================================================================ modifica di una sveglia

class AlarmEdit : public Screen {
public:
    // index = sveglia da modificare, -1 = nuova.
    explicit AlarmEdit(int index) : index_(index) {
        const auto all = app::orologio().sveglie();
        if (index_ >= 0 && index_ < int(all.size())) {
            s_ = all[size_t(index_)];
        } else {
            index_ = -1;
            s_.ora = 7;
            s_.minuti = 0;
            s_.giorni = 0;
        }
        s_.attiva = true;  // si salva accesa (anche quando era spenta: la si sta impostando)
    }
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, PX(10, 4));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_SVEGLIA, index_ < 0 ? "Nuova sveglia" : "Sveglia");
        lv_obj_t* body = column(col, PX(12, 4));
        lv_obj_set_size(body, W_SCHERMO, LV_SIZE_CONTENT);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t* r = row(body, PX(4, 2));
        lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        cell(r, 0, &mono_104);
        label(r, &mono_104, TENUE(), ":");
        cell(r, 1, &mono_104);
        lv_obj_t* r2 = row(body, PX(10, 6));
        lv_obj_set_size(r2, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        label(r2, &font_22, GRIGIO3(), "Ripeti");
        cell(r2, 2, &font_22b);
        lv_obj_t* n = label(body, &font_16, TENUE(), TXT("Cifre: W E R = 1 2 3, S D F = 4 5 6, Z X C = 7 8 9, microfono = 0",
                                                           "Cifre: W E R S D F Z X C = 1-9, microfono = 0"));
        lv_obj_set_width(n, PX(440, 300));
        lv_label_set_long_mode(n, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
        refresh();
    }
    void cell(lv_obj_t* parent, int i, const lv_font_t* f) {
        lv_obj_t* c = box(parent);
        lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        radius(c, PX(10, 6));
        pad(c, PX(2, 1), PX(10, 6), PX(2, 1), PX(10, 6));
        cells_[i] = c;
        texts_[i] = label(c, f, BIANCO(), "");
    }
    void refresh() {
        char b[8];
        snprintf(b, sizeof(b), "%02u", unsigned(s_.ora));
        lv_label_set_text(texts_[0], b);
        snprintf(b, sizeof(b), "%02u", unsigned(s_.minuti));
        lv_label_set_text(texts_[1], b);
        lv_label_set_text(texts_[2], app::giorniText(s_.giorni).c_str());
        for (int i = 0; i < 3; i++) {
            const bool on = i == sel_;
            if (temaChiaro()) {  // Carta: bianche col bordo nero, quella scelta piena di blu
                fill(cells_[i], on ? AMBRA() : SUPERFICIE());
                lv_obj_set_style_border_width(cells_[i], 2, 0);
                lv_obj_set_style_border_color(cells_[i], on ? AMBRA() : INCHIOSTRO(), 0);
            } else {
                fill(cells_[i], on ? AMBRA() : GRIGIO1());
            }
            lv_obj_set_style_text_color(texts_[i], on ? NERO() : BIANCO(), 0);
        }
    }
    void step(int dir) {
        if (sel_ == 0) s_.ora = uint8_t((s_.ora + 24 + dir) % 24);
        else if (sel_ == 1) s_.minuti = uint8_t((s_.minuti + 60 + dir) % 60);
        else {
            const int n = int(sizeof(RIPETI));
            int i = 0;
            while (i < n && RIPETI[i] != s_.giorni) i++;
            s_.giorni = RIPETI[i >= n ? 0 : (i + n + dir) % n];
        }
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        switch (k) {
            case Key::LEFT:
            case Key::RIGHT:
                sel_ = (sel_ + (k == Key::RIGHT ? 1 : 2)) % 3;
                typed_ = 0;
                break;
            case Key::UP:
            case Key::DOWN:
                step(k == Key::UP ? 1 : -1);
                typed_ = 0;
                break;
            case Key::OK:
            case Key::ENTER: save(); return true;
            case Key::MENU: options(); return true;
            case Key::BACK: return false;
            default: {
                const int d = digitOf(k);
                if (d < 0) return false;
                type(d);
                break;
            }
        }
        refresh();
        return true;
    }
    // Quattro cifre: ore e minuti (07 30). Un valore troppo grande si ferma al massimo.
    void type(int d) {
        if (sel_ == 2) sel_ = 0;
        if (typed_ == 0) sel_ = 0;
        uint8_t& v = sel_ == 0 ? s_.ora : s_.minuti;
        const int pos = typed_ % 2;
        v = uint8_t(pos == 0 ? d : std::min(sel_ == 0 ? 23 : 59, v * 10 + d));
        if (++typed_ % 2 == 0 && sel_ == 0) sel_ = 1;
        if (typed_ >= 4) {
            typed_ = 0;
            sel_ = 2;
        }
    }
    void save() {
        auto all = app::orologio().sveglie();
        if (index_ >= 0 && index_ < int(all.size())) all[size_t(index_)] = s_;
        else all.push_back(s_);
        app::orologio().setSveglie(all);
        const uint32_t now = app::rete().unixTime();
        if (now) {
            const uint32_t t = app::Orologio::prossima(s_, now);
            toast("Sveglia alle " + timeHM(t), fraQuanto(t - now), VERDE());
        } else {
            toast("Sveglia salvata", "Suonerà quando il telefono avrà l'ora", AMBRA());
        }
        pop();
    }
    void options() {
        if (index_ < 0) return;
        const auto all = app::orologio().sveglie();
        if (index_ >= int(all.size())) return;
        const bool on = all[size_t(index_)].attiva;
        char b[24];
        snprintf(b, sizeof(b), "Sveglia delle %02u:%02u", unsigned(all[size_t(index_)].ora), unsigned(all[size_t(index_)].minuti));
        ask(b, "", {on ? "Spegnila" : "Accendila", "Elimina", "Annulla"},
            [this, on](int i) {
                auto all = app::orologio().sveglie();
                if (index_ >= int(all.size()) || i < 0 || i == 2) return;
                if (i == 0) all[size_t(index_)].attiva = !on;
                else all.erase(all.begin() + index_);
                app::orologio().setSveglie(all);
                pop();
            });
    }
    std::vector<Hint> hints() override {
        if (index_ < 0) return {{"NAV", "scegli e cambia"}, {"OK", "salva"}, {"BACK", "annulla"}};
        return {{"NAV", "cambia"}, {"OK", "salva"}, {"MENU", "altro"}};
    }
#if TDECK
    bool typesDigits() const override { return true; }
#endif

private:
    int index_;
    app::Sveglia s_;
    int sel_ = 0, typed_ = 0;
    lv_obj_t* cells_[3] = {};
    lv_obj_t* texts_[3] = {};
};

// ================================================================ orologio (tre schede)

class Clock : public Screen {
public:
    enum Tab { SVEGLIE, TIMER, CRONO, N_TAB };

    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        // Schede. Carta: linguette di quaderno (la scelta piena di nero) sopra una riga nera.
        const bool carta = temaChiaro();
        lv_obj_t* bar = row(col, 0);
        lv_obj_set_size(bar, W_SCHERMO, TAB_H);
        constexpr int SU = PX(6, 4), LATO = PX(12, 8), TRA = PX(6, 4);  // margini delle linguette
        if (carta) {
            pad(bar, SU, LATO, 0, LATO);
            lv_obj_set_style_pad_column(bar, TRA, 0);
        }
        static const char* icons[N_TAB] = {IC_SVEGLIA, IC_CLESSIDRA, IC_CRONOMETRO};
        static const char* names[N_TAB] = {"Sveglie", "Timer", "Cronometro"};
        for (int i = 0; i < N_TAB; i++) {
            lv_obj_t* t = row(bar, PX(8, 4));
            lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            const bool on = i == tab_;
            if (carta) {
                lv_obj_set_size(t, (W_SCHERMO - 2 * LATO - (N_TAB - 1) * TRA) / N_TAB, TAB_H - SU);
                linguetta(t, on);
                // Sul T-Deck l'icona non ci sta ("Cronometro" toccherebbe il bordo): solo il nome.
                if (!TDECK) label(t, &icone_22, on ? SU_COLORE() : BIANCO(), icons[i]);
                label(t, &font_20, on ? SU_COLORE() : BIANCO(), names[i]);
                continue;
            }
            lv_obj_set_size(t, W_SCHERMO / N_TAB, TAB_H);
            lv_obj_set_style_border_side(t, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(t, on ? AMBRA() : GRIGIO1(), 0);
            lv_obj_set_style_border_width(t, on ? PX(3, 2) : 1, 0);
            label(t, &icone_22, on ? AMBRA() : GRIGIO2(), icons[i]);
            label(t, &font_20, on ? AMBRA() : GRIGIO3(), names[i]);
        }
        body_ = column(col, PX(8, 3));
        lv_obj_set_size(body_, W_SCHERMO, H_AREA - TAB_H);
        if (carta) {
            lv_obj_set_style_border_side(body_, LV_BORDER_SIDE_TOP, 0);
            lv_obj_set_style_border_width(body_, LINEA, 0);
            lv_obj_set_style_border_color(body_, INCHIOSTRO(), 0);
        }
        if (tab_ == SVEGLIE) buildAlarms();
        else if (tab_ == TIMER) buildTimer();
        else buildChrono();
    }

    // ---- sveglie ----
    void buildAlarms() {
        alarms_ = app::orologio().sveglie();
        list_.create(body_, W_SCHERMO, H_AREA - TAB_H - FOOT_H - (temaChiaro() ? LINEA : 0));
        for (const auto& s : alarms_) {
            lv_obj_t* r = list_.addRow(PX(58, 32));
            char b[8];
            snprintf(b, sizeof(b), "%02u:%02u", unsigned(s.ora), unsigned(s.minuti));
            // L'ora di una sveglia spenta e' grigio medio anche in Carta: grande si legge, e accanto al
            // nero di quelle accese si vede subito che e' spenta.
            List::text(r, &mono_28, s.attiva ? BIANCO() : GRIGIO2(), b);
            List::text(r, &font_20, s.attiva ? GRIGIO3() : TENUE(), s.nome.empty() ? app::giorniText(s.giorni)
                                                                                  : s.nome + " · " + app::giorniText(s.giorni),
                       true);
            List::text(r, &font_18, s.attiva ? VERDE() : TENUE(), s.attiva ? "accesa" : "spenta");
        }
        if (alarms_.size() < app::Orologio::MAX_SVEGLIE) {
            lv_obj_t* r = list_.addRow(PX(58, 32));
            List::text(r, &icone_22, AMBRA(), IC_PIU);
            List::text(r, &font_22, BIANCO(), "Nuova sveglia", true);
        }
        list_.focus(sel_);
        foot_ = label(body_, &font_16, TENUE(), "");
        lv_obj_set_width(foot_, W_SCHERMO);
        pad(foot_, 0, PX(16, 8), 0, PX(16, 8));
        unaRiga(foot_);
        updateFoot();
    }
    void updateFoot() {
        if (!foot_) return;
        const uint32_t now = app::rete().unixTime();
        uint32_t when = 0;
        int idx = -1;
        std::string t;
        if (!now) t = "Senza l'ora le sveglie non suonano: impostala in Impostazioni";
        else if (app::orologio().prossimaDi(now, when, idx)) t = "Prossima: " + quandoText(when, now) + ", " + fraQuanto(when - now);
        else t = alarms_.empty() ? "Nessuna sveglia" : "Nessuna sveglia accesa";
        lv_label_set_text(foot_, t.c_str());
    }

    // ---- timer ----
    void buildTimer() {
        lv_obj_set_flex_align(body_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        big_ = label(body_, &mono_104, BIANCO(), "");
        state_ = label(body_, &font_22, GRIGIO3(), "");
        help_ = label(body_, &font_16, TENUE(), "");
        lv_obj_set_width(help_, PX(440, 300));
        lv_label_set_long_mode(help_, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(help_, LV_TEXT_ALIGN_CENTER, 0);
        updateTimer();
    }
    // Durata scritta: cifre che entrano da destra (H MM SS), come sui telefoni.
    uint32_t entrySeconds() const {
        uint32_t v = 0;
        for (char c : entry_) v = v * 10 + uint32_t(c - '0');
        const uint32_t h = v / 10000, m = v / 100 % 100, s = v % 100;
        return h * 3600 + m * 60 + s;
    }
    void setEntrySeconds(uint32_t s) {
        s = std::min<uint32_t>(s, 9 * 3600 + 59 * 60 + 59);
        char b[8];
        snprintf(b, sizeof(b), "%lu%02lu%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
        entry_ = b;
        while (entry_.size() > 1 && entry_[0] == '0') entry_.erase(0, 1);
        if (entry_ == "0") entry_.clear();
    }
    void updateTimer() {
        if (!big_) return;
        const auto st = app::orologio().timerStato();
        const uint32_t s = st == app::Orologio::Timer::FERMO ? entrySeconds() : (app::orologio().timerRestaMs() + 999) / 1000;
        lv_label_set_text(big_, hms(s).c_str());
        lv_obj_set_style_text_color(big_, st == app::Orologio::Timer::PAUSA ? FERMO() : BIANCO(), 0);
        lv_label_set_text(state_, st == app::Orologio::Timer::VA      ? "in corso"
                                  : st == app::Orologio::Timer::PAUSA ? "in pausa"
                                  : s                                 ? "pronto"
                                                                      : "scegli la durata");
        lv_label_set_text(help_, st != app::Orologio::Timer::FERMO
                                     ? "Suona anche se esci dall'orologio o lo schermo è spento."
                                     : TXT("Scrivi la durata con le cifre (W E R S D F Z X C, microfono = 0) o su/giù di un minuto.",
                                           "Durata: cifre (W E R S D F Z X C, microfono 0) o su/giù di 1 minuto."));
    }

    // ---- cronometro ----
    void buildChrono() {
        lv_obj_set_flex_align(body_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        pad(body_, PX(10, 4), 0, 0, 0);
        big_ = label(body_, &mono_104, BIANCO(), "");
        laps_ = column(body_, PX(2, 0));
        lv_obj_set_size(laps_, PX(400, 280), LV_SIZE_CONTENT);
        lapsShown_ = size_t(-1);
        updateChrono();
    }
    void updateChrono() {
        if (!big_) return;
        lv_label_set_text(big_, chrono(app::orologio().cronoMs()).c_str());
        lv_obj_set_style_text_color(big_, app::orologio().cronoVa() || !app::orologio().cronoMs() ? BIANCO() : FERMO(), 0);
        // g tiene solo gli ultimi MAX_GIRI giri: il numero viene dal totale.
        const auto g = app::orologio().giri();
        const size_t tot = app::orologio().giriTotali();
        if (tot == lapsShown_) return;
        lapsShown_ = tot;
        lv_obj_clean(laps_);
        const size_t show = PX(5, 3);
        for (size_t i = g.size(); i > 0 && g.size() - i < show; i--) {
            lv_obj_t* r = row(laps_, PX(10, 6));
            lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_t* a = label(r, &font_20, GRIGIO3(), ("Giro " + std::to_string(tot - (g.size() - i))).c_str());
            grow(a);
            const uint32_t lap = g[i - 1] - (i > 1 ? g[i - 2] : 0);
            label(r, &mono_20, BIANCO(), chrono(lap).c_str());
            label(r, &mono_16, TENUE(), chrono(g[i - 1]).c_str());
        }
    }

    void tick() override {
        if (tab_ == TIMER) updateTimer();
        else if (tab_ == CRONO) updateChrono();
        else if (++ticks_ % 20 == 0) updateFoot();
    }
    void onEvent(const app::Event& e) override {
        if (e.type == app::Ev::TIME && tab_ == SVEGLIE) updateFoot();
    }

    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (k == Key::LEFT || k == Key::RIGHT) {
            tab_ = (tab_ + (k == Key::RIGHT ? 1 : N_TAB - 1)) % N_TAB;
            g_tab = tab_;
            big_ = state_ = help_ = laps_ = foot_ = nullptr;
            rebuild();
            return true;
        }
        if (tab_ == SVEGLIE) {
            if (list_.key(k)) {
                sel_ = list_.focused();
                refreshBars();  // "modifica" o "nuova" nella barra dei tasti
                return true;
            }
            if (k == Key::OK || k == Key::ENTER) {
                sel_ = list_.focused();
                if (sel_ < int(alarms_.size())) push(new AlarmEdit(sel_));
                else push(new AlarmEdit(-1));
                return true;
            }
            return false;
        }
        auto& o = app::orologio();
        if (tab_ == TIMER) {
            const auto st = o.timerStato();
            if (k == Key::OK || k == Key::ENTER) {
                if (st == app::Orologio::Timer::VA) o.timerPausa();
                else if (st == app::Orologio::Timer::PAUSA) o.timerRiprendi();
                else if (entrySeconds()) o.timerAvvia(entrySeconds() * 1000);
            } else if (st == app::Orologio::Timer::PAUSA && k == Key::DOWN) {
                o.timerAzzera();
            } else if (st == app::Orologio::Timer::FERMO && (k == Key::UP || k == Key::DOWN)) {
                const uint32_t s = entrySeconds();
                setEntrySeconds(k == Key::UP ? s + 60 : (s > 60 ? s - 60 : 0));
            } else if (st == app::Orologio::Timer::FERMO && (k == Key::BACK || k == Key::BKSP) && !entry_.empty()) {
                entry_.pop_back();  // come un campo di testo: cancella l'ultima cifra, vuoto = indietro
            } else if (st == app::Orologio::Timer::FERMO && digitOf(k) >= 0) {
                if (entry_.size() < 5 && !(entry_.empty() && digitOf(k) == 0)) entry_ += char('0' + digitOf(k));
            } else {
                return false;
            }
            updateTimer();
            refreshBars();
            return true;
        }
        // cronometro
        if (k == Key::OK || k == Key::ENTER) {
            if (o.cronoVa()) o.cronoFerma();
            else o.cronoAvvia();
        } else if (k == Key::DOWN) {
            if (o.cronoVa()) o.cronoGiro();
            else o.cronoAzzera();
        } else {
            return false;
        }
        updateChrono();
        refreshBars();
        return true;
    }
    std::vector<Hint> hints() override {
        if (tab_ == SVEGLIE) return {{"NAV", "schede"}, {"OK", sel_ < int(alarms_.size()) ? "modifica" : "nuova"}, {"BACK", "indietro"}};
        auto& o = app::orologio();
        if (tab_ == TIMER) {
            switch (o.timerStato()) {
                case app::Orologio::Timer::VA: return {{"OK", "pausa"}, {"NAV", "schede"}};
                case app::Orologio::Timer::PAUSA: return {{"OK", "riprendi"}, {"GIÙ", "azzera"}};
                default: return {{"OK", "avvia"}, {"SU/GIÙ", "±1 min"}, {"NAV", "schede"}};
            }
        }
        if (o.cronoVa()) return {{"OK", "ferma"}, {"GIÙ", "giro"}, {"NAV", "schede"}};
        return {{"OK", "avvia"}, {"GIÙ", "azzera"}, {"NAV", "schede"}};
    }
#if TDECK
    // Nel timer fermo le lettere W E R S D F Z X C sono le cifre della durata.
    bool typesDigits() const override { return tab_ == TIMER && app::orologio().timerStato() == app::Orologio::Timer::FERMO; }
#endif

private:
    // LINEA: la riga nera sotto le linguette (Carta).
    static constexpr int TAB_H = PX(46, 26), FOOT_H = PX(26, 16), LINEA = 2;
    static int g_tab;
    int tab_ = g_tab;
    int sel_ = 0, ticks_ = 0;
    List list_;
    std::vector<app::Sveglia> alarms_;
    std::string entry_ = "500";  // 5 minuti
    size_t lapsShown_ = 0;
    lv_obj_t *body_ = nullptr, *big_ = nullptr, *state_ = nullptr, *help_ = nullptr, *laps_ = nullptr, *foot_ = nullptr;
};

int Clock::g_tab = 0;

Screen* makeClock() { return new Clock(); }

// ================================================================ servizio

bool ringOpen() { return g_ringOpen > 0; }

bool ringOnTop() { return g_ring && top() == g_ring; }

void clockNoteActivity() { g_usato = true; }

void clockService() {
    static int64_t last = 0;
    const int64_t t = nowMs();
    if (t - last < 500) return;
    last = t;
    if (g_ringOpen > 0) return;
    auto& o = app::orologio();
    // Nessun posticipo in corso (fermato, cambiato o saltato): i posticipi automatici ripartono da 0.
    uint32_t quando = 0;
    int indice = -1;
    if (!o.posticipata(quando, indice)) g_posticipiAuto = 0;
    // Durante una chiamata, un SOS (il proprio, o uno ricevuto che suona ancora: la sveglia ne
    // coprirebbe la sirena) o una registrazione si aspetta (la sveglia suona dopo, se il ritardo non
    // e' troppo).
    if (callScreenOpen() || app::rete().sosActive() || sosAlerting() || scheda::audio().recording() ||
        scheda::audio().streaming())
        return;
    if (o.timerScaduto()) {
        push(new Ring(-1));
        return;
    }
    const int i = o.scaduta(app::rete().unixTime());
    if (i >= 0) push(new Ring(i));
}

void clockBeforePowerOff() {
    const uint32_t now = app::rete().unixTime();
    uint32_t when = 0;
    int idx = -1;
    if (!now || !app::orologio().prossimaDi(now, when, idx) || when <= now || when - now > 8 * 86400) {
        app::orologio().risveglioPer(0);
        scheda::wakeAfter(0);
        return;
    }
    // Al risveglio questa sveglia suona anche se in ritardo (risveglio dopo di lei, ora del GPS).
    app::orologio().risveglioPer(when);
    // Un minuto prima: il tempo di accendersi (e un po' di margine per l'orologio del chip).
    const uint32_t s = when - now;
    scheda::wakeAfter(s > 90 ? s - 60 : std::max<uint32_t>(5, s / 2));
}

}  // namespace ui
