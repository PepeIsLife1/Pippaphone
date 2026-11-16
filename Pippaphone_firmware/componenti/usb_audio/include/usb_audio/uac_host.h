// Cuffie USB-C (solo T-Deck Plus): un dispositivo USB Audio (classe 1.0 o 2.0) sulla USB host di ESP-IDF 5.5.1
// (componente "usb"), solo riproduzione, full speed.
//
// Dispositivo, dati il client della USB Host Library e l'indirizzo arrivato con USB_HOST_CLIENT_EVENT_NEW_DEV:
//  1. apre il dispositivo e legge i descrittori: del dispositivo (VID:PID, bMaxPacketSize0), della configurazione
//     attiva, il nome del prodotto (iProduct, quello che la libreria ha gia' letto all'enumerazione);
//  2. li analizza (uac_desc.h: analizza, trovaQuirk) e sceglie formato e frequenza (scegli); per UAC2 prima
//     chiede al clock le frequenze (GET CUR del selettore se c'e', GET RANGE del Clock Source);
//  3. configura. UAC2: frequenza sul Clock Source (GET CUR, e SET CUR se e' diversa e il clock la accetta),
//     controllo di CLOCK_VALID, poi usb_host_interface_claim e SET_INTERFACE (la libreria non lo manda da sola;
//     con QUIRK_INTERFACCIA_PRIMA l'ordine e' al contrario). UAC1: claim, SET_INTERFACE, poi SET_CUR della
//     frequenza sull'endpoint se lo accetta (frequenzaSuEndpoint). Dopo un SET riuscito la frequenza non si
//     rilegge: c'e' chi a quel GET non risponde, e una richiesta senza risposta qui non si annulla (vedi sotto;
//     Linux per quei dispositivi non la rilegge, QUIRK_FLAG_GET_SAMPLE_RATE). Tutti e due: sulla Feature Unit
//     del percorso volume a min(MAX, 0 dB) e muto tolto su ogni canale che li ha (il volume vero lo fa il
//     software, app/cuffie.cpp). Dopo ogni richiesta di classe la pausa dei quirk (pausaRichiesteMs);
//  4. fa girare lo stream isocrono in uscita: TRASFERIMENTI trasferimenti da PACCHETTI pacchetti da 1 ms
//     (32 ms in coda), lunghi quanto dice Pacchetti (dsp.h). I primi partono pieni di zeri; ogni volta che uno
//     finisce lo riempie il Riempitore e lo rimanda subito, nel callback stesso. Pacchetti SKIPPED (interrupt in
//     ritardo) ed errori si contano e basta: lo stream non si ferma;
//  5. chiudi() lo ferma: smette di rimandare i trasferimenti e aspetta che tornino tutti da soli (in coda c'e'
//     al massimo per 32 ms), poi halt e flush dell'endpoint (subito: e' vuoto), SET_INTERFACE 0 (se il
//     dispositivo c'e' ancora), release dell'interfaccia, libera tutto, close. Niente halt a stream in corso: un
//     canale isocrono che si ferma mentre la porta va in errore (cuffie staccate in quel momento) non da' mai
//     l'interrupt di halt, e usb_host_endpoint_halt() aspetta quell'interrupt senza timeout
//     (components/usb/maintainers.md, "Channel interrupt on port errors"; hcd_dwc.c, _pipe_cmd_halt).
//
// Non installa la USB Host Library, non registra il client e non crea compiti: lo fa app/cuffie.cpp, che fa
// girare il demone (usb_host_lib_handle_events) e il compito client (usb_host_client_handle_events). Tutti i
// callback dei trasferimenti, anche quelli delle richieste di controllo, arrivano nel compito client: per
// questo apri() e chiudi(), che aspettano le risposte, vanno chiamate da un ALTRO compito (quello di controllo
// delle cuffie), mai dal compito client e mai da un callback. Il callback degli eventi del client non deve
// bloccare (code verso il compito di controllo con timeout 0): chiudi() aspetta il compito client.
//
// Priorita': meglio il demone SOPRA il compito di controllo, sullo stesso core. La libreria consegna la fine di
// una richiesta su EP0 al compito client dal demone, e solo dopo, sempre nel demone, la toglie dal conto di
// quelle in volo e (dopo uno STALL) rimette EP0 attivo (usbh.c, handle_ep0_dequeue e handle_ep0_clear). Col
// demone sotto, il compito di controllo si sveglia prima: la richiesta dopo uno STALL trova EP0 ancora fermo e
// una usb_host_device_close() subito dopo l'ultima richiesta finirebbe nell'assert(num_ctrl_xfers_inflight == 0)
// di usbh_dev_close(). Qui ci sono le contromisure (nuovi tentativi se EP0 non e' attivo, una pausa prima di
// close), ma funzionano solo se nel frattempo il demone riesce a girare.
//
// La USB Host Library non ha timeout sui trasferimenti: le richieste di controllo li hanno qui
// (TIMEOUT_CONTROLLO_MS) e danno Errore::TEMPO. Un trasferimento su EP0 non si puo' annullare e, finche' e' in
// volo, il dispositivo non si puo' chiudere (assert in usbh_dev_close). Se il suo callback non arriva nemmeno
// dopo un'altra attesa, chiudi() spegne e riaccende la porta (usb_host_lib_set_root_port_power), come se le
// cuffie fossero state staccate: la libreria chiude EP0 con NO_DEVICE e manda DEV_GONE; riaccesa la porta il
// dispositivo si ripresenta con un nuovo NEW_DEV. Cosi' anche un dispositivo che non risponde piu' non impedisce
// di tornare al computer.
//
// Memoria: i buffer dei trasferimenti li alloca usb_host_transfer_alloc nella RAM interna DMA (sull'ESP32-S3
// non puo' essere PSRAM): TRASFERIMENTI x PACCHETTI x Pacchetti::massimo() x byteFrame (6 KB a 48 kHz 16 bit
// stereo), piu' una richiesta di controllo da 8 + MAX_CONTROLLO byte e due semafori. La Configurazione analizzata
// (piu' di 5 KB) sta in PSRAM e si libera alla fine di apri(). Si libera tutto in chiudi().
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "usb/usb_host.h"
#include "usb_audio/dsp.h"
#include "usb_audio/uac_desc.h"

