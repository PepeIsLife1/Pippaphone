// Politica del GPS del T-Deck (telefono/main/app/gps_politica.h): avvio, ricerche periodiche e loro
// raddoppi al chiuso, richieste delle schermate, grazia. Un giro al secondo, come services() in main.cpp.
#include <cstring>

#include "app/gps_politica.h"
#include "testing.h"

namespace {

using app::PoliticaGps;
constexpr int64_t S = 1000, MIN = 60 * S;

// Il telefono finto: il tempo, l'ora, il cielo (posizione e C/N0) e le richieste, un giro al secondo.
struct Prova {
    PoliticaGps p;
    PoliticaGps::Ingressi in;
    bool acceso = false;
    int accensioni = 0;  // quante volte si e' acceso
    int64_t cambio = 0;  // ms dell'ultimo cambio (acceso <-> spento)
    explicit Prova(PoliticaGps::Ora ora) {
        in.ms = 2 * S;  // services() parte un paio di secondi dopo l'accensione
        in.ora = ora;
        giro();
    }
    void giro() {
        const bool a = p.decidi(in);
        if (a != acceso) {
            if (a) accensioni++;
            cambio = in.ms;
        }
        acceso = a;
    }
    // Avanti di ms, un giro al secondo.
    void avanti(int64_t ms) {
        for (int64_t fatto = 0; fatto < ms; fatto += S) {
            in.ms += S;
            giro();
        }
    }
    // Avanti finche' il GPS cambia (al massimo ms): quanto e' durato lo stato di prima (-1: non e' cambiato).
    int64_t finoAlCambio(int64_t ms) {
        const bool prima = acceso;
        const int64_t da = cambio;
        for (int64_t fatto = 0; fatto < ms && acceso == prima; fatto += S) {
            in.ms += S;
            giro();
        }
        return acceso != prima ? cambio - da : -1;
    }
    bool motivo(const char* m) const { return std::strcmp(p.motivo(), m) == 0; }
};

}  // namespace

// I tempi decisi (LEGGIMI, "GPS"): cambiandoli si cambia anche il LEGGIMI. Gli altri test li usano per nome.
TEST(politica_tempi_del_progetto) {
    CHECK_EQ(PoliticaGps::AVVIO_MAX, 20 * MIN);
    CHECK_EQ(PoliticaGps::AVVIO_DOPO_FIX, 3 * MIN);
    CHECK_EQ(PoliticaGps::OGNI_SENZA_ORA, 15 * MIN);
    CHECK_EQ(PoliticaGps::OGNI, 30 * MIN);
    CHECK_EQ(PoliticaGps::OGNI_MAX, 120 * MIN);
    CHECK_EQ(PoliticaGps::RICERCA_MAX, 3 * MIN);
    CHECK_EQ(PoliticaGps::RICERCA_DOPO_FIX, MIN);
    CHECK_EQ(PoliticaGps::CHIUSO_MS, 60 * S);
    CHECK_EQ(int(PoliticaGps::SNR_APERTO), 20);
    CHECK_EQ(PoliticaGps::FIX_RECENTE, 24 * 60 * MIN);
    CHECK_EQ(PoliticaGps::GRAZIA_SE_NIENTE_DA, 4 * 60 * MIN);
    CHECK_EQ(PoliticaGps::GRAZIA, 5 * MIN);
}

