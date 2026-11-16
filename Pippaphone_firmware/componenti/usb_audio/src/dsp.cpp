// Cuffie USB-C: ricampionatore, coda, pacchetti da 1 ms, volume e formato del dispositivo (vedi dsp.h).
// Niente ESP-IDF: si compila anche nei test sul PC (test/test_dsp.cpp, anche da solo e ottimizzato con
// componenti/usb_audio/test_host_dsp).
#include "usb_audio/dsp.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace uac {

namespace {
void* allocaStandard(size_t byte, void*) { return std::malloc(byte); }
void liberaStandard(void* p, void*) { std::free(p); }
}  // namespace

Allocatore allocatoreStandard() {
    Allocatore a;
    a.alloca = allocaStandard;
    a.libera = liberaStandard;
    a.ctx = nullptr;
    return a;
}

// ---- ricampionatore ----
//
// Ogni campione d'uscita e' una somma pesata degli ultimi prese_ campioni d'ingresso. I pesi sono una sinc
// finestrata (Kaiser) presa nel punto in cui il campione d'uscita cade fra due campioni d'ingresso: la tabella
// ne tiene FASI + 1 righe (posizione 0, 1/64, ..., 64/64 fra due campioni) e fra due righe vicine si interpola
// linearmente. Per ogni campione d'uscita quindi due prodotti scalari da prese_, mescolati con alfa.
//
// Le scelte (la risposta del filtro calcolata a parte, e misurata coi toni nei test):
//  - taglio a 0,45 x la piu' bassa delle due frequenze, Kaiser beta 7, 32 prese in salita: -0,19 dB a 0,4 x la
//    piu' bassa, -6 dB a 0,45, -33 dB a 0,5, sotto -78 dB da 0,6 in su (dove cadono le prime immagini dei toni
//    fino a 0,4). Con 24 prese allo stesso taglio non si sta nei limiti: con beta 6 ondulazione 0,5 dB e
//    immagini a -68 dB, con beta 7 ondulazione 0,66 dB (il test della qualita' lo vede).
//  - In discesa (uscita piu' lenta, in pratica solo 48000 -> 44100) il filtro taglia alla frequenza d'uscita e
//    nel tempo e' piu' lungo: prese = 32 x hzIn / hzOut, per eccesso a un multiplo di 4 (36 da 48000 a 44100).
//  - 64 fasi: le immagini dovute all'interpolazione fra le fasi stanno sotto -88 dB. Con 128 non migliora
//    niente (comanda il filtro) e la tabella raddoppia.
//  - Ogni riga ha somma 1: guadagno in continua esattamente 1 in ogni posizione.
//  - La storia e' in float (si converte una volta per campione d'ingresso, non per presa) e ogni campione si
//    scrive due volte (in i e in i + prese_): la finestra degli ultimi prese_ campioni e' sempre contigua.
//
// Dove cade il prossimo campione d'uscita: q = fase_ x passoOut_ + resto_, in unita' di 1 / (FASI x passoOut_)
// di campione d'ingresso dopo l'ultimo preso, meno il ritardo fisso del filtro (prese_ / 2). Ogni campione
// d'uscita porta avanti q di FASI x passoIn_, cioe' di hzIn / hzOut campioni esatti; quando fase_ passa FASI
// serve un campione d'ingresso nuovo. Solo interi che non crescono: niente deriva, mai, e lo stesso risultato
// comunque arrivi l'ingresso (blocchi da 1 o da 4096).
//
// Costo stimato sull'ESP32-S3 (contato sul codice di xtensa-esp32s3-elf-g++ 14.2 con le opzioni di IDF, -O2;
// non misurato sulla scheda). Con 32 prese il prodotto si srotola tutto: 96 lsi e 64 madd.s/mul.s con indirizzi
// costanti, 5 istruzioni per presa. Un campione d'uscita fa 193 istruzioni in tutto (con posizione, alfa,
// arrotondamento e saturazione), uno d'ingresso 16 (conversione e doppia scrittura nella storia). Da 44100 a
// 48000 (0,92 d'ingresso per ognuno d'uscita) sono circa 210 istruzioni per campione d'uscita; contando un
// ciclo perso per ogni valore usato subito dopo la sua lettura (28 casi: il compilatore non sa le latenze della
// FPU) e 2 cicli per salto preso, circa 250 cicli: 48000 x 250 = 12 milioni di cicli al secondo, il 4-5% di un
// core a 240 MHz. Da 48000 a 44100 (36 prese) circa 290 cicli a 44100: anche li' il 5%. Da 48000 a 48000 e' una
// copia. La tabella (8,3 KB) e la storia stanno in PSRAM: ogni campione legge 2 righe da 128 byte, che restano
// nella cache dati (32 KB) mentre si lavora un blocco; se un altro compito le butta fuori, rileggerle costa al
// massimo qualche decina di cicli per campione all'inizio del blocco.
// impacchetta() a 16 bit stereo: 14 istruzioni per frame, circa lo 0,4% di un core a 48 kHz (compito client USB).

