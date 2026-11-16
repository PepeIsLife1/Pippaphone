// Ricevitore GNSS AT6558R: legge le frasi NMEA (9600 baud) e tiene l'ultima posizione e l'ora.
// Sul T-Deck Plus il ricevitore e' un u-blox M10 (o un Quectel L76K) e lo guida il pilota di
// src/tdeck/gnss_pilota.cpp (standby, sveglia, diagnosi): le parti solo T-Deck sono nei blocchi #if.
#pragma once

#include <cstdint>

#include "sdkconfig.h"
#if CONFIG_SCHEDA_TDECK_PLUS
#include <cstddef>
#endif

namespace scheda {

struct Fix {
    bool valid = false;       // posizione valida
    bool timeValid = false;   // ora e data valide (arrivano anche senza posizione)
    double lat = 0, lon = 0;  // gradi
    float altM = 0;
    float hdop = 99;
    uint8_t sats = 0;         // usati per la posizione (GGA)
    uint8_t inVista = 0;      // in vista, di tutte le costellazioni (GSV)
    uint32_t unixTime = 0;    // secondi UTC dell'ultima frase con ora valida
    uint64_t atMs = 0;        // quando e' arrivata la posizione (ms dall'avvio)
    uint64_t timeAtMs = 0;    // quando e' arrivata l'ora
#if CONFIG_SCHEDA_TDECK_PLUS
    // T-Deck: i dati scadono. valid vale solo con una RMC 'A' (con le coordinate) degli ultimi 5 s;
    // sats (usati, dalla GGA) e' 0 finche' non c'e' la posizione e dopo 5 s senza frasi; inVista,
    // conSegnale e snrMax vengono dalle serie GSV complete degli ultimi 5 s. In standby (o con il GPS
    // muto da 5 s) valid, sats, inVista, conSegnale e snrMax si azzerano; lat, lon e atMs restano
    // quelli dell'ultima posizione buona.
    uint8_t conSegnale = 0;   // satelliti con C/N0 > 0 (quelli che il ricevitore sente davvero)
    uint8_t snrMax = 0;       // C/N0 migliore tra quelli, in dB-Hz (0 = nessun segnale)
    // Diagnosi (ms dall'avvio, 0 = mai): ultima frase valida di qualunque tipo (anche GSA, TXT...),
    // ultima RMC e ultima GGA (anche senza posizione), ultima GSV.
    uint64_t frasiAtMs = 0;
    uint64_t rmcAtMs = 0;
    uint64_t ggaAtMs = 0;
    uint64_t gsvAtMs = 0;
    uint32_t frasiOk = 0;     // frasi col checksum giusto
    uint32_t frasiKo = 0;     // frasi scartate (checksum sbagliato, troppo lunghe, interrotte)
#endif
};

class Gnss {
public:
    bool begin();
    void power(bool on);
    bool powered() const { return on_; }
    Fix fix() const;
    // Precisione stimata in metri (da HDOP).
    float accuracyM() const;
    // Diagnosi (comandi USB del T-Deck): il ricevitore riconosciuto ("u-blox", "L76K", "muto"),
    // i satelliti in vista (dalle frasi GSV), la ripartenza da freddo (come dopo l'interruttore:
    // dimentica satelliti, posizione e ora) e le frasi NMEA ripetute nel registro per qualche secondo.
    const char* modulo() const;
    uint8_t satInVista() const;
    void freddo();
    void eco(uint32_t secondi);

#if CONFIG_SCHEDA_TDECK_PLUS
    // ---- T-Deck Plus: il pilota del ricevitore ----
    // Sulla seriale del GPS scrive solo il compito del GPS: power(), freddo(), chiediVersione() e prova()
    // lasciano una richiesta che il compito esegue entro un decimo di secondo circa. powered() resta "lo
    // si vuole acceso" (l'ultimo power()); se il ricevitore parla o dorme davvero lo dicono le funzioni
    // qui sotto. freddo() aspetta che il ricevitore sia sveglio (al massimo 60 s, poi la richiesta cade).

    // Come power(on), con il motivo per la storia: un testo fisso (letterale, per esempio "mappa" o "sos"),
    // mai un buffer che poi cambia. Un power() con lo stesso valore di prima non fa niente.
    void power(bool on, const char* perche);

    enum class Stato : uint8_t {
        RILEVA,      // cerca il ricevitore (u-blox a 38400 baud, poi L76K a 9600): all'avvio e se tace
        MUTO,        // nessun ricevitore: si riprova ogni 30 s finche' lo si vuole acceso
        ACCESO,      // sveglio. Anche spento "per finta": L76K, o u-blox che ignora il PMREQ
        SVEGLIA,     // lo sveglia: 0xFF e fino a 2 s per una frase valida, fino a 5 volte, poi RILEVA
        ADDORMENTA,  // standby: PMREQ (prima 0xFF e 500 ms se tace da 1,5 s) e frasi che si fermano entro 3 s
        STANDBY,     // dorme: le frasi si sono fermate dopo il PMREQ
        PROVA,       // prova al banco in corso (prova())
    };
    Stato stato() const;
    static const char* nomeStato(Stato s);  // "rileva", "muto", "acceso", "sveglia", "addormenta", ...

