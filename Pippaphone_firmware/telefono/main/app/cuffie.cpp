// Cuffie USB-C (solo T-Deck Plus): il modo cuffie, vedi cuffie.h. Sulla PCB questo file e' vuoto.
//
// Chi fa cosa (tutti sul core 0, con lo stack in PSRAM: CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM; in questa
// build la cache della flash non si spegne mai, nemmeno scrivendo NVS o FAT):
//  - compito "cuffie" (controllo, priorita' 6, sempre): comandi (accendi, spegni) ed eventi della USB arrivano
//    in una coda. Fa usb_host_install (l'interrupt della USB va sul core di chi installa) e usb_host_uninstall,
//    uac::Dispositivo::apri() e chiudi() (aspettano le risposte delle cuffie), la scatola nera del log, il
//    ritorno al computer dopo 2 minuti senza cuffie, le statistiche e Ev::CUFFIE;
//  - compito "cuffie_lib" (demone della USB Host Library, priorita' 7, solo a cuffie accese): enumerazione,
//    porta, fine delle richieste su EP0. Sta sopra il controllo, come consiglia uac_host.h (la fine di una
//    richiesta passa prima dal demone, poi dal compito client);
//  - compito "cuffie_usb" (client, priorita' 11, solo a cuffie accese): tutti i callback della USB, anche
//    quello che riempie i trasferimenti isocroni (riempi(), qui sotto). E' l'unico in tempo reale (circa 24 ms
//    di margine): sopra la radio (10), sotto la tastiera (12).
//
// La musica: audio_out (scheda/audio.cpp) chiama spingi() -> ricampionatore alla frequenza delle cuffie -> coda
// mono in PSRAM (200 ms; oltre 60 ms spingi() aspetta, cosi' il passo lo da' la USB come il DMA per la cassa)
// -> riempi() nel compito client -> volume con rampa -> formato delle cuffie. Coda, ricampionatore e buffer si
// allocano alla prima accensione e restano per sempre: spingi() non trova mai una coda che si sta liberando.
//
// La porta torna al computer con le due chiamate LL che fa anche il driver della USB-Serial-JTAG quando si
// installa (usb_host_uninstall la PHY non la restituisce). Le stesse due sono anche in holdPower() e prima del
// sonno profondo (scheda_tdeck.cpp), per i riavvii presi a cuffie accese.
#include "sdkconfig.h"

#if CONFIG_SCHEDA_TDECK_PLUS

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

#include "app/aggiorna.h"
#include "app/cuffie.h"
#include "app/eventi.h"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "scheda/audio.h"
#include "scheda/scheda.h"
#include "usb/usb_host.h"
#include "usb_audio/dsp.h"
#include "usb_audio/uac_desc.h"
#include "usb_audio/uac_host.h"

