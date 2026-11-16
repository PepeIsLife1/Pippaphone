// Giochi: record dei giochi da soli (serpente, campo minato) e sfide via radio con un contatto
// (tris, battaglia navale), una alla volta.
//
// Le sfide viaggiano nel pacchetto PippaNet GAME (cifrato, con conferma come un SMS). Contenuto
// (vedi PROTOCOLLO.md): gioco, tipo, id della partita (u16), poi i dati del tipo:
//   INVITO     nessun dato: chi invita comincia (nel tris e' la X)
//   ACCETTO    nessun dato (nella battaglia navale: ho messo le navi, si comincia)
//   RIFIUTO    motivo: 0 = no, 1 = occupato in un'altra partita
//   MOSSA      numero della mossa (1, 2...), casella (tris 0-8, battaglia 0-99)
//   ESITO      (battaglia) numero del colpo, casella, risultato (1 acqua, 2 colpito, 3 affondato,
//              4 affondato e flotta finita), caselle della nave affondata (quante + caselle)
//   ABBANDONO  nessun dato (anche risposta automatica: vedi ricevi())
// I messaggi arrivano dal compito della radio: le risposte automatiche (esito di un colpo, "sono
// occupato", "questa partita qui non c'e' piu'") partono dopo, da servizio(), chiamato dal compito
// dell'interfaccia.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "pippanet/phone.h"

namespace app {

enum class Gioco : uint8_t { TRIS = 1, NAVALE = 2 };
const char* nomeGioco(Gioco g);  // "Tris", "Battaglia navale"

struct Sfida {
    enum Stato : uint8_t {
        NESSUNA,
        PREPARA,          // battaglia: sto mettendo le navi (prima di invitare o di accettare)
        INVITATO,         // ho mandato l'invito: aspetto che accetti
        INVITO_ARRIVATO,  // mi ha sfidato: aspetta la mia risposta
        IN_CORSO,
        FINITA,
    };
    Stato stato = NESSUNA;
    Gioco gioco = Gioco::TRIS;
    uint16_t avversario = 0;
    uint16_t id = 0;
    bool invitante = false;  // l'ho sfidato io (nel tris sono la X e comincio)
    bool tocca = false;      // tocca a me
    bool inViaggio = false;  // la mia ultima mossa non e' ancora arrivata (aspetto la conferma)
    bool nonArrivata = false;  // la mia ultima mossa non e' arrivata: si puo' rimandare
    uint8_t mosse = 0;       // mosse (o colpi) fatte finora da tutti e due
    std::string esito;       // a partita finita: "Hai vinto!", "Ha vinto Marco", "Pareggio"...
    bool vinto = false;
    // tris: 0 vuota, 1 io, 2 l'altro
    uint8_t tris[9] = {};
    // battaglia navale 10 x 10: mare[i] = 0 acqua, 1..10 = nave; suoi = i suoi colpi sul mio mare
    // (0 niente, 1 acqua, 2 colpito); miei = i miei sul suo (0 niente, 1 acqua, 2 colpito, 3 affondato).
    uint8_t mare[100] = {};
    uint8_t suoi[100] = {};
    uint8_t miei[100] = {};
    int8_t ultimoMio = -1, ultimoSuo = -1;  // caselle dell'ultimo colpo (per evidenziarle)
};

class Giochi {
public:
    // ---- record (salvati) ----
    void load();
    uint32_t recordSerpente();
    bool nuovoRecordSerpente(uint32_t punti);  // true se e' un record (e lo salva)
    uint32_t recordMine(int livello);          // secondi, 0 = nessuno
    bool nuovoRecordMine(int livello, uint32_t secondi);

    // ---- sfide via radio ----
    Sfida sfida();  // copia dello stato
    // Comincia una sfida con un numero. Battaglia navale: prima PREPARA (si mettono le navi), poi
    // pronto() manda l'invito. false se c'e' gia' una partita in corso.
    bool sfidaNuova(Gioco g, uint16_t avversario);
    void accetta();   // invito arrivato: tris = si comincia, battaglia = PREPARA
    void rifiuta();
    // Battaglia: navi messe a caso (anche di nuovo) e "pronto" (manda l'invito o l'accetto).
    void rimescola();
    void pronto();
    // Tris: casella 0-8. Battaglia: casella 0-99 da colpire. false se non si puo'.
    bool gioca(int casella);
    // L'ultima mossa (o l'invito) non era arrivata. Nel tris anche a partita finita: la mossa che
    // l'ha chiusa deve arrivare, l'altro la aspetta ancora.
    void rimanda();
    void abbandona();
    void chiudi();    // partita finita vista: si torna a NESSUNA
    bool attiva();    // c'e' una partita (non finita)

