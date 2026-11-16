// Cuffie USB-C: un dispositivo USB Audio 1.0/2.0 sulla USB host di ESP-IDF, solo riproduzione (vedi uac_host.h).
//
// Il lavoro che aspetta (richieste di controllo, claim, release, close) gira nel compito di controllo delle
// cuffie; nel compito client girano solo i due callback dei trasferimenti (finito, finitoControllo), che non
// bloccano, non allocano e non loggano. I nomi fra parentesi sono dei sorgenti di ESP-IDF 5.5.1,
// components/usb: li cito perche' quasi tutto qui dipende da come la libreria fa le cose dentro.
//
// Richieste: USB Device Class Definition for Audio Devices 1.0 (5.2, A.9-A.10) e 2.0 (5.2, A.14-A.17), con le
// costanti di include/uapi/linux/usb/audio.h e include/linux/usb/audio-v2.h, scritte a mano.
#include "usb_audio/uac_host.h"

#include <cstring>
#include <new>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"

namespace uac {

namespace {

const char* TAG = "uac";

// ---- richieste ----
// bmRequestType: direzione (bit 7), tipo (bit 5-6: standard 0, classe 1), destinatario (interfaccia 1, endpoint 2).
constexpr uint8_t RT_STANDARD_INTERFACCIA_OUT = 0x01;
constexpr uint8_t RT_CLASSE_INTERFACCIA_OUT = 0x21;
constexpr uint8_t RT_CLASSE_INTERFACCIA_IN = 0xA1;
constexpr uint8_t RT_CLASSE_ENDPOINT_OUT = 0x22;

constexpr uint8_t RICHIESTA_SET_INTERFACE = 0x0B;  // USB 2.0, 9.4.10
// UAC1: SET_CUR, GET_MIN, GET_MAX, GET_RES. UAC2: CUR (SET o GET secondo la direzione) e RANGE.
constexpr uint8_t UAC1_SET_CUR = 0x01;
constexpr uint8_t UAC1_GET_MIN = 0x82;
constexpr uint8_t UAC1_GET_MAX = 0x83;
constexpr uint8_t UAC1_GET_RES = 0x84;
constexpr uint8_t UAC2_CUR = 0x01;
constexpr uint8_t UAC2_RANGE = 0x02;

// Selettori dei controlli (byte alto di wValue; il basso e' il canale, 0 = master).
constexpr uint8_t SEL_FREQUENZA = 0x01;   // UAC2 CS_SAM_FREQ_CONTROL; UAC1 SAMPLING_FREQ_CONTROL dell'endpoint
constexpr uint8_t SEL_CLOCK_VALIDO = 0x02;  // UAC2 CS_CLOCK_VALID_CONTROL
constexpr uint8_t SEL_SELETTORE = 0x01;   // UAC2 CX_CLOCK_SELECTOR_CONTROL
constexpr uint8_t SEL_MUTO = 0x01;        // MUTE_CONTROL (UAC1 e UAC2)
constexpr uint8_t SEL_VOLUME = 0x02;      // VOLUME_CONTROL (UAC1 e UAC2)

constexpr uint16_t valore(uint8_t selettore, uint8_t canale) { return uint16_t(selettore << 8 | canale); }

// ---- attese, in ms ----
constexpr uint32_t ATTESA_SCARICO_MS = 200;      // chiudi(): la coda (32 ms di audio) torna da sola
constexpr uint32_t ATTESA_BLOCCATO_MS = 1000;    // chiudi(): ancora un po' per il callback di una richiesta scaduta
constexpr uint32_t ATTESA_PORTA_MS = 1000;       // a porta spenta: i callback con NO_DEVICE
constexpr uint32_t ATTESA_DEMONE_MS = 20;        // fa finire al demone il giro su EP0 (vedi uac_host.h, priorita')
constexpr uint32_t ATTESA_RIACCENSIONE_MS = 1500;  // la porta si riaccende quando il demone l'ha recuperata
constexpr uint32_t PASSO_RIACCENSIONE_MS = 10;
constexpr int PROVE_INVIO = 10;                  // EP0 ancora fermo dopo uno STALL: si riprova
constexpr uint32_t PASSO_INVIO_MS = 2;
constexpr int PROVE_RILASCIO = 20;               // il compito client non ha ancora scalato i trasferimenti finiti
constexpr uint32_t PASSO_RILASCIO_MS = 5;
constexpr int PROVE_CLOCK_VALIDO = 5;            // UAC2: dopo SET CUR il clock puo' metterci un po' ad agganciarsi
constexpr uint32_t PASSO_CLOCK_VALIDO_MS = 10;

// _buffer_fill_isoc (hcd_dwc.c) fa assert(pacchetti x intervallo <= 64 - 3); qui l'intervallo e' sempre 1.
static_assert(PACCHETTI >= 1 && PACCHETTI <= 61, "l'HCD non accetta piu' di 61 pacchetti per trasferimento");
// Con 2 trasferimenti caricati nell'hardware (NUM_BUFFERS, hcd_dwc.c) ne servono altri in attesa, se no la coda
// si svuota a ogni giro e riparte 4 frame dopo (un buco che si sente).
static_assert(TRASFERIMENTI >= 3 && TRASFERIMENTI <= 255, "servono almeno 3 trasferimenti in coda");

// Almeno ms millisecondi (vTaskDelay(n) aspetta fra n-1 e n tick).
void aspettaMs(uint32_t ms) {
    if (ms) vTaskDelay(pdMS_TO_TICKS(ms) + 1);
}

uint32_t msDa(uint32_t tickInizio) { return uint32_t(pdTICKS_TO_MS(TickType_t(xTaskGetTickCount() - tickInizio))); }

uint32_t daLe32(const uint8_t* b) {
    return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}
int16_t daLe16(const uint8_t* b) { return static_cast<int16_t>(uint16_t(b[0] | b[1] << 8)); }

const char* testoSync(Sync s) {
    switch (s) {
    case Sync::ASINCRONA: return "asincrono";
    case Sync::ADATTIVA: return "adattivo";
    case Sync::SINCRONA: return "sincrono";
    default: return "senza sincronizzazione";
    }
}

void rigaLog(void*, const char* testo) { ESP_LOGI(TAG, "%s", testo); }

// La Configurazione (piu' di 5 KB) in PSRAM per la durata di apri(), mai sullo stack del compito.
struct ConfigurazioneInPsram {
    Configurazione* c = nullptr;
    ConfigurazioneInPsram() {
        void* m = heap_caps_malloc(sizeof(Configurazione), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (m) c = new (m) Configurazione;
    }
    ~ConfigurazioneInPsram() {
        if (c) {
            c->~Configurazione();
            heap_caps_free(c);
        }
    }
    ConfigurazioneInPsram(const ConfigurazioneInPsram&) = delete;
    ConfigurazioneInPsram& operator=(const ConfigurazioneInPsram&) = delete;
};

}  // namespace

// UAC2: il Clock Source trovato per ogni terminale streaming in uscita (cosi' i selettori si chiedono una volta).
struct Dispositivo::ClockTerminali {
    static constexpr size_t MAX = MAX_STREAMING * MAX_ALT;
    uint8_t n = 0;
    uint8_t terminale[MAX] = {};
    uint8_t sorgente[MAX] = {};
    uint8_t sorgenteDi(uint8_t t) const {
        for (uint8_t i = 0; i < n; i++)
            if (terminale[i] == t) return sorgente[i];
        return 0;
    }
    bool visto(uint8_t t) const {
        for (uint8_t i = 0; i < n; i++)
            if (terminale[i] == t) return true;
        return false;
    }
};

const char* testoErrore(Errore e) {
    switch (e) {
    case Errore::NESSUNO: return "ok";
    case Errore::DESCRITTORE: return "descrittori";
    case Errore::APERTURA: return "apertura";
    case Errore::CONTROLLO: return "richiesta rifiutata";
    case Errore::TEMPO: return "nessuna risposta";
    case Errore::CLOCK: return "clock non valido";
    case Errore::INTERFACCIA: return "interfaccia";
    case Errore::MEMORIA: return "memoria";
    case Errore::STREAM: return "stream";
    case Errore::SCOLLEGATO: return "scollegato";
    }
    return "?";
}

Dispositivo::~Dispositivo() { chiudi(); }

// ---------------------------------------------------------------------------------------------------------------
// apri()
// ---------------------------------------------------------------------------------------------------------------

void Dispositivo::azzera() {
    client_ = nullptr;
    config_ = nullptr;
    indirizzo_ = 0;
    mps0_ = 64;
    interfacciaControllo_ = 0;
    riempi_ = nullptr;
    ctx_ = nullptr;
    vid_ = pid_ = 0;
    versione_ = Versione::UAC1;
    prodotto_[0] = '\0';
    quirk_ = 0;
    errore_ = Errore::NESSUNO;
    esito_ = Esito::OK;
    formato_ = Formato{};
    scelta_ = Scelta{};
    percorso_ = Percorso{};
    interfacciaPresa_ = false;
    altImpostata_ = false;
    controlloBloccato_ = false;
    // portaSpenta_ no: la rimette a posto chiudi() (resta true solo se chiudi() non e' riuscito a chiudere).
    nAvviati_ = 0;
    nFermiVisti_ = 0;
    fermare_ = false;
    staccato_ = false;
    inVolo_ = 0;
    inCorso_ = false;
    inizioTick_ = 0;
    msStreamFinale_ = 0;
    nTrasferimenti_ = 0;
    nPacchetti_ = 0;
    nSaltati_ = 0;
    nErrori_ = 0;
    nFrameVeri_ = 0;
    nFrameSilenzio_ = 0;
    nInvioFallito_ = 0;
}

Errore Dispositivo::apri(usb_host_client_handle_t client, uint8_t indirizzo, Riempitore riempi, void* ctx) {
    if (dev_.load() != nullptr) {
        ESP_LOGW(TAG, "apri() con un dispositivo ancora aperto: prima lo chiudo");
        chiudi();
        if (dev_.load() != nullptr) {
            errore_ = Errore::APERTURA;
            return errore_;
        }
    }
    azzera();
    client_ = client;
    indirizzo_ = indirizzo;
    riempi_ = riempi;
    ctx_ = ctx;

    Errore e = (client != nullptr && riempi != nullptr && indirizzo != 0) ? apriDispositivo() : Errore::APERTURA;
    if (e == Errore::NESSUNO) e = analizzaEConfigura();
    if (e == Errore::NESSUNO) e = avviaStream();
    errore_ = e;
    if (e != Errore::NESSUNO) {
        if (e == Errore::DESCRITTORE)
            ESP_LOGW(TAG, "%04x:%04x non usabile: %s", vid_, pid_, testoEsito(esito_));
        else
            ESP_LOGW(TAG, "%04x:%04x non parte: %s", vid_, pid_, testoErrore(e));
        chiudi();
    }
    return e;
}

Errore Dispositivo::apriDispositivo() {
    usb_device_handle_t dev = nullptr;
    const esp_err_t r = usb_host_device_open(client_, indirizzo_, &dev);
    if (r != ESP_OK || dev == nullptr) {
        ESP_LOGW(TAG, "usb_host_device_open(%u): %s", unsigned(indirizzo_), esp_err_to_name(r));
        // usbh_devs_open: NOT_FOUND = gia' liberato, INVALID_STATE = gia' staccato (o in liberazione).
        if (r == ESP_ERR_NOT_FOUND || r == ESP_ERR_INVALID_STATE) {
            staccato_ = true;
            return Errore::SCOLLEGATO;
        }
        return Errore::APERTURA;
    }
    dev_.store(dev);

    // Descrittori in cache della libreria (letti all'enumerazione): nessuna richiesta sul bus.
    const usb_device_desc_t* dd = nullptr;
    if (usb_host_get_device_descriptor(dev, &dd) != ESP_OK || dd == nullptr) return Errore::APERTURA;
    vid_ = dd->idVendor;
    pid_ = dd->idProduct;
    const uint8_t mps0 = dd->bMaxPacketSize0;
    // 64 e' multiplo di tutti i valori leciti: va bene anche se il descrittore dice una cosa strana.
    mps0_ = (mps0 == 8 || mps0 == 16 || mps0 == 32 || mps0 == 64) ? mps0 : 64;
    if (usb_host_get_active_config_descriptor(dev, &config_) != ESP_OK || config_ == nullptr) {
        config_ = nullptr;
        return Errore::APERTURA;
    }

    usb_device_info_t info;
    std::memset(&info, 0, sizeof(info));
    const bool conInfo = usb_host_device_info(dev, &info) == ESP_OK;
    if (conInfo && info.str_desc_product != nullptr) {
        const uint8_t* s = reinterpret_cast<const uint8_t*>(info.str_desc_product);
        testoStringa(s, s[0], prodotto_, sizeof(prodotto_));
    }
    quirk_ = trovaQuirk(vid_, pid_);
    ESP_LOGI(TAG, "dispositivo %04x:%04x \"%s\": indirizzo %u, %s speed, EP0 %u byte, configurazione %u di %u byte, "
             "quirk 0x%x", vid_, pid_, prodotto_, unsigned(indirizzo_),
             !conInfo ? "?" : info.speed == USB_SPEED_LOW ? "low" : info.speed == USB_SPEED_FULL ? "full" : "high",
             unsigned(mps0_), unsigned(config_->bConfigurationValue), unsigned(config_->wTotalLength),
             unsigned(quirk_));

    // La richiesta di controllo: setup + MAX_CONTROLLO byte di dati, nella RAM interna DMA.
    fattoControllo_ = xSemaphoreCreateBinary();
    fermi_ = xSemaphoreCreateCounting(TRASFERIMENTI, 0);
    if (fattoControllo_ == nullptr || fermi_ == nullptr) return Errore::MEMORIA;
    if (usb_host_transfer_alloc(USB_SETUP_PACKET_SIZE + MAX_CONTROLLO, 0, &controllo_) != ESP_OK ||
        controllo_ == nullptr) {
        controllo_ = nullptr;
        ESP_LOGE(TAG, "RAM interna DMA esaurita per la richiesta di controllo");
        return Errore::MEMORIA;
    }
    controllo_->device_handle = dev;
    controllo_->bEndpointAddress = 0;
    controllo_->callback = finitoControllo;
    controllo_->context = this;
    controllo_->flags = 0;
    controllo_->timeout_ms = 0;
    return Errore::NESSUNO;
}

Errore Dispositivo::analizzaEConfigura() {
    ConfigurazioneInPsram cfg;
    if (cfg.c == nullptr) {
        ESP_LOGE(TAG, "niente PSRAM per i descrittori (%u byte)", unsigned(sizeof(Configurazione)));
        return Errore::MEMORIA;
    }
    Configurazione& c = *cfg.c;
    esito_ = analizza(reinterpret_cast<const uint8_t*>(config_), config_->wTotalLength, c);
    if (esito_ != Esito::OK) return Errore::DESCRITTORE;
    versione_ = c.versione;
    interfacciaControllo_ = c.interfacciaControllo;
    descrivi(c, rigaLog, nullptr);
    if (c.troncata) ESP_LOGW(TAG, "descrittori oltre i limiti del modello: il resto e' ignorato");

    ClockTerminali clock;
    if (c.versione == Versione::UAC2) {
        const Errore e = frequenzeUac2(c, clock);
        if (e != Errore::NESSUNO) return e;
    }
    esito_ = scegli(c, quirk_, scelta_);
    if (esito_ != Esito::OK) return Errore::DESCRITTORE;
    if (!percorso(c, scelta_.terminale, percorso_)) {
        percorso_ = Percorso{};
        ESP_LOGW(TAG, "il terminale %u non arriva a nessuna uscita: niente Feature Unit", unsigned(scelta_.terminale));
    }
    formato_.hz = scelta_.hz;
    formato_.canali = scelta_.canali;
    formato_.byteCampione = scelta_.byteCampione;
    formato_.bit = scelta_.bit;
    ESP_LOGI(TAG, "scelta: interfaccia %u alt %u, %u Hz, %u canali, %u bit in %u byte, ep 0x%02x da %u byte, %s; "
             "uscita 0x%04x, Feature Unit %u, clock %u", unsigned(scelta_.interfaccia), unsigned(scelta_.alt),
             unsigned(scelta_.hz), unsigned(scelta_.canali), unsigned(scelta_.bit), unsigned(scelta_.byteCampione),
             unsigned(scelta_.endpoint), unsigned(scelta_.maxPacchetto), testoSync(scelta_.sync),
             unsigned(percorso_.tipoUscita), unsigned(percorso_.featureUnit), unsigned(percorso_.clock));

    Errore e;
    if (versione_ == Versione::UAC2) {
        // Prima la frequenza sul clock, poi l'alternativa (come Linux); al contrario con QUIRK_INTERFACCIA_PRIMA.
        const bool interfacciaPrima = (quirk_ & QUIRK_INTERFACCIA_PRIMA) != 0;
        if (interfacciaPrima && (e = prendiInterfaccia()) != Errore::NESSUNO) return e;
        if ((e = impostaClockUac2(c, clock.sorgenteDi(scelta_.terminale))) != Errore::NESSUNO) return e;
        if (!interfacciaPrima && (e = prendiInterfaccia()) != Errore::NESSUNO) return e;
    } else {
        if ((e = prendiInterfaccia()) != Errore::NESSUNO) return e;
        if ((e = impostaFrequenzaUac1(c)) != Errore::NESSUNO) return e;
    }
    return preparaFeatureUnit(c);
}

// UAC2: le frequenze stanno nel clock. Per ogni terminale streaming in uscita: il suo Clock Source (seguendo
// selettori e moltiplicatori), GET RANGE della frequenza, e le frequenze copiate nelle sue alternative.
Errore Dispositivo::frequenzeUac2(Configurazione& c, ClockTerminali& clock) {
    for (uint8_t s = 0; s < c.nStreaming; s++) {
        const Streaming& st = c.streaming[s];
        if (!st.uscita) continue;
        for (uint8_t a = 0; a < st.nAlt; a++) {
            const uint8_t t = st.alt[a].terminale;
            if (clock.visto(t)) continue;
            const Entita* it = trovaEntita(c, t);
            const uint8_t sorgente = it ? sorgenteClock(c, it->clock, pinSelettore, this) : 0;
            Errore e = grave();
            if (e != Errore::NESSUNO) return e;
            if (clock.n < ClockTerminali::MAX) {
                clock.terminale[clock.n] = t;
                clock.sorgente[clock.n] = sorgente;
                clock.n++;
            }
            Frequenze f;
            bool ok = false;
            if (sorgente != 0) {
                uint8_t buf[MAX_CONTROLLO];
                uint16_t lun = 0;
                ok = leggiRange(valore(SEL_FREQUENZA, 0), uint16_t(sorgente << 8 | interfacciaControllo_), 12, buf,
                                lun) &&
                     analizzaRangeFrequenze(buf, lun, f) && f.n > 0;
                e = grave();
                if (e != Errore::NESSUNO) return e;
            }
            if (!ok) {
                // Senza RANGE si prova la frequenza che hanno tutte le cuffie USB.
                f.n = 0;
                f.aggiungi(48000, 48000);
                ESP_LOGW(TAG, "terminale %u, clock %u: frequenze non lette, provo solo 48000 Hz", unsigned(t),
                         unsigned(sorgente));
            } else {
                for (uint8_t i = 0; i < f.n; i++)
                    ESP_LOGI(TAG, "terminale %u, clock %u: %u-%u Hz passo %u", unsigned(t), unsigned(sorgente),
                             unsigned(f.voci[i].min), unsigned(f.voci[i].max), unsigned(f.voci[i].passo));
            }
            impostaFrequenzeClock(c, t, f);
        }
    }
    return Errore::NESSUNO;
}

uint8_t Dispositivo::pinSelettore(void* ctx, uint8_t idSelettore) {
    // Solo GET: un selettore non si tocca mai (P1 suona sul clock che il dispositivo ha gia' scelto; per il
    // Samsung 04e8:a051 un SET lo manderebbe in tilt, QUIRK_NON_TOCCARE_SELETTORE).
    Dispositivo* d = static_cast<Dispositivo*>(ctx);
    uint8_t pin = 0;
    uint16_t fatti = 0;
    const Errore e = d->richiesta(RT_CLASSE_INTERFACCIA_IN, UAC2_CUR, valore(SEL_SELETTORE, 0),
                                  uint16_t(idSelettore << 8 | d->interfacciaControllo_), &pin, 1, &fatti);
    if (e != Errore::NESSUNO || fatti < 1) {
        ESP_LOGW(TAG, "selettore di clock %u: ingresso non letto (%s), prendo il primo", unsigned(idSelettore),
                 testoErrore(e));
        return 0;
    }
    ESP_LOGI(TAG, "selettore di clock %u: ingresso %u", unsigned(idSelettore), unsigned(pin));
    return pin;
}

// Come set_sample_rate_v2v3 di Linux: si legge la frequenza attuale e si cambia solo se e' diversa (e il clock
// e' programmabile); poi CLOCK_VALID, se il clock ce l'ha. Dopo il SET la frequenza non si rilegge (Linux si'):
// una lettura senza risposta fermerebbe apri() con TEMPO per un valore che andrebbe solo nel log (uac_host.h,
// passo 3). Il controllo vero e' CLOCK_VALID.
Errore Dispositivo::impostaClockUac2(const Configurazione& c, uint8_t sorgente) {
    if (sorgente == 0) {
        ESP_LOGW(TAG, "terminale %u senza Clock Source: la frequenza resta quella del dispositivo",
                 unsigned(scelta_.terminale));
        return Errore::NESSUNO;
    }
    const Entita* cs = trovaEntita(c, sorgente);
    const uint8_t controlli = cs ? cs->controlliClock : 0;
    const bool programmabile = (controlli & 0x03) == 0x03;   // bmControls D1..0: frequenza
    const bool conValidita = ((controlli >> 2) & 0x03) != 0;  // bmControls D3..2: validita'
    const uint16_t wIndex = uint16_t(sorgente << 8 | interfacciaControllo_);
    uint8_t b[4] = {};
    uint16_t fatti = 0;

    Errore e = richiesta(RT_CLASSE_INTERFACCIA_IN, UAC2_CUR, valore(SEL_FREQUENZA, 0), wIndex, b, 4, &fatti);
    if (e == Errore::SCOLLEGATO || e == Errore::TEMPO) return e;
    const uint32_t prima = (e == Errore::NESSUNO && fatti == 4) ? daLe32(b) : 0;
    if (prima != scelta_.hz) {
        if (programmabile) {
            b[0] = uint8_t(scelta_.hz);
            b[1] = uint8_t(scelta_.hz >> 8);
            b[2] = uint8_t(scelta_.hz >> 16);
            b[3] = uint8_t(scelta_.hz >> 24);
            e = richiesta(RT_CLASSE_INTERFACCIA_OUT, UAC2_CUR, valore(SEL_FREQUENZA, 0), wIndex, b, 4);
            if (e != Errore::NESSUNO) {
                ESP_LOGE(TAG, "clock %u: SET CUR %u Hz %s", unsigned(sorgente), unsigned(scelta_.hz),
                         testoErrore(e));
                return e;
            }
        } else if (prima != 0) {
            ESP_LOGE(TAG, "clock %u fisso a %u Hz, non a %u", unsigned(sorgente), unsigned(prima),
                     unsigned(scelta_.hz));
            return Errore::CLOCK;
        } else {
            ESP_LOGW(TAG, "clock %u non programmabile e frequenza non letta: provo lo stesso", unsigned(sorgente));
        }
    }
    if (conValidita) {
        for (int prova = 1;; prova++) {
            uint8_t valido = 0;
            e = richiesta(RT_CLASSE_INTERFACCIA_IN, UAC2_CUR, valore(SEL_CLOCK_VALIDO, 0), wIndex, &valido, 1,
                          &fatti);
            if (e == Errore::SCOLLEGATO || e == Errore::TEMPO) return e;
            if (e != Errore::NESSUNO || fatti < 1) {
                ESP_LOGW(TAG, "clock %u: CLOCK_VALID non letto (%s), vado avanti", unsigned(sorgente),
                         testoErrore(e));
                break;
            }
            if (valido) break;
            if (prova >= PROVE_CLOCK_VALIDO) {
                ESP_LOGE(TAG, "clock %u non valido a %u Hz", unsigned(sorgente), unsigned(scelta_.hz));
                return Errore::CLOCK;
            }
            aspettaMs(PASSO_CLOCK_VALIDO_MS);
        }
    }
    ESP_LOGI(TAG, "clock %u a %u Hz", unsigned(sorgente), unsigned(scelta_.hz));
    return Errore::NESSUNO;
}

// UAC1: la frequenza va all'endpoint (SET_CUR di SAMPLING_FREQ_CONTROL, 3 byte), dopo SET_INTERFACE. Dopo un
// SET_CUR riuscito non si rilegge con GET_CUR: c'e' chi non risponde (Linux la salta per quei dispositivi,
// QUIRK_FLAG_GET_SAMPLE_RATE), e una lettura senza risposta fermerebbe apri() con TEMPO per un valore che
// andrebbe solo nel log.
Errore Dispositivo::impostaFrequenzaUac1(const Configurazione& c) {
    if (!scelta_.frequenzaSuEndpoint) return Errore::NESSUNO;
    uint8_t b[3] = {uint8_t(scelta_.hz), uint8_t(scelta_.hz >> 8), uint8_t(scelta_.hz >> 16)};
    Errore e = richiesta(RT_CLASSE_ENDPOINT_OUT, UAC1_SET_CUR, valore(SEL_FREQUENZA, 0), scelta_.endpoint, b, 3);
    if (e == Errore::SCOLLEGATO || e == Errore::TEMPO) return e;
    if (e != Errore::NESSUNO) {
        // Con una frequenza sola non c'e' niente da scegliere: si va avanti lo stesso.
        const bool indiciBuoni = scelta_.indiceStreaming < c.nStreaming &&
                                 scelta_.indiceAlt < c.streaming[scelta_.indiceStreaming].nAlt;
        const Frequenze* f =
            indiciBuoni ? &c.streaming[scelta_.indiceStreaming].alt[scelta_.indiceAlt].frequenze : nullptr;
        if (f != nullptr && f->n == 1 && f->voci[0].min == f->voci[0].max) {
            ESP_LOGW(TAG, "SET_CUR della frequenza rifiutato, ma l'alternativa ha solo %u Hz: vado avanti",
                     unsigned(scelta_.hz));
            return Errore::NESSUNO;
        }
        ESP_LOGE(TAG, "SET_CUR %u Hz sull'endpoint 0x%02x: %s", unsigned(scelta_.hz), unsigned(scelta_.endpoint),
                 testoErrore(e));
        return e;
    }
    return Errore::NESSUNO;
}

// claim (la libreria alloca le pipe dell'alternativa e controlla wMaxPacketSize contro la FIFO) e SET_INTERFACE,
// che la libreria non manda (usb_host.c, interface_claim).
Errore Dispositivo::prendiInterfaccia() {
    const esp_err_t r = usb_host_interface_claim(client_, dev_.load(), scelta_.interfaccia, scelta_.alt);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "claim dell'interfaccia %u alt %u: %s", unsigned(scelta_.interfaccia), unsigned(scelta_.alt),
                 esp_err_to_name(r));
        return Errore::INTERFACCIA;
    }
    interfacciaPresa_ = true;
    altImpostata_ = true;  // anche se la richiesta qui sotto fallisce, chiudi() rimanda la 0: non fa male
    const Errore e =
        richiesta(RT_STANDARD_INTERFACCIA_OUT, RICHIESTA_SET_INTERFACE, scelta_.alt, scelta_.interfaccia, nullptr, 0);
    if (e == Errore::NESSUNO) return e;
    ESP_LOGE(TAG, "SET_INTERFACE(%u, alt %u): %s", unsigned(scelta_.interfaccia), unsigned(scelta_.alt),
             testoErrore(e));
    return (e == Errore::SCOLLEGATO || e == Errore::TEMPO) ? e : Errore::INTERFACCIA;
}

