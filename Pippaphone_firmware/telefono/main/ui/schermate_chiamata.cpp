// Chiamata in diretta, a turni: si parla tenendo premuto il PTT, poi tocca all'altro.
#include "app/dati.h"
#include "app/rete.h"
#include "app/voce.h"
#include "scheda/scheda.h"
#include "ui/schermate.h"
#include "ui/widget.h"

namespace ui {

namespace {

class CallScreen;
CallScreen* g_call = nullptr;

const char* resultText(pn::CallResult r) {
    switch (r) {
        case pn::CallResult::ENDED: return "Chiamata finita";
        case pn::CallResult::HUNG_UP: return "Ha riattaccato";
        case pn::CallResult::REFUSED: return "Ha rifiutato la chiamata";
        case pn::CallResult::BUSY: return "Occupato";
        case pn::CallResult::NO_ANSWER: return "Non risponde";
        case pn::CallResult::UNREACHABLE: return "Non raggiungibile";
        case pn::CallResult::DROPPED: return "Chiamata caduta";
        case pn::CallResult::TOO_LONG: return "Durata massima raggiunta";
        case pn::CallResult::MISSED: return "Chiamata persa";
        default: return "Chiamata finita";
    }
}

class CallScreen : public Screen {
public:
    // dial = chiama adesso; altrimenti mostra la chiamata gia' in corso (in arrivo).
    CallScreen(uint16_t number, bool dial) : number_(number), dial_(dial) { g_call = this; }
    ~CallScreen() override {
        // Una schermata vecchia (chiamata gia' finita) non tocca quella nuova.
        if (g_call != this) return;
        g_call = nullptr;
        tones(pn::CallState::IDLE);
        // Si esce dalla schermata solo a chiamata finita, ma per sicurezza.
        if (!ended_ && app::rete().callInfo().state != pn::CallState::IDLE) app::rete().endCall();
    }

