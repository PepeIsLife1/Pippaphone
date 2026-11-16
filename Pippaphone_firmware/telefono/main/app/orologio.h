// Orologio: sveglie (salvate nella memoria dei dati), timer e cronometro (solo in RAM).
//
// Le sveglie usano l'ora del telefono (rete().unixTime(), fuso italiano); timer e cronometro il
// tempo dall'accensione, che non salta quando arriva l'ora dal GPS. A far suonare ci pensa
// l'interfaccia (ui::clockService), che chiede a questo modulo cosa e' scaduto.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace app {

struct Sveglia {
    uint8_t ora = 7, minuti = 0;
    uint8_t giorni = 0;  // bit 0 = lunedi' ... bit 6 = domenica; 0 = una volta sola
    bool attiva = true;
    std::string nome;    // "" = "Sveglia"
};

class Orologio {
public:
    static constexpr size_t MAX_SVEGLIE = 10;
    static constexpr uint32_t POSTICIPA_S = 5 * 60;

    void load();

    // ---- sveglie ----
    std::vector<Sveglia> sveglie();
    // E le salva. La sveglia posticipata resta posticipata solo se c'e' ancora uguale (anche spostata).
    void setSveglie(const std::vector<Sveglia>& s);
    // Prossimo suono di una sveglia dopo 'after' (secondi Unix, ora locale): 0 se non suona mai.
    static uint32_t prossima(const Sveglia& s, uint32_t after);
    // Prossima sveglia attiva (o posticipata) dopo 'now'; false se nessuna.
    bool prossimaDi(uint32_t now, uint32_t& quando, int& indice);
    // Da chiamare ogni secondo circa con l'ora del telefono (0 = non nota). Restituisce l'indice della
    // sveglia che deve suonare adesso (-1 = nessuna); piu' sveglie scadute insieme (o una sveglia e il
    // posticipo) suonano con una schermata sola. Una sveglia "una volta sola" si spegne da sola.
    // Se l'orologio salta in avanti (ora dal GPS) le sveglie saltate di piu' di 2 minuti non suonano
    // (e quelle "una volta sola" si spengono lo stesso), tranne quella del risveglio (risveglioPer).
    int scaduta(uint32_t now);
    // Sveglia che sta suonando: posticipa di 5 minuti. Il posticipo resta anche nel sonno profondo
    // del T-Deck spento, che si risveglia per suonare.
    void posticipa(int indice, uint32_t now);
    void annullaPosticipo();
    bool posticipata(uint32_t& quando, int& indice) const;
    // Prima di "Spegni": l'ora della sveglia (o del posticipo) per cui il T-Deck si risveglia, 0 =
    // nessuna. Al risveglio quella suona anche in ritardo, fino a 30 minuti (l'orologio del chip si
    // sposta nel sonno e l'ora del GPS puo' saltare avanti).
    void risveglioPer(uint32_t quando);

    // ---- timer (conto alla rovescia) ----
    enum class Timer : uint8_t { FERMO, VA, PAUSA };
    void timerAvvia(uint32_t durataMs);
    void timerPausa();
    void timerRiprendi();
    void timerAzzera();
    Timer timerStato() const { return timer_; }
    uint32_t timerDurataMs() const { return timerDurata_; }
    uint32_t timerRestaMs() const;
    // true una volta sola, quando il timer arriva a zero (poi torna FERMO).
    bool timerScaduto();

    // ---- cronometro ----
    void cronoAvvia();
    void cronoFerma();
    void cronoAzzera();
    void cronoGiro();
    bool cronoVa() const { return cronoVa_; }
    uint32_t cronoMs() const;
    std::vector<uint32_t> giri() const { return giri_; }  // tempo totale a ogni giro (gli ultimi)
    size_t giriTotali() const { return giriTotali_; }      // giri dall'azzeramento (anche quelli tolti)
    static constexpr size_t MAX_GIRI = 30;

private:
    void save();
    static int64_t nowMs();

    mutable std::recursive_mutex mu_;
    std::vector<Sveglia> sveglie_;
    uint32_t lastCheck_ = 0;
    int snoozeIndex_ = -1;
    uint32_t snoozeAt_ = 0;

    Timer timer_ = Timer::FERMO;
    uint32_t timerDurata_ = 0;
    int64_t timerFine_ = 0;      // ms dall'accensione (quando va)
    uint32_t timerResta_ = 0;    // ms (in pausa)

    bool cronoVa_ = false;
    int64_t cronoInizio_ = 0;
    uint32_t cronoAccumulato_ = 0;
    std::vector<uint32_t> giri_;
    size_t giriTotali_ = 0;
};

Orologio& orologio();

// Testo dei giorni di una sveglia: "una volta", "tutti i giorni", "lun-ven", "sab e dom", "lun mer ven".
std::string giorniText(uint8_t giorni);

}  // namespace app
