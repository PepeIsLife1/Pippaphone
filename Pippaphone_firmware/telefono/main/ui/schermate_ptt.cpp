// Walkie-talkie: registrazione con PTT, invio del vocale, riproduzione.
#include "app/dati.h"
#include "app/memoria.h"
#include "app/rete.h"
#include "app/voce.h"
#include "scheda/scheda.h"
#include "ui/schermate.h"
#include "ui/widget.h"

namespace ui {

namespace {
constexpr uint32_t MIN_VOICE_MS = 400;  // piu' corto: premuto per sbaglio

// Durata e pezzi persi di un vocale salvato.
bool voiceInfo(uint32_t file, uint16_t& ms, uint32_t& missing) {
    std::vector<uint8_t> f;
    if (!app::readFile(std::string(app::DATI) + "/voce/" + std::to_string(file) + ".c2", f) || f.size() < 7)
        return false;
    ms = uint16_t(f[1] | (f[2] << 8));
    missing = uint32_t(f[3]) | (uint32_t(f[4]) << 8) | (uint32_t(f[5]) << 16) | (uint32_t(f[6]) << 24);
    return true;
}

// Cerchio grande con un'icona al centro.
lv_obj_t* bigCircle(lv_obj_t* parent, int size, lv_obj_t** icon) {
    lv_obj_t* c = box(parent);
    lv_obj_set_size(c, size, size);
    radius(c, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(c, PX(4, 3), 0);
    *icon = label(c, &icone_44, BIANCO(), IC_MICROFONO);
    lv_obj_center(*icon);
    return c;
}

lv_obj_t* bar(lv_obj_t* parent, int w, int h, lv_color_t color) {
    lv_obj_t* b = lv_bar_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, GRIGIO1(), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_bg_color(b, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, h / 2, LV_PART_INDICATOR);
    return b;
}

class Walkie;
Walkie* g_walkie = nullptr;

#if TDECK
// Testi col tasto del PTT: sul T-Deck e' il tasto 0/microfono (tenuto premuto), oppure, col
// firmware vecchio della tastiera, SHIFT + microfono premuto una volta per cominciare e una per finire.
std::string ptt(const char* raw, const char* chars) {
    return scheda::tastiera().rawMode() ? raw : chars;
}
#endif
}  // namespace

// ================================================================ walkie-talkie

namespace {
class Walkie : public Screen {
public:
    Walkie(uint16_t n, bool startNow) : number_(n), startNow_(startNow) { g_walkie = this; }
    ~Walkie() override {
        if (g_walkie == this) g_walkie = nullptr;
        if (app::voce().recording()) app::voce().stopRecording();
    }
    uint16_t number() const { return number_; }
    bool busy() const { return state_ == REC; }