    // Dalla rete (compito della radio). msgId: id radio del messaggio, che per chi lo manda cresce
    // sempre (anche dopo un riavvio): dice quale di due suoi inviti e' il piu' recente. 0 = non noto.
    void ricevi(uint16_t from, const uint8_t* data, size_t len, uint32_t msgId = 0);
    void consegna(uint16_t to, uint32_t msgId, pn::Delivery d);
    // Certificato nuovo di un numero (ripristino di fabbrica, telefono nuovo): i suoi id radio
    // ripartono da capo, quindi i suoi messaggi di prima non dicono piu' quale invito e' vecchio.
    void nuovaIdentita(uint16_t numero);
    // Dal compito dell'interfaccia: manda le risposte in attesa.
    void servizio();

    // Navi della flotta (lunghezze): 1 da 4, 2 da 3, 3 da 2, 4 da 1, che non si toccano.
    static constexpr uint8_t FLOTTA[10] = {4, 3, 3, 2, 2, 2, 1, 1, 1, 1};
    static bool flottaACaso(uint8_t mare[100], uint32_t seme);

private:
    enum Tipo : uint8_t { INVITO = 1, ACCETTO = 2, RIFIUTO = 3, MOSSA = 4, ESITO = 5, ABBANDONO = 6 };
    void manda(uint16_t to, const std::vector<uint8_t>& m, bool tieni);
    std::vector<uint8_t> msg(Tipo t, std::initializer_list<uint8_t> dati = {}) const;
    // risultato: vinta, persa o pari (false = finita per abbandono o rifiuto).
    void fine(bool vinto, const std::string& testo, bool risultato);
    void ricordaChiusa();  // la partita finita in s_ si toglie: la si ricorda in chiusa_
    void rispondi(uint16_t to, const std::vector<uint8_t>& m);  // ABBANDONO o RIFIUTO, seguito
    bool finaleDi(uint16_t to, Gioco g, uint16_t id) const;     // finale_ e' di quella partita
    // Il mio invito non serve piu' (abbandonato, sostituito, o gia' arrivato): esce dai tentativi.
    void ritiraInviti();
    void trisControlla();
    void saveRecords();
    void evento(int32_t a);

    std::recursive_mutex mu_;
    Sfida s_;
    std::vector<uint8_t> ultimo_;   // ultima mossa mia (per rimandarla)
    uint32_t ultimoId_ = 0;         // id del messaggio dell'ultima mossa mia
    struct Uscita {
        uint16_t to;
        std::vector<uint8_t> m;
        bool tieni;
    };
    std::vector<Uscita> uscita_;    // da mandare dal compito dell'interfaccia
    // Miei inviti della partita in s_ (id radio) ancora nei tentativi, e quelli da togliere: da
    // servizio(), fuori dal blocco dei giochi, come sendGame.
    std::vector<uint32_t> mieiInviti_;
    std::vector<std::pair<uint16_t, uint32_t>> annulla_;
    // Battaglia: l'ultimo esito che ho mandato (a ultimoEsitoA_, per il suo colpo ultimoEsitoN_).
    // Resta anche dopo una sfida nuova, finche' non ne mando un altro: chi ha sparato puo' ancora
    // aspettarlo.
    uint8_t ultimoEsitoN_ = 0;
    uint16_t ultimoEsitoA_ = 0;
    std::vector<uint8_t> ultimoEsito_;
    // Consegna dell'ultimo esito mandato (esitoMsg_): se non arriva, chi ha sparato aspetterebbe per
    // sempre (la sua mossa e' confermata e non la rimanda). Si rimanda da soli, una volta.
    uint32_t esitoId_ = 0;
    uint16_t esitoTo_ = 0;
    std::vector<uint8_t> esitoMsg_;
    bool esitoRimandato_ = false;
    bool risultato_ = false;  // s_ e' finita con un risultato (non per abbandono o rifiuto)
    // Id radio piu' alto di un suo invito, accetto o mossa per la partita in s_: un suo invito con un
    // id piu' basso e' vecchio (rimasto nei tentativi della radio).
    uint32_t daLui_ = 0;
    // L'ultima partita chiusa (chiudi(), o finita e sostituita da un'altra): per riconoscerne le copie.
    struct Chiusa {
        uint16_t avversario = 0, id = 0;
        Gioco gioco = Gioco::TRIS;
        uint8_t mosse = 0;
        bool risultato = false;
    } chiusa_;
    // Tris: la mia mossa che ha chiuso la partita, seguita anche dopo la chiusura: se non arriva si
    // rimanda da sola, una volta (l'altro, se no, la aspetterebbe per sempre).
    std::vector<uint8_t> finale_;
    uint16_t finaleTo_ = 0;
    uint32_t finaleId_ = 0;
    bool finaleRimandato_ = false;
    // L'ultimo ABBANDONO o RIFIUTO mandato (anche automatico): dopo, l'altro non manda piu' niente e,
    // se si perde, aspetterebbe per sempre. Si rimanda una volta.
    std::vector<uint8_t> rispostaAuto_;
    uint16_t rispostaAutoTo_ = 0;
    uint32_t rispostaAutoId_ = 0;
    bool rispostaAutoRimandata_ = false;
    uint32_t recSerpente_ = 0;
    uint32_t recMine_[3] = {};
    uint32_t seme_ = 1;
};

Giochi& giochi();

}  // namespace app
