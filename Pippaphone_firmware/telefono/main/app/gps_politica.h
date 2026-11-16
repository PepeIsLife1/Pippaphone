// Politica del GPS del T-Deck Plus: quando lo si vuole acceso. La chiama services() (main.cpp) una volta
// al secondo; il ricevitore lo accende e lo mette in standby il pilota (scheda::Gnss::power). Solo logica,
// senza ESP-IDF, per provarla sul PC (test/test_gps_politica.cpp). La PCB ha ancora la sua regola in
// main.cpp.
//
// - Avvio, con qualunque ora: acceso dall'accensione fino alla prima posizione piu' 3 minuti, al massimo
//   20 minuti (prima si spegneva appena arrivava un'ora, dalla rete o dal GPS, spesso prima della posizione).
// - Ricerca periodica, finito l'avvio: ogni 15 minuti senza ora; ogni 30 con l'ora messa a mano o ritrovata
//   nell'orologio del chip, oppure senza una posizione nelle ultime 24 ore. Ogni ricerca dura al massimo 3
//   minuti (o fino a 1 minuto dopo la posizione); se in 60 s nessun satellite arriva a 20 dB-Hz (al chiuso)
//   si ferma e l'intervallo raddoppia, fino a 2 ore. Una posizione, un'ora buona nuova (GPS o rete) o una
//   schermata del GPS riportano l'intervallo all'inizio. Si conta da quando il GPS si e' spento.
// - Richieste: le schermate che usano il GPS (posizione, mappa, SOS..., anche sotto altre o sotto il PIN),
//   wantGnss() (richieste di posizione, SOS, comandi USB) e l'SOS attivo.
// - Grazia: finite le richieste, se nelle ultime 4 ore non c'e' stata una posizione, ancora 5 minuti o fino
//   alla prima posizione.
#pragma once

#include <cstdint>

namespace app {

class PoliticaGps {
public:
    // L'ora del telefono (app::Rete::TimeSource): nessuna; buona (dal GPS o dalla rete); da correggere
    // (messa a mano, o ritrovata nell'orologio del chip).
    enum class Ora : uint8_t { NESSUNA, BUONA, DA_CORREGGERE };
    struct Ingressi {
        int64_t ms = 0;          // ms dall'avvio
        Ora ora = Ora::NESSUNA;
        bool fix = false;        // c'e' una posizione di adesso (scheda::Fix::valid)
        uint8_t snrMax = 0;      // C/N0 migliore adesso, in dB-Hz (scheda::Fix::snrMax)
        bool schermata = false;  // e' aperta una schermata che usa il GPS
        bool richiesta = false;  // wantGnss() o SOS attivo
    };

    static constexpr int64_t MIN = 60 * 1000;
    static constexpr int64_t AVVIO_MAX = 20 * MIN;               // finestra dell'avvio, al massimo
    static constexpr int64_t AVVIO_DOPO_FIX = 3 * MIN;           // ... o fino a 3 minuti dopo la prima posizione
    static constexpr int64_t OGNI_SENZA_ORA = 15 * MIN;          // ricerca periodica senza ora
    static constexpr int64_t OGNI = 30 * MIN;                    // ora da correggere, o niente posizione da 24 ore
    static constexpr int64_t OGNI_MAX = 120 * MIN;               // coi raddoppi dopo le ricerche al chiuso
    static constexpr int64_t RICERCA_MAX = 3 * MIN;
    static constexpr int64_t RICERCA_DOPO_FIX = MIN;
    static constexpr int64_t CHIUSO_MS = 60 * 1000;              // nessun satellite a SNR_APERTO entro: al chiuso
    static constexpr uint8_t SNR_APERTO = 20;                    // dB-Hz
    static constexpr int64_t FIX_RECENTE = 24 * 60 * MIN;        // senza posizione da di piu': ricerche periodiche
    static constexpr int64_t GRAZIA_SE_NIENTE_DA = 4 * 60 * MIN;  // senza posizione da di piu': grazia
    static constexpr int64_t GRAZIA = 5 * MIN;

    // Un giro: true = lo si vuole acceso.
    bool decidi(const Ingressi& in);
    // Perche' e' acceso ("avvio", "schermata"...), o perche' si e' appena spento ("ricerca finita: al chiuso"...).
    // Testo fisso: si puo' passare a scheda::Gnss::power(), che lo tiene nella storia.
    const char* motivo() const { return motivo_; }
    // Fra quanti ms parte la prossima ricerca periodica; -1 se il GPS e' acceso o non ce n'e' in programma.
    int64_t prossimaRicercaTra(int64_t ms) const {
        const int64_t ogni = intervallo(ms);
        if (acceso_ || ogni <= 0) return -1;
        const int64_t tra = spentoDa_ + ogni - ms;
        return tra > 0 ? tra : 0;
    }
    // Ogni quanto si cerca adesso (0: niente ricerche periodiche).
    int64_t intervallo(int64_t ms) const {
        int64_t base = 0;
        if (ora_ == Ora::NESSUNA) base = OGNI_SENZA_ORA;
        else if (ora_ == Ora::DA_CORREGGERE || !recente(ms, FIX_RECENTE)) base = OGNI;
        if (base == 0) return 0;
        const int64_t v = base << raddoppi_;
        return v < OGNI_MAX ? v : OGNI_MAX;
    }
    bool inAvvio() const { return !avvioFinito_; }
    bool inRicerca() const { return ricercaDa_ >= 0; }
    bool inGrazia() const { return graziaFino_ >= 0; }

private:
    bool recente(int64_t ms, int64_t entro) const { return ultimoFix_ >= 0 && ms - ultimoFix_ < entro; }