// Volume a min(MAX, 0 dB) e muto tolto su ogni canale della Feature Unit che li ha. Gli errori si scrivono nel
// log e basta: il volume vero lo fa il software e lo stream puo' suonare lo stesso.
Errore Dispositivo::preparaFeatureUnit(const Configurazione& c) {
    if (percorso_.featureUnit == 0) {
        ESP_LOGI(TAG, "nessuna Feature Unit sul percorso");
        return Errore::NESSUNO;
    }
    const Entita* fu = trovaEntita(c, percorso_.featureUnit);
    if (fu == nullptr) return Errore::NESSUNO;
    const uint16_t wIndex = uint16_t(fu->id << 8 | interfacciaControllo_);
    const uint8_t setCur = versione_ == Versione::UAC2 ? UAC2_CUR : UAC1_SET_CUR;  // tutti e due 0x01
    const uint8_t ultimo = fu->canali < MAX_CANALI_FU ? fu->canali : uint8_t(MAX_CANALI_FU);
    for (uint8_t ch = 0; ch <= ultimo; ch++) {
        const uint16_t bit = uint16_t(1u << ch);
        if (fu->volume & bit) {
            RangeVolume r;
            if (leggiRangeVolume(wIndex, ch, r)) {
                const int16_t v = volumeZeroDb(r);
                uint8_t b[2] = {uint8_t(uint16_t(v)), uint8_t(uint16_t(v) >> 8)};
                const Errore e = richiesta(RT_CLASSE_INTERFACCIA_OUT, setCur, valore(SEL_VOLUME, ch), wIndex, b, 2);
                ESP_LOGI(TAG, "Feature Unit %u canale %u: volume %d/256 dB (da %d a %d, passo %d): %s",
                         unsigned(fu->id), unsigned(ch), int(v), int(r.min), int(r.max), int(r.passo),
                         testoErrore(e));
            } else {
                ESP_LOGW(TAG, "Feature Unit %u canale %u: range del volume non letto", unsigned(fu->id),
                         unsigned(ch));
            }
            const Errore g = grave();
            if (g != Errore::NESSUNO) return g;
        }
        if (fu->muto & bit) {
            uint8_t zero = 0;
            const Errore e = richiesta(RT_CLASSE_INTERFACCIA_OUT, setCur, valore(SEL_MUTO, ch), wIndex, &zero, 1);
            if (e != Errore::NESSUNO)
                ESP_LOGW(TAG, "Feature Unit %u canale %u: muto non tolto (%s)", unsigned(fu->id), unsigned(ch),
                         testoErrore(e));
            const Errore g = grave();
            if (g != Errore::NESSUNO) return g;
        }
    }
    return Errore::NESSUNO;
}

