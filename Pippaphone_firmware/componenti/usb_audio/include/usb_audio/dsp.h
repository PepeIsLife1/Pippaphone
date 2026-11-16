// Cuffie USB-C (solo T-Deck Plus): i campioni che vanno alle cuffie. Niente ESP-IDF: si compila e si prova
// anche sul PC (test/).
//  - Ricampionatore: da qualunque frequenza (8000..48000 Hz) a quella del dispositivo (44100 o 48000), mono.
//  - Anello: coda di int16 fra un compito che scrive e uno che legge (anche su core diversi), senza lock.
//  - Pacchetti: quanti frame vanno in ogni pacchetto USB da 1 ms (48 a 48 kHz, 44 o 45 a 44,1 kHz).
//  - Guadagno e impacchetta(): volume con rampa e conversione nel formato del dispositivo.
// La memoria la da' chi le usa (Allocatore): sul T-Deck PSRAM (heap_caps_malloc), sul PC malloc.
//
// Chi le usa (app/cuffie.cpp):
//   compito audio_out:  Ricampionatore::elabora -> Anello::scrivi       (produttore)
//   compito client USB: Anello::leggi -> impacchetta (con Guadagno)     (consumatore, nel Riempitore di
//                       uac_host.h), Pacchetti lo usa uac_host per le lunghezze dei pacchetti.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace uac {

// Attenzione: alloca e' anche una macro con un argomento (<stdlib.h>/<alloca.h>, sul PC e su ESP-IDF):
// a.alloca(byte, ctx) non compila, si chiama come (a.alloca)(byte, ctx). Assegnarla (a.alloca = f) va bene.
struct Allocatore {
    void* (*alloca)(size_t byte, void* ctx) = nullptr;  // nullptr se non c'e' memoria
    void (*libera)(void* p, void* ctx) = nullptr;
    void* ctx = nullptr;
};
// malloc e free (PC, test).
Allocatore allocatoreStandard();

// ---- ricampionatore ----

// Polifase a sinc finestrata (Kaiser), per ogni rapporto fra HZ_MIN e HZ_MAX, mono int16:
//  - taglio a 0,45 x la piu' bassa delle due frequenze; per i toni fino a 0,4 x la piu' bassa, immagini e
//    alias almeno 60 dB sotto il tono (misurati nei test: almeno 75 dB), ondulazione in banda sotto 0,2 dB;
//  - 32 prese quando l'uscita e' piu' veloce, di piu' quando e' piu' lenta (36 da 48000 a 44100: il filtro
//    taglia alla frequenza d'uscita), 64 fasi con interpolazione lineare fra due fasi vicine; tabella in
//    float, (64 + 1) x prese (8,3 KB a 32 prese), piu' la storia (2 x prese float). Costo: due prodotti
//    scalari da `prese` per ogni campione d'uscita (stima in dsp.cpp: il 4-5% di un core a 48 kHz);
//  - passo esatto nel lungo periodo (contatore di fase razionale, niente virgola mobile che deriva): un'ora a
//    44100 Hz in ingresso da' 172 800 000 campioni a 48000 (+-1);
//  - stesso risultato comunque si spezzi l'ingresso (blocchi da 1 campione o da 4096);
//  - satura e non si ribalta (onde quadre a fondo scala, le melodie "quasi quadre" della cassa);
//  - con hzIn == hzOut copia i campioni cosi' come sono, senza ritardo (e senza tabella). Altrimenti il
//    ritardo e' prese/2 campioni d'ingresso (16 a 32 prese: 0,36 ms a 44,1 kHz, 2 ms a 8 kHz).
// Lo usa un compito solo (quello che spinge, audio_out). elabora() non alloca.
class Ricampionatore {
public:
    static constexpr uint32_t HZ_MIN = 8000;
    static constexpr uint32_t HZ_MAX = 48000;

    Ricampionatore() = default;
    ~Ricampionatore();
    Ricampionatore(const Ricampionatore&) = delete;
    Ricampionatore& operator=(const Ricampionatore&) = delete;

