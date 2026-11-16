// Cuffie USB-C (solo T-Deck Plus). Di solito la porta USB-C e' del computer: console USB-Serial-JTAG (log,
// pagina del flasher, mappe via cavo, comandi @). Con le cuffie accese la porta passa al controller USB-OTG,
// che fa da host, e la musica va a cuffie USB Audio (classe 1.0 o 2.0, full speed) attaccate con uno
// sdoppiatore OTG alimentato a 5 V: il T-Deck sulla USB-C non da' corrente. In questa fase (P1) va alle cuffie
// solo la musica; vocali, chiamate, avvisi e sveglie restano sulla cassa.
//
// Il modo vale per una sessione: all'avvio e' sempre spento, e torna al computer da solo dopo 2 minuti senza
// cuffie, prima dell'aggiornamento Wi-Fi e con "Spegni". Si accende solo senza computer attaccato, senza
// aggiornamento Wi-Fi e con abbastanza RAM interna. Finche' e' acceso il log va in una scatola nera in PSRAM,
// stampata quando la console torna.
//
// Il lavoro lo fa il compito "cuffie" (core 0); le funzioni qui sotto si possono chiamare da qualunque compito e
// non bloccano, tranne spegniEAspetta(). Ogni cambio di stato manda Ev::CUFFIE all'interfaccia (a = Stato,
// id = Errore). Niente ESP-IDF qui: l'header va bene anche al simulatore (che ha delle cuffie finte).
#pragma once

#include <cstdint>
#include <string>

namespace app {

class Cuffie {
public:
    enum class Stato : uint8_t {
        SPENTE,          // la porta e' del computer (sempre, all'avvio)
        ACCENSIONE,      // sta passando alle cuffie (installazione della USB host)
        IN_ATTESA,       // porta alle cuffie, nessun dispositivo attaccato
        COLLEGAMENTO,    // dispositivo attaccato: lettura dei descrittori e configurazione
        COLLEGATE,       // suonano: la musica va qui (pronta())
        NON_SUPPORTATE,  // e' attaccato un dispositivo che non va (il motivo in errore())
        ERRORE,          // il dispositivo ha smesso di rispondere o lo stream si e' fermato (motivo in errore())
        SPEGNIMENTO,     // sta tornando al computer
    };
    enum class Errore : uint8_t {
        NESSUNO,
        // accendi() rifiutato (resta SPENTE):
        COMPUTER,           // la USB-C e' attaccata a un computer ("Scollega il computer")
        AGGIORNAMENTO,      // c'e' l'aggiornamento Wi-Fi acceso
        MEMORIA,            // poca RAM interna (servono circa 40 KB liberi e un blocco DMA da 8 KB)
        // dispositivo attaccato ma non supportato (NON_SUPPORTATE):
        NON_AUDIO,          // non e' un dispositivo audio
        SOLO_UAC3,          // solo USB Audio 3
        PACCHETTI_GRANDI,   // pacchetti troppo grandi (wMaxPacketSize oltre 600 byte)
        DESCRITTORE_LUNGO,  // descrittore di configurazione oltre 1024 byte (l'enumerazione non riesce)
        FORMATO,            // nessun formato adatto (44,1 o 48 kHz, 16-32 bit, 1 o 2 canali)
        // guasti (ERRORE; DRIVER anche con SPENTE, se la USB host non parte):
        DISPOSITIVO,        // il dispositivo non risponde bene (richieste, clock, interfaccia, stream)
        DRIVER,             // la USB host non parte (installazione, client, compiti)
        // tornata al computer da sola (SPENTE), non e' un guasto:
        SENZA_CUFFIE,       // 2 minuti senza cuffie ("Spente dopo 2 minuti senza cuffie")
    };
    // Per @CUFFIE?: la sessione in corso, o l'ultima (resta anche dopo lo spegnimento).
    struct Statistiche {
        uint32_t sessioni = 0;          // accensioni dall'avvio del telefono
        Stato stato = Stato::SPENTE;
        Errore errore = Errore::NESSUNO;
        char nome[33] = {};             // prodotto (iProduct), UTF-8
        uint16_t vid = 0, pid = 0;
        uint8_t uac = 0;                // 1 o 2; 0 = nessun dispositivo audio in questa sessione
        uint32_t hz = 0;                // formato dello stream
        uint8_t canali = 0, bit = 0;
        uint32_t secondiSuonati = 0;    // con lo stream acceso
        uint32_t sottoflussi = 0;       // trasferimenti con del silenzio mentre la musica suonava (coda vuota)
        uint32_t saltati = 0;           // pacchetti saltati (SKIPPED: l'interrupt e' arrivato tardi)
        uint32_t errori = 0;            // trasferimenti finiti con un errore
        uint32_t ramInternaPrima = 0;   // RAM interna libera prima di accendere (byte)
        uint32_t ramInternaMin = 0;     // la minima vista con le cuffie accese
        uint32_t ramInternaDopo = 0;    // dopo lo spegnimento (se e' meno di prima, qualcosa non e' stato liberato)
    };