bool Dispositivo::leggiRangeVolume(uint16_t wIndex, uint8_t canale, RangeVolume& r) {
    r = RangeVolume{};
    if (versione_ == Versione::UAC2) {
        uint8_t buf[MAX_CONTROLLO];
        uint16_t lun = 0;
        return leggiRange(valore(SEL_VOLUME, canale), wIndex, 6, buf, lun) && analizzaRangeVolume(buf, lun, r);
    }
    // UAC1 non ha RANGE: minimo, massimo e passo uno per uno (2 byte con segno, 1/256 dB).
    uint8_t b[2] = {};
    uint16_t fatti = 0;
    if (richiesta(RT_CLASSE_INTERFACCIA_IN, UAC1_GET_MIN, valore(SEL_VOLUME, canale), wIndex, b, 2, &fatti) !=
            Errore::NESSUNO ||
        fatti < 2)
        return false;
    r.min = daLe16(b);
    if (richiesta(RT_CLASSE_INTERFACCIA_IN, UAC1_GET_MAX, valore(SEL_VOLUME, canale), wIndex, b, 2, &fatti) !=
            Errore::NESSUNO ||
        fatti < 2)
        return false;
    r.max = daLe16(b);
    if (richiesta(RT_CLASSE_INTERFACCIA_IN, UAC1_GET_RES, valore(SEL_VOLUME, canale), wIndex, b, 2, &fatti) ==
            Errore::NESSUNO &&
        fatti >= 2)
        r.passo = daLe16(b);
    return true;
}