TEST(politica_avvio_posizione_piu_3_minuti) {
    Prova t(PoliticaGps::Ora::BUONA);
    CHECK(t.acceso);
    CHECK(t.motivo("avvio"));
    t.in.snrMax = 35;
    t.avanti(2 * MIN);
    CHECK(t.acceso);
    t.in.fix = true;  // prima posizione
    const int64_t fixMs = t.in.ms + S;
    t.finoAlCambio(10 * MIN);
    CHECK(!t.acceso);
    CHECK_EQ(t.cambio - fixMs, PoliticaGps::AVVIO_DOPO_FIX);
    CHECK(t.motivo("avvio finito: posizione"));
    // Ora buona e posizione recente: niente ricerche periodiche per 24 ore.
    t.in.fix = false;
    t.in.snrMax = 0;
    CHECK_EQ(t.p.intervallo(t.in.ms), int64_t(0));
    CHECK_EQ(t.p.prossimaRicercaTra(t.in.ms), int64_t(-1));
    t.avanti(23 * 60 * MIN);
    CHECK(!t.acceso);
    CHECK_EQ(t.accensioni, 1);
    // Dopo 24 ore senza posizione si ricomincia: ogni 30 minuti (piu' radi al chiuso).
    t.avanti(2 * 60 * MIN);
    CHECK(t.accensioni >= 2);
    CHECK(t.p.intervallo(t.in.ms) >= PoliticaGps::OGNI);
}

TEST(politica_avvio_20_minuti_con_ora_rete) {
    // Prima l'ora della rete spegneva il GPS subito: adesso l'avvio dura comunque fino alla posizione.
    Prova t(PoliticaGps::Ora::BUONA);
    t.avanti(19 * MIN);
    CHECK(t.acceso);
    t.finoAlCambio(2 * MIN);
    CHECK(!t.acceso);
    CHECK_EQ(t.cambio, PoliticaGps::AVVIO_MAX);
    CHECK(t.motivo("avvio finito: 20 minuti senza posizione"));
    // Mai una posizione: ricerca ogni 30 minuti.
    CHECK_EQ(t.p.intervallo(t.in.ms), PoliticaGps::OGNI);
    CHECK_EQ(t.p.prossimaRicercaTra(t.cambio), PoliticaGps::OGNI);
}

TEST(politica_al_chiuso_raddoppia_fino_2_ore) {
    Prova t(PoliticaGps::Ora::BUONA);  // mai una posizione: ogni 30 minuti
    t.in.snrMax = 12;                  // al chiuso: si sente poco
    t.avanti(PoliticaGps::AVVIO_MAX);
    CHECK(!t.acceso);
    const int64_t attese[] = {30 * MIN, 60 * MIN, 120 * MIN, 120 * MIN};
    for (int64_t attesa : attese) {
        CHECK_EQ(t.finoAlCambio(4 * 60 * MIN), attesa);  // spento per un intervallo
        CHECK(t.acceso);
        CHECK(t.motivo("ricerca periodica"));
        CHECK_EQ(t.finoAlCambio(10 * MIN), PoliticaGps::CHIUSO_MS);  // acceso 60 s
        CHECK(!t.acceso);
        CHECK(t.motivo("ricerca finita: al chiuso (nessun satellite a 20 dB-Hz)"));
    }
    // Una schermata del GPS riporta l'intervallo all'inizio.
    t.in.schermata = true;
    t.avanti(S);
    CHECK(t.acceso);
    CHECK(t.motivo("schermata"));
    t.in.schermata = false;
    t.avanti(S);  // senza una posizione da 4 ore: grazia
    CHECK(t.acceso);
    CHECK(t.motivo("grazia"));
    t.finoAlCambio(10 * MIN);
    CHECK(!t.acceso);
    CHECK_EQ(t.p.intervallo(t.in.ms), PoliticaGps::OGNI);
}

TEST(politica_ora_buona_azzera_intervallo) {
    Prova t(PoliticaGps::Ora::NESSUNA);  // senza ora: ogni 15 minuti
    t.in.snrMax = 5;
    t.avanti(PoliticaGps::AVVIO_MAX);
    CHECK_EQ(t.p.intervallo(t.in.ms), PoliticaGps::OGNI_SENZA_ORA);
    for (int i = 0; i < 2; i++) {  // due ricerche al chiuso: 15 -> 30 -> 60 minuti
        t.finoAlCambio(2 * 60 * MIN);
        t.finoAlCambio(5 * MIN);
    }
    CHECK(!t.acceso);
    CHECK_EQ(t.p.intervallo(t.in.ms), 4 * PoliticaGps::OGNI_SENZA_ORA);
    t.in.ora = PoliticaGps::Ora::BUONA;  // arriva l'ora della rete (mai una posizione: ogni 30 minuti)
    t.avanti(S);
    CHECK_EQ(t.p.intervallo(t.in.ms), PoliticaGps::OGNI);
}