    // true se negli ultimi 3 s e' arrivata una frase NMEA valida, di qualunque tipo.
    bool risponde() const;
    // true se il ricevitore dorme davvero (standby confermato: dopo il PMREQ le frasi si sono fermate).
    bool inStandby() const;
    // Silenzio mentre lo si vuole acceso, in ms: dall'ultima frase valida o dall'ultimo power(true), il
    // piu' recente dei due (una sveglia appena chiesta non conta come silenzio). 0 se non lo si vuole
    // acceso. Oltre 5000 il ricevitore non risponde (il pilota intanto lo risveglia o lo cerca).
    uint32_t silenzioMs() const;

    // Diagnosi (@GPS?).
    struct Diagnosi {
        Stato stato = Stato::RILEVA;
        bool standby = false;          // come inStandby()
        int8_t pmreq = 0;              // standby col PMREQ normale (flag 0x02): 1 rispettato, -1 ignorato, 0 mai
        int32_t svegliaMs = -1;        // ultima sveglia riuscita: ms dal primo 0xFF alla prima frase (-1 nessuna)
        uint8_t svegliaTentativi = 0;  // 0xFF mandati per quella sveglia
        uint8_t snrMaxAcceso = 0;      // C/N0 migliore (dB-Hz) dalla sveglia (accesoAtMs; un u-blox che ignora
                                       // il PMREQ non si sveglia mai: per una finestra tua usa fix().snrMax)
        uint64_t accesoAtMs = 0;       // prima frase dopo l'ultima sveglia o accensione (ms dall'avvio, 0 mai)
        uint32_t freddi = 0;           // ripartenze da freddo fatte
        uint32_t stackLibero = 0;      // byte di stack del compito del GPS mai usati (dall'avvio)
    };
    Diagnosi diagnosi() const;

    // Storia: gli ultimi STORIA eventi del ricevitore (ms dall'avvio), per @GPS!STORIA.
    enum class Ev : uint8_t {
        VOLUTO,          // a = 1 voluto acceso, 0 spento; perche
        RILEVATO,        // a = 1 u-blox, 2 L76K; b = baud; c = ms che ci sono voluti
        MUTO,            // nessun ricevitore a 38400 e 9600 baud; c = ms
        SILENZIO,        // voluto acceso ma muto: a = ms di silenzio
        SVEGLIO,         // a = ms dal primo 0xFF alla prima frase, b = 0xFF mandati
        SVEGLIA_FALLITA, // a = 0xFF mandati
        PMREQ_OK,        // a = ms dal PMREQ all'ultima frase, b = flag, c = 1 se parlava (0: taceva gia')
        PMREQ_IGNORATO,  // a = flag
        PMREQ_RIPROVA,   // il PMREQ e' arrivato mentre si stava ancora svegliando: si rimanda
        DA_SOLO,         // in standby sono tornate le frasi
        RINUNCIA,        // si risveglia da solo: resta acceso fino al prossimo power(true)
        PRIMA_ORA,       // a = ms dalla sveglia
        PRIMO_FIX,       // a = ms dalla sveglia, b = usati, c = con segnale
        RICERCA,         // ogni minuto senza posizione: a = con segnale, b = C/N0 migliore, c = in vista
        FREDDO,          // ripartenza da freddo fatta
        FREDDO_SCADUTO,  // ripartenza da freddo non fatta: il ricevitore non si e' svegliato entro 60 s
        PROVA_INIZIO,    // a = cicli, b = flag
        PROVA_CICLO,     // a = esito, b = ms frasi ferme, c = ms sveglia
        PROVA_FINE,      // a = cicli fatti, b = standby, c = ignorati; perche = errore
    };
    struct Evento {
        uint32_t ms = 0;               // ms dall'avvio
        Ev tipo = Ev::VOLUTO;
        int32_t a = 0, b = 0, c = 0;
        const char* perche = nullptr;  // testo fisso (motivo di power(), errore della prova)
    };
    static constexpr int STORIA = 64;
    // Copia gli eventi (dal piu' vecchio) in out; restituisce quanti.
    int storia(Evento* out, int max) const;
    // L'evento in una riga di testo (senza l'ora).
    static void testoEvento(const Evento& e, char* out, size_t n);

    // Versione del ricevitore (@GPS!VER): chiediVersione() manda UBX-MON-VER e UBX-MON-HW3 appena
    // l'u-blox e' sveglio (la richiesta vale 30 s); le risposte restano in versione().
    struct Versione {
        uint32_t risposteVer = 0;      // quante MON-VER sono arrivate dall'avvio
        uint32_t risposteHw3 = 0;      // quante MON-HW3
        char sw[31] = {};              // swVersion, per esempio "ROM SPG 5.10 (7b202e)"
        char hw[11] = {};              // hwVersion, per esempio "000A0000"
        uint8_t nEstensioni = 0;
        char estensioni[8][31] = {};   // "FWVER=SPG 5.10", "PROTVER=34.10", "MOD=MIA-M10Q"...
        uint8_t hw3Flags = 0;          // bit 0 rtcCalib, bit 1 safeBoot, bit 2 xtalAbsent (manca il quarzo RTC)
        uint8_t hw3Pin = 0;            // nPins
        uint16_t hw3Len = 0;           // byte in hw3Dati
        uint8_t hw3Dati[200] = {};     // payload MON-HW3 com'e' arrivato
    };
    void chiediVersione();
    Versione versione() const;