// Prima 2 byte (wNumSubRanges), poi 2 + n x voce byte, al massimo MAX_CONTROLLO (voci intere): cosi' fa anche
// Linux, e nessun dispositivo riceve una wLength che non si aspetta. Se la risposta e' tagliata (dal limite qui o
// dal dispositivo) si tengono le voci intere arrivate e il conteggio si corregge.
bool Dispositivo::leggiRange(uint16_t wValue, uint16_t wIndex, size_t voce, uint8_t* buf, uint16_t& lun) {
    lun = 0;
    uint16_t fatti = 0;
    if (richiesta(RT_CLASSE_INTERFACCIA_IN, UAC2_RANGE, wValue, wIndex, buf, 2, &fatti) != Errore::NESSUNO ||
        fatti < 2)
        return false;
    const size_t n = size_t(buf[0]) | size_t(buf[1]) << 8;
    if (n == 0) return false;
    const size_t massimo = 2 + ((MAX_CONTROLLO - 2) / voce) * voce;
    const size_t chiesti = (2 + n * voce < massimo) ? 2 + n * voce : massimo;
    if (richiesta(RT_CLASSE_INTERFACCIA_IN, UAC2_RANGE, wValue, wIndex, buf, uint16_t(chiesti), &fatti) !=
            Errore::NESSUNO ||
        fatti < 2 + voce)
        return false;
    const size_t intere = (size_t(fatti) - 2) / voce;
    if (intere < n) {
        buf[0] = uint8_t(intere);
        buf[1] = uint8_t(intere >> 8);
    }
    lun = uint16_t(2 + intere * voce);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// stream
// ---------------------------------------------------------------------------------------------------------------

Errore Dispositivo::avviaStream() {
    pacchetti_.configura(formato_.hz);
    const size_t byteFrame = formato_.byteFrame();
    const size_t massimo = size_t(pacchetti_.massimo()) * byteFrame;  // il pacchetto piu' lungo
    if (byteFrame == 0 || massimo == 0 || massimo > scelta_.maxPacchetto) {
        ESP_LOGE(TAG, "pacchetto da %u byte con wMaxPacketSize %u", unsigned(massimo), unsigned(scelta_.maxPacchetto));
        return Errore::STREAM;
    }
    const size_t byte = massimo * PACCHETTI;
    for (int i = 0; i < TRASFERIMENTI; i++) {
        if (usb_host_transfer_alloc(byte, PACCHETTI, &iso_[i]) != ESP_OK || iso_[i] == nullptr) {
            iso_[i] = nullptr;
            ESP_LOGE(TAG, "RAM interna DMA esaurita per i trasferimenti (%d x %u byte)", TRASFERIMENTI,
                     unsigned(byte));
            return Errore::MEMORIA;
        }
        usb_transfer_t* t = iso_[i];
        t->device_handle = dev_.load();
        t->bEndpointAddress = scelta_.endpoint;
        t->callback = finito;
        t->context = this;
        t->flags = 0;
        t->timeout_ms = 0;
    }
    // Tutti pieni di zeri prima di mandarne uno: da qui in poi Pacchetti lo usa solo il compito client. I
    // pacchetti stanno uno dopo l'altro nel buffer (l'HCD li prende cosi', _buffer_fill_isoc) e num_bytes deve
    // essere la loro somma (usbh.c, transfer_check_usb_compliance).
    for (int i = 0; i < TRASFERIMENTI; i++) {
        usb_transfer_t* t = iso_[i];
        uint32_t frame = 0;
        int totale = 0;
        for (int p = 0; p < t->num_isoc_packets; p++) {
            const uint16_t f = pacchetti_.prossimo();
            t->isoc_packet_desc[p].num_bytes = int(f) * int(byteFrame);
            t->isoc_packet_desc[p].actual_num_bytes = 0;
            t->isoc_packet_desc[p].status = USB_TRANSFER_STATUS_COMPLETED;
            frame += f;
            totale += int(f) * int(byteFrame);
        }
        std::memset(t->data_buffer, 0, size_t(totale));
        t->num_bytes = totale;
        nFrameSilenzio_.fetch_add(frame, std::memory_order_relaxed);
    }
    inizioTick_ = uint32_t(xTaskGetTickCount());
    inCorso_ = true;
    for (int i = 0; i < TRASFERIMENTI; i++) {
        inVolo_.fetch_add(1);  // prima dell'invio: il callback puo' gia' scalarlo
        const esp_err_t r = usb_host_transfer_submit(iso_[i]);
        if (r != ESP_OK) {
            if (inVolo_.fetch_sub(1) == 1) {
                msStreamFinale_ = msDa(inizioTick_.load());
                inCorso_ = false;
            }
            ESP_LOGE(TAG, "invio del trasferimento isocrono %d: %s", i, esp_err_to_name(r));
            const Errore g = grave();
            return g != Errore::NESSUNO ? g : Errore::STREAM;
        }
        nAvviati_++;
    }
    ESP_LOGI(TAG, "stream partito: %d trasferimenti da %d pacchetti, fino a %u byte per pacchetto", TRASFERIMENTI,
             PACCHETTI, unsigned(massimo));
    return Errore::NESSUNO;
}

// Compito client, una volta per trasferimento isocrono finito. Niente attese, allocazioni o log: e' il percorso
// in tempo reale (al massimo ~24 ms di margine prima che la coda si svuoti).
void Dispositivo::finito(usb_transfer_t* t) {
    Dispositivo* d = static_cast<Dispositivo*>(t->context);
    bool ferma = false;
    switch (t->status) {
    case USB_TRANSFER_STATUS_COMPLETED: {
        // SKIPPED = il frame e' passato prima che il controller arrivasse a quel pacchetto (interrupt in ritardo):
        // non e' un errore (usb_types_stack.h). Lo stato dei pacchetti c'e' solo con il trasferimento COMPLETED.
        uint32_t mandati = 0, saltati = 0, errati = 0;
        for (int i = 0; i < t->num_isoc_packets; i++) {
            switch (t->isoc_packet_desc[i].status) {
            case USB_TRANSFER_STATUS_COMPLETED: mandati++; break;
            case USB_TRANSFER_STATUS_SKIPPED: saltati++; break;
            default: errati++; break;
            }
        }
        d->nTrasferimenti_.fetch_add(1, std::memory_order_relaxed);
        d->nPacchetti_.fetch_add(mandati, std::memory_order_relaxed);
        if (saltati) d->nSaltati_.fetch_add(saltati, std::memory_order_relaxed);
        if (errati) d->nErrori_.fetch_add(1, std::memory_order_relaxed);
        break;
    }
    case USB_TRANSFER_STATUS_NO_DEVICE:
        // La libreria ha chiuso la pipe perche' la porta non c'e' piu' (cuffie staccate): DEV_GONE arriva dopo.
        d->staccato_ = true;
        ferma = true;
        break;
    case USB_TRANSFER_STATUS_CANCELED:
        ferma = true;
        break;
    default:
        // ERROR, STALL, OVERFLOW: errore del canale. La pipe e' ferma (halted) e la libreria ha gia' cancellato
        // gli altri trasferimenti (usb_host.c, _handle_pending_ep): rimandarlo fallirebbe e basta.
        d->nErrori_.fetch_add(1, std::memory_order_relaxed);
        ferma = true;
        break;
    }
    if (!ferma && (d->fermare_.load() || d->staccato_.load())) ferma = true;
    if (!ferma) {
        const int byteFrame = d->formato_.byteFrame();
        uint32_t frame = 0;
        int totale = 0;
        for (int i = 0; i < t->num_isoc_packets; i++) {
            const uint16_t f = d->pacchetti_.prossimo();
            t->isoc_packet_desc[i].num_bytes = int(f) * byteFrame;
            frame += f;
            totale += int(f) * byteFrame;
        }
        size_t veri = d->riempi_(d->ctx_, t->data_buffer, frame, d->formato_);
        if (veri > frame) veri = frame;
        d->nFrameVeri_.fetch_add(uint32_t(veri), std::memory_order_relaxed);
        d->nFrameSilenzio_.fetch_add(frame - uint32_t(veri), std::memory_order_relaxed);
        t->num_bytes = totale;
        if (usb_host_transfer_submit(t) != ESP_OK) {
            d->nInvioFallito_.fetch_add(1, std::memory_order_relaxed);
            ferma = true;
        }
    }
    if (ferma) {
        if (d->inVolo_.fetch_sub(1) == 1) {
            d->msStreamFinale_ = msDa(d->inizioTick_.load());
            d->inCorso_ = false;
        }
        xSemaphoreGive(d->fermi_);  // per ultimo: dopo questo chiudi() puo' liberare tutto
    }
}

// ---------------------------------------------------------------------------------------------------------------
// richieste di controllo
// ---------------------------------------------------------------------------------------------------------------

void Dispositivo::finitoControllo(usb_transfer_t* t) {
    // Compito client. Lo stato lo legge chi aspetta, dopo il semaforo.
    Dispositivo* d = static_cast<Dispositivo*>(t->context);
    xSemaphoreGive(d->fattoControllo_);
}

Errore Dispositivo::richiesta(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
                              uint8_t* dati, uint16_t n, uint16_t* fatti) {
    if (fatti) *fatti = 0;
    usb_transfer_t* t = controllo_;
    if (dev_.load() == nullptr || t == nullptr || fattoControllo_ == nullptr || n > MAX_CONTROLLO ||
        (n > 0 && dati == nullptr))
        return Errore::CONTROLLO;
    if (staccato_) return Errore::SCOLLEGATO;
    if (controlloBloccato_) {
        // Una richiesta di prima e' scaduta: il trasferimento torna nostro solo quando il suo callback e' arrivato.
        if (xSemaphoreTake(fattoControllo_, 0) != pdTRUE) return Errore::TEMPO;
        controlloBloccato_ = false;
        if (t->status == USB_TRANSFER_STATUS_NO_DEVICE) {
            staccato_ = true;
            return Errore::SCOLLEGATO;
        }
    }
    const bool in = (bmRequestType & USB_BM_REQUEST_TYPE_DIR_IN) != 0;
    uint8_t* b = t->data_buffer;
    b[0] = bmRequestType;
    b[1] = bRequest;
    b[2] = uint8_t(wValue);
    b[3] = uint8_t(wValue >> 8);
    b[4] = uint8_t(wIndex);
    b[5] = uint8_t(wIndex >> 8);
    b[6] = uint8_t(n);
    b[7] = uint8_t(n >> 8);
    if (!in && n > 0) std::memcpy(b + USB_SETUP_PACKET_SIZE, dati, n);
    // OUT: esattamente setup + wLength; IN: la fase dati arrotondata a bMaxPacketSize0 (usbh.c,
    // transfer_check_usb_compliance: num_bytes >= 8 + wLength). Con n <= 256 sta nel buffer da 8 + 256.
    t->num_bytes = int(USB_SETUP_PACKET_SIZE) + (in ? usb_round_up_to_mps(n, mps0_) : int(n));

    esp_err_t r = ESP_FAIL;
    for (int prova = 0; prova < PROVE_INVIO; prova++) {
        r = usb_host_transfer_submit_control(client_, t);
        // INVALID_STATE: EP0 e' ancora fermo dopo lo STALL di prima, finche' il demone non lo rimette attivo.
        if (r != ESP_ERR_INVALID_STATE || staccato_) break;
        aspettaMs(PASSO_INVIO_MS);
    }
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "richiesta %02x %02x %04x %04x non mandata: %s", unsigned(bmRequestType), unsigned(bRequest),
                 unsigned(wValue), unsigned(wIndex), esp_err_to_name(r));
        return staccato_ ? Errore::SCOLLEGATO : Errore::CONTROLLO;
    }
    if (xSemaphoreTake(fattoControllo_, pdMS_TO_TICKS(TIMEOUT_CONTROLLO_MS)) != pdTRUE) {
        // Il trasferimento resta della libreria finche' il callback non arriva: niente free e niente close.
        controlloBloccato_ = true;
        ESP_LOGE(TAG, "richiesta %02x %02x %04x %04x senza risposta dopo %u ms", unsigned(bmRequestType),
                 unsigned(bRequest), unsigned(wValue), unsigned(wIndex), unsigned(TIMEOUT_CONTROLLO_MS));
        return Errore::TEMPO;
    }
    Errore e;
    switch (t->status) {
    case USB_TRANSFER_STATUS_COMPLETED: e = Errore::NESSUNO; break;
    case USB_TRANSFER_STATUS_NO_DEVICE:
        staccato_ = true;
        e = Errore::SCOLLEGATO;
        break;
    default: e = Errore::CONTROLLO; break;  // STALL (richiesta rifiutata), ERROR, OVERFLOW, CANCELED
    }
    if (e == Errore::NESSUNO) {
        int ricevuti = t->actual_num_bytes - int(USB_SETUP_PACKET_SIZE);
        if (ricevuti < 0) ricevuti = 0;
        if (ricevuti > int(n)) ricevuti = n;  // piu' di wLength non dovrebbe arrivare: si tiene wLength
        if (in) {
            if (ricevuti > 0) std::memcpy(dati, b + USB_SETUP_PACKET_SIZE, size_t(ricevuti));
            if (fatti) *fatti = uint16_t(ricevuti);
        } else if (fatti) {
            *fatti = n;
        }
    } else if (e == Errore::CONTROLLO) {
        // Dopo uno STALL il demone deve ancora rimettere EP0 attivo (usbh.c, handle_ep0_clear): un attimo per lui.
        aspettaMs(PASSO_INVIO_MS);
    }
    if ((bmRequestType & USB_BM_REQUEST_TYPE_TYPE_MASK) == USB_BM_REQUEST_TYPE_TYPE_CLASS)
        aspettaMs(pausaRichiesteMs(quirk_));
    return e;
}