    // Prepara la conversione hzIn -> hzOut (tutte e due fra HZ_MIN e HZ_MAX) e azzera la storia. I coefficienti
    // e la storia si allocano qui con `a` (la memoria di prima si libera; tutto si libera nel distruttore).
    // Se il rapporto non cambia non rialloca. false se le frequenze sono fuori dai limiti o manca memoria: il
    // ricampionatore resta spento (pronto() false, hzIn() e hzOut() 0, elabora() non produce niente).
    bool configura(uint32_t hzIn, uint32_t hzOut, const Allocatore& a);
    // Dimentica i campioni passati: il prossimo pezzo riparte dal silenzio (sorgente nuova, dopo svuota()).
    void azzera();
    // Converte: prende da `in` al massimo nIn campioni (quanti, in `letti`) e scrive in `out` al massimo capOut
    // campioni; restituisce quanti ne ha scritti. Si ferma quando finisce l'ingresso o lo spazio: i campioni
    // non letti si ripassano alla chiamata dopo. Spento (pronto() false): letti = nIn e nessun campione.
    size_t elabora(const int16_t* in, size_t nIn, size_t& letti, int16_t* out, size_t capOut);
    // Campioni d'uscita al massimo per nIn d'ingresso, in qualunque stato (per dimensionare out): con un out
    // cosi' grande elabora() prende sempre tutti gli nIn campioni.
    size_t uscitaMassima(size_t nIn) const;
    bool pronto() const { return pronto_; }
    uint32_t hzIn() const { return hzIn_; }
    uint32_t hzOut() const { return hzOut_; }

private:
    // I campi privati li puo' cambiare chi implementa (gli altri vedono solo i metodi pubblici).
    void libera();
    bool pronto_ = false;
    bool copia_ = false;          // hzIn == hzOut
    uint32_t hzIn_ = 0, hzOut_ = 0;
    Allocatore a_{};
    float* coeff_ = nullptr;      // (FASI + 1) righe x prese_; un blocco solo con la storia in fondo
    float* storia_ = nullptr;     // 2 x prese_: ogni campione scritto due volte, la finestra e' contigua
    uint32_t prese_ = 0;
    uint32_t posStoria_ = 0;      // dove comincia la finestra (il campione piu' vecchio), 0 .. prese_ - 1
    // Dove cade il prossimo campione d'uscita dopo l'ultimo d'ingresso, solo con interi (il passo e' esatto):
    // riga fase_ della tabella, piu' resto_ / passoOut_ verso la riga dopo.
    uint32_t fase_ = 0;           // 0 .. FASI - 1
    uint32_t resto_ = 0;          // 0 .. passoOut_ - 1
    uint32_t passoIn_ = 0, passoOut_ = 0;  // hzIn e hzOut divisi per il loro massimo comun divisore
    uint32_t incFase_ = 0, incResto_ = 0;  // di quanto si va avanti per ogni campione d'uscita
    uint32_t manca_ = 0;          // campioni d'ingresso da prendere prima del prossimo d'uscita
    float invOut_ = 0;            // 1 / passoOut_
};

// ---- coda ----

// Coda circolare di int16 fra UN compito che scrive e UN compito che legge, anche su core diversi, senza lock:
// gli indici sono atomici (scrittura release, lettura acquire). La capacita' e' qualunque (non serve una
// potenza di 2) e va bene per sempre (niente problemi quando i contatori girano, anche dopo giorni a 48 kHz).
class Anello {
public:
    Anello() = default;
    ~Anello();
    Anello(const Anello&) = delete;
    Anello& operator=(const Anello&) = delete;

    // capacita in campioni. false se manca memoria. crea() e distruggi() a compiti fermi (non sono thread-safe).
    bool crea(size_t capacita, const Allocatore& a);
    void distruggi();
    size_t capacita() const { return cap_; }

    // Lato produttore.
    size_t scrivi(const int16_t* s, size_t n);   // copia quanto ci sta e dice quanti
    size_t liberi() const;
    // Lato consumatore.
    size_t leggi(int16_t* s, size_t n);          // copia al massimo n e dice quanti
    void scarta();                               // butta tutto quello che c'e' adesso
    // Tutti e due (il valore di un attimo fa).
    size_t pieni() const;

private:
    // I campi privati li puo' cambiare chi implementa.
    // Le due posizioni vanno da 0 a 2 x cap_ - 1 (il campione e' in buf_[pos % cap_]): cosi' pieno (distanza
    // cap_) e vuoto (distanza 0) si distinguono senza lasciare un posto libero, e non c'e' mai un contatore
    // che arriva a 2^32 (con una capacita' che non e' una potenza di 2 l'indice salterebbe).
    int16_t* buf_ = nullptr;
    size_t cap_ = 0;
    std::atomic<uint32_t> testa_{0};   // dove scrive il produttore (lo cambia solo lui)
    std::atomic<uint32_t> coda_{0};    // dove legge il consumatore (lo cambia solo lui)
    Allocatore a_{};
};