    // Prova al banco (@GPS!PROVA): cicli (1-10) di PMREQ (flag 0x02, o 0x06 "force" con forza), frasi
    // che si fermano (o "ignorato" dopo 3 s), 5 s di sonno controllando che taccia, sveglia misurata.
    // Alla fine il ricevitore torna come lo vuole power(). Il risultato del PMREQ normale (pmreq in
    // Diagnosi) non cambia.
    struct CicloProva {
        int8_t esito = -1;             // 1 standby, 0 PMREQ ignorato, -1 non fatto
        int16_t fermeMs = -1;          // ms dal PMREQ all'ultima frase (-1: non si sono fermate)
        uint8_t frasiNelSonno = 0;     // frasi valide arrivate nei 5 s di sonno (0 = dormiva davvero)
        int16_t sveglioMs = -1;        // ms dal primo 0xFF alla prima frase (-1: non si e' svegliato)
        uint8_t tentativi = 0;         // 0xFF mandati per svegliarlo
    };
    static constexpr int PROVA_MAX = 10;
    struct Prova {
        uint32_t numero = 0;           // cresce a ogni prova chiesta
        bool inCorso = false;
        bool forza = false;
        uint8_t cicli = 0, fatti = 0;
        const char* errore = nullptr;  // perche' non e' partita o si e' fermata (nullptr: nessun errore)
        CicloProva ciclo[PROVA_MAX];
    };
    void prova(int cicli, bool forza);
    Prova provaStato() const;
#endif

private:
    bool on_ = false;
};

Gnss& gnss();

// Analizzatore NMEA (separato per poterlo provare sul PC).
#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck (src/tdeck/gnss_pilota.cpp, provato in componenti/scheda/test_host): conta le frasi buone e
// quelle scartate, segna l'ora dell'ultima frase di ogni tipo, e dalle GSV tiene una serie per
// costellazione e segnale (NMEA 4.10+), che vale solo intera e scade dopo 5 s.
class NmeaParser {
public:
    // Passa un carattere; true quando una RMC, GGA o GSV valida ha aggiornato il fix.
    bool feed(char c, Fix& fix, uint64_t nowMs);
    // Da chiamare almeno ogni mezzo secondo, anche senza caratteri: fa scadere la posizione (5 s senza
    // RMC 'A'), usati e hdop (5 s senza frasi) e le serie GSV non rinnovate da 5 s.
    void scadenze(Fix& fix, uint64_t nowMs);
    // Dimentica la riga a meta' e i satelliti (standby, ripartenza da freddo).
    void azzera();

    static constexpr uint64_t POSIZIONE_MS = 5000;  // senza RMC 'A' da 5 s: posizione non valida
    static constexpr uint64_t CIELO_MS = 5000;      // serie GSV non rinnovata da 5 s: non conta piu'

private:
    // Una serie di GSV: una costellazione (talker GP, GL, GA, GB, BD, GQ...) e un segnale.
    struct Serie {
        char talker[2] = {0, 0};  // {0,0} = posto libero
        char segnale = 0;         // signalId (NMEA 4.10+), 0 se la frase non ce l'ha
        uint8_t numMsg = 0;       // serie in arrivo: quanti messaggi
        uint8_t prossimo = 0;     // serie in arrivo: numero del prossimo messaggio (0 = nessuna)
        uint8_t elencatiP = 0, conSegnaleP = 0, migliorP = 0;  // serie in arrivo
        uint8_t elencati = 0, conSegnale = 0, miglior = 0;     // ultima serie completa
        bool letta = false;       // c'e' una serie completa
        uint64_t atMs = 0;        // quando e' finita l'ultima serie completa
    };
    static constexpr int SERIE = 12;  // u-blox M10: GP, GA, GQ (due segnali), GL, GB; CASIC: GP, BD, GL

    bool parse(Fix& fix, uint64_t nowMs);
    void gsv(char** f, int n, Fix& fix, uint64_t nowMs);
    void cielo(Fix& fix, uint64_t nowMs) const;

    char line_[100];
    int len_ = 0;
    Serie serie_[SERIE];
};
#else
class NmeaParser {
public:
    // Passa un carattere; true quando una frase valida ha aggiornato il fix.
    bool feed(char c, Fix& fix, uint64_t nowMs);

private:
    bool parse(Fix& fix, uint64_t nowMs);
    char line_[100];
    int len_ = 0;
    uint8_t inView_[6] = {};  // satelliti in vista per costellazione (GP, GL, GA, GB, BD, GQ)
};
#endif

}  // namespace scheda
