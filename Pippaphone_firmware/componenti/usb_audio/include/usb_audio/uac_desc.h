// Cuffie USB-C (solo T-Deck Plus): descrittori USB Audio, classe 1.0 (UAC1) e 2.0 (UAC2), solo per suonare.
// Niente ESP-IDF: si compila anche sul PC e si prova con i descrittori veri di cuffie e adattatori (test/).
//
// analizza() legge il descrittore di configurazione intero, cosi' come arriva dal dispositivo
// (usb_host_get_active_config_descriptor, wTotalLength byte), e tiene solo quello che serve per suonare:
//  - l'interfaccia AudioControl (classe 0x01, sottoclasse 0x01): versione, terminali, unita' (la Feature Unit
//    per muto e volume) e, per UAC2, i clock;
//  - le interfacce AudioStreaming (sottoclasse 0x02), ognuna con le sue impostazioni alternative: formato,
//    canali, byte per campione, frequenze, endpoint, wMaxPacketSize, bInterval, sincronizzazione, feedback.
// scegli() trova alternativa e frequenza con cui suonare; trovaQuirk() dice le stranezze note di un
// dispositivo (per VID:PID). Le richieste USB (SET_INTERFACE, frequenza, volume) le fa uac_host.h.
//
// I campi hanno i nomi delle specifiche (USB Device Class Definition for Audio Devices 1.0 e 2.0, capitolo 4)
// e i valori sono gli stessi di include/uapi/linux/usb/audio.h e include/linux/usb/audio-v2.h; qui sono scritti
// a mano, non c'e' codice copiato da altri driver.
#pragma once

#include <cstddef>
#include <cstdint>

namespace uac {

// ---- limiti del modello: oltre si ignora il resto e Configurazione::troncata diventa true ----
constexpr size_t MAX_ENTITA = 32;    // terminali, unita' e clock dell'interfaccia AudioControl
constexpr size_t MAX_SORGENTI = 8;   // ingressi di un'unita' (mixer, selettore) o di un selettore di clock
constexpr size_t MAX_STREAMING = 4;  // interfacce AudioStreaming della funzione audio scelta
constexpr size_t MAX_ALT = 8;        // alternative di un'interfaccia AudioStreaming (senza la 0)
constexpr size_t MAX_FREQ = 10;      // frequenze (o intervalli) di un'alternativa
constexpr size_t MAX_CANALI_FU = 15; // canali di una Feature Unit oltre al master (maschere da 16 bit)

// wMaxPacketSize massimo che l'ESP32-S3 riesce a mandare: FIFO periodica di 600 byte con
// CONFIG_USB_HOST_HW_BUFFER_BIAS_PERIODIC_OUT (con "Balanced" sarebbero 128). Un endpoint piu' grande fa
// fallire usb_host_interface_claim ("EP MPS exceeds supported limit").
constexpr uint16_t MAX_PACCHETTO = 600;

// wTerminalType dei terminali che servono.
constexpr uint16_t TERMINALE_STREAMING_USB = 0x0101;
constexpr uint16_t TERMINALE_ALTOPARLANTE = 0x0301;
constexpr uint16_t TERMINALE_CUFFIE = 0x0302;
constexpr uint16_t TERMINALE_CUFFIE_MICROFONO = 0x0402;  // headset

enum class Versione : uint8_t { UAC1 = 1, UAC2 = 2 };

// Sincronizzazione dell'endpoint isocrono (bmAttributes, bit 2-3).
enum class Sync : uint8_t { NESSUNA = 0, ASINCRONA = 1, ADATTIVA = 2, SINCRONA = 3 };

// Esito di analizza() e scegli().
enum class Esito : uint8_t {
    OK = 0,
    MALFORMATO,        // descrittore rotto: bLength 0 o oltre la fine, tipo della configurazione sbagliato
    NON_AUDIO,         // nessuna interfaccia AudioControl (chiavetta, tastiera, ...)
    SOLO_UAC3,         // c'e' solo USB Audio 3.0 (bInterfaceProtocol 0x30)
    NESSUNA_USCITA,    // audio, ma niente streaming verso il dispositivo (per esempio solo un microfono)
    PACCHETTI_GRANDI,  // l'uscita c'e', ma ogni alternativa adatta ha wMaxPacketSize oltre MAX_PACCHETTO
    FORMATO,           // nessuna alternativa adatta (PCM 16-32 bit, 1-2 canali, 44,1/48 kHz, bInterval 1)
};
// Motivo breve in italiano, per il log e per il messaggio "Dispositivo USB non supportato": "ok",
// "descrittore non valido", "non audio", "solo USB Audio 3", "senza uscita audio", "pacchetti troppo grandi",
// "formato non adatto".
const char* testoEsito(Esito e);

// ---- frequenze di campionamento ----

// Una frequenza sola e' {f, f, 0}. Con min < max: passo 0 = qualunque frequenza tra min e max (UAC1
// continuo, o UAC2 con dRES 0); passo > 0 = min, min + passo, ... fino a max (UAC2 dRES).
struct IntervalloHz {
    uint32_t min = 0;
    uint32_t max = 0;
    uint32_t passo = 0;
};

struct Frequenze {
    uint8_t n = 0;
    IntervalloHz voci[MAX_FREQ];