namespace {
constexpr uint32_t FASI = 64;           // posizioni fra due campioni d'ingresso (potenza di 2: / e % sono shift)
constexpr uint32_t PRESE_SALITA = 32;   // prese con hzOut > hzIn (multiplo di 4, per il ciclo interno)
constexpr float TAGLIO = 0.45f;         // del piu' basso fra hzIn e hzOut
constexpr float BETA = 7.0f;            // finestra di Kaiser
constexpr float PI_F = 3.14159265358979f;

// Prese per una coppia di frequenze: 32 in salita; in discesa il filtro taglia alla frequenza d'uscita ed e'
// lungo hzIn / hzOut volte tanto (per eccesso, multiplo di 4).
constexpr uint32_t presePer(uint32_t hzIn, uint32_t hzOut) {
    return hzIn <= hzOut ? PRESE_SALITA : 4 * ((PRESE_SALITA / 4 * hzIn + hzOut - 1) / hzOut);
}
constexpr uint32_t PRESE_DISCESA = presePer(48000, 44100);  // l'unica discesa che capita davvero
static_assert(PRESE_SALITA % 4 == 0 && PRESE_DISCESA == 36, "il ciclo interno vuole multipli di 4");

uint32_t mcd(uint32_t a, uint32_t b) {
    while (b) {
        const uint32_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

// Funzione di Bessel modificata di prima specie di ordine 0 (per la finestra di Kaiser): la serie
// somma ((x/2)^k / k!)^2 converge in una ventina di termini per x <= 10.
float besselI0(float x) {
    const float q = 0.25f * x * x;
    float s = 1.0f, t = 1.0f;
    for (int k = 1; k < 60; k++) {
        t *= q / float(k * k);
        s += t;
        if (t < s * 1e-9f) break;
    }
    return s;
}

// La tabella: riga r (posizione r / FASI fra due campioni d'ingresso), presa k (k = 0 e' il campione piu' vecchio
// della finestra, prese - 1 l'ultimo arrivato). Il campione d'uscita cade prese/2 campioni prima dell'ultimo,
// piu' r / FASI: la presa k ne dista t = prese/2 - 1 - k + r/FASI. fc in cicli per campione d'ingresso.
void riempiTabella(float* c, uint32_t prese, float fc) {
    const float meta = 0.5f * float(prese);
    const float invI0 = 1.0f / besselI0(BETA);
    for (uint32_t r = 0; r <= FASI; r++) {
        float* riga = c + size_t(r) * prese;
        float somma = 0.0f;
        for (uint32_t k = 0; k < prese; k++) {
            const float t = meta - 1.0f - float(k) + float(r) / float(FASI);
            const float u = t / meta;  // -1 .. 1 sui bordi della finestra
            const float w = besselI0(BETA * std::sqrt(std::fmax(0.0f, 1.0f - u * u))) * invI0;
            const float x = 2.0f * fc * t;
            const float s = std::fabs(x) < 1e-6f ? 1.0f : std::sin(PI_F * x) / (PI_F * x);
            riga[k] = 2.0f * fc * s * w;
            somma += riga[k];
        }
        const float inv = 1.0f / somma;
        for (uint32_t k = 0; k < prese; k++) riga[k] *= inv;
    }
}

// Un campione d'uscita: la finestra w (prese campioni, il piu' vecchio per primo) per le righe c0 e c0 + prese,
// mescolate con alfa (0..1). Quattro somme parziali indipendenti: madd.s ha qualche ciclo di latenza e con
// un accumulatore solo ogni prodotto aspetterebbe il precedente.
// P fisso (32 in salita, 36 da 48000 a 44100): il ciclo si srotola tutto e gli indirizzi sono costanti (lsi con
// offset), 3 letture e 2 madd.s per presa e nient'altro. P = 0: prese qualunque (multiplo di 4, almeno 4).
template <uint32_t P>
inline float prodotto(const float* w, const float* c0, uint32_t prese, float alfa) {
    const uint32_t n = P ? P : prese;
    const float* c1 = c0 + n;
    float a0 = w[0] * c0[0], b0 = w[0] * c1[0];
    float a1 = w[1] * c0[1], b1 = w[1] * c1[1];
    if constexpr (P != 0) {
#pragma GCC unroll 32
        for (uint32_t k = 2; k < P; k += 2) {
            a0 += w[k] * c0[k];
            b0 += w[k] * c1[k];
            a1 += w[k + 1] * c0[k + 1];
            b1 += w[k + 1] * c1[k + 1];
        }
    } else {
        a0 += w[2] * c0[2];
        b0 += w[2] * c1[2];
        a1 += w[3] * c0[3];
        b1 += w[3] * c1[3];
        for (uint32_t k = 4; k < n; k += 4) {
            a0 += w[k] * c0[k];
            b0 += w[k] * c1[k];
            a1 += w[k + 1] * c0[k + 1];
            b1 += w[k + 1] * c1[k + 1];
            a0 += w[k + 2] * c0[k + 2];
            b0 += w[k + 2] * c1[k + 2];
            a1 += w[k + 3] * c0[k + 3];
            b1 += w[k + 3] * c1[k + 3];
        }
    }
    const float y0 = a0 + a1, y1 = b0 + b1;
    return y0 + alfa * (y1 - y0);
}

// Da float a int16, arrotondato e saturato. |y| resta molto sotto 2^31 (la somma dei moduli di una riga e'
// intorno a 1,3), quindi la conversione in int32 e' sicura; la saturazione si fa dopo, sugli interi.
// y + 32768,5 troncato e' floor(y + 0,5) per tutto quello che non satura (y >= -32768,5).
inline int16_t arrotonda(float y) {
    int32_t v = int32_t(y + 32768.5f) - 32768;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return int16_t(v);
}

// Lo stato di elabora() in variabili semplici, per il ciclo qui sotto.
struct Ciclo {
    float* storia;
    const float* coeff;
    uint32_t prese, passoOut, incFase, incResto;
    float invOut;
    uint32_t pos, fase, resto, manca;
};

// Il ciclo di elabora(). Un giro prende un campione d'ingresso (se ne manca uno al prossimo d'uscita) oppure
// calcola un campione d'uscita. Lo stato e' copiato in variabili locali, che restano nei registri: le scritture
// nella storia (float) potrebbero toccare i campi float di Ciclo e il compilatore li rileggerebbe a ogni giro.
template <uint32_t P>
size_t ciclo(Ciclo& c, const int16_t* in, size_t nIn, size_t& letti, int16_t* out, size_t capOut) {
    float* const storia = c.storia;
    const float* const coeff = c.coeff;
    const uint32_t prese = P ? P : c.prese;
    const uint32_t passoOut = c.passoOut, incFase = c.incFase, incResto = c.incResto;
    const float invOut = c.invOut;
    uint32_t pos = c.pos, fase = c.fase, resto = c.resto, manca = c.manca;
    const int16_t* p = in;
    const int16_t* const fineIn = in + nIn;
    int16_t* q = out;
    int16_t* const fineOut = out + capOut;
    for (;;) {
        if (manca) {
            if (p == fineIn) break;
            const float v = float(*p++);
            storia[pos] = v;
            storia[pos + prese] = v;
            if (++pos == prese) pos = 0;
            manca--;
            continue;
        }
        if (q == fineOut) break;
        *q++ = arrotonda(prodotto<P>(storia + pos, coeff + size_t(fase) * prese, prese, float(resto) * invOut));
        // Avanti di hzIn / hzOut campioni d'ingresso: FASI x passoIn_ unita' = incFase righe + incResto.
        resto += incResto;
        fase += incFase;
        if (resto >= passoOut) {
            resto -= passoOut;
            fase++;
        }
        manca = fase / FASI;  // quante volte si e' passato un campione d'ingresso (anche piu' di una, in discesa)
        fase %= FASI;
    }
    c.pos = pos;
    c.fase = fase;
    c.resto = resto;
    c.manca = manca;
    letti = size_t(p - in);
    return size_t(q - out);
}
}  // namespace

Ricampionatore::~Ricampionatore() { libera(); }

void Ricampionatore::libera() {
    // La storia sta nello stesso blocco della tabella: si libera solo coeff_.
    if (coeff_ && a_.libera) a_.libera(coeff_, a_.ctx);
    coeff_ = nullptr;
    storia_ = nullptr;
    prese_ = 0;
    passoIn_ = passoOut_ = 0;
    pronto_ = false;
    copia_ = false;
    hzIn_ = hzOut_ = 0;
}

bool Ricampionatore::configura(uint32_t hzIn, uint32_t hzOut, const Allocatore& a) {
    if (hzIn < HZ_MIN || hzIn > HZ_MAX || hzOut < HZ_MIN || hzOut > HZ_MAX) {
        libera();
        return false;
    }
    if (hzIn == hzOut) {
        // Copia diretta: niente tabella (quella di prima si libera) e niente ritardo.
        libera();
        copia_ = true;
        hzIn_ = hzIn;
        hzOut_ = hzOut;
        pronto_ = true;
        return true;
    }
    const uint32_t m = mcd(hzIn, hzOut);
    const uint32_t pIn = hzIn / m, pOut = hzOut / m;
    // Stesso rapporto (anche con frequenze diverse, 22050 -> 44100 e 24000 -> 48000) e stesso allocatore: la
    // tabella e' gia' quella giusta, si azzera solo la storia.
    const bool stessa = coeff_ && pIn == passoIn_ && pOut == passoOut_ && a.alloca == a_.alloca &&
                        a.libera == a_.libera && a.ctx == a_.ctx;
    if (!stessa) {
        libera();
        if (!a.alloca) return false;
        const uint32_t prese = presePer(hzIn, hzOut);
        const size_t n = size_t(FASI + 1) * prese + 2 * size_t(prese);
        float* blocco = static_cast<float*>((a.alloca)(n * sizeof(float), a.ctx));  // (): alloca e' anche una macro
        if (!blocco) return false;
        a_ = a;
        coeff_ = blocco;
        storia_ = blocco + size_t(FASI + 1) * prese;
        prese_ = prese;
        passoIn_ = pIn;
        passoOut_ = pOut;
        const uint32_t d = FASI * pIn;  // <= 64 x 48000: sta in 32 bit
        incFase_ = d / pOut;
        incResto_ = d % pOut;
        invOut_ = 1.0f / float(pOut);
        const uint32_t piuBassa = hzIn < hzOut ? hzIn : hzOut;
        riempiTabella(coeff_, prese, TAGLIO * float(piuBassa) / float(hzIn));
    }
    hzIn_ = hzIn;
    hzOut_ = hzOut;
    copia_ = false;
    pronto_ = true;
    azzera();
    return true;
}

void Ricampionatore::azzera() {
    if (storia_) std::memset(storia_, 0, 2 * size_t(prese_) * sizeof(float));
    posStoria_ = 0;
    fase_ = 0;
    resto_ = 0;
    manca_ = 1;  // il primo campione d'uscita viene dopo il primo d'ingresso
}

size_t Ricampionatore::elabora(const int16_t* in, size_t nIn, size_t& letti, int16_t* out, size_t capOut) {
    if (!pronto_) {
        letti = nIn;  // spento: si butta tutto, chi chiama non resta in un giro
        return 0;
    }
    if (copia_) {
        const size_t n = nIn < capOut ? nIn : capOut;
        if (n) std::memcpy(out, in, n * sizeof(int16_t));
        letti = n;
        return n;
    }
    Ciclo c{storia_, coeff_, prese_, passoOut_, incFase_, incResto_, invOut_, posStoria_, fase_, resto_, manca_};
    size_t n;
    if (prese_ == PRESE_SALITA) {
        n = ciclo<PRESE_SALITA>(c, in, nIn, letti, out, capOut);
    } else if (prese_ == PRESE_DISCESA) {
        n = ciclo<PRESE_DISCESA>(c, in, nIn, letti, out, capOut);
    } else {
        n = ciclo<0>(c, in, nIn, letti, out, capOut);
    }
    posStoria_ = c.pos;
    fase_ = c.fase;
    resto_ = c.resto;
    manca_ = c.manca;
    return n;
}

size_t Ricampionatore::uscitaMassima(size_t nIn) const {
    if (!pronto_) return 0;
    if (copia_) return nIn;
    // Il caso peggiore e' lo stato con nessun campione d'ingresso che manca e posizione 0: allora i campioni
    // d'uscita sono ceil((nIn + 1) x hzOut / hzIn). Con nIn assurdi (oltre 2^29) si dice "quanti vuoi".
    if (nIn >= (size_t(1) << 29)) return SIZE_MAX;
    return size_t(((uint64_t(nIn) + 1) * passoOut_ + passoIn_ - 1) / passoIn_);
}

// ---- coda ----

namespace {
// Le posizioni vanno da 0 a 2 x cap - 1 (vedi dsp.h).
inline uint32_t distanza(uint32_t testa, uint32_t coda, uint32_t doppia) {
    return testa >= coda ? testa - coda : testa + doppia - coda;
}
inline uint32_t avanti(uint32_t pos, uint32_t n, uint32_t doppia) {
    pos += n;  // pos < 2^31, n <= 2^30: niente traboccamento
    return pos >= doppia ? pos - doppia : pos;
}
}  // namespace

Anello::~Anello() { distruggi(); }

bool Anello::crea(size_t capacita, const Allocatore& a) {
    distruggi();
    if (capacita == 0 || capacita > (size_t(1) << 30) || !a.alloca) return false;
    buf_ = static_cast<int16_t*>((a.alloca)(capacita * sizeof(int16_t), a.ctx));
    if (!buf_) return false;
    a_ = a;
    cap_ = capacita;
    testa_.store(0, std::memory_order_relaxed);
    coda_.store(0, std::memory_order_relaxed);
    return true;
}

void Anello::distruggi() {
    if (buf_ && a_.libera) a_.libera(buf_, a_.ctx);
    buf_ = nullptr;
    cap_ = 0;
    testa_.store(0, std::memory_order_relaxed);
    coda_.store(0, std::memory_order_relaxed);
}

size_t Anello::scrivi(const int16_t* s, size_t n) {
    if (!buf_ || n == 0) return 0;
    const uint32_t cap = uint32_t(cap_), doppia = 2 * cap;
    const uint32_t t = testa_.load(std::memory_order_relaxed);  // la cambia solo chi scrive
    const uint32_t c = coda_.load(std::memory_order_acquire);   // i posti che il consumatore ha finito di leggere
    const size_t liberi = cap - distanza(t, c, doppia);
    if (n > liberi) n = liberi;
    if (n == 0) return 0;
    const uint32_t i = t < cap ? t : t - cap;
    const size_t primo = n < cap - i ? n : cap - i;
    std::memcpy(buf_ + i, s, primo * sizeof(int16_t));
    if (n > primo) std::memcpy(buf_, s + primo, (n - primo) * sizeof(int16_t));
    testa_.store(avanti(t, uint32_t(n), doppia), std::memory_order_release);  // i campioni prima della testa
    return n;
}

size_t Anello::liberi() const { return buf_ ? cap_ - pieni() : 0; }

size_t Anello::leggi(int16_t* s, size_t n) {
    if (!buf_ || n == 0) return 0;
    const uint32_t cap = uint32_t(cap_), doppia = 2 * cap;
    const uint32_t c = coda_.load(std::memory_order_relaxed);   // la cambia solo chi legge
    const uint32_t t = testa_.load(std::memory_order_acquire);  // e i campioni scritti prima
    const size_t pieni = distanza(t, c, doppia);
    if (n > pieni) n = pieni;
    if (n == 0) return 0;
    const uint32_t i = c < cap ? c : c - cap;
    const size_t primo = n < cap - i ? n : cap - i;
    std::memcpy(s, buf_ + i, primo * sizeof(int16_t));
    if (n > primo) std::memcpy(s + primo, buf_, (n - primo) * sizeof(int16_t));
    coda_.store(avanti(c, uint32_t(n), doppia), std::memory_order_release);  // posti di nuovo liberi
    return n;
}

void Anello::scarta() {
    if (!buf_) return;
    // Tutto quello scritto fino a questo momento; quello che il produttore scrive dopo resta.
    coda_.store(testa_.load(std::memory_order_acquire), std::memory_order_release);
}

size_t Anello::pieni() const {
    if (!buf_) return 0;
    const uint32_t cap = uint32_t(cap_);
    const uint32_t t = testa_.load(std::memory_order_acquire);
    const uint32_t c = coda_.load(std::memory_order_acquire);
    const uint32_t d = distanza(t, c, 2 * cap);
    // Dal produttore o dal consumatore d <= cap sempre. Letta da un terzo compito la coppia puo' non essere
    // dello stesso istante: se il consumatore ha superato la testa letta prima, d esce oltre cap e vuol dire
    // che la coda si e' appena svuotata.
    return d <= cap ? d : 0;
}

// ---- pacchetti da 1 ms ----

void Pacchetti::configura(uint32_t hz) {
    hz_ = hz;
    resto_ = 0;
}

uint16_t Pacchetti::prossimo() {
    resto_ += hz_;
    const uint32_t n = resto_ / 1000;
    resto_ %= 1000;
    return uint16_t(n);
}

uint16_t Pacchetti::massimo() const { return uint16_t((hz_ + 999) / 1000); }

void Pacchetti::azzera() { resto_ = 0; }

// ---- volume e formato del dispositivo ----

namespace {
inline int32_t limitaGuadagno(int32_t g) { return g < 0 ? 0 : (g > GUADAGNO_UNO ? GUADAGNO_UNO : g); }
}  // namespace

int32_t guadagnoDaVolume(uint8_t volume) {
    const int32_t v = volume > 10 ? 10 : volume;
    return v * v * GUADAGNO_UNO / 100;
}

void Guadagno::imposta(int32_t obiettivo, uint32_t campioniRampa) {
    obiettivo = limitaGuadagno(obiettivo);
    if (campioniRampa == 0 || obiettivo == attuale_) {
        subito(obiettivo);
        return;
    }
    // In Q15 x 32768: il guadagno sta fra 0 e 2^30, il passo fra -2^30 e 2^30. La divisione tronca verso lo
    // zero, quindi passo x campioniRampa non supera mai la distanza: la rampa non va oltre l'obiettivo, e
    // l'ultimo campione lo prende esatto.
    obiettivo_ = obiettivo;
    accumulatore_ = attuale_ * GUADAGNO_UNO;
    passo_ = int32_t(int64_t((obiettivo - attuale_) * GUADAGNO_UNO) / int64_t(campioniRampa));
    restanti_ = campioniRampa;
}

void Guadagno::subito(int32_t g) {
    attuale_ = obiettivo_ = limitaGuadagno(g);
    restanti_ = 0;
}

int32_t Guadagno::avanza() {
    if (restanti_ == 0) return attuale_;
    if (--restanti_ == 0) {
        attuale_ = obiettivo_;
    } else {
        accumulatore_ += passo_;
        attuale_ = accumulatore_ >> 15;  // accumulatore_ >= 0 sempre
    }
    return attuale_;
}

namespace {
// Un formato alla volta, senza scelte dentro il ciclo. Little endian a byte, qualunque sia l'allineamento di out.
// |x * g| <= 2^30 (x int16, g 0..2^15): sta in int32 anche raddoppiato per i 32 bit.
template <int BYTE, int CANALI>
size_t impacchettaFormato(const int16_t* mono, size_t n, Guadagno& g, uint8_t* out) {
    uint8_t* p = out;
    for (size_t i = 0; i < n; i++) {
        const int32_t x = mono[i];
        const int32_t prodotto = x * g.avanza();
        uint32_t u;
        if constexpr (BYTE == 2) {
            u = uint32_t((prodotto + 16384) >> 15);  // arrotondato; a 0 dB e' x
        } else if constexpr (BYTE == 3) {
            u = uint32_t(prodotto >> 7);  // a 0 dB e' x * 256
        } else {
            u = uint32_t(prodotto * 2);  // a 0 dB e' x * 65536 (moltiplicazione, non shift di un negativo)
        }
        for (int c = 0; c < CANALI; c++) {
            p[0] = uint8_t(u);
            p[1] = uint8_t(u >> 8);
            if constexpr (BYTE >= 3) p[2] = uint8_t(u >> 16);
            if constexpr (BYTE == 4) p[3] = uint8_t(u >> 24);
            p += BYTE;
        }
    }
    return n * BYTE * CANALI;
}
}  // namespace

size_t impacchetta(const int16_t* mono, size_t n, uint8_t canali, uint8_t byteCampione, Guadagno& g, uint8_t* out) {
    // La rampa gira su una copia locale: le scritture a byte in out potrebbero toccare qualunque cosa, anche g,
    // e il compilatore rileggerebbe lo stato del guadagno dalla memoria a ogni campione.
    Guadagno locale = g;
    size_t byte = 0;
    if (canali == 1) {
        if (byteCampione == 2) byte = impacchettaFormato<2, 1>(mono, n, locale, out);
        if (byteCampione == 3) byte = impacchettaFormato<3, 1>(mono, n, locale, out);
        if (byteCampione == 4) byte = impacchettaFormato<4, 1>(mono, n, locale, out);
    } else if (canali == 2) {
        if (byteCampione == 2) byte = impacchettaFormato<2, 2>(mono, n, locale, out);
        if (byteCampione == 3) byte = impacchettaFormato<3, 2>(mono, n, locale, out);
        if (byteCampione == 4) byte = impacchettaFormato<4, 2>(mono, n, locale, out);
    }
    if (byte) g = locale;  // formato non valido: niente scritto e il guadagno resta com'era
    return byte;
}

size_t silenzio(size_t n, uint8_t canali, uint8_t byteCampione, uint8_t* out) {
    if (canali < 1 || canali > 2 || byteCampione < 2 || byteCampione > 4) return 0;
    const size_t byte = n * canali * byteCampione;
    std::memset(out, 0, byte);
    return byte;
}

}  // namespace uac