namespace uac {

constexpr int TRASFERIMENTI = 4;                // in coda sull'endpoint: 2 nell'hardware, 2 in attesa
constexpr int PACCHETTI = 8;                    // pacchetti da 1 ms per trasferimento (l'HCD fa assert oltre 61)
constexpr uint32_t TIMEOUT_CONTROLLO_MS = 500;  // per ogni richiesta di controllo
constexpr size_t MAX_CONTROLLO = 256;           // byte di dati di una richiesta di controllo (oltre il setup)
constexpr size_t MAX_NOME = 32;                 // byte del nome del prodotto (UTF-8, senza lo zero)

// Formato dello stream, fisso per tutta la connessione (copiato dalla Scelta).
struct Formato {
    uint32_t hz = 0;           // 48000 o 44100
    uint8_t canali = 0;        // 1 o 2
    uint8_t byteCampione = 0;  // 2, 3 o 4
    uint8_t bit = 0;           // risoluzione dichiarata (16, 24, 32)
    uint16_t byteFrame() const { return uint16_t(canali * byteCampione); }
};

// Riempie un trasferimento. Lo chiama SOLO il compito client USB, una volta per trasferimento finito, prima di
// rimandarlo: deve scrivere esattamente `frame` frame nel formato `f` in `dst` (frame x f.byteFrame() byte) e
// non deve mai bloccare, allocare o loggare. Se non ha abbastanza campioni completa con silenzio. Restituisce
// quanti frame erano veri (il resto e' silenzio): serve solo per le statistiche.
using Riempitore = size_t (*)(void* ctx, uint8_t* dst, size_t frame, const Formato& f);

enum class Errore : uint8_t {
    NESSUNO = 0,
    DESCRITTORE,    // il dispositivo non va: il motivo in esito() (non audio, solo UAC3, pacchetti, formato...)
    APERTURA,       // usb_host_device_open o la lettura dei descrittori non sono riuscite
    CONTROLLO,      // una richiesta di controllo e' fallita (STALL o errore)
    TEMPO,          // una richiesta di controllo non ha risposto entro TIMEOUT_CONTROLLO_MS
    CLOCK,          // UAC2: il clock non e' valido dopo SET CUR della frequenza
    INTERFACCIA,    // usb_host_interface_claim o SET_INTERFACE non riusciti (es. "EP MPS exceeds supported limit")
    MEMORIA,        // trasferimenti non allocati (RAM interna DMA) o niente PSRAM per la Configurazione
    STREAM,         // l'invio dei trasferimenti isocroni non e' riuscito
    SCOLLEGATO,     // il dispositivo se n'e' andato (DEV_GONE, o trasferimenti finiti con NO_DEVICE)
};
// Breve, in italiano, per il log: "ok", "descrittori", "apertura", "richiesta rifiutata", ...
const char* testoErrore(Errore e);

// Contatori dello stream dall'ultimo apri() (le copie si possono leggere da ogni compito).
struct Statistiche {
    uint32_t trasferimenti = 0;  // trasferimenti isocroni finiti bene (COMPLETED; quasi tutti poi rimandati)
    uint32_t pacchetti = 0;      // pacchetti da 1 ms mandati
    uint32_t saltati = 0;        // pacchetti SKIPPED: l'interrupt e' arrivato tardi e il frame e' andato perso
    uint32_t errori = 0;         // trasferimenti finiti con un errore (non SKIPPED, non CANCELED)
    uint32_t frameVeri = 0;      // frame con campioni, secondo il Riempitore
    uint32_t frameSilenzio = 0;  // frame di silenzio messi dal Riempitore (coda vuota) o dai primi trasferimenti
    uint32_t msStream = 0;       // da quando lo stream e' partito (o quanto e' durato, dopo chiudi())
    uint32_t invioFallito = 0;   // usb_host_transfer_submit non riuscito (quel trasferimento resta fermo)
};

// Da quale compito: apri() e chiudi() e gli accessori dei descrittori (vid() ... percorsoAudio()) dal compito di
// controllo; scollegato() dal compito client; handle(), staccato(), attivo() e statistiche() da ogni compito.
class Dispositivo {
public:
    Dispositivo() = default;
    ~Dispositivo();  // chiudi()
    Dispositivo(const Dispositivo&) = delete;
    Dispositivo& operator=(const Dispositivo&) = delete;