TEST(politica_posizione_azzera_intervallo) {
    Prova t(PoliticaGps::Ora::NESSUNA);  // senza ora: ogni 15 minuti
    t.in.snrMax = 5;                     // al chiuso
    t.avanti(PoliticaGps::AVVIO_MAX);
    for (int i = 0; i < 2; i++) {  // due ricerche al chiuso: 15 -> 30 -> 60 minuti
        t.finoAlCambio(2 * 60 * MIN);
        t.finoAlCambio(5 * MIN);
    }
    CHECK_EQ(t.p.intervallo(t.in.ms), 4 * PoliticaGps::OGNI_SENZA_ORA);
    // La ricerca dopo, all'aperto, trova la posizione: l'intervallo torna a 15 minuti.
    CHECK_EQ(t.finoAlCambio(2 * 60 * MIN), 4 * PoliticaGps::OGNI_SENZA_ORA);
    CHECK(t.acceso);
    t.in.snrMax = 35;
    t.avanti(30 * S);
    t.in.fix = true;
    t.finoAlCambio(5 * MIN);
    CHECK(!t.acceso);
    CHECK(t.motivo("ricerca finita: posizione"));
    t.in.fix = false;
    CHECK_EQ(t.p.intervallo(t.in.ms), PoliticaGps::OGNI_SENZA_ORA);
    CHECK_EQ(t.p.prossimaRicercaTra(t.cambio), PoliticaGps::OGNI_SENZA_ORA);
}

TEST(politica_ricerca_3_minuti_o_1_dopo_fix) {
    Prova t(PoliticaGps::Ora::NESSUNA);
    t.in.snrMax = 20;  // all'aperto (un satellite arriva proprio a 20 dB-Hz), ma senza posizione
    t.avanti(PoliticaGps::AVVIO_MAX);
    CHECK_EQ(t.finoAlCambio(20 * MIN), PoliticaGps::OGNI_SENZA_ORA);
    CHECK_EQ(t.finoAlCambio(10 * MIN), PoliticaGps::RICERCA_MAX);
    CHECK(t.motivo("ricerca finita: 3 minuti senza posizione"));
    // Niente raddoppio: si sentivano i satelliti.
    CHECK_EQ(t.p.intervallo(t.in.ms), PoliticaGps::OGNI_SENZA_ORA);
    // La prossima trova la posizione dopo 40 s: resta acceso un minuto in piu'.
    t.finoAlCambio(20 * MIN);
    CHECK(t.acceso);
    t.avanti(40 * S);
    t.in.fix = true;
    t.in.ora = PoliticaGps::Ora::DA_CORREGGERE;  // per esempio l'ora ritrovata nell'orologio del chip
    const int64_t fixMs = t.in.ms + S;
    t.finoAlCambio(5 * MIN);
    CHECK(!t.acceso);
    CHECK_EQ(t.cambio - fixMs, PoliticaGps::RICERCA_DOPO_FIX);
    CHECK(t.motivo("ricerca finita: posizione"));
    t.in.fix = false;
    CHECK_EQ(t.p.intervallo(t.in.ms), PoliticaGps::OGNI);  // ora da correggere: ogni 30 minuti
}

