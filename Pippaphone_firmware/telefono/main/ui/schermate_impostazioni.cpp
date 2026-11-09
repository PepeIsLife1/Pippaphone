// Impostazioni, segnale, aggiornamento del firmware via Wi-Fi, amministrazione della rete.
#include <cstdio>

#include "app/aggiorna.h"
#include "app/dati.h"
#include "app/memoria.h"
#include "app/rete.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scheda/scheda.h"
#include "ui/schermate.h"
#include "ui/widget.h"

namespace ui {

namespace {
std::string comma(std::string s) {
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string agoText(uint32_t s) {
    if (s < 60) return std::to_string(s) + " s fa";
    if (s < 3600) return std::to_string(s / 60) + " min fa";
    return std::to_string(s / 3600) + " h fa";
}

std::string repeaterName(uint16_t a) { return "R" + std::to_string(a - pn::ADDR_REPEATER_FIRST); }

const uint16_t SCREEN_OFF[] = {15, 30, 60, 120, 300};
const int8_t POWERS[] = {0, 22, 20, 17, 14, 10};
}  // namespace

// ================================================================ PIN

class PinEdit : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, PX(12, 4));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_LUCCHETTO, "Blocco con PIN");
        lv_obj_t* body = column(col, PX(10, 4));
        lv_obj_set_size(body, W_SCHERMO, LV_SIZE_CONTENT);
        pad(body, PX(8, 4), PX(20, 10), 0, PX(20, 10));
        lv_obj_t* t = label(body, &font_22, BIANCO(),
                            first_.empty() ? "Scrivi il nuovo PIN (da 4 a 8 cifre). Lascia vuoto per togliere il blocco."
                                           : "Scrivi di nuovo il PIN per conferma.");
        lv_obj_set_width(t, PX(440, 300));
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        in_.create(body, PX(260, 180), PX(56, 32), false, 8, "PIN");
        in_.setDigitsOnly(true);
        in_.setPassword(true);
        in_.setFocused(true);
        msg_ = label(body, &font_20, ROSSO(), "");
#if TDECK
        lv_obj_t* h = label(body, &font_16, GRIGIO2(), "Cifre: W E R = 1 2 3, S D F = 4 5 6, Z X C = 7 8 9, microfono = 0");
        lv_obj_set_width(h, 300);
        lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
#else
        label(body, &font_16, GRIGIO2(), "Cifre: W E R = 1 2 3, S D F = 4 5 6, Z X C = 7 8 9, microfono = 0");
#endif
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (k == Key::OK || k == Key::ENTER) {
            const std::string p = in_.text();
            app::Settings s = app::dati().settings();
            if (first_.empty() && p.empty()) {
                s.pin.clear();
                app::dati().setSettings(s);
                toast("Blocco tolto", "", GRIGIO3());
                pop();
            } else if (first_.empty()) {
                if (p.size() < 4) {
                    lv_label_set_text(msg_, "Almeno 4 cifre");
                    return true;
                }
                first_ = p;
                rebuild();
            } else if (p == first_) {
                s.pin = p;
                app::dati().setSettings(s);
                toast("PIN impostato", "Si blocca quando lo schermo si spegne", VERDE());
                pop();
            } else {
                first_.clear();
                rebuild();
                lv_label_set_text(msg_, "I due PIN non sono uguali: ricomincia");
            }
            return true;
        }
        if (k == Key::BACK) return false;
        return in_.key(k, pressed) != TextInput::NONE;
    }
    std::vector<Hint> hints() override { return {{"OK", "conferma"}, {"BACK", "annulla"}}; }
#if TDECK
    const TextInput* input() const override { return &in_; }
#endif

private:
    TextInput in_;
    std::string first_;
    lv_obj_t* msg_ = nullptr;
};

// ================================================================ impostazioni

class Settings : public Screen {
public:
    enum Row { RING, VOLUME, VIBRA, BRIGHT, SCREEN, KEYLIGHT, PIN, POWER, NUMBER, ADMIN, OTA, INFO, RESET };

    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_IMPOSTAZIONI, "Impostazioni");
        list_.create(col, W_SCHERMO, H_AREA - H_TITOLO);
        rows_.clear();
        values_.clear();
        add(RING, IC_VOLUME, "Suoneria");
        add(VOLUME, IC_VOLUME, "Volume");
#if !TDECK
        add(VIBRA, IC_TELEFONO, "Vibrazione");  // il T-Deck non ha il motore della vibrazione