    // false se e' piena (la voce non entra).
    bool aggiungi(uint32_t min, uint32_t max, uint32_t passo = 0) {
        if (n >= MAX_FREQ) return false;
        voci[n].min = min;
        voci[n].max = max;
        voci[n].passo = passo;
        n++;
        return true;
    }
    bool contiene(uint32_t hz) const {
        for (uint8_t i = 0; i < n && i < MAX_FREQ; i++) {
            const IntervalloHz& v = voci[i];
            if (hz < v.min || hz > v.max) continue;
            if (v.passo == 0 || (hz - v.min) % v.passo == 0) return true;
        }
        return false;
    }
};

// ---- entita' dell'interfaccia AudioControl ----

enum class TipoEntita : uint8_t {
    TERMINALE_INGRESSO,    // INPUT_TERMINAL
    TERMINALE_USCITA,      // OUTPUT_TERMINAL
    MIXER,                 // MIXER_UNIT
    SELETTORE,             // SELECTOR_UNIT
    FEATURE,               // FEATURE_UNIT
    ALTRA_UNITA,           // processing, effect, extension, convertitore di frequenza: si attraversano
    CLOCK_SORGENTE,        // UAC2 CLOCK_SOURCE
    CLOCK_SELETTORE,       // UAC2 CLOCK_SELECTOR
    CLOCK_MOLTIPLICATORE,  // UAC2 CLOCK_MULTIPLIER
};

struct Entita {
    uint8_t id = 0;                       // bTerminalID / bUnitID / bClockID
    TipoEntita tipo = TipoEntita::ALTRA_UNITA;
    uint16_t tipoTerminale = 0;           // solo terminali: wTerminalType
    uint8_t clock = 0;                    // UAC2: bCSourceID di terminali e moltiplicatori (0 = nessuno)
    uint8_t nSorgenti = 0;                // bSourceID (1) / baSourceID (n) / baCSourceID dei selettori di clock
    uint8_t sorgenti[MAX_SORGENTI] = {};
    uint8_t attributiClock = 0;           // UAC2 clock source: bmAttributes (bit 0-1 tipo, bit 2 legato allo SOF)
    uint8_t controlliClock = 0;           // UAC2 clock source e selettore: bmControls (cosi' come sono)
    // Feature Unit. Canale 0 = master, 1..canali i canali logici. Il bit c e' 1 se il canale c ha quel
    // controllo impostabile dall'host (UAC1: bit del controllo a 1; UAC2: i due bit a 0b11).
    uint8_t canali = 0;                   // canali logici descritti, senza il master (al massimo MAX_CANALI_FU)
    uint16_t muto = 0;
    uint16_t volume = 0;
};

// ---- interfacce AudioStreaming ----

struct Alternativa {
    uint8_t numero = 0;                   // bAlternateSetting (>= 1: la 0 e' quella senza banda)
    uint8_t terminale = 0;                // bTerminalLink (AS_GENERAL)
    bool pcm = false;                     // UAC1: wFormatTag 0x0001; UAC2: bFormatType 1 e bmFormats bit 0. Tipo I
    uint8_t canali = 0;                   // bNrChannels (UAC1: FORMAT_TYPE; UAC2: AS_GENERAL)
    uint8_t byteCampione = 0;             // bSubframeSize (UAC1) / bSubslotSize (UAC2)
    uint8_t bit = 0;                      // bBitResolution
    Frequenze frequenze;                  // UAC1: dal FORMAT_TYPE. UAC2: vuote finche' uac_host non chiama
                                          // impostaFrequenzeClock() con quelle del clock
    uint8_t endpoint = 0;                 // bEndpointAddress dell'endpoint dei dati (bit 7 = IN, dal dispositivo)
    uint16_t maxPacchetto = 0;            // wMaxPacketSize, bit 0-10
    uint8_t intervallo = 0;               // bInterval (a full speed un pacchetto ogni 2^(bInterval-1) ms)
    Sync sync = Sync::NESSUNA;            // dell'endpoint dei dati
    uint8_t endpointFeedback = 0;         // endpoint del feedback (IN isocrono) se c'e', 0 se no. UAC1: il
                                          // bSynchAddress dell'endpoint dei dati (solo se ha 9 byte)
    uint8_t intervalloFeedback = 0;       // UAC1: bRefresh dell'endpoint di sincronizzazione (quello di
                                          // bSynchAddress, se e' nella stessa alternativa: quello dei dati vale
                                          // sempre 0), feedback ogni 2^bRefresh ms; UAC2: bInterval del feedback
    bool frequenzaSuEndpoint = false;     // UAC1: il CS endpoint ha bmAttributes bit 0 (SET_CUR della frequenza)
};

struct Streaming {
    uint8_t interfaccia = 0;              // bInterfaceNumber
    bool uscita = false;                  // endpoint dei dati OUT: verso il dispositivo, si suona qui
    uint8_t nAlt = 0;
    Alternativa alt[MAX_ALT];             // solo le alternative con l'endpoint dei dati
};

// Una funzione audio (quella scelta da analizza()). E' grande (piu' di 5 KB): sul T-Deck la si tiene in PSRAM
// (heap), non sullo stack dei compiti.
struct Configurazione {
    Versione versione = Versione::UAC1;
    uint16_t bcdADC = 0;                  // dall'header AudioControl (0x0100, 0x0200)
    uint8_t interfacciaControllo = 0;     // bInterfaceNumber dell'AudioControl (wIndex delle richieste di classe)
    uint8_t nEntita = 0;
    Entita entita[MAX_ENTITA];
    uint8_t nStreaming = 0;
    Streaming streaming[MAX_STREAMING];
    bool troncata = false;                // c'era piu' roba dei limiti MAX_*: il resto e' stato ignorato
};

// Legge il descrittore di configurazione: n byte da desc (dal bLength della configurazione in poi; di solito
// n = wTotalLength, ma si legge al massimo min(n, wTotalLength)). Prende la prima funzione audio UAC1 o UAC2
// (AudioControl con bInterfaceProtocol 0x00 o 0x20) che ha uno streaming in uscita. Gli streaming di una
// funzione sono quelli di baInterfaceNr dell'header (UAC1) o dell'Interface Association Descriptor (UAC2; se
// manca, quelli che seguono l'AudioControl fino alla prossima AudioControl). Non alloca niente.
// out e' valido solo con Esito::OK; gli altri esiti dicono perche' il dispositivo non va (e lasciano out vuota:
// nEntita e nStreaming a 0).
// Quanto e' severa: MALFORMATO solo se la catena dei bLength e' rotta (un bLength < 2, un descrittore che
// esce da min(n, wTotalLength), un primo descrittore che non e' la configurazione o e' piu' corto di 9 byte,
// wTotalLength piu' corto della configurazione stessa). Dentro una
// catena buona, come fa Linux: un descrittore troppo corto per i campi che servono si salta (se e' quello di
// un'interfaccia, si salta tutta l'interfaccia), un elenco piu' lungo del suo descrittore si legge fin dove
// arriva, gli id 0 non si tengono e di un id ripetuto vale la prima entita' (come in Linux), gli endpoint non
// isocroni e quelli con numero 0 non sono endpoint dei dati.
// Un'AudioControl con un bInterfaceProtocol sconosciuto si legge come UAC1 (come fa Linux).
Esito analizza(const uint8_t* desc, size_t n, Configurazione& out);

// L'entita' con quell'id, nullptr se non c'e'.
const Entita* trovaEntita(const Configurazione& c, uint8_t id);

// Dove va il suono di un terminale streaming (il bTerminalLink di un'alternativa in uscita): si parte dai
// terminali d'uscita e si risale per bSourceID/baSourceID fino al terminale.
struct Percorso {
    uint8_t terminaleUscita = 0;   // l'Output Terminal a cui arriva (0 = nessuno)
    uint16_t tipoUscita = 0;       // il suo wTerminalType (0x0301 altoparlante, 0x0302 cuffie, ...)
    uint8_t featureUnit = 0;       // la Feature Unit sulla strada piu' vicina al terminale streaming (0 = nessuna)
    uint8_t clock = 0;             // UAC2: bCSourceID del terminale streaming (sorgente, selettore o
                                   // moltiplicatore); UAC1: 0
};
// false se il terminale non c'e' (o non e' un Input Terminal) o non arriva a nessun terminale d'uscita; clock
// pero' si riempie lo stesso, se il terminale c'e' (la frequenza si imposta anche senza Feature Unit).
// La strada si cerca in profondita' (sorgenti nell'ordine, senza ripassare da un'entita': niente giri) da ogni
// terminale d'uscita che non sia di tipo 0x0101; se piu' d'uno arriva al terminale, vince il primo che ha una
// Feature Unit sulla strada, se no il primo. Le entita' dei clock non fanno parte della strada.
bool percorso(const Configurazione& c, uint8_t terminale, Percorso& out);

// UAC2: segue il clock `id` (selettori e moltiplicatori) fino al Clock Source e ne restituisce l'id (0 se non
// lo trova o c'e' un giro). Per ogni selettore chiede l'ingresso attivo a `pin` (1..n, cioe' GET CUR del
// selettore; 0 = non si sa); senza `pin`, con 0 o con un numero oltre gli ingressi, prende il primo ingresso.
// `pin` si chiama al massimo una volta per selettore.
using PinSelettore = uint8_t (*)(void* ctx, uint8_t idSelettore);
uint8_t sorgenteClock(const Configurazione& c, uint8_t id, PinSelettore pin = nullptr, void* ctx = nullptr);

// UAC2: le frequenze non sono nei descrittori ma nel clock (GET RANGE). uac_host le chiede al Clock Source del
// terminale e le copia qui in tutte le alternative con quel terminale, prima di scegli().
void impostaFrequenzeClock(Configurazione& c, uint8_t terminale, const Frequenze& f);

// UAC2: risposta a GET RANGE della frequenza (CS_SAM_FREQ_CONTROL): wNumSubRanges (2 byte) e poi
// wNumSubRanges x {dMIN, dMAX, dRES} da 4 byte, little endian. Le voci oltre MAX_FREQ si ignorano, ma una che
// contiene 48000 o 44100 (non ancora contenute) prende il posto di una voce che non contiene nessuna delle due:
// le sole frequenze che si usano non si perdono mai (lo stesso vale per le frequenze UAC1 in analizza()). Le
// voci con dMIN > dMAX si saltano. false se la risposta e' troppo corta per quello che dice o non ha voci.
bool analizzaRangeFrequenze(const uint8_t* dati, size_t n, Frequenze& out);

// Volume di una Feature Unit, in 1/256 dB con segno (0x8000 = meno infinito, solo UAC1).
struct RangeVolume {
    int16_t min = 0;
    int16_t max = 0;
    int16_t passo = 0;   // wRES (0 = non si sa)
};
// UAC2: risposta a GET RANGE del volume: wNumSubRanges e poi n x {wMIN, wMAX, wRES} da 2 byte; si prende il
// minimo dei minimi, il massimo dei massimi e il passo del primo intervallo. (UAC1 non ha RANGE: uac_host
// chiede GET_MIN, GET_MAX e GET_RES una per una e riempie RangeVolume da se'.) false se troppo corta.
bool analizzaRangeVolume(const uint8_t* dati, size_t n, RangeVolume& out);
// Il volume da mandare al posto di "0 dB": il piu' alto tra quelli possibili che non supera 0 dB, cioe'
// min(max, 0) (sul passo, contando da min), e mai sotto min. Con max < 0 e' max; con passo <= 0 (non si sa)
// e' 0, o min se min > 0.
int16_t volumeZeroDb(const RangeVolume& r);

// ---- scelta del formato ----

struct Scelta {
    uint8_t indiceStreaming = 0;   // in Configurazione::streaming
    uint8_t indiceAlt = 0;         // in Streaming::alt
    uint8_t interfaccia = 0;       // bInterfaceNumber (claim e SET_INTERFACE)
    uint8_t alt = 0;               // bAlternateSetting
    uint8_t terminale = 0;         // bTerminalLink (per percorso())
    uint8_t endpoint = 0;          // endpoint dei dati (OUT)
    uint16_t maxPacchetto = 0;     // wMaxPacketSize
    uint8_t canali = 0;            // 1 o 2
    uint8_t byteCampione = 0;      // 2, 3 o 4
    uint8_t bit = 0;               // bBitResolution
    uint32_t hz = 0;               // 48000 o 44100
    Sync sync = Sync::NESSUNA;
    bool frequenzaSuEndpoint = false;  // UAC1: dopo SET_INTERFACE la frequenza va all'endpoint (SET_CUR)
};

// Sceglie alternativa e frequenza. Vanno bene le alternative degli streaming in uscita con:
//  - PCM, 1 o 2 canali, byteCampione 2, 3 o 4 e 16 <= bit <= 8 x byteCampione;
//  - bInterval 1 (un pacchetto in ogni frame da 1 ms);
//  - 48000 o 44100 Hz fra le frequenze (con QUIRK_SOLO_48K solo 48000);
//  - spazio per il pacchetto piu' lungo: ceil(hz / 1000) x canali x byteCampione <= wMaxPacketSize, e
//    wMaxPacketSize <= MAX_PACCHETTO.
// Fra quelle buone vince, in quest'ordine: stereo prima di mono; poi 16 bit prima di 24 e 32 (byteCampione
// piu' piccolo); poi 48 kHz prima di 44,1; poi sincrono o adattivo prima di asincrono (in P1 niente feedback:
// un endpoint asincrono suona ma ogni tanto scivola); poi wMaxPacketSize piu' piccolo; poi l'ordine nel
// descrittore. Cioe': 48 kHz/16 bit/stereo, se no 44,1 kHz, se no 24 bit, se no mono.
// Se non ce n'e' nessuna: PACCHETTI_GRANDI se almeno una sarebbe andata bene salvo wMaxPacketSize >
// MAX_PACCHETTO, NESSUNA_USCITA se non c'e' nessuno streaming in uscita, altrimenti FORMATO.
// quirk: i flag di trovaQuirk().
Esito scegli(const Configurazione& c, uint32_t quirk, Scelta& out);

// ---- stranezze note dei dispositivi (src/quirk.cpp), per VID:PID ----
enum : uint32_t {
    QUIRK_PAUSA_5MS = 1u << 0,             // 5 ms dopo ogni richiesta di classe (Samsung AKG 04e8:a051)
    QUIRK_PAUSA_1MS = 1u << 1,             // 1 ms dopo ogni richiesta di classe (C-Media 0d8c:0014)
    QUIRK_NON_TOCCARE_SELETTORE = 1u << 2, // mai SET sui selettori di clock, solo GET (Samsung AKG 04e8:a051)
    QUIRK_INTERFACCIA_PRIMA = 1u << 3,     // UAC2: SET_INTERFACE prima della frequenza, come in UAC1
    QUIRK_SOLO_48K = 1u << 4,              // niente 44,1 kHz (Apple 05ac:110a adattatore, 05ac:110b EarPods)
    QUIRK_PAUSA_20MS = 1u << 5,            // 20 ms dopo ogni richiesta di classe (Plantronics, Linux
                                           // QUIRK_FLAG_CTL_MSG_DELAY)
};
// Le stranezze di un dispositivo (0 = nessuna). Prima si cerca VID:PID, poi le voci valide per tutto il
// produttore (come fa Linux, sound/usb/quirks.c).
uint32_t trovaQuirk(uint16_t vid, uint16_t pid);
// Pausa da fare dopo ogni richiesta di classe, in ms (la piu' lunga, se ce n'e' piu' d'una).
inline uint32_t pausaRichiesteMs(uint32_t quirk) {
    return (quirk & QUIRK_PAUSA_20MS) ? 20 : (quirk & QUIRK_PAUSA_5MS) ? 5 : (quirk & QUIRK_PAUSA_1MS) ? 1 : 0;
}

// ---- altro ----

// Testo di un descrittore stringa USB (bLength, bDescriptorType 0x03, poi UTF-16LE) in UTF-8: al massimo
// cap - 1 byte piu' lo zero finale, senza spezzare un carattere. I caratteri di controllo diventano spazi,
// gli spazi in fondo si tolgono, quelli fuori dal piano base (coppie surrogate, o surrogati soli) diventano
// '?'. Restituisce i byte scritti senza lo zero; 0 (e out = "") se il descrittore non e' valido. Con out
// nullptr o cap 0 non scrive niente. Si legge al massimo min(n, bLength).
size_t testoStringa(const uint8_t* desc, size_t n, char* out, size_t cap);

// Il modello in righe di testo, per il log (una riga per la funzione, una per ogni alternativa, per esempio
// "if 1 alt 2: PCM 2 ch 16/2 byte, 48000 Hz, ep 0x02 192 B ogni 1 ms, sincrono"). riga() riceve ogni riga
// senza "\n".
void descrivi(const Configurazione& c, void (*riga)(void* ctx, const char* testo), void* ctx);

}  // namespace uac