Errore Dispositivo::grave() const {
    if (staccato_) return Errore::SCOLLEGATO;
    if (controlloBloccato_) return Errore::TEMPO;
    return Errore::NESSUNO;
}

// ---------------------------------------------------------------------------------------------------------------
// chiudi()
// ---------------------------------------------------------------------------------------------------------------

void Dispositivo::aspettaFermi(uint32_t ms) {
    const TickType_t inizio = xTaskGetTickCount();
    const TickType_t durata = pdMS_TO_TICKS(ms);
    while (nFermiVisti_ < nAvviati_) {
        const TickType_t passati = xTaskGetTickCount() - inizio;
        const TickType_t resto = passati < durata ? durata - passati : 0;
        if (xSemaphoreTake(fermi_, resto) != pdTRUE) break;
        nFermiVisti_++;
    }
}

void Dispositivo::aspettaControllo(uint32_t ms) {
    if (!controlloBloccato_) return;
    if (xSemaphoreTake(fattoControllo_, pdMS_TO_TICKS(ms)) != pdTRUE) return;
    controlloBloccato_ = false;
    if (controllo_->status == USB_TRANSFER_STATUS_NO_DEVICE) staccato_ = true;
}

// Ultima risorsa, quando la libreria tiene qualcosa che non restituisce (una richiesta senza risposta): si
// spegne la porta, la libreria vede il dispositivo staccato e chiude tutto con NO_DEVICE (lo fa anche il test
// "sudden disconnection" di ESP-IDF, test_apps/usb_host). Prima pero' si lascia girare il demone: se le cuffie si
// stanno staccando proprio adesso, finisce lui, e spegnere la porta in quel momento lo confonderebbe (hub.c:
// hub_root_stop fa assert se c'e' un evento della porta in sospeso, root_port_recycle fa abort se la trova spenta).
void Dispositivo::staccaPorta() {
    aspettaMs(ATTESA_DEMONE_MS);
    aspettaFermi(0);
    aspettaControllo(0);
    if (staccato_ || (nFermiVisti_ >= nAvviati_ && !controlloBloccato_)) return;
    ESP_LOGW(TAG, "%04x:%04x non restituisce i trasferimenti: spengo la porta USB per staccarlo", vid_, pid_);
    const esp_err_t r = usb_host_lib_set_root_port_power(false);
    if (r == ESP_OK)
        portaSpenta_ = true;
    else
        ESP_LOGE(TAG, "porta USB non spenta: %s", esp_err_to_name(r));
}