    static constexpr uint32_t SECONDI_SENZA_CUFFIE = 120;  // poi la porta torna al computer
    static constexpr uint8_t VOLUME_INIZIALE = 4;          // 0..10

    // All'avvio, dopo scheda::init(): registra le cuffie come uscita esterna dell'audio
    // (scheda::Audio::setUscitaEsterna) e prepara il compito. Non accende niente.
    void avvia();
    // Accende il modo cuffie. Computer, aggiornamento e RAM li controlla subito: se non si puo' restituisce false
    // (motivo in errore(), stato SPENTE). Altrimenti true, e il compito passa la porta alle cuffie (ACCENSIONE,
    // poi IN_ATTESA, COLLEGAMENTO, COLLEGATE).
    bool accendi();
    // Prova da banco (@CUFFIE! <secondi>): come accendi() ma senza il controllo del computer (il comando arriva
    // proprio dal computer); la porta torna al computer da sola dopo `secondi`, cuffie o no.
    bool prova(uint32_t secondi);
    // Torna al computer. Non blocca: lo fa il compito (qualche decimo di secondo).
    void spegni();
    // Come spegni(), ma aspetta che la porta sia tornata al computer, al massimo attesaMs: prima
    // dell'aggiornamento Wi-Fi e di "Spegni". true se e' tornata (o era gia' spenta). Di solito bastano pochi
    // decimi di secondo; con un dispositivo che non risponde o che si sta enumerando lo spegnimento ne dura
    // qualche secondo in piu' (la libreria si disinstalla solo senza dispositivi), da qui i 6 s.
    bool spegniEAspetta(uint32_t attesaMs = 6000);

    Stato stato() const;
    bool accese() const;   // stato() != SPENTE
    bool pronta() const;   // COLLEGATE: la musica va alle cuffie
    // Motivo dell'ultimo rifiuto, del dispositivo non supportato, del guasto o del ritorno al computer da sola.
    Errore errore() const;
    std::string nome() const;   // prodotto delle cuffie attaccate ("" se non ci sono o non ha nome)
    // Il valore della riga "Cuffie USB-C" in Impostazioni: "spente", "in attesa", il nome delle cuffie (o
    // "collegate" se non ha nome), "non supportate", "errore".
    std::string testo() const;
    static const char* testoStato(Stato s);
    // Breve, per i messaggi: "c'e' un computer", "non audio", "solo USB Audio 3", "pacchetti troppo grandi", ...
    static const char* testoErrore(Errore e);

    // Volume delle cuffie 0..10, con la curva v^2 della cassa (4 = -16 dB): lo applica il software, con una
    // rampa; il volume del dispositivo resta a 0 dB. Lo salva chi lo cambia (impostazioni, "volume_cuffie").
    void setVolume(uint8_t v);
    uint8_t volume() const;

    Statistiche statistiche() const;
};

Cuffie& cuffie();

}  // namespace app
