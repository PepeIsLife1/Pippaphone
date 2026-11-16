// Menu Giochi (serpente, campo minato, sfide via radio a tris e battaglia navale) e avvisi delle
// sfide che arrivano mentre si fa altro.
#include <algorithm>
#include <cstdio>

#include "app/dati.h"
#include "app/giochi.h"
#include "app/rete.h"
#include "scheda/scheda.h"
#include "ui/giochi.h"
#include "ui/schermate.h"
#include "ui/widget.h"

namespace ui {

namespace {
struct SchermataSfida {
    Screen* s;
    uint16_t id;  // partita che mostra
};
std::vector<SchermataSfida> g_challenge;
}  // namespace

void challengeScreen(Screen* s, bool open, uint16_t id) {
    if (open) g_challenge.push_back({s, id});
    else
        g_challenge.erase(std::remove_if(g_challenge.begin(), g_challenge.end(), [s](const SchermataSfida& c) { return c.s == s; }),
                          g_challenge.end());
}

int challengeOnTop() {
    for (const auto& c : g_challenge)
        if (c.s == top()) return c.id;
    return -1;
}

const char* digitText(int n) {
    static const char* const D[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    return n >= 0 && n <= 9 ? D[n] : "";
}

void openChallenge() {
    const app::Sfida s = app::giochi().sfida();
    if (s.stato == app::Sfida::NESSUNA) return;
    push(s.gioco == app::Gioco::TRIS ? makeTris() : makeNavale());
}

namespace {
std::string mmss(uint32_t s) {
    char b[16];
    snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    return b;
}

// Testo breve sullo stato della sfida (per il menu e gli avvisi).
std::string statoSfida(const app::Sfida& s) {
    const std::string nome = app::dati().nameOf(s.avversario);
    switch (s.stato) {
        case app::Sfida::PREPARA: return "metti le navi";
        case app::Sfida::INVITATO: return s.nonArrivata ? "invito non arrivato" : "aspetto che accetti";
        case app::Sfida::INVITO_ARRIVATO: return "ti ha sfidato";
        case app::Sfida::IN_CORSO:
            if (s.nonArrivata) return "mossa non arrivata";
            return s.tocca ? "tocca a te" : "tocca a " + nome;
        // Tris finito dalla mia mossa, che pero' non e' arrivata: l'altro la aspetta ancora.
        case app::Sfida::FINITA: return s.nonArrivata ? "mossa non arrivata" : s.esito;
        default: return "";
    }
}

// Invito a una sfida: si risponde subito o si lascia per dopo (resta nel menu Giochi). "Dopo" e' la
// prima scelta: un OK o un Invio dato per altro (la finestra arriva mentre si fa altro) non accetta
// e non rifiuta.
void askInvite() {
    const app::Sfida s = app::giochi().sfida();
    if (s.stato != app::Sfida::INVITO_ARRIVATO) return;
    const std::string nome = app::dati().nameOf(s.avversario);
    ask(std::string("Sfida a ") + app::nomeGioco(s.gioco), nome + " ti sfida via radio. Giochi?",
        {"Dopo", "Gioca", "No, grazie"}, [](int i) {
            if (i == 1) {
                app::giochi().accetta();
                openChallenge();
            } else if (i == 2) {
                app::giochi().rifiuta();
                toast("Sfida rifiutata", "", GRIGIO3());
            }
        });
}
}  // namespace

// ================================================================ menu

class Games : public Screen {
public:
    enum Riga { SFIDA, SERPENTE, MINE, TRIS, NAVALE };

    void build(lv_obj_t* root) override {
        lv_obj_t* col = column(root, 0);
        lv_obj_set_size(col, W_SCHERMO, H_AREA);
        header(col, IC_GIOCHI, "Giochi");
        list_.create(col, W_SCHERMO, H_AREA - H_TITOLO);
        rows_.clear();
        const app::Sfida s = app::giochi().sfida();
        if (s.stato != app::Sfida::NESSUNA) {
            add(SFIDA, s.gioco == app::Gioco::TRIS ? IC_TRIS : IC_NAVE,
                TXT(std::string(app::nomeGioco(s.gioco)) + " con " + app::dati().nameOf(s.avversario),
                    "Con " + app::dati().nameOf(s.avversario)),
                statoSfida(s), s.tocca || s.stato == app::Sfida::INVITO_ARRIVATO ? AMBRA() : GRIGIO3());
        }
        const uint32_t rs = app::giochi().recordSerpente();
        add(SERPENTE, IC_MELA, "Serpente", rs ? "record " + std::to_string(rs) : "");
        uint32_t best = 0;
        for (int l = 0; l < 3; l++) {
            const uint32_t r = app::giochi().recordMine(l);
            if (r && (!best || l > 0)) best = r;  // il record del livello piu' difficile fatto
        }
        add(MINE, IC_BOMBA, "Campo minato", best ? "record " + mmss(best) : "");
        add(TRIS, IC_TRIS, "Tris", "via radio");
        add(NAVALE, IC_NAVE, "Battaglia navale", "via radio");
        list_.focus(sel_);
    }
    void add(Riga r, const char* icon, const std::string& text, const std::string& right, lv_color_t rc = GRIGIO3()) {
        lv_obj_t* row = list_.addRow(PX(56, 30));
        List::text(row, &icone_22, AMBRA(), icon);
        lv_obj_t* t = List::text(row, &font_22, BIANCO(), text, true);
        // Carta: anche sulla PCB su una riga sola, coi puntini ("Battaglia navale con Marco Rossi"
        // andava a capo e lasciava il cognome da solo sotto).
        if (temaChiaro()) lv_obj_set_height(t, lv_font_get_line_height(&font_22));
        if (!right.empty()) List::text(row, &font_18, rc, right);
        rows_.push_back(r);
    }
    bool onKey(Key k, bool pressed) override {
        if (!pressed) return false;
        if (list_.key(k)) {
            sel_ = list_.focused();
            return true;
        }
        if (k != Key::OK && k != Key::ENTER) return false;
        sel_ = list_.focused();
        if (sel_ < 0 || sel_ >= int(rows_.size())) return true;
        switch (rows_[size_t(sel_)]) {
            case SFIDA: {
                const app::Sfida s = app::giochi().sfida();
                if (s.stato == app::Sfida::INVITO_ARRIVATO) askInvite();
                else openChallenge();
                break;
            }
            case SERPENTE: push(makeSnake()); break;
            case MINE:
                ask("Campo minato", "", {"Facile (9 x 9, 10 mine)", "Medio (16 x 12, 30 mine)", "Difficile (24 x 14, 60 mine)"},
                    [](int i) {
                        if (i >= 0) push(makeMines(i));
                    });
                break;
            case TRIS:
            case NAVALE: challenge(rows_[size_t(sel_)] == TRIS ? app::Gioco::TRIS : app::Gioco::NAVALE); break;
        }
        return true;
    }
    void challenge(app::Gioco g) {
        if (app::giochi().attiva()) {
            const app::Sfida s = app::giochi().sfida();
            ask("C'è già una partita", std::string(app::nomeGioco(s.gioco)) + " con " + app::dati().nameOf(s.avversario) +
                                           ": finiscila o abbandonala prima di cominciarne un'altra.",
                {"Vai alla partita", "Annulla"}, [](int i) {
                    if (i == 0) openChallenge();
                });
            return;
        }
        // Tris finito dalla mia mossa che pero' non e' arrivata: prima la si rimanda (l'altro la
        // aspetta ancora), poi la partita si toglie.
        if (app::giochi().sfida().nonArrivata) {
            openChallenge();
            return;
        }
        app::giochi().chiudi();  // una partita finita si toglie
        push(makePickContact(std::string("Sfida a ") + app::nomeGioco(g), [g](uint16_t n) {
            if (!app::giochi().sfidaNuova(g, n)) {
                toast("Non si può", "Scegli un altro numero", ROSSO());
                return;
            }
            if (g == app::Gioco::TRIS) toast("Sfida mandata", app::dati().nameOf(n) + " deve accettare");
            openChallenge();
        }));
    }
    void onEvent(const app::Event& e) override {
        if (e.type == app::Ev::GAME) rebuild();
    }
    std::vector<Hint> hints() override { return {{"OK", "gioca"}, {"BACK", "indietro"}}; }

private:
    List list_;
    std::vector<Riga> rows_;
    int sel_ = 0;
};

Screen* makeGames() { return new Games(); }

// ================================================================ avvisi

void gameEvent(const app::Event& e) {
    const app::Sfida s = app::giochi().sfida();
    const int sopra = challengeOnTop();
    // La schermata della partita si aggiorna da sola (a schermo spento pero' si avvisa).
    if (sopra == int(s.id) && screenOn()) return;
    // In cima c'e' la schermata di una partita che non c'e' piu' (finita e sostituita da un'altra):
    // si chiude, se no giocherebbe quella nuova. Prima dell'invito: pop() chiude le finestre aperte.
    if (sopra >= 0 && sopra != int(s.id)) pop();
    const std::string nome = app::dati().nameOf(e.from);
    const app::Settings st = app::dati().settings();
    auto alert = [&]() {
        wake();
        // Niente suono ne' vibrazione sopra un suono da non coprire, che spesso non riparte (suoneria
        // di una chiamata, sirena di un SOS, un vocale; la sveglia riparte da sola), ne' mentre si
        // registra (lo squillo finirebbe nel vocale): resta l'avviso.
        if (callScreenOpen() || walkieRecording() || scheda::audio().recording() || scheda::audio().busy()) return;
        if (st.ringtone) scheda::audio().play(scheda::suoni::SQUILLO);
        if (st.vibration) scheda::vibrate(150);
    };
    switch (e.a) {
        case 1: {  // invito
            alert();
            // Niente finestra sopra il blocco col PIN, una chiamata, una sveglia che suona, il PTT
            // tenuto premuto o il proprio SOS (si prenderebbe i loro tasti), ne' sul T-Deck mentre si
            // scrive (W e S muoverebbero la scelta e l'Invio la confermerebbe): basta l'avviso,
            // l'invito resta nel menu Giochi.
            bool soloAvviso = isLockOpen() || callScreenOpen() || ringOpen() || walkieRecording() ||
                              app::rete().sosActive();
#if TDECK
            if (const Screen* t = top()) soloAvviso = soloAvviso || t->input() || t->typesDigits();
#endif
            if (soloAvviso) {
                toast(std::string("Sfida a ") + app::nomeGioco(s.gioco),
                      nome + (isLockOpen() ? " ti sfida: sblocca e apri Giochi" : " ti sfida: apri Giochi"), AMBRA());
                break;
            }
            askInvite();
            break;
        }
        case 2:  // accettato
            alert();
            toast(nome + " ha accettato", std::string(app::nomeGioco(s.gioco)) + ": tocca a te (Giochi)", VERDE());
            break;
        case 3:  // rifiutato
            toast("Sfida non accettata", s.esito, GRIGIO3());
            break;
        case 4:  // mossa dell'altro
            alert();
            toast(s.stato == app::Sfida::FINITA ? "Partita finita" : "Tocca a te",
                  s.stato == app::Sfida::FINITA ? s.esito : std::string(app::nomeGioco(s.gioco)) + " con " + nome, AMBRA());
            break;
        case 5:  // esito del mio colpo
            if (s.stato == app::Sfida::FINITA) toast("Partita finita", s.esito, s.vinto ? VERDE() : AMBRA());
            break;
        case 6:  // abbandonata
            toast("Partita finita", s.esito, GRIGIO3());
            break;
        case 7:  // consegna
            if (!s.nonArrivata) break;
            if (s.stato == app::Sfida::INVITATO) toast("Invito non arrivato", "Apri Giochi per rimandarlo", ROSSO());
            else toast("Mossa non arrivata", "Apri Giochi per rimandarla", ROSSO());
            break;
        default:
            break;
    }
}

}  // namespace ui