    const char* motivo_ = "";
    bool acceso_ = false;
    int64_t spentoDa_ = 0;         // quando si e' spento l'ultima volta
    bool avvioFinito_ = false;
    int64_t primoFix_ = -1, ultimoFix_ = -1;
    Ora ora_ = Ora::NESSUNA;
    uint8_t raddoppi_ = 0;         // ricerche al chiuso di fila (l'intervallo raddoppia, al massimo 3 volte)
    bool richiestaPrima_ = false;
    int64_t graziaFino_ = -1;
    int64_t ricercaDa_ = -1, ricercaFix_ = -1;
    uint8_t ricercaSnr_ = 0;       // C/N0 migliore dall'inizio della ricerca
};

inline bool PoliticaGps::decidi(const Ingressi& in) {
    const int64_t t = in.ms;
    // Riportano l'intervallo all'inizio: una posizione, un'ora buona nuova, una schermata del GPS.
    if (in.fix) {
        if (primoFix_ < 0) primoFix_ = t;
        ultimoFix_ = t;
        raddoppi_ = 0;
    }
    if (in.ora == Ora::BUONA && ora_ != Ora::BUONA) raddoppi_ = 0;
    ora_ = in.ora;
    if (in.schermata) raddoppi_ = 0;

    const char* fine = nullptr;  // perche' e' finito, in questo giro, un motivo per tenerlo acceso
    // Avvio: fino alla prima posizione piu' 3 minuti, al massimo 20 minuti.
    if (!avvioFinito_) {
        if (primoFix_ >= 0 && t - primoFix_ >= AVVIO_DOPO_FIX) {
            avvioFinito_ = true;
            fine = "avvio finito: posizione";
        } else if (t >= AVVIO_MAX) {
            avvioFinito_ = true;
            fine = primoFix_ >= 0 ? "avvio finito: 20 minuti" : "avvio finito: 20 minuti senza posizione";
        }
    }
    // Richieste (schermate, wantGnss, SOS); quando finiscono, la grazia se manca una posizione recente.
    const bool richiesta = in.schermata || in.richiesta;
    if (richiesta) {
        graziaFino_ = -1;
    } else if (richiestaPrima_) {
        if (recente(t, GRAZIA_SE_NIENTE_DA)) fine = "richieste finite";
        else graziaFino_ = t + GRAZIA;
    }
    richiestaPrima_ = richiesta;
    if (graziaFino_ >= 0 && (in.fix || t >= graziaFino_)) {
        fine = in.fix ? "grazia finita: posizione" : "grazia finita: 5 minuti senza posizione";
        graziaFino_ = -1;
    }
    // Ricerca periodica in corso.
    if (ricercaDa_ >= 0) {
        if (in.fix && ricercaFix_ < 0) ricercaFix_ = t;
        if (in.snrMax > ricercaSnr_) ricercaSnr_ = in.snrMax;
        if (ricercaFix_ >= 0 && t - ricercaFix_ >= RICERCA_DOPO_FIX) {
            fine = "ricerca finita: posizione";
            ricercaDa_ = -1;
        } else if (t - ricercaDa_ >= RICERCA_MAX) {
            // Anche con una posizione arrivata nell'ultimo minuto: i 3 minuti sono il massimo.
            fine = ricercaFix_ >= 0 ? "ricerca finita: 3 minuti, posizione trovata"
                                    : "ricerca finita: 3 minuti senza posizione";
            ricercaDa_ = -1;
        } else if (ricercaFix_ < 0 && t - ricercaDa_ >= CHIUSO_MS && ricercaSnr_ < SNR_APERTO) {
            fine = "ricerca finita: al chiuso (nessun satellite a 20 dB-Hz)";
            ricercaDa_ = -1;
            if (raddoppi_ < 3) raddoppi_++;
        }
    }
    // Ricerca periodica che parte: niente altro lo tiene acceso e da spento e' passato un intervallo.
    const bool altro = !avvioFinito_ || richiesta || graziaFino_ >= 0;
    if (ricercaDa_ < 0 && !altro && !acceso_) {
        const int64_t ogni = intervallo(t);
        if (ogni > 0 && t - spentoDa_ >= ogni) {
            ricercaDa_ = t;
            ricercaFix_ = -1;
            ricercaSnr_ = in.snrMax;
        }
    }
    const bool voluto = altro || ricercaDa_ >= 0;
    if (voluto) {
        motivo_ = !avvioFinito_       ? "avvio"
                  : in.schermata      ? "schermata"
                  : in.richiesta      ? "richiesta"
                  : graziaFino_ >= 0  ? "grazia"
                                      : "ricerca periodica";
    } else if (acceso_) {
        motivo_ = fine ? fine : "spento";
        spentoDa_ = t;
    }
    acceso_ = voluto;
    return voluto;
}

}  // namespace app
