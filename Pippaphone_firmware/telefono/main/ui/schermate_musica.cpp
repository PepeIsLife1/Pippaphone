// Musica: sul T-Deck i brani MP3 e WAV della cartella /musica della microSD; sulla PCB per ora
// solo un avviso (lo slot microSD c'e', ma il firmware non lo usa ancora).
#include <algorithm>
#include <cstdio>

#include "app/dati.h"
#include "app/memoria.h"
#include "app/musica.h"
#include "scheda/scheda.h"
#include "ui/schermate.h"
#include "ui/widget.h"
#if TDECK
#include "app/cuffie.h"
#endif

namespace ui {

#if !TDECK
class MusicPcb : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, PX(14, 6));
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_MUSICA, "Musica");
        lv_obj_t* body = column(col, PX(14, 6));
        lv_obj_set_size(body, W_SCHERMO, LV_SIZE_CONTENT);
        pad(body, PX(20, 8), PX(24, 12), 0, PX(24, 12));
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        label(body, &icone_44, GRIGIO2(), IC_MUSICA);
        lv_obj_t* t = label(body, &font_22, BIANCO(), "Per la musica serve la microSD.");
        lv_obj_set_width(t, 420);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t* s = label(body, &font_18, GRIGIO3(),
                            "Lo slot c'è (J6), ma questa versione del firmware non lo usa ancora: arriverà dopo "
                            "le prove sulla PCB vera. Sul LilyGO T-Deck Plus la musica c'è già (cartella /musica "
                            "della microSD, file MP3 e WAV).");
        lv_obj_set_width(s, 420);
        lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(s, LV_TEXT_ALIGN_CENTER, 0);
    }
    std::vector<Hint> hints() override { return {{"BACK", "indietro"}}; }
};

Screen* makeMusic() { return new MusicPcb(); }
#else
namespace {
std::string mmss(uint32_t s) {
    char b[16];
    snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    return b;
}

// Voce del MENU per le cuffie USB-C: accenderle (prima la domanda) o spegnerle.
const char* voceCuffie() { return app::cuffie().accese() ? "Spegni le cuffie USB-C" : "Ascolta con le cuffie USB-C"; }
void sceltaCuffie() {
    if (app::cuffie().accese()) app::cuffie().spegni();
    else accendiCuffie();
}
}  // namespace

// ================================================================ in riproduzione