    void build(lv_obj_t* root) override {
        name_ = app::dati().nameOf(number_);
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_MICROFONO, "Walkie-talkie", name_);
        lv_obj_t* mid = column(col, PX(12, 4));
        lv_obj_set_size(mid, W_SCHERMO, H_AREA - H_TITOLO - PX(70, 30));
        lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        circle_ = bigCircle(mid, PX(150, 70), &icon_);
        title_ = label(mid, &font_26b, BIANCO(), "");
        sub_ = label(mid, &font_20, GRIGIO3(), "");
        lv_obj_set_width(sub_, PX(440, 300));
        lv_obj_set_style_text_align(sub_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(sub_, LV_LABEL_LONG_WRAP);
        time_ = bar(mid, PX(360, 240), PX(10, 6), ROSSO());
        lv_bar_set_range(time_, 0, app::VOICE_MAX_MS / 100);
        vu_ = bar(mid, PX(360, 240), PX(6, 4), VERDE());
        lv_bar_set_range(vu_, 0, 100);
        // Ultimo vocale ricevuto da questo contatto.
        lv_obj_t* foot = row(col, PX(10, 6));
        lv_obj_set_size(foot, W_SCHERMO, PX(70, 30));
        pad(foot, 0, PX(20, 10), 0, PX(20, 10));
        lv_obj_set_style_border_side(foot, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_color(foot, GRIGIO1(), 0);
        lv_obj_set_style_border_width(foot, 1, 0);
        lastFile_ = 0;
        const app::Chat c = app::dati().chat({app::ChatKind::DIRECT, number_});
        for (auto it = c.msgs.rbegin(); it != c.msgs.rend(); ++it)
            if (it->kind == app::MsgKind::VOICE && !it->out && it->voiceFile) {
                lastFile_ = it->voiceFile;
                lastMs_ = it->voiceMs;
                lastTime_ = it->time;
                break;
            }
        label(foot, &icone_22, lastFile_ ? AMBRA() : GRIGIO2(), IC_PLAY);
        lv_obj_t* fl = label(foot, &font_20, lastFile_ ? BIANCO() : GRIGIO2(),
                             lastFile_ ? ("Ultimo vocale ricevuto · " + durationMS(lastMs_) + " · " + whenShort(lastTime_)).c_str()
                                       : "Nessun vocale ricevuto");
        grow(fl);
        unaRiga(fl);
        show();
        if (startNow_) {
            startNow_ = false;
            startRec();
        }
    }

    // Aggiorna i colori e i testi secondo lo stato.
    void show() {
        lv_color_t ring = GRIGIO1(), inside = NERO();
        std::string t, s;
        lv_obj_add_flag(vu_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(time_, LV_OBJ_FLAG_HIDDEN);
        switch (state_) {
            case IDLE:
                t = TXT("Tieni premuto PTT", ptt("Tieni premuto " IC_MICROFONO, "Premi shift+" IC_MICROFONO));
                s = TXT("parla e poi lascia il tasto: il vocale parte da solo (massimo ",
                        ptt("parla e poi lascia il tasto: il vocale parte da solo (massimo ",
                            "parla e poi ripremilo: il vocale parte da solo (massimo ")) +
                    std::to_string(app::VOICE_MAX_MS / 1000) + " secondi)";
                ring = AMBRA();
                break;
            case REC:
                t = "Registrazione " + durationMS(app::voce().recordedMs());
                s = TXT("lascia PTT per inviare", ptt("lascia " IC_MICROFONO " per inviare", "ripremi shift+" IC_MICROFONO " per inviare"));
                ring = ROSSO();
                inside = ROSSO_SCURO();
                lv_obj_clear_flag(vu_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(time_, LV_OBJ_FLAG_HIDDEN);
                break;
            case SENT:
                switch (sendState_) {
                    case pn::VoiceSendState::REQUESTING:
                        t = "Chiamata...";
                        s = "aspetto che " + name_ + " sia pronto";
                        ring = AMBRA();
                        break;
                    case pn::VoiceSendState::SENDING:
                        t = "Invio...";
                        s = "vocale di " + durationMS(sentMs_) + " in viaggio";
                        ring = AMBRA();
                        break;
                    case pn::VoiceSendState::DELIVERED:
                        t = "Consegnato";
                        s = name_ + " lo sta ascoltando";
                        ring = VERDE();
                        break;
                    case pn::VoiceSendState::PARTIAL:
                        t = "Consegnato in parte";
                        s = "qualche pezzo si è perso per strada";
                        ring = AMBRA();
                        break;
                    case pn::VoiceSendState::BUSY:
                        t = "Occupato";
                        s = name_ + " sta già ricevendo un altro vocale: riprova tra poco";
                        ring = ROSSO();
                        break;
                    case pn::VoiceSendState::UNREACHABLE:
                        t = "Non raggiungibile";
                        s = name_ + " non risponde";
                        ring = ROSSO();
                        break;
                }
                break;
            case PLAY:
                t = "Ascolto " + durationMS(app::voce().playedMs());
                s = "vocale di " + name_;
                ring = VERDE();
                lv_obj_clear_flag(time_, LV_OBJ_FLAG_HIDDEN);
                break;
        }
        lv_obj_set_style_border_color(circle_, ring, 0);
        fill(circle_, inside);
        lv_label_set_text(icon_, state_ == PLAY ? IC_VOLUME : IC_MICROFONO);
        lv_obj_set_style_text_color(icon_, ring, 0);
        lv_label_set_text(title_, t.c_str());
        lv_label_set_text(sub_, s.c_str());
    }

    void startRec() {
        if (state_ == REC) return;
        if (app::voce().playing()) app::voce().stop();
        if (!app::voce().startRecording()) {
            toast("Microfono non disponibile", "", ROSSO());
            return;
        }
        scheda::vibrate(40);
        state_ = REC;
        show();
    }

    void finishRec() {
        uint16_t ms = 0;
        std::vector<uint8_t> data = app::voce().takeRecording(ms);
        if (ms < MIN_VOICE_MS || data.empty()) {
            state_ = IDLE;
            show();
#if TDECK
            lv_label_set_text(sub_, ptt("troppo corto: tieni premuto " IC_MICROFONO " mentre parli",
                                        "troppo corto: parla prima di ripremere")
                                        .c_str());
#else
            lv_label_set_text(sub_, "troppo corto: tieni premuto PTT mentre parli");
#endif
            return;
        }
        sentId_ = app::rete().sendVoice(number_, data, ms);
        if (!sentId_) {
            state_ = IDLE;
            show();
            toast("Vocale non inviato", "C'è già un vocale in invio: aspetta che finisca", ROSSO());
            return;
        }
        scheda::vibrate(40);
        sentMs_ = ms;
        sendState_ = pn::VoiceSendState::REQUESTING;
        state_ = SENT;
        show();
    }

    void play(uint32_t file, uint16_t ms) {
        if (!file) return;
        playMs_ = ms;
        app::voce().play(file);
        state_ = PLAY;
        lv_obj_set_style_bg_color(time_, VERDE(), LV_PART_INDICATOR);
        lv_bar_set_range(time_, 0, std::max<int>(1, ms / 100));
        lv_bar_set_value(time_, 0, LV_ANIM_OFF);
        show();
    }

    void tick() override {
        if (state_ == REC) {
            if (!app::voce().recording()) {
                finishRec();  // arrivato al massimo o microfono fermo
                return;
            }
            lv_bar_set_value(time_, int32_t(app::voce().recordedMs() / 100), LV_ANIM_OFF);
            lv_bar_set_value(vu_, scheda::audio().level(), LV_ANIM_OFF);
            lv_label_set_text(title_, ("Registrazione " + durationMS(app::voce().recordedMs())).c_str());
        } else if (state_ == PLAY) {
            if (!app::voce().playing() && ++idleTicks_ > 4) {
                idleTicks_ = 0;
                state_ = IDLE;
                lv_obj_set_style_bg_color(time_, ROSSO(), LV_PART_INDICATOR);
                lv_bar_set_range(time_, 0, app::VOICE_MAX_MS / 100);
                show();
                return;
            }
            if (app::voce().playing()) idleTicks_ = 0;
            lv_bar_set_value(time_, int32_t(app::voce().playedMs() / 100), LV_ANIM_OFF);
            lv_label_set_text(title_, ("Ascolto " + durationMS(app::voce().playedMs())).c_str());
        }
    }

    bool onKey(Key k, bool pressed) override {
        if (k == Key::PTT) {
            if (pressed) startRec();
            else if (state_ == REC) app::voce().stopRecording();  // il resto in tick()
            return true;
        }
        if (!pressed) return false;
        if (k == Key::OK && state_ != REC) {
            if (state_ == PLAY) {
                app::voce().stop();
            } else {
                play(lastFile_, lastMs_);
            }
            return true;
        }
        if (k == Key::CALL) {
            replace(makeChat({app::ChatKind::DIRECT, number_}));
            return true;
        }
        return state_ == REC;  // durante la registrazione gli altri tasti non fanno nulla
    }

    void onEvent(const app::Event& e) override {
        if (e.type == app::Ev::VOICE_STATE && e.id == sentId_ && state_ == SENT) {
            sendState_ = pn::VoiceSendState(e.a);
            if (sendState_ == pn::VoiceSendState::DELIVERED) scheda::audio().play(scheda::suoni::INVIO_OK);
            if (sendState_ == pn::VoiceSendState::UNREACHABLE || sendState_ == pn::VoiceSendState::BUSY)
                scheda::audio().play(scheda::suoni::ERRORE);
            show();
        } else if (e.type == app::Ev::VOICE_READY && e.from == number_ && state_ != REC) {
            // Risposta arrivata: si ascolta subito qui.
            lastFile_ = e.id;
            lastMs_ = uint16_t(e.a);
            lastTime_ = app::rete().unixTime();
            play(lastFile_, lastMs_);
        } else if (e.type == app::Ev::VOICE_INCOMING && e.from == number_ && state_ != REC) {
            lv_label_set_text(sub_, (name_ + " sta parlando...").c_str());
        }
    }

    std::vector<Hint> hints() override {
        if (state_ == REC) return {{"PTT", "lascia per inviare"}};
        if (state_ == PLAY) return {{"OK", "ferma"}, {"PTT", "rispondi"}};
        return {{"PTT", "parla"}, {"OK", "ascolta l'ultimo"}, {"CALL", "chat"}};
    }
    bool capturesEnd() const override { return state_ == REC; }

private:
    enum State { IDLE, REC, SENT, PLAY };
    uint16_t number_;
    bool startNow_;
    std::string name_;
    State state_ = IDLE;
    pn::VoiceSendState sendState_ = pn::VoiceSendState::REQUESTING;
    uint32_t sentId_ = 0;
    uint16_t sentMs_ = 0;
    uint32_t lastFile_ = 0;
    uint16_t lastMs_ = 0, playMs_ = 0;
    uint32_t lastTime_ = 0;
    int idleTicks_ = 0;
    lv_obj_t *circle_ = nullptr, *icon_ = nullptr, *title_ = nullptr, *sub_ = nullptr, *time_ = nullptr,
             *vu_ = nullptr;
};
}  // namespace

Screen* makeWalkie(uint16_t number, bool startNow) {
    if (number) return new Walkie(number, startNow);
    return makePickContact("Walkie-talkie: con chi?", [](uint16_t n) { push(new Walkie(n, false)); });
}

bool walkieTakesVoice(uint16_t from) {
    return g_walkie && top() == static_cast<Screen*>(g_walkie) && g_walkie->number() == from;
}

bool walkieRecording() { return g_walkie && g_walkie->busy(); }

// ================================================================ riproduzione di un vocale

namespace {
class VoicePlayer : public Screen {
public:
    VoicePlayer(uint16_t from, uint32_t file, bool autoplay) : from_(from), file_(file), autoplay_(autoplay) {
        uint32_t missing = 0;
        voiceInfo(file_, ms_, missing);
        for (int i = 0; i < 32; i++)
            if (missing & (1u << i)) lost_++;
    }
    ~VoicePlayer() override {
        if (app::voce().playing() && app::voce().playingFile() == file_) app::voce().stop();
    }
    void build(lv_obj_t* root) override {
        const bool mine = from_ == app::rete().number();
        const std::string name = mine ? "Tu" : app::dati().nameOf(from_);
        lv_obj_t* col = column(root, PX(14, 4));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_MICROFONO, "Vocale", mine ? "inviato da te" : "da " + name);
        lv_obj_t* mid = column(col, PX(14, 4));
        lv_obj_set_size(mid, W_SCHERMO, H_AREA - H_TITOLO - PX(14, 4));
        lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        if (!mine) avatar(mid, from_, app::dati().nameOf(from_), PX(90, 36));
        rigaPiena(label(mid, &font_32b, BIANCO(), name.c_str()), true);
        circle_ = bigCircle(mid, PX(96, 48), &icon_);
        lv_obj_t* r = row(mid, PX(12, 8));
        lv_obj_set_size(r, PX(420, 290), LV_SIZE_CONTENT);
        pos_ = label(r, &mono_20, BIANCO(), "0:00");
        prog_ = bar(r, PX(300, 190), PX(10, 6), VERDE());
        lv_bar_set_range(prog_, 0, std::max<int>(1, ms_ / 100));
        label(r, &mono_20, GRIGIO3(), durationMS(ms_).c_str());
        if (lost_)
            label(mid, &font_18, AMBRA(),
                  ("Qualche pezzo si è perso (" + std::to_string(lost_) + "): li senti come silenzio").c_str());
        if (!ms_) label(mid, &font_18, ROSSO(), "File del vocale non trovato");
        if (autoplay_ && ms_) {
            autoplay_ = false;
            app::voce().play(file_);
            started_ = true;
        }
        show();
    }
    void show() {
        const bool on = app::voce().playing() && app::voce().playingFile() == file_;
        lv_label_set_text(icon_, on ? IC_VOLUME : IC_PLAY);
        lv_obj_set_style_text_color(icon_, on ? VERDE() : AMBRA(), 0);
        lv_obj_set_style_border_color(circle_, on ? VERDE() : AMBRA(), 0);
    }
    void tick() override {
        const bool on = app::voce().playing() && app::voce().playingFile() == file_;
        if (on) waited_ = 0;
        else if (started_ && ++waited_ > 8) started_ = false;  // finito (o mai partito)
        const uint32_t p = on ? app::voce().playedMs() : 0;
        lv_label_set_text(pos_, durationMS(p).c_str());
        lv_bar_set_value(prog_, int32_t(p / 100), LV_ANIM_OFF);
        show();
    }
    bool onKey(Key k, bool pressed) override {
        if (k == Key::PTT && from_ != app::rete().number()) {
            if (pressed) {
                app::voce().stop();
                // Risposta: si passa al walkie-talkie e si comincia subito a registrare.
                replace(new Walkie(from_, true));
            }
            return true;
        }
        if (!pressed) return false;
        if (k == Key::OK && ms_) {
            if (app::voce().playing() && app::voce().playingFile() == file_) {
                app::voce().stop();
            } else {
                app::voce().play(file_);
                started_ = true;
                waited_ = 0;
            }
            show();
            return true;
        }
        if (k == Key::CALL && from_ != app::rete().number()) {
            replace(makeChat({app::ChatKind::DIRECT, from_}));
            return true;
        }
        return false;
    }
    std::vector<Hint> hints() override {
        if (from_ == app::rete().number()) return {{"OK", "ascolta/ferma"}, {"BACK", "chiudi"}};
        return {{"OK", "ascolta/ferma"}, {"PTT", "rispondi"}, {"CALL", "chat"}};
    }

private:
    uint16_t from_;
    uint32_t file_;
    bool autoplay_;
    uint16_t ms_ = 0;
    int lost_ = 0;
    bool started_ = false;
    int waited_ = 0;
    lv_obj_t *circle_ = nullptr, *icon_ = nullptr, *pos_ = nullptr, *prog_ = nullptr;
};
}  // namespace

Screen* makeVoicePlayer(uint16_t from, uint32_t file, bool autoplay) {
    return new VoicePlayer(from, file, autoplay);
}

}  // namespace ui