// La posizione arriva nell'ultimo minuto della ricerca: si spegne comunque a 3 minuti, e il motivo (registro e
// @GPS!STORIA) dice che la posizione c'era (prima diceva "senza posizione").
TEST(politica_ricerca_fix_ultimo_minuto) {
    Prova t(PoliticaGps::Ora::NESSUNA);
    t.in.snrMax = 30;
    t.avanti(PoliticaGps::AVVIO_MAX);
    CHECK_EQ(t.finoAlCambio(20 * MIN), PoliticaGps::OGNI_SENZA_ORA);
    CHECK(t.acceso && t.p.inRicerca());
    t.avanti(2 * MIN + 50 * S);
    t.in.fix = true;
    CHECK_EQ(t.finoAlCambio(5 * MIN), PoliticaGps::RICERCA_MAX);
    CHECK(!t.acceso);
    CHECK(t.motivo("ricerca finita: 3 minuti, posizione trovata"));
    t.in.fix = false;
    CHECK_EQ(t.p.intervallo(t.in.ms), PoliticaGps::OGNI_SENZA_ORA);
}

TEST(politica_schermata_e_grazia) {
    Prova t(PoliticaGps::Ora::BUONA);
    t.in.snrMax = 40;
    t.avanti(MIN);
    t.in.fix = true;
    t.avanti(4 * MIN);
    t.in.fix = false;
    t.in.snrMax = 0;
    CHECK(!t.acceso);
    // Una schermata (la mappa) lo tiene acceso finche' e' aperta; con una posizione recente si spegne
    // appena si chiude.
    t.avanti(30 * MIN);
    t.in.schermata = true;
    t.avanti(S);
    CHECK(t.acceso);
    CHECK(t.motivo("schermata"));
    t.avanti(10 * MIN);
    CHECK(t.acceso);
    t.in.schermata = false;
    t.avanti(S);
    CHECK(!t.acceso);
    CHECK(t.motivo("richieste finite"));
    // Dopo 4 ore senza posizione: 5 minuti di grazia.
    t.avanti(4 * 60 * MIN);
    t.in.richiesta = true;  // wantGnss() (una richiesta di posizione)
    t.avanti(S);
    CHECK(t.acceso);
    CHECK(t.motivo("richiesta"));
    t.avanti(3 * MIN);
    t.in.richiesta = false;
    t.avanti(S);
    const int64_t fine = t.in.ms;
    CHECK(t.acceso);
    CHECK(t.motivo("grazia"));
    t.finoAlCambio(10 * MIN);
    CHECK(!t.acceso);
    CHECK_EQ(t.cambio - fine, PoliticaGps::GRAZIA);
    CHECK(t.motivo("grazia finita: 5 minuti senza posizione"));
    // La grazia finisce prima con la posizione.
    t.in.schermata = true;
    t.avanti(S);
    t.in.schermata = false;
    t.avanti(S);
    CHECK(t.acceso);
    t.avanti(MIN);
    t.in.fix = true;
    t.avanti(S);
    CHECK(!t.acceso);
    CHECK(t.motivo("grazia finita: posizione"));
}

TEST(politica_richieste_durante_ricerca) {
    Prova t(PoliticaGps::Ora::NESSUNA);
    t.in.snrMax = 8;
    t.avanti(PoliticaGps::AVVIO_MAX);
    t.finoAlCambio(20 * MIN);  // parte una ricerca
    CHECK(t.acceso);
    CHECK(t.p.inRicerca());
    // Un SOS durante la ricerca: resta acceso anche quando la ricerca si ferma al chiuso.
    t.in.richiesta = true;
    t.avanti(2 * MIN);
    CHECK(t.acceso);
    CHECK(!t.p.inRicerca());
    CHECK(t.motivo("richiesta"));
    t.in.richiesta = false;
    t.avanti(S);
    CHECK(t.acceso);  // grazia: nessuna posizione da 4 ore
    CHECK(t.p.inGrazia());
    t.finoAlCambio(10 * MIN);
    CHECK(!t.acceso);
    // La ricerca si era fermata al chiuso: l'intervallo e' raddoppiato.
    CHECK_EQ(t.p.intervallo(t.in.ms), 2 * PoliticaGps::OGNI_SENZA_ORA);
    CHECK_EQ(t.p.prossimaRicercaTra(t.cambio), 2 * PoliticaGps::OGNI_SENZA_ORA);
}