class NowPlaying : public Screen {
public:
    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 4);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        pad(col, 8, 12, 4, 12);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        icon_ = dur_ = vol_ = play_ = tempi_ = tasti_ = nullptr;
        if (temaChiaro()) {
            buildCarta(col);
            update();
            return;
        }
        icon_ = label(col, &icone_44, AMBRA(), IC_MUSICA);
        title_ = rigaPiena(label(col, &font_26b, BIANCO(), ""), true);
        artist_ = rigaPiena(label(col, &font_20, GRIGIO3(), ""), true);
        bar_ = lv_bar_create(col);
        lv_obj_remove_style_all(bar_);
        lv_obj_set_size(bar_, 280, 6);
        lv_obj_set_style_bg_color(bar_, GRIGIO1(), 0);
        lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar_, 3, 0);
        lv_obj_set_style_bg_color(bar_, AMBRA(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar_, 3, LV_PART_INDICATOR);
        lv_bar_set_range(bar_, 0, 1000);
        lv_obj_t* r = row(col, 0);
        lv_obj_set_size(r, 280, LV_SIZE_CONTENT);
        time_ = label(r, &mono_16, GRIGIO3(), "");
        grow(time_);
        state_ = label(r, &font_16, GRIGIO2(), "");
        update();
    }
    // Carta (come lo sketch): titolo grande in alto, barra spessa col bordo nero, sotto il tasto della
    // pausa tondo e nero tra precedente e successivo.
    void buildCarta(lv_obj_t* col) {
        pad(col, 10, 14, 6, 14);
        n_ = int(app::musica().elenco().size());
        state_ = rigaPiena(label(col, &font_16, GRIGIO3(), ""));  // "IN RIPRODUZIONE · 1 DI 4"
        lv_obj_set_style_text_letter_space(state_, 1, 0);
        title_ = rigaPiena(label(col, &font_32b, BIANCO(), ""));
        artist_ = rigaPiena(label(col, &font_20, GRIGIO3(), ""));
        lv_obj_t* spazio = box(col);
        lv_obj_set_size(spazio, 1, 0);
        grow(spazio);
        bar_ = lv_bar_create(col);
        lv_obj_remove_style_all(bar_);
        lv_obj_set_size(bar_, lv_pct(100), 12);
        lv_obj_set_style_border_width(bar_, 2, 0);
        lv_obj_set_style_border_color(bar_, INCHIOSTRO(), 0);
        lv_obj_set_style_radius(bar_, 6, 0);
        lv_obj_set_style_pad_all(bar_, 2, 0);  // il blu dentro il bordo
        lv_obj_set_style_bg_color(bar_, AMBRA(), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar_, 4, LV_PART_INDICATOR);
        lv_bar_set_range(bar_, 0, 1000);
        tempi_ = row(col, 0);
        lv_obj_set_size(tempi_, lv_pct(100), LV_SIZE_CONTENT);
        time_ = label(tempi_, &mono_16, BIANCO(), "");
        vol_ = label(tempi_, &font_16, GRIGIO3(), "");
        grow(vol_);
        lv_obj_set_style_text_align(vol_, LV_TEXT_ALIGN_CENTER, 0);
        dur_ = label(tempi_, &mono_16, BIANCO(), "");
        tasti_ = row(col, 22);
        lv_obj_set_size(tasti_, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_align(tasti_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        label(tasti_, &icone_22, BIANCO(), IC_PRECEDENTE);
        lv_obj_t* tondo = box(tasti_);
        lv_obj_set_size(tondo, 40, 40);
        radius(tondo, LV_RADIUS_CIRCLE);
        fill(tondo, INCHIOSTRO());
        play_ = label(tondo, &icone_22, SU_COLORE(), IC_PAUSA);
        lv_obj_center(play_);
        label(tasti_, &icone_22, BIANCO(), IC_SUCCESSIVO);
    }
    // Carta: barra, tempi e tasti si vedono solo mentre c'e' un brano (a musica finita OK non fa
    // niente: il tasto play tondo prometterebbe una cosa che non c'e').
    void mostraLettore(bool si) {
        for (lv_obj_t* o : {bar_, tempi_, tasti_}) {
            // Gia' cosi': niente. Togliere HIDDEN fa sempre ridisegnare (4 volte al secondo, sul bus
            // SPI della microSD da cui arriva la musica).
            if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != si) continue;
            if (si) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        }
    }
    void update() {
        auto& m = app::musica();
        if (!m.attiva()) {
            lv_label_set_text(title_, "Musica finita");
            lv_label_set_text(artist_, m.errore().c_str());
            lv_label_set_text(time_, "");
            lv_label_set_text(state_, "");
            if (dur_) {  // Carta
                lv_label_set_text(dur_, "");
                lv_label_set_text(vol_, "");
                lv_label_set_text(play_, IC_PLAY);
                mostraLettore(false);
            }
            lv_bar_set_value(bar_, 0, LV_ANIM_OFF);
            return;
        }
        if (dur_) mostraLettore(true);
        const app::Brano b = m.brano();
        lv_label_set_text(title_, (b.titolo.empty() ? b.nome : b.titolo).c_str());
        lv_label_set_text(artist_, b.artista.c_str());
        const uint32_t pos = m.posizioneS(), dur = m.durataS();
        // Con le cuffie USB-C che suonano il volume e' il loro.
        const bool cuffie = app::cuffie().pronta();
        const unsigned volume = cuffie ? app::cuffie().volume() : scheda::audio().volume();
        const char* dove = cuffie ? " · cuffie" : "";
        char s[48];
        if (dur_) {  // Carta: tempo a sinistra, durata a destra, il volume in mezzo
            lv_label_set_text(time_, mmss(pos).c_str());
            lv_label_set_text(dur_, dur ? mmss(dur).c_str() : "");
            const char* st = m.inPausa() ? "IN PAUSA" : "IN RIPRODUZIONE";
            if (n_ > 0) snprintf(s, sizeof(s), "%s · %d DI %d", st, m.corrente() + 1, n_);
            else snprintf(s, sizeof(s), "%s", st);
            lv_label_set_text(state_, s);
            snprintf(s, sizeof(s), "volume %u%s", volume, dove);
            lv_label_set_text(vol_, s);
            lv_label_set_text(play_, m.inPausa() ? IC_PLAY : IC_PAUSA);
        } else {
            lv_label_set_text(time_, (mmss(pos) + (dur ? " / " + mmss(dur) : "")).c_str());
            snprintf(s, sizeof(s), "%s · volume %u%s", m.inPausa() ? "in pausa" : "suona", volume, dove);
            lv_label_set_text(state_, s);
            lv_obj_set_style_text_color(icon_, m.inPausa() ? GRIGIO2() : AMBRA(), 0);
        }
        lv_bar_set_value(bar_, dur ? int32_t(std::min<uint32_t>(1000, pos * 1000 / dur)) : 0, LV_ANIM_OFF);
    }
    void tick() override { update(); }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        auto& m = app::musica();
        switch (k) {
            case Key::OK:
            case Key::ENTER:
                if (m.attiva()) m.pausa(!m.inPausa());
                break;
            case Key::LEFT: m.precedente(); break;
            case Key::RIGHT: m.successivo(); break;
            case Key::UP:
            case Key::DOWN: {
                if (app::cuffie().pronta()) {  // le cuffie USB-C suonano: il loro volume
                    cambiaVolumeCuffie(k == Key::UP ? 1 : -1);
                    break;
                }
                app::Settings s = app::dati().settings();
                s.volume = uint8_t(std::max(0, std::min(10, s.volume + (k == Key::UP ? 1 : -1))));
                app::dati().setSettings(s);
                scheda::audio().setVolume(s.volume);
                break;
            }
            case Key::MENU:
                ask("Musica", "", {"Avanti 10 secondi", "Indietro 10 secondi", voceCuffie(), "Ferma la musica"}, [](int i) {
                    if (i == 0) app::musica().salta(10);
                    else if (i == 1) app::musica().salta(-10);
                    else if (i == 2) sceltaCuffie();
                    else if (i == 3) {
                        app::musica().ferma();
                        pop();
                    }
                });
                return true;
            default: return false;
        }
        update();
        refreshBars();
        return true;
    }
    std::vector<Hint> hints() override {
        auto& m = app::musica();
        if (!m.attiva()) return {{"</>", "brano"}, {"BACK", "indietro"}};  // finita: OK non fa niente
        return {{"OK", m.inPausa() ? "riprendi" : "pausa"}, {"</>", "brano"}, {"MENU", "altro"}};
    }