    // Compito di controllo. Apre, analizza, configura e fa partire lo stream (passi 1-4 qui sopra). riempi e ctx
    // restano validi fino a chiudi(). Se qualcosa non va chiude tutto (il dispositivo resta attaccato ma
    // chiuso) e restituisce il motivo, che resta anche in errore() ed esito(). Attenzione: la libreria manda
    // DEV_GONE solo ai client che hanno il dispositivo aperto, quindi dopo un apri() fallito il suo distacco
    // non arriva come evento (si vede da usb_host_lib_info: num_devices torna a 0).
    Errore apri(usb_host_client_handle_t client, uint8_t indirizzo, Riempitore riempi, void* ctx);
    // Compito di controllo. Ferma e chiude (passo 5). Si puo' chiamare sempre: dopo un apri() fallito, due volte,
    // col dispositivo gia' staccato (allora salta SET_INTERFACE 0). Al ritorno nessun callback di questo
    // Dispositivo e' piu' in corso e il Riempitore non viene piu' chiamato. Di solito dura 50-100 ms; con una
    // richiesta rimasta senza risposta circa 1 s in piu' (spegne e riaccende la porta, vedi sopra), al massimo
    // qualche secondo. Se la libreria non restituisce i trasferimenti nemmeno cosi' (non dovrebbe succedere mai)
    // il dispositivo resta aperto, aperto() resta true, e un chiudi() dopo riprova. Con riaccendi false (quando
    // le cuffie si spengono) la porta, se si e' dovuta spegnere, resta spenta: riaccesa, il dispositivo ancora
    // attaccato si ripresenterebbe proprio mentre la libreria si disinstalla.
    void chiudi(bool riaccendi = true);
    // Compito client (callback degli eventi, USB_HOST_CLIENT_EVENT_DEV_GONE con handle() == dev_hdl): il
    // dispositivo non c'e' piu'. Non blocca: i trasferimenti che finiscono non si rimandano piu'. Poi il compito
    // di controllo chiama chiudi().
    void scollegato();

    bool aperto() const { return dev_.load() != nullptr; }  // apri() riuscito e non ancora chiuso
    bool attivo() const;      // lo stream gira: aperto, non scollegato, almeno un trasferimento in volo
    // Il dispositivo se n'e' andato: scollegato() chiamato, o un trasferimento o una richiesta finiti con
    // NO_DEVICE (la libreria li chiude cosi' prima ancora di mandare DEV_GONE). Con attivo() false distingue le
    // cuffie staccate (DEV_GONE arriva) da uno stream morto per un errore.
    bool staccato() const { return staccato_.load(); }
    usb_device_handle_t handle() const { return dev_.load(); }
    uint8_t indirizzo() const { return indirizzo_; }

    // Validi dopo apri() (anche fallito, per quanto e' arrivato) fino al prossimo apri().
    uint16_t vid() const { return vid_; }
    uint16_t pid() const { return pid_; }
    Versione versione() const { return versione_; }
    const char* prodotto() const { return prodotto_; }  // iProduct in UTF-8, "" se manca
    uint32_t quirk() const { return quirk_; }
    Errore errore() const { return errore_; }
    Esito esito() const { return esito_; }
    // Validi dopo apri() riuscito.
    Formato formato() const { return formato_; }
    const Scelta& scelta() const { return scelta_; }
    const Percorso& percorsoAudio() const { return percorso_; }
    Statistiche statistiche() const;

private:
    // I campi e i metodi privati li puo' cambiare chi implementa (gli altri vedono solo quelli pubblici).
    struct ClockTerminali;                           // UAC2: il Clock Source di ogni terminale streaming in uscita