// La porta si riaccende solo quando il demone ha liberato il dispositivo e recuperato la porta (hub.c,
// root_port_req): fino ad allora hub_root_start dice INVALID_STATE senza fare niente, quindi si riprova.
void Dispositivo::riaccendiPorta() {
    if (!portaSpenta_) return;
    portaSpenta_ = false;
    for (uint32_t t = 0; t < ATTESA_RIACCENSIONE_MS; t += PASSO_RIACCENSIONE_MS) {
        aspettaMs(PASSO_RIACCENSIONE_MS);
        if (usb_host_lib_set_root_port_power(true) == ESP_OK) {
            ESP_LOGI(TAG, "porta USB riaccesa");
            return;
        }
    }
    // Resta spenta: per questa sessione non si collega piu' niente (usb_host_uninstall la gestisce lo stesso).
    ESP_LOGE(TAG, "porta USB non riaccesa");
}

void Dispositivo::chiudi(bool riaccendi) {
    usb_device_handle_t dev = dev_.load();
    if (dev == nullptr) return;
    fermare_ = true;

    // 1. Lo stream: i trasferimenti in coda non si rimandano piu' e tornano da soli entro 32 ms (con le cuffie
    //    staccate li chiude la libreria con NO_DEVICE). Se non tornano si stacca il dispositivo.
    if (nFermiVisti_ < nAvviati_) {
        aspettaFermi(ATTESA_SCARICO_MS);
        if (nFermiVisti_ < nAvviati_ && !staccato_) {
            ESP_LOGW(TAG, "%u trasferimenti isocroni non tornano", unsigned(nAvviati_ - nFermiVisti_));
            staccaPorta();
        }
        if (nFermiVisti_ < nAvviati_) aspettaFermi(ATTESA_PORTA_MS);
        if (nFermiVisti_ < nAvviati_) {
            ESP_LOGE(TAG, "%u trasferimenti isocroni ancora nella libreria: il dispositivo resta aperto",
                     unsigned(nAvviati_ - nFermiVisti_));
            return;
        }
    }
    if (inCorso_) {
        msStreamFinale_ = msDa(inizioTick_.load());
        inCorso_ = false;
    }

    // 2. halt e flush dell'endpoint: e' vuoto, quindi il canale e' gia' fermo e non si aspetta nessun interrupt
    //    (_pipe_cmd_halt). Con le cuffie staccate l'ha gia' fatto la libreria.
    if (interfacciaPresa_ && nAvviati_ > 0 && !staccato_) {
        esp_err_t r = usb_host_endpoint_halt(dev, scelta_.endpoint);
        if (r == ESP_OK) r = usb_host_endpoint_flush(dev, scelta_.endpoint);
        if (r != ESP_OK) ESP_LOGW(TAG, "halt/flush dell'endpoint 0x%02x: %s", unsigned(scelta_.endpoint),
                                  esp_err_to_name(r));
    }

    // 3. L'interfaccia torna all'alternativa 0 (niente banda), se il dispositivo c'e' ancora.
    if (altImpostata_ && !staccato_ && !controlloBloccato_) {
        const Errore e =
            richiesta(RT_STANDARD_INTERFACCIA_OUT, RICHIESTA_SET_INTERFACE, 0, scelta_.interfaccia, nullptr, 0);
        if (e != Errore::NESSUNO)
            ESP_LOGW(TAG, "SET_INTERFACE(%u, alt 0): %s", unsigned(scelta_.interfaccia), testoErrore(e));
    }
    altImpostata_ = false;

    // 4. Una richiesta senza risposta tiene EP0, e finche' e' in volo il dispositivo non si chiude.
    if (controlloBloccato_) {
        aspettaControllo(ATTESA_BLOCCATO_MS);
        if (controlloBloccato_ && !staccato_) staccaPorta();
        aspettaControllo(ATTESA_PORTA_MS);
        if (controlloBloccato_) {
            ESP_LOGE(TAG, "la richiesta di controllo non torna: il dispositivo resta aperto");
            return;
        }
    }

    // 5. release: la libreria rifiuta finche' il compito client non ha finito di scalare i trasferimenti tornati.
    if (interfacciaPresa_) {
        esp_err_t r = ESP_FAIL;
        for (int prova = 0; prova < PROVE_RILASCIO; prova++) {
            r = usb_host_interface_release(client_, dev, scelta_.interfaccia);
            if (r != ESP_ERR_INVALID_STATE) break;
            aspettaMs(PASSO_RILASCIO_MS);
        }
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "release dell'interfaccia %u: %s (il dispositivo resta aperto)",
                     unsigned(scelta_.interfaccia), esp_err_to_name(r));
            return;
        }
        interfacciaPresa_ = false;
    }

    // 6. Si libera tutto: niente e' piu' in volo.
    for (usb_transfer_t*& t : iso_) {
        if (t) usb_host_transfer_free(t);
        t = nullptr;
    }
    if (controllo_) usb_host_transfer_free(controllo_);
    controllo_ = nullptr;
    if (fattoControllo_) vSemaphoreDelete(fattoControllo_);
    fattoControllo_ = nullptr;
    if (fermi_) vSemaphoreDelete(fermi_);
    fermi_ = nullptr;
    const unsigned avviati = nAvviati_;
    nAvviati_ = 0;
    nFermiVisti_ = 0;

    // 7. close. Prima il demone deve aver finito il giro dell'ultima richiesta (vedi uac_host.h, priorita').
    aspettaMs(ATTESA_DEMONE_MS);
    const esp_err_t r = usb_host_device_close(client_, dev);
    if (r != ESP_OK) ESP_LOGE(TAG, "usb_host_device_close: %s", esp_err_to_name(r));
    dev_.store(nullptr);
    config_ = nullptr;
    if (avviati > 0) {
        const Statistiche s = statistiche();
        ESP_LOGI(TAG, "%04x:%04x chiuso dopo %u ms di stream: %u trasferimenti, %u pacchetti, %u saltati, %u errori, "
                 "%u invii falliti, %u frame veri, %u di silenzio%s", vid_, pid_, unsigned(s.msStream),
                 unsigned(s.trasferimenti), unsigned(s.pacchetti), unsigned(s.saltati), unsigned(s.errori),
                 unsigned(s.invioFallito), unsigned(s.frameVeri), unsigned(s.frameSilenzio),
                 staccato_ ? " (staccato)" : "");
    } else {
        ESP_LOGI(TAG, "%04x:%04x chiuso", vid_, pid_);
    }

    // 8. Se per sbloccarlo si e' spenta la porta, si riaccende: il dispositivo si ripresenta (NEW_DEV). Con
    //    riaccendi false (le cuffie si spengono) resta spenta: usb_host_uninstall la trova gia' spenta e non
    //    c'e' nessun dispositivo che si ripresenta mentre la libreria se ne va.
    if (riaccendi)
        riaccendiPorta();
    else
        portaSpenta_ = false;
}

void Dispositivo::scollegato() {
    staccato_ = true;
    fermare_ = true;
}

bool Dispositivo::attivo() const {
    return dev_.load() != nullptr && !staccato_.load() && !fermare_.load() && inVolo_.load() > 0;
}

Statistiche Dispositivo::statistiche() const {
    Statistiche s;
    s.trasferimenti = nTrasferimenti_.load(std::memory_order_relaxed);
    s.pacchetti = nPacchetti_.load(std::memory_order_relaxed);
    s.saltati = nSaltati_.load(std::memory_order_relaxed);
    s.errori = nErrori_.load(std::memory_order_relaxed);
    s.frameVeri = nFrameVeri_.load(std::memory_order_relaxed);
    s.frameSilenzio = nFrameSilenzio_.load(std::memory_order_relaxed);
    s.invioFallito = nInvioFallito_.load(std::memory_order_relaxed);
    s.msStream = inCorso_.load() ? msDa(inizioTick_.load()) : msStreamFinale_.load();
    return s;
}

}  // namespace uac