private:
    lv_obj_t *icon_ = nullptr, *title_ = nullptr, *artist_ = nullptr, *bar_ = nullptr, *time_ = nullptr, *state_ = nullptr;
    lv_obj_t *dur_ = nullptr, *vol_ = nullptr, *play_ = nullptr, *tempi_ = nullptr, *tasti_ = nullptr;  // solo Carta
    int n_ = 0;  // brani in tutto (Carta: "1 DI 4")
};

// ================================================================ elenco dei brani

class MusicList : public Screen {
public:
    explicit MusicList(bool rileggi = false) : rileggi_(rileggi) {}
    void build(lv_obj_t* root) override {
        // Con rebuild() le righe di prima sono gia' cancellate: l'elenco riparte vuoto anche quando
        // poi non si crea (niente microSD, nessun brano), se no i tasti userebbero oggetti liberati.
        list_ = List();
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        if (!app::sdPresente()) {
            empty(col, "Per la musica serve la microSD", "Metti i file MP3 o WAV nella cartella /musica della scheda.");
            return;
        }
        brani_ = app::musica().elenco(rileggi_);
        rileggi_ = false;
        if (brani_.empty()) {  // empty() mette la sua intestazione
            // La microSD si prepara solo all'accensione: tolta e rimessa a telefono acceso non risponde
            // piu' (e "Nessun brano" farebbe cercare l'errore nella cartella).
            if (app::musica().sdNonRisponde())
                empty(col, "La microSD non risponde",
                      "Se l'hai tolta e rimessa a telefono acceso, spegni e riaccendi il telefono: la microSD si "
                      "legge all'accensione.");
            else
                empty(col, "Nessun brano",
                      "Copia file MP3 o WAV nella cartella /musica della microSD (anche in sottocartelle), a "
                      "telefono spento.");
            return;
        }
        header(col, IC_MUSICA, "Musica", std::to_string(brani_.size()) + " brani");
        list_.create(col, W_SCHERMO, H_AREA - H_TITOLO);
        const int playing = app::musica().attiva() ? app::musica().corrente() : -1;
        for (size_t i = 0; i < brani_.size(); i++) {
            lv_obj_t* r = list_.addRow(30);
            List::text(r, &icone_18, int(i) == playing ? VERDE() : colore(0x808080, 0x404040),  // Carta: piu' scura
                       int(i) == playing ? IC_PLAY : IC_MUSICA);
            List::text(r, &font_20, BIANCO(), brani_[i].nome, true);
        }
        list_.focus(sel_ >= 0 ? sel_ : std::max(0, playing));
    }
    void empty(lv_obj_t* col, const char* title, const char* text) {
        header(col, IC_MUSICA, "Musica");
        lv_obj_t* b = column(col, 8);
        lv_obj_set_size(b, W_SCHERMO, LV_SIZE_CONTENT);
        pad(b, 16, 14, 0, 14);
        label(b, &font_22b, BIANCO(), title);
        lv_obj_t* t = label(b, &font_18, GRIGIO3(), text);
        lv_obj_set_width(t, 290);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (list_.key(k)) {
            sel_ = list_.focused();
            return true;
        }
        if ((k == Key::OK || k == Key::ENTER) && !brani_.empty()) {
            sel_ = list_.focused();
            app::musica().suona(sel_);
            push(new NowPlaying());
            return true;
        }
        if (k == Key::MENU) {
            ask("Musica", "", {"Rileggi la microSD", voceCuffie(), "Annulla"}, [this](int i) {
                if (i == 1) sceltaCuffie();
                if (i != 0) return;
                rileggi_ = true;
                rebuild();
            });
            return true;
        }
        return false;
    }
    std::vector<Hint> hints() override { return {{"OK", "suona"}, {"MENU", "rileggi"}, {"BACK", "indietro"}}; }

private:
    bool rileggi_;
    int sel_ = -1;
    List list_;
    std::vector<app::Brano> brani_;
};

// Con la musica che suona si apre direttamente "in riproduzione".
Screen* makeMusic() {
    if (!app::musica().attiva()) return new MusicList();
    return new NowPlaying();
}
#endif

}  // namespace ui