    static void finito(usb_transfer_t* t);           // callback dei trasferimenti isocroni (compito client)
    static void finitoControllo(usb_transfer_t* t);  // callback della richiesta di controllo (compito client)
    static uint8_t pinSelettore(void* ctx, uint8_t idSelettore);  // PinSelettore: GET CUR di un selettore di clock
    // Richiesta di controllo bloccante (con timeout e pausa dei quirk). Nel setup wLength = n; per le IN
    // num_bytes si arrotonda a bMaxPacketSize0 (usb_round_up_to_mps). Restituisce i byte di dati ricevuti/mandati
    // in *fatti (se non nullptr).
    Errore richiesta(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t* dati,
                     uint16_t n, uint16_t* fatti = nullptr);
    // UAC2 GET RANGE in due passi (prima wNumSubRanges, poi tutto): voce = 12 per la frequenza, 6 per il volume.
    bool leggiRange(uint16_t wValue, uint16_t wIndex, size_t voce, uint8_t* buf, uint16_t& lun);
    bool leggiRangeVolume(uint16_t wIndex, uint8_t canale, RangeVolume& r);

    void azzera();
    Errore apriDispositivo();
    Errore analizzaEConfigura();
    Errore frequenzeUac2(Configurazione& c, ClockTerminali& clock);
    Errore impostaClockUac2(const Configurazione& c, uint8_t sorgente);
    Errore impostaFrequenzaUac1(const Configurazione& c);
    Errore prendiInterfaccia();
    Errore preparaFeatureUnit(const Configurazione& c);
    Errore avviaStream();
    Errore grave() const;                            // SCOLLEGATO o TEMPO se non ha senso andare avanti
    void aspettaFermi(uint32_t ms);
    void aspettaControllo(uint32_t ms);
    void staccaPorta();
    void riaccendiPorta();

    usb_host_client_handle_t client_ = nullptr;
    std::atomic<usb_device_handle_t> dev_{nullptr};
    const usb_config_desc_t* config_ = nullptr;     // quello della libreria, valido finche' e' aperto
    uint8_t indirizzo_ = 0;
    uint8_t mps0_ = 64;                              // bMaxPacketSize0
    uint8_t interfacciaControllo_ = 0;               // AudioControl: il byte basso di wIndex delle richieste
    Riempitore riempi_ = nullptr;
    void* ctx_ = nullptr;
    uint16_t vid_ = 0, pid_ = 0;
    Versione versione_ = Versione::UAC1;
    char prodotto_[MAX_NOME + 1] = {};
    uint32_t quirk_ = 0;
    Errore errore_ = Errore::NESSUNO;
    Esito esito_ = Esito::OK;
    Formato formato_{};
    Scelta scelta_{};
    Percorso percorso_{};
    Pacchetti pacchetti_;                            // solo il compito client, dopo l'avvio dello stream
    bool interfacciaPresa_ = false;                  // claim riuscito: va fatto release
    bool altImpostata_ = false;                      // SET_INTERFACE(alt) mandato: chiudi() rimanda la 0
    bool controlloBloccato_ = false;                 // una richiesta non ha risposto: controllo_ e' della libreria
    bool portaSpenta_ = false;                       // chiudi() ha spento la porta per sbloccare: va riaccesa
                                                     // (con chiudi(false) resta spenta)
    uint8_t nAvviati_ = 0;                           // trasferimenti isocroni partiti (ognuno da' fermi_ una volta)
    uint8_t nFermiVisti_ = 0;                        // quelli di cui chiudi() ha gia' preso fermi_
    usb_transfer_t* iso_[TRASFERIMENTI] = {};
    usb_transfer_t* controllo_ = nullptr;
    SemaphoreHandle_t fattoControllo_ = nullptr;    // lo da' finitoControllo
    SemaphoreHandle_t fermi_ = nullptr;             // contatore: un give per ogni trasferimento isocrono fermo
    std::atomic<bool> fermare_{false};              // chiudi() o scollegato(): non rimandare piu'
    std::atomic<bool> staccato_{false};
    std::atomic<int> inVolo_{0};
    std::atomic<bool> inCorso_{false};              // stream partito e non ancora fermo del tutto
    std::atomic<uint32_t> inizioTick_{0};
    std::atomic<uint32_t> msStreamFinale_{0};
    std::atomic<uint32_t> nTrasferimenti_{0}, nPacchetti_{0}, nSaltati_{0}, nErrori_{0};
    std::atomic<uint32_t> nFrameVeri_{0}, nFrameSilenzio_{0}, nInvioFallito_{0};
};

}  // namespace uac