#endif
        add(BRIGHT, IC_INFO, "Luminosità");
        add(SCREEN, IC_OROLOGIO, "Spegni lo schermo dopo");
        add(KEYLIGHT, IC_INFO, "Luce della tastiera");
        add(PIN, IC_LUCCHETTO, "Blocco con PIN");
        add(POWER, IC_TORRE, "Potenza radio");
        add(NUMBER, IC_PERSONA, "Il mio numero");
        if (app::rete().isAdmin()) add(ADMIN, IC_CHIAVE, "Amministrazione");
        add(OTA, IC_WIFI, "Aggiorna il firmware");
        add(INFO, IC_INFO, "Informazioni");
        add(RESET, IC_CESTINO, "Ripristino di fabbrica");
        list_.focus(sel_);
        refresh();
    }
    void add(Row r, const char* icon, const char* text) {
        lv_obj_t* row = list_.addRow(PX(54, 30));
        const bool red = r == RESET;
        List::text(row, &icone_18, red ? ROSSO() : GRIGIO3(), icon);
        List::text(row, &font_22, red ? ROSSO() : BIANCO(), text, true);
        values_.push_back(List::text(row, &font_20, AMBRA(), ""));
        rows_.push_back(r);
    }
    std::string value(Row r) {
        const app::Settings s = app::dati().settings();
        switch (r) {
            case RING: return s.ringtone == 0 ? "silenziosa" : s.ringtone == 1 ? "classica" : "breve";
            case VOLUME: return std::to_string(s.volume) + "/10";
            case VIBRA: return s.vibration ? "sì" : "no";
            case BRIGHT: return std::to_string(s.brightness) + "%";
            case SCREEN: return s.screenOffS < 60 ? std::to_string(s.screenOffS) + " s" : std::to_string(s.screenOffS / 60) + " min";
            case KEYLIGHT: return s.keyLight ? "sì" : "no";
            case PIN: return s.pin.empty() ? "no" : "attivo";
            case POWER:
                return s.txPower ? std::to_string(s.txPower) + " dBm"
                                 : "auto (" + std::to_string(app::rete().power()) + " dBm)";
            case NUMBER: return std::to_string(app::rete().number());
            default: return "";
        }
    }
    void refresh() {
        for (size_t i = 0; i < rows_.size(); i++) lv_label_set_text(values_[i], value(rows_[i]).c_str());
        refreshBars();
    }
    // dir: +1 / -1 (destra/sinistra), 0 = OK.
    void change(Row r, int dir) {
        app::Settings s = app::dati().settings();
        const int d = dir ? dir : 1;
        switch (r) {
            case RING:
                s.ringtone = uint8_t((s.ringtone + 3 + d) % 3);
                app::dati().setSettings(s);
                if (s.ringtone) scheda::audio().play(s.ringtone == 1 ? scheda::suoni::CLASSICA : scheda::suoni::SQUILLO);
                break;
            case VOLUME:
                if (!dir) return;
                s.volume = uint8_t(std::max(0, std::min(10, s.volume + dir)));
                app::dati().setSettings(s);
                scheda::audio().setVolume(s.volume);
                scheda::audio().play(scheda::suoni::CLIC);
                break;
            case VIBRA:
                s.vibration = !s.vibration;
                app::dati().setSettings(s);
                if (s.vibration) scheda::vibrate(150);
                break;
            case BRIGHT:
                if (!dir) return;
                s.brightness = uint8_t(std::max(10, std::min(100, s.brightness + 10 * dir)));
                app::dati().setSettings(s);
                scheda::display().backlight(s.brightness);
                break;
            case SCREEN: {
                int i = 0;
                while (i < 5 && SCREEN_OFF[i] != s.screenOffS) i++;
                i = (i + 5 + d) % 5;
                s.screenOffS = SCREEN_OFF[i];
                app::dati().setSettings(s);
                break;
            }
            case KEYLIGHT:
                s.keyLight = !s.keyLight;
                app::dati().setSettings(s);
                scheda::keyboardLight(s.keyLight ? 60 : 0);
                break;
            case PIN:
                if (!dir) push(new PinEdit());
                return;
            case POWER: {
                int i = 0;
                while (i < 6 && POWERS[i] != s.txPower) i++;
                i = (i + 6 + d) % 6;
                s.txPower = POWERS[i];
                app::dati().setSettings(s);
                app::rete().setPower(s.txPower);
                break;
            }
            case NUMBER:
                if (dir) return;
                ask("Il mio numero: " + std::to_string(app::rete().number()),
                    "Codice di questo telefono:\n" + app::rete().myCode() + "\n\nCodice dell'amministratore:\n" +
                        app::rete().adminCode(),
                    {"OK"}, nullptr);
                return;
            case ADMIN:
                if (!dir) push(makeAdmin());
                return;
            case OTA:
                if (!dir) push(makeOta());
                return;
            case INFO:
                if (!dir) showInfo();
                return;
            case RESET:
                if (dir) return;
                ask("Ripristino di fabbrica?",
                    "Si cancellano rubrica, messaggi, gruppi, impostazioni e il numero di questo telefono. Dopo "
                    "servirà un nuovo numero dall'amministratore.",
                    {"Annulla", "Cancella tutto"}, [](int i) {
                        if (i != 1) return;
                        app::eraseAll();
                        esp_restart();
                    });
                return;
        }
        refresh();
    }
    void showInfo() {
        uint8_t mac[6] = {};
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char b[400];
        const uint8_t pc = scheda::batteria().percent();
        char air[16], volts[16];
        snprintf(air, sizeof(air), "%.1f", double(app::rete().airtimeMs()) / 1000.0);
        snprintf(volts, sizeof(volts), "%.2f", double(scheda::batteria().volts()));
        snprintf(b, sizeof(b),
                 "Firmware %s\nNumero %u%s\nBatteria %s (%s V)\nTrasmissione totale %s s\nAcceso da %lu min\n"
                 "Memoria libera %u kB (PSRAM %u kB)\nMAC %02X:%02X:%02X:%02X:%02X:%02X",
                 app::firmwareVersion(), unsigned(app::rete().number()), app::rete().isAdmin() ? " (amministratore)" : "",
                 pc <= 100 ? (std::to_string(pc) + "%").c_str() : "?", comma(volts).c_str(),
                 comma(air).c_str(),
                 (unsigned long)(esp_timer_get_time() / 60000000), unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                 unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), mac[0], mac[1], mac[2], mac[3], mac[4],
                 mac[5]);
        ask("Pippaphone", b, {"OK"}, nullptr);
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        sel_ = list_.focused();
        if (k == Key::LEFT || k == Key::RIGHT) {
            change(rows_[sel_], k == Key::RIGHT ? 1 : -1);
            return true;
        }
        if (list_.key(k)) {
            sel_ = list_.focused();
            return true;
        }
        if (k == Key::OK) {
            change(rows_[sel_], 0);
            return true;
        }
        return false;
    }
    std::vector<Hint> hints() override { return {{"OK", "cambia"}, {"</>", "regola"}, {"BACK", "indietro"}}; }