// ---- pacchetti da 1 ms ----

// Frame in ogni pacchetto da 1 ms alla frequenza del dispositivo, con un accumulatore: acc += hz;
// frame = acc / 1000; acc %= 1000. A 48000 sempre 48; a 44100 nove pacchetti da 44 e uno da 45 ogni 10 ms. In
// un'ora la somma e' esattamente hz x 3600.
class Pacchetti {
public:
    void configura(uint32_t hz);   // e riparte dall'inizio della sequenza
    uint16_t prossimo();           // frame del pacchetto dopo
    uint16_t massimo() const;      // ceil(hz / 1000): il pacchetto piu' lungo (per dimensionare i trasferimenti)
    void azzera();
    uint32_t hz() const { return hz_; }

private:
    uint32_t hz_ = 48000;
    uint32_t resto_ = 0;
};

// ---- volume e formato del dispositivo ----

// Guadagno in Q15: GUADAGNO_UNO (32768) = 0 dB, i campioni passano identici.
constexpr int32_t GUADAGNO_UNO = 32768;
// Volume 0..10 con la curva della cassa (scheda/src/audio.cpp): (v/10)^2, cioe' v*v*32768/100. 10 = 0 dB,
// 8 = -3,9 dB, 6 = -8,9 dB, 4 = -15,9 dB, 2 = -28 dB, 1 = -40 dB, 0 = muto. Sopra 10 vale 10.
int32_t guadagnoDaVolume(uint8_t volume);

// Guadagno che va verso l'obiettivo con una rampa lineare, un passo per campione: niente "zip" quando cambia
// il volume, niente clic quando si sfuma. Lo usa un compito solo (il consumatore).
class Guadagno {
public:
    // Nuovo obiettivo (0..GUADAGNO_UNO, fuori si limita): ci arriva in campioniRampa campioni (0 = subito),
    // partendo da dove e' adesso (anche a meta' di un'altra rampa). Il campione numero campioniRampa ha gia'
    // l'obiettivo; la rampa sale (o scende) sempre, senza mai passare l'obiettivo.
    void imposta(int32_t obiettivo, uint32_t campioniRampa);
    void subito(int32_t g);       // attuale = obiettivo = g
    int32_t attuale() const { return attuale_; }
    int32_t obiettivo() const { return obiettivo_; }
    bool fermo() const { return attuale_ == obiettivo_; }
    // Il guadagno per il prossimo campione, e un passo della rampa.
    int32_t avanza();

private:
    // I campi privati li puo' cambiare chi implementa.
    int32_t attuale_ = GUADAGNO_UNO;
    int32_t obiettivo_ = GUADAGNO_UNO;
    int32_t passo_ = 0;           // per campione, in Q15 x 32768 (sta in 32 bit: |passo| <= 2^30)
    int32_t accumulatore_ = 0;    // il guadagno in Q15 x 32768 durante la rampa (0 .. 2^30)
    uint32_t restanti_ = 0;       // campioni alla fine della rampa
};

// n campioni mono -> n frame nel formato del dispositivo (little endian), col guadagno campione per campione
// (g.avanza() una volta per campione):
//  - canali 1 o 2: in stereo lo stesso campione a sinistra e a destra;
//  - byteCampione 2: (x * g + 16384) >> 15, 16 bit;
//    byteCampione 3: (x * g) >> 7, 24 bit (a 0 dB e' x * 256);
//    byteCampione 4: x * g * 2, 32 bit allineati in alto (a 0 dB e' x * 65536; va bene anche per 24 bit in 4
//    byte). Niente shift a sinistra di numeri negativi (in C++17 non e' definito).
// Con g fermo a GUADAGNO_UNO e 2 byte i campioni escono identici. Scrive n x canali x byteCampione byte e
// restituisce i byte scritti (0 con canali o byteCampione non validi).
size_t impacchetta(const int16_t* mono, size_t n, uint8_t canali, uint8_t byteCampione, Guadagno& g, uint8_t* out);
// n frame di silenzio nel formato del dispositivo; restituisce i byte scritti.
size_t silenzio(size_t n, uint8_t canali, uint8_t byteCampione, uint8_t* out);

}  // namespace uac