    void build(lv_obj_t* root) override {
        if (dial_) {
            dial_ = false;
            if (!app::rete().call(number_)) {
                result_ = "Non si puo' chiamare adesso";
                detail_ = "C'e' gia' una chiamata o un vocale in corso";
                ended_ = true;
                endedTicks_ = 0;
            }
        }
        name_ = app::dati().nameOf(number_);
        lv_obj_t* col = column(root, PX(10, 3));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        header(col, IC_TELEFONO, "Chiamata", std::to_string(number_));
        lv_obj_t* who = row(col, PX(14, 8));
        lv_obj_set_size(who, PX(440, 300), LV_SIZE_CONTENT);
        lv_obj_set_flex_align(who, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        avatar(who, number_, name_, PX(64, 34));
        lv_obj_t* nl = label(who, PX(&font_32b, &font_26b), BIANCO(), name_.c_str());
        unaRiga(nl);
        circle_ = box(col);
        lv_obj_set_size(circle_, PX(140, 64), PX(140, 64));
        radius(circle_, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(circle_, PX(4, 3), 0);
        icon_ = label(circle_, &icone_44, BIANCO(), IC_TELEFONO);
        lv_obj_center(icon_);
        title_ = label(col, &font_26b, BIANCO(), "");
        sub_ = label(col, &font_20, GRIGIO3(), "");
        lv_obj_set_width(sub_, PX(440, 300));
        lv_obj_set_style_text_align(sub_, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(sub_, LV_LABEL_LONG_WRAP);
        info_ = label(col, &font_16, GRIGIO2(), "");
        show();
    }

    // Carta: mentre parli il cerchio e' pieno d'arancio con l'icona bianca (nel tema scuro anello
    // rosso su fondo rosso scuro), e la riga in fondo diventa arancio quando la chiamata sta per chiudere.
    void show() {
        if (!title_) return;
        const app::Rete::CallInfo ci = app::rete().callInfo();
        lv_color_t ring = AMBRA(), inside = NERO();
        bool full = false, closing = false;
        std::string t, s, i;
        const char* ic = IC_TELEFONO;
        if (ended_) {
            t = result_;
            s = detail_;
            ring = GRIGIO2();
        } else {
            switch (ci.state) {
                case pn::CallState::IDLE:
                    t = "Chiamata finita";
                    ring = GRIGIO2();
                    break;
                case pn::CallState::DIALING:
                    t = "Chiamo...";
                    s = "aspetto che squilli";
                    break;
                case pn::CallState::RINGBACK:
                    t = "Squilla...";
                    s = "aspetto che " + name_ + " risponda";
                    break;
                case pn::CallState::INCOMING:
                    t = name_ + " ti chiama";
                    s = TXT("CALL per rispondere, END per rifiutare",
                            std::string(tasto("CALL")) + " per rispondere, " + tasto("END") + " per rifiutare");
                    ring = VERDE();
                    break;
                case pn::CallState::CONNECTING:
                    t = "Collegamento...";
                    s = "si apre la strada sul canale voce";
                    break;
                case pn::CallState::ACTIVE:
                    ic = IC_MICROFONO;
                    if (ci.talking) {
                        t = "Parli tu";
                        s = TXT("lascia PTT quando hai finito", "lascia " IC_MICROFONO " quando hai finito");
                        ring = ROSSO();
                        inside = temaChiaro() ? ROSSO() : ROSSO_SCURO();
                        full = temaChiaro();
                    } else if (ci.hearing) {
                        t = "Parla " + name_;
                        s = "aspetta che finisca: senti un bip";
                        ring = VERDE();
                        ic = IC_VOLUME;
                    } else {
                        t = "In chiamata " + durationMS(ci.elapsedMs);
                        s = TXT("tieni premuto PTT per parlare", "tieni premuto " IC_MICROFONO " per parlare");
                        ring = AMBRA();
                    }
                    if (!waitMsg_.empty()) s = waitMsg_;
                    i = durationMS(ci.elapsedMs);
                    i += ci.hops ? " · " + std::to_string(ci.hops) + (ci.hops == 1 ? " ripetitore" : " ripetitori")
                                 : " · diretta";
                    if (ci.lostPct) i += " · voce persa " + std::to_string(ci.lostPct) + "%";
                    if (ci.leftMs < 30000) i += " · chiude tra " + std::to_string(ci.leftMs / 1000) + " s";
                    closing = ci.leftMs < 30000;
                    break;
            }
        }
        lv_obj_set_style_border_color(circle_, ring, 0);
        fill(circle_, inside);
        lv_label_set_text(icon_, ic);
        lv_obj_set_style_text_color(icon_, full ? SU_COLORE() : ring, 0);
        lv_label_set_text(title_, t.c_str());
        lv_label_set_text(sub_, s.c_str());
        lv_label_set_text(info_, i.c_str());
        // Carta: e' l'unica notizia sulla qualita' della chiamata, grigio scuro e non chiaro.
        if (temaChiaro()) lv_obj_set_style_text_color(info_, closing ? ROSSO() : GRIGIO3(), 0);
    }

    // Squillo per chi riceve, tono di chiamata per chi chiama.
    void tones(pn::CallState s) {
        const app::Settings st = app::dati().settings();
        if (s == pn::CallState::INCOMING && !ringing_) {
            ringing_ = true;
            if (st.ringtone) scheda::audio().play(scheda::suoni::CLASSICA, true);
            if (st.vibration) scheda::vibratePattern(20, 400, 800);
            scheda::ledBlink(0, 60, 0, 300, 700, 0);
        } else if (s != pn::CallState::INCOMING && ringing_) {
            ringing_ = false;
            scheda::audio().stop();
            scheda::ledOff();
        }
        if (s == pn::CallState::RINGBACK && !ringback_) {
            ringback_ = true;
            scheda::audio().play(scheda::suoni::SQUILLO_USCITA, true);
        } else if (s != pn::CallState::RINGBACK && ringback_) {
            ringback_ = false;
            scheda::audio().stop();
        }
    }

    void tick() override {
        if (ended_) {
            // Si mostra l'esito per un paio di secondi, poi si torna indietro.
            if (++endedTicks_ > 10) pop();
            return;
        }
        const app::Rete::CallInfo ci = app::rete().callInfo();
        tones(ci.state);
        if (ci.state == pn::CallState::ACTIVE && !wasActive_) {
            wasActive_ = true;
            scheda::vibrate(60);
        }
        if (!waitMsg_.empty() && ++waitTicks_ > 8) waitMsg_.clear();
        show();
        refreshHints();
    }

    void onEvent(const app::Event& e) override {
        if (e.type != app::Ev::CALL || e.from != number_) return;
        if (pn::CallState(e.a) == pn::CallState::IDLE) {
            const pn::CallResult r = pn::CallResult(e.id);
            ended_ = true;
            endedTicks_ = 0;
            result_ = resultText(r);
            detail_ = r == pn::CallResult::UNREACHABLE ? "forse e' spento o troppo lontano dai ripetitori"
                      : r == pn::CallResult::DROPPED   ? "il segnale non basta piu'"
                      : r == pn::CallResult::TOO_LONG  ? "chi e' in chiamata non riceve i messaggi: massimo 3 minuti"
                                                       : "";
            tones(pn::CallState::IDLE);
            if (r != pn::CallResult::ENDED) scheda::vibrate(150);
            show();
            refreshHints();
        }
    }

    bool onKey(Key k, bool pressed) override {
        const app::Rete::CallInfo ci = app::rete().callInfo();
        if (k == Key::PTT) {
            if (ci.state != pn::CallState::ACTIVE) return true;
            if (pressed) {
                if (app::rete().callTalk(true)) {
                    scheda::vibrate(30);
                    waitMsg_.clear();
                } else {
                    waitMsg_ = "Aspetta: sta parlando " + name_;
                    waitTicks_ = 0;
                    scheda::vibratePattern(2, 60, 60);
                }
            } else {
                app::rete().callTalk(false);
            }
            show();
            return true;
        }
        if (!pressed) return true;
        if (ended_ || ci.state == pn::CallState::IDLE) {
            if (k == Key::BACK || k == Key::END || k == Key::OK) pop();
            return true;
        }
        if ((k == Key::CALL || k == Key::OK) && ci.state == pn::CallState::INCOMING) {
            tones(pn::CallState::IDLE);
            app::rete().answerCall();
            return true;
        }
        if (k == Key::END) {
            app::rete().endCall();
            return true;
        }
        // Durante la chiamata gli altri tasti non portano via la schermata.
        return true;
    }

    std::vector<Hint> hints() override {
        if (ended_) return {{"OK", "chiudi"}};
        switch (app::rete().callInfo().state) {
            case pn::CallState::INCOMING: return {{"CALL", "rispondi"}, {"END", "rifiuta"}};
            case pn::CallState::ACTIVE: return {{"PTT", "parla"}, {"END", "riattacca"}};
            case pn::CallState::IDLE: return {{"OK", "chiudi"}};
            default: return {{"END", "annulla"}};
        }
    }
    bool capturesEnd() const override { return true; }
    uint16_t number() const { return number_; }
    bool ended() const { return ended_; }

private:
    void refreshHints() {
        const auto h = hints();
        std::string key;
        for (const auto& x : h) key += std::string(x.key) + x.text;
        if (key != hintKey_) {
            hintKey_ = key;
            refreshBars();
        }
    }

    uint16_t number_;
    bool dial_;
    std::string name_;
    bool ended_ = false, ringing_ = false, ringback_ = false, wasActive_ = false;
    int endedTicks_ = 0, waitTicks_ = 0;
    std::string result_, detail_, waitMsg_, hintKey_;
    lv_obj_t *circle_ = nullptr, *icon_ = nullptr, *title_ = nullptr, *sub_ = nullptr, *info_ = nullptr;
};

}  // namespace

Screen* makeCall(uint16_t number) {
    if (number) return new CallScreen(number, true);
    return makePickContact("Chiama: chi?", [](uint16_t n) { push(new CallScreen(n, true)); });
}

Screen* makeIncomingCall(uint16_t from) { return new CallScreen(from, false); }

bool callScreenOpen() { return g_call != nullptr && !g_call->ended(); }

}  // namespace ui