private:
    List list_;
    std::vector<Row> rows_;
    std::vector<lv_obj_t*> values_;
    int sel_ = 0;
};

Screen* makeSettings() { return new Settings(); }

// ================================================================ segnale

class Signal : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_TORRE, "Segnale");
        top_ = column(col, PX(4, 2));
        lv_obj_set_size(top_, W_SCHERMO, PX(120, 64));
        pad(top_, PX(10, 4), PX(20, 10), PX(10, 4), PX(20, 10));
        underline(top_);
        list_ = column(col, 0);
        lv_obj_set_size(list_, W_SCHERMO, H_AREA - H_TITOLO - PX(120, 64) - PX(60, 36));
        lv_obj_add_flag(list_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);
        foot_ = column(col, 2);
        lv_obj_set_size(foot_, W_SCHERMO, PX(60, 36));
        pad(foot_, PX(6, 3), PX(20, 10), 0, PX(20, 10));
        refresh();
    }
    void refresh() {
        lv_obj_clean(top_);
        lv_obj_clean(list_);
        lv_obj_clean(foot_);
        const app::Rete::Signal s = app::rete().signal();
        lv_obj_t* r = row(top_, PX(14, 8));
        lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_t* bars = box(r);
        lv_obj_set_size(bars, PX(50, 30), PX(40, 28));
        for (int i = 0; i < 4; i++) {
            lv_obj_t* b = box(bars);
            lv_obj_set_size(b, PX(9, 6), PX(10 + i * 10, 7 + i * 7));
            lv_obj_set_pos(b, PX(i * 13, i * 8), PX(30 - i * 10, 21 - i * 7));
            fill(b, i < s.bars ? VERDE() : GRIGIO1());
        }
        lv_obj_t* t = column(r, 0);
        grow(t);
        lv_obj_set_height(t, LV_SIZE_CONTENT);
        if (s.heard) {
            label(t, &font_26b, BIANCO(), ("Ripetitore " + repeaterName(s.repeater)).c_str());
            char b[64];
            snprintf(b, sizeof(b), "%d dBm · SNR %.1f dB · %s", s.rssi, double(s.snr), agoText(s.agoS).c_str());
            label(t, &font_20, GRIGIO3(), comma(b).c_str());
        } else {
            label(t, &font_26b, ROSSO(), "Nessun ripetitore");
            label(t, &font_20, GRIGIO3(), "i messaggi partono ma nessuno li inoltra");
        }
        const auto reps = app::rete().repeaters();
        label(top_, &font_16, GRIGIO2(), (std::to_string(reps.size()) + " ripetitori sentiti").c_str());
        for (const auto& rp : reps) {
            lv_obj_t* rr = row(list_, PX(10, 6));
            lv_obj_set_size(rr, lv_pct(100), PX(40, 24));
            pad(rr, 0, PX(20, 10), 0, PX(20, 10));
            underline(rr);
            label(rr, &mono_20, BIANCO(), repeaterName(rp.addr).c_str());
            char b[64];
            snprintf(b, sizeof(b), "%d dBm  SNR %.1f", rp.rssi, double(rp.snr));
            lv_obj_t* l = label(rr, &font_20, GRIGIO3(), comma(b).c_str());
            grow(l);
            label(rr, &font_18, GRIGIO2(), agoText(rp.agoS).c_str());
        }
        const app::Settings st = app::dati().settings();
        char f[96];
        snprintf(f, sizeof(f), TXT("Potenza %d dBm%s · in onda per %s s in tutto", "Potenza %d dBm%s · in onda %s s"),
                 app::rete().power(),
                 st.txPower ? "" : " (auto)", comma(std::to_string(app::rete().airtimeMs() / 1000)).c_str());
        label(foot_, &font_18, GRIGIO3(), f);
        label(foot_, &font_16, app::rete().radioOk() ? GRIGIO2() : ROSSO(),
              app::rete().radioOk() ? (app::rete().timeValid() ? "Radio ok · ora sincronizzata" : "Radio ok · ora non ancora sincronizzata")
                                    : "Radio LoRa non risponde!");
    }
    void tick() override {
        if (++ticks_ % 8 == 0) refresh();
    }
    std::vector<Hint> hints() override { return {{"BACK", "indietro"}}; }