namespace app {

namespace {
const char* TAG = "cuffie";

using Stato = Cuffie::Stato;
using Errore = Cuffie::Errore;

// ---- misure ----
constexpr uint32_t RAM_INTERNA_MIN = 40 * 1024;  // per accendere: la USB host ne usa 11-13 KB, il resto agli altri
constexpr uint32_t BLOCCO_DMA_MIN = 8 * 1024;    // trasferimenti da 1,5 KB e liste allineate a 512 byte
constexpr uint32_t HZ_MAX = 48000;
constexpr uint32_t MS_CODA = 200;                // la coda delle cuffie
constexpr uint32_t MS_SOGLIA = 60;               // oltre, spingi() con aspetta si ferma ad aspettare
constexpr uint32_t MS_ATTESA_MAX = 200;          // e comunque torna entro questo
constexpr size_t CAP_CODA = HZ_MAX * MS_CODA / 1000;                         // 9600 campioni (19,2 KB)
constexpr size_t MAX_FRAME = size_t(uac::PACCHETTI) * ((HZ_MAX + 999) / 1000);  // 384 frame in un trasferimento
constexpr size_t BLOCCO_IN = 128;                // spingi(): campioni d'ingresso alla volta
constexpr size_t CAP_OUT = 512;                  // spingi(): campioni ricampionati alla volta
constexpr size_t SCATOLA = 32 * 1024;            // la scatola nera del log
constexpr size_t RIGA = 256;                     // una riga del log
constexpr uint32_t STACK_CONTROLLO = 8 * 1024;
constexpr uint32_t STACK_CLIENT = 6 * 1024;
constexpr uint32_t STACK_DEMONE = 6 * 1024;
constexpr UBaseType_t PRIO_CONTROLLO = 6, PRIO_DEMONE = 7, PRIO_CLIENT = 11;
constexpr uint32_t PERIODO_MS = 100;             // il giro del controllo a cuffie accese
constexpr uint32_t MS_SPINTA_RECENTE = 30;       // riempi(): la musica sta arrivando (per i sottoflussi)
constexpr int MAX_TENTATIVI = 2;                 // dispositivo che non risponde: apri() al massimo 2 volte
constexpr uint32_t MS_TOLTO = 1000;              // nessun dispositivo per 1 s: e' stato tolto (non e' la porta
                                                 // spenta e riaccesa da chiudi(), che dura circa 0,3 s)

// Bit del gruppo di eventi del controllo.
constexpr EventBits_t BIT_CLIENT_FERMO = 1 << 0;  // il compito client e' uscito dal suo giro (parcheggiato)
constexpr EventBits_t BIT_DEMONE_FERMO = 1 << 1;  // il demone e' uscito dal suo giro (parcheggiato)
constexpr EventBits_t BIT_TUTTI_LIBERI = 1 << 2;  // USB_HOST_LIB_EVENT_FLAGS_ALL_FREE

enum class Cmd : uint8_t {
    ACCENDI,            // da accendi()/prova(): secondi = 0, o quelli della prova
    SPEGNI,             // da spegni()
    NUOVO,              // compito client: USB_HOST_CLIENT_EVENT_NEW_DEV (indirizzo)
    VIA,                // compito client: USB_HOST_CLIENT_EVENT_DEV_GONE (handle)
    DESCRITTORE_LUNGO,  // log dell'enumerazione: descrittore di configurazione oltre 1024 byte
};
struct Msg {
    Cmd cmd = Cmd::SPEGNI;
    uint8_t indirizzo = 0;
    uint32_t secondi = 0;
    uint32_t sessione = 0;  // SPEGNI: la sessione in cui e' stato chiesto
    usb_device_handle_t handle = nullptr;
};

// Lo stato grande sta in un blocco solo in PSRAM, creato da avvia() e mai liberato: la RAM interna serve
// alla USB host e al Wi-Fi, e niente di questo deve stare li'.
struct Mondo {
    uac::Dispositivo disp;            // solo il controllo (piu' i callback, nel compito client)
    // Uscita: allocata alla prima accensione, poi per sempre.
    bool uscitaPronta = false;
    uac::Anello anello;               // produttore audio_out (spingi), consumatore compito client (riempi)
    uac::Ricampionatore ricamp;       // solo audio_out
    int16_t* in = nullptr;            // solo audio_out: BLOCCO_IN campioni mono
    int16_t* out = nullptr;           // solo audio_out: CAP_OUT campioni ricampionati
    uint32_t genVista = 0;            // solo audio_out: l'ultima svuota() gia' fatta
    uac::Guadagno guadagno;           // solo il compito client (e il controllo quando lo stream e' fermo)
    int16_t* mono = nullptr;          // solo il compito client: MAX_FRAME campioni
    bool zitto = true;                // solo il compito client: l'ultimo campione mandato e' silenzio
    // Scatola nera (protetta da g_muScatola).
    char* scatola = nullptr;
    char* riga = nullptr;
    size_t scritti = 0;
    bool registra = false;
    // Statistiche (protette da g_muxStat).
    Cuffie::Statistiche stat;
    uint32_t msBase = 0, saltatiBase = 0, erroriBase = 0;  // dei dispositivi gia' chiusi in questa sessione
    bool vivo = false;                // i contatori di disp sono di questa sessione e non ancora sommati
};

Mondo* g = nullptr;

// Stato piccolo, nella RAM interna.
QueueHandle_t g_coda = nullptr;
EventGroupHandle_t g_bit = nullptr;
SemaphoreHandle_t g_muScatola = nullptr;
portMUX_TYPE g_muxStat = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_controllo = nullptr, g_compitoClient = nullptr, g_compitoDemone = nullptr;
usb_host_client_handle_t g_client = nullptr;
bool g_installata = false;            // usb_host_install riuscito, uninstall non ancora
bool g_sporca = false;                // uno spegnimento non e' riuscito a liberare tutto: niente piu' cuffie
vprintf_like_t g_vprintfPrima = nullptr;
std::atomic<uint32_t> g_stato{uint32_t(Stato::SPENTE)};
std::atomic<uint32_t> g_errore{uint32_t(Errore::NESSUNO)};
std::atomic<uint32_t> g_sessione{0};  // sale a ogni accensione: uno SPEGNI vale solo per la sua sessione
std::atomic<bool> g_pronta{false};    // COLLEGATE e lo stream gira: spingi() scrive
std::atomic<uint8_t> g_volume{Cuffie::VOLUME_INIZIALE};
std::atomic<uint32_t> g_hzUscita{48000};
std::atomic<uint32_t> g_genChiesta{0};  // svuota() lo fa salire
std::atomic<uint32_t> g_genFatta{0};    // riempi() (o il controllo, a stream fermo) dice fin dove ha buttato
std::atomic<uint32_t> g_tickSpinta{0};  // l'ultima spingi()
std::atomic<uint32_t> g_sottoflussi{0};
std::atomic<bool> g_fermaClient{false}, g_fermaDemone{false};
// Solo il controllo.
bool g_timer = false;                 // i 2 minuti senza cuffie stanno contando
TickType_t g_inizioTimer = 0;
uint32_t g_secondiProva = 0;          // prova da banco: dopo questi secondi si torna al computer (0 = no)
TickType_t g_inizioProva = 0;
int g_tentativi = 0;                  // apri() finiti in ERRORE da quando il dispositivo e' stato attaccato
bool g_visto = false;                 // il dispositivo non supportato o guasto e' stato enumerato (NEW_DEV)
bool g_vuoto = false;                 // nessun dispositivo da g_vuotoDa
TickType_t g_vuotoDa = 0;

Stato statoAdesso() { return Stato(g_stato.load()); }

uint32_t ramInterna() { return uint32_t(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)); }

void* allocaPsram(size_t byte, void*) { return heap_caps_malloc(byte, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void liberaPsram(void* p, void*) { heap_caps_free(p); }
// Per coda e ricampionatore: sempre lo stesso (il ricampionatore non rialloca se il rapporto e l'allocatore
// restano quelli).
const uac::Allocatore& psram() {
    static uac::Allocatore a = [] {
        uac::Allocatore x;
        x.alloca = allocaPsram;  // assegnarla va bene (alloca e' anche una macro, vedi dsp.h)
        x.libera = liberaPsram;
        return x;
    }();
    return a;
}

// Cambio di stato (solo dal compito di controllo, o da accendi() per SPENTE -> ACCENSIONE): lo dice
// all'interfaccia con Ev::CUFFIE.
void avvisa(Stato s, Errore e) { post({Ev::CUFFIE, {}, 0, uint32_t(e), int32_t(s)}); }

void impostaStato(Stato s, Errore e) {
    g_errore.store(uint32_t(e));
    g_stato.store(uint32_t(s));
    portENTER_CRITICAL(&g_muxStat);
    g->stat.stato = s;
    g->stat.errore = e;
    portEXIT_CRITICAL(&g_muxStat);
    ESP_LOGI(TAG, "%s%s%s", Cuffie::testoStato(s), e == Errore::NESSUNO ? "" : ": ",
             e == Errore::NESSUNO ? "" : Cuffie::testoErrore(e));
    avvisa(s, e);
}

void avviaTimer() {
    g_timer = true;
    g_inizioTimer = xTaskGetTickCount();
}

// ---------------------------------------------------------------- scatola nera del log

// Con la porta alle cuffie il log non ha dove uscire: va qui (32 KB in PSRAM, gli ultimi), e si stampa quando
// la console torna. Cosi' non si paga nemmeno l'attesa di 50 ms della prima riga senza computer
// (usb_serial_jtag_vfs.c). Guarda anche se l'enumerazione ha rifiutato un descrittore troppo lungo
// (enum.c): in quel caso NEW_DEV non arriva e questo e' l'unico modo di saperlo.
int scriviScatola(const char* fmt, va_list ap) {
    if (fmt && std::strstr(fmt, "Configuration descriptor larger than control transfer max length")) {
        Msg m;
        m.cmd = Cmd::DESCRITTORE_LUNGO;
        xQueueSend(g_coda, &m, 0);  // non blocca: chi logga e' il demone della USB
    }
    if (xSemaphoreTake(g_muScatola, portMAX_DELAY) != pdTRUE) return 0;
    int n = 0;
    if (g->registra) {
        if (g->scatola) {  // senza memoria per la scatola la riga si perde (ma senza l'attesa sulla console)
            n = vsnprintf(g->riga, RIGA, fmt, ap);
            const size_t k = n <= 0 ? 0 : std::min(size_t(n), RIGA - 1);
            for (size_t i = 0; i < k; i++) g->scatola[(g->scritti + i) % SCATOLA] = g->riga[i];
            g->scritti += k;
        }
    } else if (g_vprintfPrima) {
        // Riga arrivata mentre la scatola si chiudeva: alla console di prima.
        n = g_vprintfPrima(fmt, ap);
    }
    xSemaphoreGive(g_muScatola);
    return n;
}

void apriScatola() {
    if (!g->scatola) g->scatola = static_cast<char*>(heap_caps_malloc(SCATOLA, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    xSemaphoreTake(g_muScatola, portMAX_DELAY);
    g->scritti = 0;
    g->registra = true;  // anche senza memoria: le righe si perdono, ma senza l'attesa sulla console
    xSemaphoreGive(g_muScatola);
    g_vprintfPrima = esp_log_set_vprintf(scriviScatola);
}

void chiudiScatola() {
    if (!g_vprintfPrima) return;
    esp_log_set_vprintf(g_vprintfPrima);
    // Chi stava scrivendo nella scatola finisce prima (stesso lucchetto); dopo nessuno la tocca piu'.
    xSemaphoreTake(g_muScatola, portMAX_DELAY);
    g->registra = false;
    xSemaphoreGive(g_muScatola);
}

// Quando il computer ricomincia a mandare i SOF la console c'e' di nuovo: si aspetta un po' (al massimo 3 s, piu'
// mezzo secondo perche' il computer riapra la porta), poi si stampa e si libera. Senza computer le righe si
// perdono (la console le butta senza aspettare).
void stampaScatola() {
    if (!g->scatola) return;
    for (int i = 0; i < 30 && !usb_serial_jtag_is_connected(); i++) vTaskDelay(pdMS_TO_TICKS(100));
    if (usb_serial_jtag_is_connected()) vTaskDelay(pdMS_TO_TICKS(500));
    const size_t n = std::min(g->scritti, SCATOLA);
    const size_t inizio = g->scritti > SCATOLA ? g->scritti % SCATOLA : 0;
    printf("---- cuffie USB-C: scatola nera del log, %u byte%s ----\n", unsigned(n),
           g->scritti > SCATOLA ? " (gli ultimi)" : "");
    for (size_t i = 0; i < n;) {
        const size_t pos = (inizio + i) % SCATOLA;
        const size_t k = std::min(n - i, SCATOLA - pos);
        fwrite(g->scatola + pos, 1, k, stdout);
        i += k;
    }
    printf("---- fine della scatola nera ----\n");
    fflush(stdout);
    heap_caps_free(g->scatola);
    g->scatola = nullptr;
}

// ---------------------------------------------------------------- uscita della musica

// Coda, ricampionatore e buffer: alla prima accensione, poi per sempre.
// Si fa con nessuno che la usa: pronta() e' false (spingi() non tocca niente) e lo stream non c'e'. Se manca
// memoria si riprova alla prossima accensione, prendendo solo quello che manca.
bool preparaUscita() {
    if (g->uscitaPronta) return true;
    if (!g->anello.capacita() && !g->anello.crea(CAP_CODA, psram())) return false;
    if (!g->in) g->in = static_cast<int16_t*>(allocaPsram(BLOCCO_IN * sizeof(int16_t), nullptr));
    if (!g->out) g->out = static_cast<int16_t*>(allocaPsram(CAP_OUT * sizeof(int16_t), nullptr));
    if (!g->mono) g->mono = static_cast<int16_t*>(allocaPsram(MAX_FRAME * sizeof(int16_t), nullptr));
    if (!g->in || !g->out || !g->mono) return false;
    g->uscitaPronta = true;
    return true;
}

// Compito client, una volta per trasferimento isocrono (uac::Riempitore): i campioni della coda nel formato delle
// cuffie, col volume. Niente attese, allocazioni o log. Dopo svuota() sfuma in 4 ms quello che c'e', butta il
// resto e lo dice a spingi(); dopo del silenzio (inizio, pausa, coda rimasta vuota) la musica entra sfumando.
size_t riempi(void*, uint8_t* dst, size_t frame, const uac::Formato& f) {
    Mondo& m = *g;
    const size_t rampa = f.hz / 250;  // 4 ms
    const int32_t volume = uac::guadagnoDaVolume(g_volume.load(std::memory_order_relaxed));
    uint8_t* p = dst;
    size_t resto = frame, veri = 0;
    const uint32_t gen = g_genChiesta.load(std::memory_order_acquire);
    if (gen != g_genFatta.load(std::memory_order_relaxed)) {
        const size_t n = m.zitto ? 0 : m.anello.leggi(m.mono, std::min({resto, rampa, MAX_FRAME}));
        if (n) {
            m.guadagno.imposta(0, uint32_t(n));
            p += uac::impacchetta(m.mono, n, f.canali, f.byteCampione, m.guadagno, p);
            resto -= n;
            veri += n;
        }
        m.anello.scarta();
        m.guadagno.subito(0);
        m.zitto = true;
        g_genFatta.store(gen, std::memory_order_release);
    }
    while (resto) {
        const size_t n = m.anello.leggi(m.mono, std::min(resto, MAX_FRAME));
        if (!n) break;
        if (m.zitto) {
            m.guadagno.subito(0);
            m.guadagno.imposta(volume, uint32_t(rampa));
            m.zitto = false;
        } else if (m.guadagno.obiettivo() != volume) {
            m.guadagno.imposta(volume, uint32_t(f.hz / 66));  // 15 ms: niente "zip" girando il volume
        }
        p += uac::impacchetta(m.mono, n, f.canali, f.byteCampione, m.guadagno, p);
        resto -= n;
        veri += n;
    }
    if (resto) {
        uac::silenzio(resto, f.canali, f.byteCampione, p);
        // Coda finita mentre la musica arrivava: un sottoflusso (con la musica ferma, in pausa o finita no).
        const uint32_t da = uint32_t(xTaskGetTickCount()) - g_tickSpinta.load(std::memory_order_relaxed);
        if (!m.zitto && da < pdMS_TO_TICKS(MS_SPINTA_RECENTE)) g_sottoflussi.fetch_add(1, std::memory_order_relaxed);
        m.zitto = true;
    }
    return veri;
}

class Uscita : public scheda::UscitaEsterna {
public:
    bool pronta() const override { return g_pronta.load(std::memory_order_acquire); }
    size_t spingi(const int16_t* pcm, size_t frame, uint8_t canali, uint32_t hz, bool aspetta) override;
    void svuota() override { g_genChiesta.fetch_add(1, std::memory_order_acq_rel); }
};

// Solo audio_out (un produttore). Locali piccoli: i buffer sono in PSRAM.
size_t Uscita::spingi(const int16_t* pcm, size_t frame, uint8_t canali, uint32_t hz, bool aspetta) {
    if (!g || !frame || (canali != 1 && canali != 2) || !pronta()) return 0;
    Mondo& m = *g;
    const TickType_t inizio = xTaskGetTickCount();
    const TickType_t limite = pdMS_TO_TICKS(MS_ATTESA_MAX);
    g_tickSpinta.store(uint32_t(inizio), std::memory_order_relaxed);
    // Dopo svuota() si scrive solo quando riempi() ha buttato la coda (se no butterebbe anche questo), e il
    // ricampionatore riparte dal silenzio.
    const uint32_t gen = g_genChiesta.load(std::memory_order_acquire);
    if (gen != m.genVista) {
        while (int32_t(g_genFatta.load(std::memory_order_acquire) - gen) < 0) {
            if (!aspetta || !pronta() || xTaskGetTickCount() - inizio >= limite) return 0;
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        m.ricamp.azzera();
        m.genVista = gen;
    }
    const uint32_t hzOut = g_hzUscita.load(std::memory_order_acquire);
    if ((m.ricamp.hzIn() != hz || m.ricamp.hzOut() != hzOut) && !m.ricamp.configura(hz, hzOut, psram()))
        return frame;  // frequenza fuori dai limiti (o niente memoria): si butta
    const size_t soglia = size_t(hzOut) * MS_SOGLIA / 1000;
    size_t presi = 0;
    while (presi < frame) {
        const size_t blocco = std::min(frame - presi, BLOCCO_IN);
        const int16_t* s = pcm + presi * canali;
        if (canali == 2) {
            for (size_t i = 0; i < blocco; i++) m.in[i] = int16_t((int32_t(s[2 * i]) + s[2 * i + 1]) / 2);
        } else {
            std::memcpy(m.in, s, blocco * sizeof(int16_t));
        }
        size_t fatti = 0;
        while (fatti < blocco) {
            // Posto nella coda. Con aspetta la si tiene sui 60 ms: il passo lo da' il clock della USB.
            while (m.anello.liberi() < CAP_OUT || (aspetta && m.anello.pieni() > soglia)) {
                if (!aspetta) return frame;  // non c'e' posto: il resto si butta
                if (!pronta() || g_genChiesta.load(std::memory_order_acquire) != gen ||
                    xTaskGetTickCount() - inizio >= limite)
                    return presi + fatti;
                vTaskDelay(pdMS_TO_TICKS(2));
            }
            size_t letti = 0;
            const size_t n = m.ricamp.elabora(m.in + fatti, blocco - fatti, letti, m.out, CAP_OUT);
            if (n) m.anello.scrivi(m.out, n);
            fatti += letti;
            if (!letti && !n) break;  // non succede (spento prende tutto): niente giri a vuoto
        }
        presi += blocco;
    }
    return presi;
}

Uscita g_uscita;

// ---------------------------------------------------------------- compiti della USB

// Compito client: callback degli eventi. Non blocca: tutto va in coda al controllo.
void eventoClient(const usb_host_client_event_msg_t* ev, void*) {
    Msg m;
    if (ev->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        m.cmd = Cmd::NUOVO;
        m.indirizzo = ev->new_dev.address;
    } else if (ev->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        m.cmd = Cmd::VIA;
        m.handle = ev->dev_gone.dev_hdl;
        // Subito, prima del controllo: audio_out smette di spingere e mette in pausa la musica prima della cassa.
        if (m.handle && m.handle == g->disp.handle()) {
            g_pronta.store(false, std::memory_order_release);
            g->disp.scollegato();
        }
    } else {
        return;
    }
    xQueueSend(g_coda, &m, 0);
}

void compitoClient(void*) {
    while (!g_fermaClient.load()) usb_host_client_handle_events(g_client, portMAX_DELAY);
    xEventGroupSetBits(g_bit, BIT_CLIENT_FERMO);
    for (;;) vTaskSuspend(nullptr);  // lo cancella il controllo (vTaskDeleteWithCaps)
}

void compitoDemone(void*) {
    while (!g_fermaDemone.load()) {
        uint32_t f = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &f);
        // ALL_FREE arriva anche dopo ogni distacco (l'ultimo dispositivo liberato): esci() pulisce il bit prima.
        if (f & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) xEventGroupSetBits(g_bit, BIT_TUTTI_LIBERI);
    }
    xEventGroupSetBits(g_bit, BIT_DEMONE_FERMO);
    for (;;) vTaskSuspend(nullptr);
}

bool aspettaBit(EventBits_t bit, uint32_t ms) {
    return (xEventGroupWaitBits(g_bit, bit, pdTRUE, pdFALSE, pdMS_TO_TICKS(ms)) & bit) != 0;
}

// La PHY interna torna alla USB-Serial-JTAG: come usb_serial_jtag_driver_install (usb_serial_jtag.c).
void ridaiPhy() {
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(true);
}

// ---------------------------------------------------------------- dispositivo

// Chiude il dispositivo e somma i suoi contatori a quelli della sessione. Con riaccendi false (esci()) la porta,
// se chiudi() ha dovuto spegnerla per staccare il dispositivo, resta spenta.
void chiudiDispositivo(bool riaccendi = true) {
    g_pronta.store(false, std::memory_order_release);
    g->disp.chiudi(riaccendi);
    if (g->disp.aperto()) ESP_LOGE(TAG, "il dispositivo non si chiude (si riprova allo spegnimento)");
    if (g->vivo) {
        const uac::Statistiche s = g->disp.statistiche();
        portENTER_CRITICAL(&g_muxStat);
        g->msBase += s.msStream;
        g->saltatiBase += s.saltati;
        g->erroriBase += s.errori;
        g->vivo = false;
        portEXIT_CRITICAL(&g_muxStat);
    }
}

// Da uac::Errore (e uac::Esito) a quello che vede l'utente (CONTRATTI.md 6.2).
void traduci(uac::Errore e, uac::Esito es, Stato& s, Errore& er) {
    s = Stato::ERRORE;
    er = Errore::DISPOSITIVO;
    switch (e) {
    case uac::Errore::NESSUNO:
        s = Stato::COLLEGATE;
        er = Errore::NESSUNO;
        break;
    case uac::Errore::DESCRITTORE:
        if (es == uac::Esito::NON_AUDIO) {
            s = Stato::NON_SUPPORTATE;
            er = Errore::NON_AUDIO;
        } else if (es == uac::Esito::SOLO_UAC3) {
            s = Stato::NON_SUPPORTATE;
            er = Errore::SOLO_UAC3;
        } else if (es == uac::Esito::PACCHETTI_GRANDI) {
            s = Stato::NON_SUPPORTATE;
            er = Errore::PACCHETTI_GRANDI;
        } else if (es == uac::Esito::FORMATO || es == uac::Esito::NESSUNA_USCITA) {
            s = Stato::NON_SUPPORTATE;
            er = Errore::FORMATO;
        }
        break;  // MALFORMATO: guasto
    case uac::Errore::MEMORIA: er = Errore::MEMORIA; break;
    case uac::Errore::SCOLLEGATO:
        s = Stato::IN_ATTESA;
        er = Errore::NESSUNO;
        break;
    default: break;  // apertura, richieste, tempo, clock, interfaccia, stream: guasto
    }
}

void collega(uint8_t indirizzo) {
    impostaStato(Stato::COLLEGAMENTO, Errore::NESSUNO);
    g_visto = true;
    g_vuoto = false;
    // La coda riparte vuota. Lo stream non e' ancora partito, quindi nessuno la sta leggendo: lo fa il controllo
    // al posto di riempi(). La prossima spingi() vede la svuota() e azzera il ricampionatore.
    g->anello.scarta();
    g->guadagno.subito(0);
    g->zitto = true;
    g_genFatta.store(g_genChiesta.fetch_add(1) + 1, std::memory_order_release);
    const uac::Errore e = g->disp.apri(g_client, indirizzo, riempi, nullptr);
    portENTER_CRITICAL(&g_muxStat);
    g->stat.vid = g->disp.vid();
    g->stat.pid = g->disp.pid();
    std::strncpy(g->stat.nome, g->disp.prodotto(), sizeof(g->stat.nome) - 1);
    g->stat.nome[sizeof(g->stat.nome) - 1] = '\0';
    portEXIT_CRITICAL(&g_muxStat);
    Stato s;
    Errore er;
    traduci(e, g->disp.esito(), s, er);
    if (s == Stato::COLLEGATE) {
        const uac::Formato f = g->disp.formato();
        portENTER_CRITICAL(&g_muxStat);
        g->stat.uac = g->disp.versione() == uac::Versione::UAC2 ? 2 : 1;
        g->stat.hz = f.hz;
        g->stat.canali = f.canali;
        g->stat.bit = f.bit;
        g->vivo = true;
        portEXIT_CRITICAL(&g_muxStat);
        ESP_LOGI(TAG, "collegate: %04x:%04x \"%s\", UAC%u, %u Hz, %u canali, %u bit", unsigned(g->disp.vid()),
                 unsigned(g->disp.pid()), g->disp.prodotto(), unsigned(g->stat.uac), unsigned(f.hz),
                 unsigned(f.canali), unsigned(f.bit));
        g_hzUscita.store(f.hz, std::memory_order_release);
        g_pronta.store(true, std::memory_order_release);
        g_tentativi = 0;
        g_timer = false;  // con le cuffie che suonano i 2 minuti non contano
        impostaStato(Stato::COLLEGATE, Errore::NESSUNO);
        return;
    }
    if (s == Stato::ERRORE) g_tentativi++;
    if (s == Stato::IN_ATTESA) g_visto = false;  // se n'e' andato durante apri()
    impostaStato(s, er);
}

// Cuffie staccate (DEV_GONE, o trasferimenti finiti con NO_DEVICE).
void scollega() {
    chiudiDispositivo();
    g_tentativi = 0;
    g_visto = false;
    impostaStato(Stato::IN_ATTESA, Errore::NESSUNO);
    avviaTimer();  // i 2 minuti ripartono
}

// Lo stream si e' fermato da solo (errori del canale, invii falliti) con le cuffie ancora attaccate.
void guasto() {
    const uac::Statistiche s = g->disp.statistiche();
    ESP_LOGE(TAG, "stream fermo: %u errori, %u invii falliti", unsigned(s.errori), unsigned(s.invioFallito));
    chiudiDispositivo();
    g_tentativi++;
    g_visto = true;
    g_vuoto = false;
    impostaStato(Stato::ERRORE, Errore::DISPOSITIVO);
    avviaTimer();
}

// ---------------------------------------------------------------- accendere e spegnere

// Torna al computer, da qualunque punto dell'accensione (anche a meta', se qualcosa non e' partito).
void esci(Errore motivo) {
    impostaStato(Stato::SPEGNIMENTO, Errore::NESSUNO);
    g_pronta.store(false, std::memory_order_release);
    // Se chiudi() deve spegnere la porta per staccare le cuffie, la lascia spenta: riaccesa, le cuffie ancora
    // attaccate si ripresenterebbero proprio mentre la libreria si disinstalla (passo 5).
    if (g->disp.aperto()) chiudiDispositivo(false);
    bool pulita = !g->disp.aperto();
    // 1. Il compito client esce dal suo giro (prima finisce i callback che ha in mano) e si parcheggia.
    bool clientFermo = !g_compitoClient;
    if (g_compitoClient) {
        g_fermaClient.store(true);
        if (g_client) usb_host_client_unblock(g_client);
        clientFermo = aspettaBit(BIT_CLIENT_FERMO, 500);
        if (!clientFermo) ESP_LOGE(TAG, "il compito client non si ferma");
    }
    // 2. Il client. La libreria lo rifiuta se ha ancora qualcosa da consegnare: lo consegna il controllo stesso
    //    (il compito client e' fermo, quindi nessuno lo fa insieme a lui).
    if (g_client && clientFermo && pulita) {
        esp_err_t r = ESP_FAIL;
        for (int i = 0; i < 20; i++) {
            r = usb_host_client_deregister(g_client);
            if (r != ESP_ERR_INVALID_STATE) break;
            usb_host_client_handle_events(g_client, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (r == ESP_OK) g_client = nullptr;
        else ESP_LOGE(TAG, "usb_host_client_deregister: %s", esp_err_to_name(r));
    }
    pulita = pulita && !g_client;
    // 3. I dispositivi (anche quelli attaccati ma non aperti: non supportati, guasti).
    if (g_installata && pulita) {
        xEventGroupClearBits(g_bit, BIT_TUTTI_LIBERI);
        const esp_err_t r = usb_host_device_free_all();
        if (r == ESP_ERR_NOT_FINISHED) {
            if (!aspettaBit(BIT_TUTTI_LIBERI, 1000)) ESP_LOGW(TAG, "i dispositivi non si liberano");
        } else if (r != ESP_OK) {
            ESP_LOGW(TAG, "usb_host_device_free_all: %s", esp_err_to_name(r));
        }
        vTaskDelay(pdMS_TO_TICKS(20));  // il demone finisce il giro (la porta si disabilita)
    }
    // 4. Il demone.
    bool demoneFermo = !g_compitoDemone;
    if (g_compitoDemone) {
        g_fermaDemone.store(true);
        if (g_installata) usb_host_lib_unblock();
        demoneFermo = aspettaBit(BIT_DEMONE_FERMO, 500);
        if (!demoneFermo) ESP_LOGE(TAG, "il demone della USB non si ferma");
    }
    // 5. La libreria. usb_host_uninstall guarda solo i flag degli eventi: con un dispositivo ancora nella libreria
    //    hub_uninstall fa abort (ESP_ERROR_CHECK di hcd_port_deinit, che rifiuta finche' c'e' la pipe di EP0), e
    //    ce n'e' uno se e' arrivato dopo il passo 3 (debounce e reset durano 0,3 s) o se la sua enumerazione non
    //    finisce (enum.c non ha timeout). Quindi gli eventi li gira il controllo (il demone e' fermo), anche
    //    debounce, reset ed enumerazione di un attacco appena arrivato, e si disinstalla solo a zero dispositivi.
    //    Uno che dopo 1 s c'e' ancora si stacca spegnendo la porta, subito dopo il giro degli eventi e dal solo
    //    compito che li gira, come fa il test "sudden disconnection" di ESP-IDF (test_usb_host_plugging.c):
    //    hub_root_stop fa assert se trova un evento della porta in sospeso. In quel giro niente
    //    usb_host_device_free_all: un dispositivo liberato prima che la libreria veda il distacco lascerebbe
    //    hub.c senza il suo nodo (ESP_ERROR_CHECK di dev_tree_node_dev_gone). Dopo 3 s si rinuncia: la libreria
    //    resta installata, la porta torna lo stesso al computer (g_sporca, qui sotto).
    if (g_installata && pulita && demoneFermo) {
        esp_err_t r = ESP_ERR_INVALID_STATE;
        bool spenta = false;
        const TickType_t inizio = xTaskGetTickCount();
        while (xTaskGetTickCount() - inizio < pdMS_TO_TICKS(3000)) {
            uint32_t f = 0;
            usb_host_lib_handle_events(0, &f);
            usb_host_lib_info_t info = {};
            if (usb_host_lib_info(&info) != ESP_OK) break;
            if (info.num_devices == 0) {
                r = usb_host_uninstall();
                if (r != ESP_ERR_INVALID_STATE) break;
            } else if (!spenta && xTaskGetTickCount() - inizio >= pdMS_TO_TICKS(1000)) {
                spenta = true;  // una volta sola
                const esp_err_t p = usb_host_lib_set_root_port_power(false);
                ESP_LOGW(TAG, "un dispositivo non si libera: spengo la porta (%s)", esp_err_to_name(p));
            } else {
                usb_host_device_free_all();  // anche quello arrivato dopo il passo 3
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (r == ESP_OK) g_installata = false;
        else ESP_LOGE(TAG, "usb_host_uninstall: %s", esp_err_to_name(r));
    }
    // 6. I compiti, solo se parcheggiati (mai dentro una funzione della libreria).
    if (g_compitoClient && clientFermo) {
        ESP_LOGI(TAG, "stack del compito client: %u byte mai usati", unsigned(uxTaskGetStackHighWaterMark(g_compitoClient)));
        vTaskDeleteWithCaps(g_compitoClient);
        g_compitoClient = nullptr;
    }
    if (g_compitoDemone && demoneFermo) {
        ESP_LOGI(TAG, "stack del demone: %u byte mai usati", unsigned(uxTaskGetStackHighWaterMark(g_compitoDemone)));
        vTaskDeleteWithCaps(g_compitoDemone);
        g_compitoDemone = nullptr;
    }
    if (g_installata || g_client || g_compitoClient || g_compitoDemone) {
        // Qualcosa e' rimasto nella libreria: la porta torna lo stesso al computer, ma fino al prossimo riavvio
        // le cuffie non si riaccendono (accendi() dice DRIVER).
        ESP_LOGE(TAG, "spegnimento incompleto: cuffie disattivate fino al riavvio");
        g_sporca = true;
    }
    // 7. La porta al computer, il log alla console.
    ridaiPhy();
    ESP_LOGI(TAG, "porta USB-C al computer; stack del controllo: %u byte mai usati",
             unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    chiudiScatola();
    g_timer = false;
    g_secondiProva = 0;
    const uint32_t dopo = ramInterna();
    portENTER_CRITICAL(&g_muxStat);
    g->stat.ramInternaDopo = dopo;
    portEXIT_CRITICAL(&g_muxStat);
    scheda::tieni(scheda::Tieni::CUFFIE, false);
    impostaStato(Stato::SPENTE, motivo);
    ESP_LOGI(TAG, "RAM interna libera: %u prima, %u minima, %u dopo", unsigned(g->stat.ramInternaPrima),
             unsigned(g->stat.ramInternaMin), unsigned(dopo));
    stampaScatola();
}

void entra(uint32_t secondiProva) {
    // Col risparmio di energia la CPU scende a 40 MHz e il PLL si spegne: la USB host (che nella libreria di
    // ESP-IDF non ha un blocco suo) resta alla frequenza piena fino alla fine di esci().
    scheda::tieni(scheda::Tieni::CUFFIE, true);
    const uint32_t prima = ramInterna();
    portENTER_CRITICAL(&g_muxStat);
    const uint32_t sessioni = g->stat.sessioni + 1;
    g->stat = Cuffie::Statistiche();
    g->stat.sessioni = sessioni;
    g->stat.stato = Stato::ACCENSIONE;
    g->stat.ramInternaPrima = g->stat.ramInternaMin = prima;
    g->msBase = g->saltatiBase = g->erroriBase = 0;
    g->vivo = false;
    portEXIT_CRITICAL(&g_muxStat);
    g_sottoflussi.store(0);
    g_fermaClient.store(false);
    g_fermaDemone.store(false);
    xEventGroupClearBits(g_bit, BIT_CLIENT_FERMO | BIT_DEMONE_FERMO | BIT_TUTTI_LIBERI);
    g_tentativi = 0;
    g_visto = false;
    g_vuoto = false;
    g_secondiProva = secondiProva;
    g_inizioProva = xTaskGetTickCount();
    // Quello che sta uscendo dalla console (la risposta a @CUFFIE!, il log) esce prima che la porta cambi.
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    apriScatola();
    ESP_LOGI(TAG, "accendo (sessione %u%s): RAM interna libera %u, blocco DMA piu' grande %u", unsigned(sessioni),
             secondiProva ? ", prova da banco" : "", unsigned(prima),
             unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)));
    usb_host_config_t cfg = {};
    cfg.skip_phy_setup = false;      // la libreria prende la PHY interna per il controller USB-OTG
    cfg.root_port_unpowered = true;  // la porta si accende col client gia' registrato: nessun NEW_DEV perso
    cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
    esp_err_t r = usb_host_install(&cfg);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install: %s", esp_err_to_name(r));
        esci(Errore::DRIVER);
        return;
    }
    g_installata = true;
    if (xTaskCreatePinnedToCoreWithCaps(compitoDemone, "cuffie_lib", STACK_DEMONE, nullptr, PRIO_DEMONE,
                                        &g_compitoDemone, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
        g_compitoDemone = nullptr;
        ESP_LOGE(TAG, "compito del demone non creato");
        esci(Errore::DRIVER);
        return;
    }
    usb_host_client_config_t cc = {};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 5;
    cc.async.client_event_callback = eventoClient;
    cc.async.callback_arg = nullptr;
    r = usb_host_client_register(&cc, &g_client);
    if (r != ESP_OK) {
        g_client = nullptr;
        ESP_LOGE(TAG, "usb_host_client_register: %s", esp_err_to_name(r));
        esci(Errore::DRIVER);
        return;
    }
    if (xTaskCreatePinnedToCoreWithCaps(compitoClient, "cuffie_usb", STACK_CLIENT, nullptr, PRIO_CLIENT,
                                        &g_compitoClient, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
        g_compitoClient = nullptr;
        ESP_LOGE(TAG, "compito client non creato");
        esci(Errore::DRIVER);
        return;
    }
    r = usb_host_lib_set_root_port_power(true);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "porta USB non accesa: %s", esp_err_to_name(r));
        esci(Errore::DRIVER);
        return;
    }
    avviaTimer();
    impostaStato(Stato::IN_ATTESA, Errore::NESSUNO);
}

// ---------------------------------------------------------------- compito di controllo

void gestisci(const Msg& m) {
    const Stato s = statoAdesso();
    switch (m.cmd) {
    case Cmd::ACCENDI:
        if (s == Stato::ACCENSIONE) entra(m.secondi);
        break;
    case Cmd::SPEGNI:
        // Uno rimasto in coda dalla sessione di prima (chiesto durante SPEGNIMENTO, o due OK di fila) non spegne
        // quella nuova.
        if (s != Stato::SPENTE && m.sessione == g_sessione.load()) esci(Errore::NESSUNO);
        break;
    case Cmd::NUOVO:
        // Un dispositivo nuovo (con una porta sola e senza hub ce n'e' al massimo uno). Uno che non risponde
        // si riprova una volta sola: chiudi() lo stacca spegnendo la porta e si ripresenta da solo ogni volta.
        if (s == Stato::IN_ATTESA || s == Stato::NON_SUPPORTATE || (s == Stato::ERRORE && g_tentativi < MAX_TENTATIVI))
            collega(m.indirizzo);
        else if (s == Stato::ERRORE)
            ESP_LOGW(TAG, "dispositivo %u di nuovo: non lo riapro (non risponde)", unsigned(m.indirizzo));
        break;
    case Cmd::VIA:
        // DEV_GONE arriva solo per un dispositivo aperto; uno vecchio (gia' chiuso) non conta.
        if (m.handle && m.handle == g->disp.handle() && (s == Stato::COLLEGATE || s == Stato::COLLEGAMENTO)) scollega();
        break;
    case Cmd::DESCRITTORE_LUNGO:
        if (s == Stato::IN_ATTESA) {
            g_visto = false;  // non e' stato enumerato: il distacco non si vede, restano i 2 minuti
            impostaStato(Stato::NON_SUPPORTATE, Errore::DESCRITTORE_LUNGO);
        }
        break;
    }
}

// Ogni PERIODO_MS a cuffie accese.
void servizio() {
    const uint32_t libera = ramInterna();
    portENTER_CRITICAL(&g_muxStat);
    if (libera < g->stat.ramInternaMin) g->stat.ramInternaMin = libera;
    portEXIT_CRITICAL(&g_muxStat);
    const TickType_t ora = xTaskGetTickCount();
    const Stato s = statoAdesso();
    if (s == Stato::COLLEGATE && !g->disp.attivo()) {
        // Staccate (DEV_GONE arriva dopo, o e' gia' arrivato) oppure stream morto con le cuffie attaccate.
        if (g->disp.staccato()) scollega();
        else guasto();
    } else if ((s == Stato::NON_SUPPORTATE || s == Stato::ERRORE) && g_visto) {
        // Dopo un apri() fallito il dispositivo e' chiuso e DEV_GONE non arriva: il distacco si vede dal numero
        // dei dispositivi della libreria.
        usb_host_lib_info_t info = {};
        if (usb_host_lib_info(&info) == ESP_OK && info.num_devices == 0) {
            if (!g_vuoto) {
                g_vuoto = true;
                g_vuotoDa = ora;
            } else if (ora - g_vuotoDa >= pdMS_TO_TICKS(MS_TOLTO)) {
                g_vuoto = false;
                g_visto = false;
                g_tentativi = 0;
                impostaStato(Stato::IN_ATTESA, Errore::NESSUNO);  // i 2 minuti continuano
            }
        } else {
            g_vuoto = false;
        }
    }
    if (g_secondiProva && ora - g_inizioProva >= pdMS_TO_TICKS(g_secondiProva * 1000)) {
        ESP_LOGI(TAG, "fine della prova da banco (%u s)", unsigned(g_secondiProva));
        esci(Errore::NESSUNO);
        return;
    }
    if (g_timer && statoAdesso() != Stato::COLLEGATE &&
        ora - g_inizioTimer >= pdMS_TO_TICKS(Cuffie::SECONDI_SENZA_CUFFIE * 1000)) {
        ESP_LOGI(TAG, "%u s senza cuffie: torno al computer", unsigned(Cuffie::SECONDI_SENZA_CUFFIE));
        esci(Errore::SENZA_CUFFIE);
    }
}

void compitoControllo(void*) {
    for (;;) {
        Msg m;
        const bool accese = statoAdesso() != Stato::SPENTE;
        if (xQueueReceive(g_coda, &m, accese ? pdMS_TO_TICKS(PERIODO_MS) : portMAX_DELAY) == pdTRUE) gestisci(m);
        if (statoAdesso() != Stato::SPENTE && statoAdesso() != Stato::ACCENSIONE) servizio();
    }
}

bool rifiuta(Errore e) {
    g_errore.store(uint32_t(e));
    ESP_LOGW(TAG, "non accese: %s", Cuffie::testoErrore(e));
    return false;
}

bool accendiCon(uint32_t secondiProva) {
    if (!g || !g_controllo) return rifiuta(Errore::DRIVER);
    if (statoAdesso() != Stato::SPENTE) return statoAdesso() != Stato::SPEGNIMENTO;  // gia' accese
    if (!secondiProva && usb_serial_jtag_is_connected()) return rifiuta(Errore::COMPUTER);
    if (otaStatus().active) return rifiuta(Errore::AGGIORNAMENTO);
    if (ramInterna() < RAM_INTERNA_MIN ||
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) < BLOCCO_DMA_MIN)
        return rifiuta(Errore::MEMORIA);
    if (g_sporca) return rifiuta(Errore::DRIVER);
    uint32_t atteso = uint32_t(Stato::SPENTE);
    if (!g_stato.compare_exchange_strong(atteso, uint32_t(Stato::ACCENSIONE))) return true;  // un altro, adesso
    g_sessione.fetch_add(1);  // uno SPEGNI chiesto prima di qui era per la sessione di prima
    // Prima ci si annuncia (ACCENSIONE), poi si guarda l'aggiornamento: otaStart() fa lo stesso al contrario,
    // quindi se partono insieme almeno uno dei due vede l'altro e rinuncia.
    Errore no = Errore::NESSUNO;
    if (otaStatus().active) no = Errore::AGGIORNAMENTO;
    else if (!preparaUscita()) no = Errore::MEMORIA;
    if (no != Errore::NESSUNO) {
        g_stato.store(uint32_t(Stato::SPENTE));
        return rifiuta(no);
    }
    g_errore.store(uint32_t(Errore::NESSUNO));
    avvisa(Stato::ACCENSIONE, Errore::NESSUNO);
    Msg m;
    m.cmd = Cmd::ACCENDI;
    m.secondi = secondiProva;
    if (xQueueSend(g_coda, &m, 0) != pdTRUE) {
        g_stato.store(uint32_t(Stato::SPENTE));
        avvisa(Stato::SPENTE, Errore::DRIVER);
        return rifiuta(Errore::DRIVER);
    }
    return true;
}
}  // namespace

// ---------------------------------------------------------------- Cuffie

Cuffie& cuffie() {
    static Cuffie c;
    return c;
}

void Cuffie::avvia() {
    if (g) return;
    void* p = heap_caps_calloc(1, sizeof(Mondo), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char* riga = static_cast<char*>(heap_caps_malloc(RIGA, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_coda = xQueueCreate(8, sizeof(Msg));
    g_bit = xEventGroupCreate();
    g_muScatola = xSemaphoreCreateMutex();
    if (!p || !riga || !g_coda || !g_bit || !g_muScatola) {
        ESP_LOGE(TAG, "niente memoria: cuffie USB-C non disponibili");
        heap_caps_free(p);
        heap_caps_free(riga);
        return;  // g resta nullptr: accendi() dice DRIVER (code e semafori restano: non si usano)
    }
    Mondo* mondo = new (p) Mondo();
    mondo->riga = riga;
    g = mondo;
    if (xTaskCreatePinnedToCoreWithCaps(compitoControllo, "cuffie", STACK_CONTROLLO, nullptr, PRIO_CONTROLLO,
                                        &g_controllo, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
        g_controllo = nullptr;
        ESP_LOGE(TAG, "compito delle cuffie non creato");
        return;
    }
    scheda::audio().setUscitaEsterna(&g_uscita);
}

bool Cuffie::accendi() { return accendiCon(0); }

bool Cuffie::prova(uint32_t secondi) { return accendiCon(secondi ? secondi : 1); }

void Cuffie::spegni() {
    if (!g_coda || statoAdesso() == Stato::SPENTE) return;
    Msg m;
    m.cmd = Cmd::SPEGNI;
    m.sessione = g_sessione.load();
    xQueueSend(g_coda, &m, 0);
}

bool Cuffie::spegniEAspetta(uint32_t attesaMs) {
    if (statoAdesso() == Stato::SPENTE) return true;
    spegni();
    const TickType_t inizio = xTaskGetTickCount();
    while (statoAdesso() != Stato::SPENTE) {
        if (xTaskGetTickCount() - inizio >= pdMS_TO_TICKS(attesaMs)) {
            ESP_LOGW(TAG, "non ancora spente dopo %u ms", unsigned(attesaMs));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

Cuffie::Stato Cuffie::stato() const { return statoAdesso(); }

bool Cuffie::accese() const { return statoAdesso() != Stato::SPENTE; }

bool Cuffie::pronta() const { return g_pronta.load(std::memory_order_acquire); }

Cuffie::Errore Cuffie::errore() const { return Errore(g_errore.load()); }

std::string Cuffie::nome() const {
    const Stato s = statoAdesso();
    if (!g || (s != Stato::COLLEGATE && s != Stato::NON_SUPPORTATE && s != Stato::ERRORE)) return "";
    char n[sizeof(g->stat.nome)];
    portENTER_CRITICAL(&g_muxStat);
    std::memcpy(n, g->stat.nome, sizeof(n));
    portEXIT_CRITICAL(&g_muxStat);
    n[sizeof(n) - 1] = '\0';
    return n;
}

std::string Cuffie::testo() const {
    switch (statoAdesso()) {
    case Stato::ACCENSIONE:
    case Stato::IN_ATTESA:
    case Stato::COLLEGAMENTO: return "in attesa";
    case Stato::COLLEGATE: {
        const std::string n = nome();
        return n.empty() ? "collegate" : n;
    }
    case Stato::NON_SUPPORTATE: return "non supportate";
    case Stato::ERRORE: return "errore";
    default: return "spente";  // SPENTE, SPEGNIMENTO
    }
}

const char* Cuffie::testoStato(Stato s) {
    switch (s) {
    case Stato::SPENTE: return "spente";
    case Stato::ACCENSIONE: return "accensione";
    case Stato::IN_ATTESA: return "in attesa";
    case Stato::COLLEGAMENTO: return "collegamento";
    case Stato::COLLEGATE: return "collegate";
    case Stato::NON_SUPPORTATE: return "non supportate";
    case Stato::ERRORE: return "errore";
    case Stato::SPEGNIMENTO: return "spegnimento";
    }
    return "?";
}

const char* Cuffie::testoErrore(Errore e) {
    switch (e) {
    case Errore::NESSUNO: return "nessuno";
    case Errore::COMPUTER: return "c'e' un computer collegato";
    case Errore::AGGIORNAMENTO: return "aggiornamento Wi-Fi acceso";
    case Errore::MEMORIA: return "poca memoria";
    case Errore::NON_AUDIO: return "non audio";
    case Errore::SOLO_UAC3: return "solo USB Audio 3";
    case Errore::PACCHETTI_GRANDI: return "pacchetti troppo grandi";
    case Errore::DESCRITTORE_LUNGO: return "descrittore troppo lungo";
    case Errore::FORMATO: return "formato non adatto";
    case Errore::DISPOSITIVO: return "il dispositivo non risponde";
    case Errore::DRIVER: return "USB host non partita";
    case Errore::SENZA_CUFFIE: return "2 minuti senza cuffie";
    }
    return "?";
}

void Cuffie::setVolume(uint8_t v) { g_volume.store(v > 10 ? 10 : v, std::memory_order_relaxed); }

uint8_t Cuffie::volume() const { return g_volume.load(std::memory_order_relaxed); }

Cuffie::Statistiche Cuffie::statistiche() const {
    Statistiche s;
    if (!g) return s;
    const uac::Statistiche d = g->disp.statistiche();  // fuori dal lucchetto: legge atomici e l'ora
    portENTER_CRITICAL(&g_muxStat);
    s = g->stat;
    const bool vivo = g->vivo;
    const uint32_t ms = g->msBase + (vivo ? d.msStream : 0);
    s.saltati = g->saltatiBase + (vivo ? d.saltati : 0);
    s.errori = g->erroriBase + (vivo ? d.errori : 0);
    portEXIT_CRITICAL(&g_muxStat);
    s.secondiSuonati = ms / 1000;
    s.sottoflussi = g_sottoflussi.load(std::memory_order_relaxed);
    s.stato = statoAdesso();
    s.errore = errore();
    return s;
}

}  // namespace app

#endif  // CONFIG_SCHEDA_TDECK_PLUS