private:
    lv_obj_t *top_ = nullptr, *list_ = nullptr, *foot_ = nullptr;
    int ticks_ = 0;
};

Screen* makeSignal() { return new Signal(); }

// ================================================================ aggiornamento Wi-Fi

class Ota : public Screen {
public:
    Ota() { app::otaStart(app::rete().number()); }
    ~Ota() override { app::otaStop(); }
    void build(lv_obj_t* root) override {
        const app::OtaStatus st = app::otaStatus();
        lv_obj_t* col = column(root, PX(8, 3));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_WIFI, "Aggiorna il firmware", std::string("versione ") + app::firmwareVersion());
        lv_obj_t* body = column(col, PX(8, 3));
        lv_obj_set_size(body, W_SCHERMO, LV_SIZE_CONTENT);
        pad(body, PX(4, 2), PX(20, 10), 0, PX(20, 10));
        auto step = [&](const char* n, const std::string& t) {
            lv_obj_t* r = row(body, PX(10, 6));
            lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
            label(r, PX(&mono_20, &mono_16), AMBRA(), n);
            lv_obj_t* l = label(r, PX(&font_20, &font_16), BIANCO(), t.c_str());
            grow(l);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        };
        step("1", "Col computer collegati alla rete Wi-Fi:");
        lv_obj_t* box1 = column(body, PX(2, 1));
        lv_obj_set_size(box1, PX(440, 300), LV_SIZE_CONTENT);
        fill(box1, GRIGIO1());
        radius(box1, PX(10, 6));
        pad(box1, PX(8, 4), PX(14, 8), PX(10, 4), PX(14, 8));
        label(box1, &mono_20, BIANCO(), st.ssid.c_str());
        label(box1, &font_16, GRIGIO3(), "password");
        label(box1, &mono_28, AMBRA(), st.password.c_str());
        step("2", "Apri nel browser http://" + st.address);
        step("3", "Scegli il file pippaphone.bin e premi Carica");
        status_ = label(body, &font_22b, BIANCO(), "");
        prog_ = lv_bar_create(body);
        lv_obj_remove_style_all(prog_);
        lv_obj_set_size(prog_, PX(440, 300), PX(12, 8));
        lv_obj_set_style_bg_color(prog_, GRIGIO1(), 0);
        lv_obj_set_style_bg_opa(prog_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(prog_, PX(6, 4), 0);
        lv_obj_set_style_bg_color(prog_, VERDE(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(prog_, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(prog_, PX(6, 4), LV_PART_INDICATOR);
        lv_bar_set_range(prog_, 0, 1000);
        update();
    }
    void update() {
        const app::OtaStatus st = app::otaStatus();
        std::string t;
        lv_color_t c = BIANCO();
        if (!st.error.empty()) {
            t = "Errore: " + st.error;
            c = ROSSO();
        } else if (st.done) {
            t = "Fatto! Riavvio...";
            c = VERDE();
        } else if (st.total) {
            t = "Ricevo " + std::to_string(st.received / 1024) + " / " + std::to_string(st.total / 1024) + " kB";
        } else {
            t = st.active ? "In attesa del file..." : "Wi-Fi non partito";
        }
        lv_label_set_text(status_, t.c_str());
        lv_obj_set_style_text_color(status_, c, 0);
        lv_bar_set_value(prog_, st.total ? int32_t(uint64_t(st.received) * 1000 / st.total) : 0, LV_ANIM_OFF);
        if (st.done && ++doneTicks_ > 8) app::otaStop();  // riavvia col firmware nuovo
    }
    void tick() override {
        wake();  // lo schermo resta acceso durante l'aggiornamento
        update();
    }
    std::vector<Hint> hints() override { return {{"BACK", "annulla e spegni il Wi-Fi"}}; }
    bool capturesEnd() const override { return app::otaStatus().total > 0; }

private:
    lv_obj_t *status_ = nullptr, *prog_ = nullptr;
    int doneTicks_ = 0;
};

Screen* makeOta() { return new Ota(); }

// ================================================================ amministrazione

class Admin : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_CHIAVE, "Amministrazione");
        lv_obj_t* top = column(col, 2);
        lv_obj_set_size(top, W_SCHERMO, PX(96, 64));
        pad(top, PX(8, 3), PX(20, 10), PX(8, 3), PX(20, 10));
        underline(top);
        label(top, &font_16, GRIGIO3(), "Il tuo codice (il telefono nuovo mostra lo stesso)");
        label(top, &mono_28, AMBRA(), app::rete().myCode().c_str());
        label(top, &font_16, GRIGIO2(), "Telefoni nuovi qui vicino, che chiedono un numero:");
        list_.create(col, W_SCHERMO, H_AREA - H_TITOLO - PX(96, 64));
        items_ = app::rete().hellos();
        for (const auto& h : items_) {
            lv_obj_t* r = list_.addRow(PX(60, 34));
            List::text(r, &icone_22, AMBRA(), IC_TELEFONO);
            List::text(r, &mono_20, BIANCO(), h.code, true);
            List::text(r, &font_18, GRIGIO3(), std::to_string(h.rssi) + " dBm");
        }
        if (items_.empty()) {
            lv_obj_t* t = label(list_.obj(), &font_20, GRIGIO2(),
                                "Nessuno. Accendi il telefono nuovo vicino a questo: sul suo schermo compare il "
                                "suo codice, che comparirà anche qui.");
            lv_obj_set_width(t, PX(440, 300));
            lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
            pad(t, PX(16, 6), PX(20, 10), 0, PX(20, 10));
        }
        list_.focus(sel_);
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (list_.key(k)) {
            sel_ = list_.focused();
            return true;
        }
        if (k == Key::OK && !items_.empty()) {
            sel_ = list_.focused();
            const uint64_t id = items_[sel_].shortId;
            const std::string code = items_[sel_].code;
            push(makePickContact("Numero da assegnare", [id, code](uint16_t n) {
                if (n == app::rete().number()) {
                    toast("Numero non valido", "È il numero dell'amministratore", ROSSO());
                    return;
                }
                if (app::rete().assign(id, n))
                    toast("Numero " + std::to_string(n) + " inviato",
                          "Sul telefono nuovo controlla il codice e premi OK", VERDE());
                else toast("Non inviato", "Riprova", ROSSO());
            }));
            return true;
        }
        return false;
    }
    void onEvent(const app::Event& e) override {
        if (e.type == app::Ev::PROV_HELLO) rebuild();
    }
    std::vector<Hint> hints() override {
        if (items_.empty()) return {{"BACK", "indietro"}};
        return {{"OK", "assegna un numero"}};
    }

private:
    List list_;
    std::vector<app::Rete::Hello> items_;
    int sel_ = 0;
};

Screen* makeAdmin() { return new Admin(); }

}  // namespace ui
