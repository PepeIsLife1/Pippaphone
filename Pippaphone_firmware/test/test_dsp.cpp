// Test del DSP delle cuffie USB-C del T-Deck (componenti/usb_audio, dsp.h): ricampionatore, coda, pacchetti,
// volume e formato del dispositivo. I test che misurano stampano i numeri (righe rientrate) prima di ok/ERRORE.
// Girano in usb_audio_test (test/CMakeLists.txt, filtro "dsp_"), oppure da soli e ottimizzati in
// componenti/usb_audio/test_host_dsp (dsp_test), dove c'e' anche la misura dei tempi (dsp_bench).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

#include "testing.h"
#include "usb_audio/dsp.h"

using namespace uac;

namespace {

constexpr double PI = 3.14159265358979323846;

double dB(double rapporto) { return 20.0 * std::log10(rapporto > 1e-30 ? rapporto : 1e-30); }

// Tutto l'ingresso attraverso il ricampionatore, a blocchi di `blocco` campioni e con un'uscita di `bloccoOut`
// campioni per chiamata. Alla fine raccoglie anche quello che si puo' ancora calcolare senza ingresso nuovo,
// cosi' il risultato non dipende da come si e' spezzato.
std::vector<int16_t> ricampiona(Ricampionatore& r, const int16_t* in, size_t n, size_t blocco, size_t bloccoOut) {
    std::vector<int16_t> out, tmp(bloccoOut);
    size_t i = 0;
    while (i < n) {
        const size_t k = std::min(blocco, n - i);
        size_t presi = 0;
        while (presi < k) {
            size_t letti = 0;
            const size_t m = r.elabora(in + i + presi, k - presi, letti, tmp.data(), tmp.size());
            out.insert(out.end(), tmp.begin(), tmp.begin() + std::ptrdiff_t(m));
            presi += letti;
            if (m == 0 && letti == 0) return out;  // fermo senza motivo: i confronti sulle lunghezze lo vedono
        }
        i += k;
    }
    for (;;) {
        size_t letti = 0;
        const size_t m = r.elabora(in, 0, letti, tmp.data(), tmp.size());
        if (m == 0) break;
        out.insert(out.end(), tmp.begin(), tmp.begin() + std::ptrdiff_t(m));
    }
    return out;
}

std::vector<int16_t> ricampiona(uint32_t hzIn, uint32_t hzOut, const std::vector<int16_t>& in, size_t blocco = 4096,
                                size_t bloccoOut = 4096) {
    Ricampionatore r;
    if (!r.configura(hzIn, hzOut, allocatoreStandard())) return {};
    return ricampiona(r, in.data(), in.size(), blocco, bloccoOut);
}

// FFT complessa radice 2, sul posto (n potenza di 2).
void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * PI / double(len);
        for (size_t i = 0; i < n; i += len) {
            for (size_t j = 0; j < len / 2; j++) {
                const std::complex<double> w = std::polar(1.0, ang * double(j));
                const std::complex<double> u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
            }
        }
    }
}

// Blackman-Harris a 4 termini: lobi laterali a -92 dB, lobo principale di +-4 righe.
std::vector<double> finestra(size_t n) {
    std::vector<double> w(n);
    for (size_t i = 0; i < n; i++) {
        const double x = 2.0 * PI * double(i) / double(n);
        w[i] = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x);
    }
    return w;
}

struct Spettro {
    double ampiezza = 0;  // del tono, in campioni int16
    double spuria = 0;    // la riga piu' alta fuori dal tono (immagini, alias, distorsione), in campioni
    double fSpuria = 0;   // dove (Hz)
};

// Il tono a f Hz (con la DFT a un punto, alla frequenza esatta) e la riga piu' alta fuori da +-8 righe intorno
// al tono, su n campioni di y (n potenza di 2) a fs Hz, con la finestra di Blackman-Harris.
Spettro analizza(const int16_t* y, size_t n, double f, double fs) {
    const std::vector<double> w = finestra(n);
    double sw = 0;
    for (double v : w) sw += v;
    Spettro s;
    std::complex<double> t = 0;
    for (size_t i = 0; i < n; i++) t += double(y[i]) * w[i] * std::polar(1.0, -2.0 * PI * f * double(i) / fs);
    s.ampiezza = 2.0 * std::abs(t) / sw;
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; i++) a[i] = double(y[i]) * w[i];
    fft(a);
    const double kTono = f / fs * double(n);
    for (size_t k = 0; k <= n / 2; k++) {
        if (std::fabs(double(k) - kTono) <= 8.0) continue;
        const double v = 2.0 * std::abs(a[k]) / sw;
        if (v > s.spuria) {
            s.spuria = v;
            s.fSpuria = double(k) * fs / double(n);
        }
    }
    return s;
}

// Un tono di ampiezza A a f Hz, ricampionato da hzIn a hzOut; si analizzano N campioni d'uscita dopo che il
// filtro si e' assestato.
Spettro misuraTono(uint32_t hzIn, uint32_t hzOut, double f, double A) {
    constexpr size_t N = 16384, SALTA = 1024;
    const size_t nIn = size_t(double(N + SALTA) * hzIn / hzOut) + 256;
    std::vector<int16_t> x(nIn);
    for (size_t i = 0; i < nIn; i++) x[i] = int16_t(std::lround(A * std::sin(2.0 * PI * f * double(i) / hzIn)));
    const std::vector<int16_t> y = ricampiona(hzIn, hzOut, x);
    if (y.size() < N + SALTA) return {};
    return analizza(y.data() + SALTA, N, f, hzOut);
}

// Contatore per vedere quante volte il ricampionatore e la coda allocano e liberano.
struct Conta {
    int allocazioni = 0, liberazioni = 0;
    size_t byte = 0;      // dell'ultima allocazione
    bool fallisci = false;
};
void* contaAlloca(size_t byte, void* ctx) {
    Conta* c = static_cast<Conta*>(ctx);
    if (c->fallisci) return nullptr;
    c->allocazioni++;
    c->byte = byte;
    return std::malloc(byte);
}
void contaLibera(void* p, void* ctx) {
    static_cast<Conta*>(ctx)->liberazioni++;
    std::free(p);
}
Allocatore allocatoreConta(Conta& c) {
    Allocatore a;
    a.alloca = contaAlloca;
    a.libera = contaLibera;
    a.ctx = &c;
    return a;
}

std::vector<int16_t> rumore(size_t n, uint32_t seme, int ampiezza) {
    std::mt19937 rng(seme);
    std::uniform_int_distribution<int> d(-ampiezza, ampiezza);
    std::vector<int16_t> x(n);
    for (size_t i = 0; i < n; i++) x[i] = int16_t(d(rng));
    return x;
}

}  // namespace

// ---- ricampionatore ----

TEST(dsp_ricampionatore_configura) {
    Conta c;
    {
        Ricampionatore r;
        const Allocatore a = allocatoreConta(c);
        CHECK(!r.pronto());
        CHECK(!r.configura(7999, 48000, a));
        CHECK(!r.configura(8000, 48001, a));
        CHECK(!r.configura(48000, 7999, a));
        CHECK(!r.configura(48001, 44100, a));
        CHECK(!r.configura(0, 0, a));
        CHECK(!r.pronto());
        CHECK_EQ(c.allocazioni, 0);

        CHECK(r.configura(44100, 48000, a));
        CHECK(r.pronto());
        CHECK_EQ(r.hzIn(), 44100u);
        CHECK_EQ(r.hzOut(), 48000u);
        CHECK_EQ(c.allocazioni, 1);
        const size_t byteSalita = c.byte;
        CHECK_EQ(byteSalita, size_t((65 * 32 + 2 * 32) * 4));  // tabella (64 + 1) x 32 e storia 2 x 32, float
        // Stesso rapporto (1:2) con frequenze diverse: la tabella resta quella.
        CHECK(r.configura(22050, 44100, a));
        CHECK_EQ(c.allocazioni, 2);
        CHECK_EQ(c.liberazioni, 1);
        CHECK(r.configura(24000, 48000, a));
        CHECK_EQ(c.allocazioni, 2);
        CHECK_EQ(r.hzIn(), 24000u);
        CHECK(r.configura(48000, 44100, a));
        CHECK_EQ(c.allocazioni, 3);
        const size_t byteDiscesa = c.byte;
        CHECK_EQ(byteDiscesa, size_t((65 * 36 + 2 * 36) * 4));  // in discesa 36 prese
        // Uguali: copia, niente tabella.
        CHECK(r.configura(48000, 48000, a));
        CHECK_EQ(c.allocazioni, 3);
        CHECK_EQ(c.liberazioni, 3);
        CHECK_EQ(r.uscitaMassima(100), size_t(100));
        // Fuori dai limiti dopo una configurazione buona: spento, si butta l'ingresso.
        CHECK(r.configura(16000, 48000, a));
        CHECK(!r.configura(16000, 96000, a));
        CHECK(!r.pronto());
        CHECK_EQ(r.hzIn(), 0u);
        CHECK_EQ(c.liberazioni, 4);
        int16_t in[10] = {1, 2, 3}, out[10] = {};
        size_t letti = 0;
        CHECK_EQ(r.elabora(in, 10, letti, out, 10), size_t(0));
        CHECK_EQ(letti, size_t(10));
        CHECK_EQ(r.uscitaMassima(10), size_t(0));
        // Senza memoria: false e spento; la copia invece non ne chiede.
        c.fallisci = true;
        CHECK(!r.configura(44100, 48000, a));
        CHECK(!r.pronto());
        CHECK(r.configura(44100, 44100, a));
        c.fallisci = false;
        CHECK(r.configura(8000, 48000, a));
        std::printf("    tabella e storia: %zu byte in salita (32 prese), %zu da 48000 a 44100 (36)\n", byteSalita,
                    byteDiscesa);
    }
    CHECK_EQ(c.allocazioni, c.liberazioni);  // il distruttore libera
}

TEST(dsp_ricampionatore_copia) {
    // Con hzIn == hzOut i campioni escono identici e senza ritardo, comunque si spezzino.
    const std::vector<int16_t> x = rumore(10000, 7, 32767);
    for (const uint32_t hz : {8000u, 44100u, 48000u}) {
        for (const size_t b : {size_t(1), size_t(7), size_t(4096)}) {
            Ricampionatore r;
            CHECK(r.configura(hz, hz, allocatoreStandard()));
            const std::vector<int16_t> y = ricampiona(r, x.data(), x.size(), b, b + 3);
            CHECK(y == x);
        }
    }
    Ricampionatore r;
    CHECK(r.configura(44100, 44100, allocatoreStandard()));
    int16_t out[5];
    size_t letti = 0;
    CHECK_EQ(r.elabora(x.data(), 100, letti, out, 5), size_t(5));  // lo spazio finisce prima
    CHECK_EQ(letti, size_t(5));
    CHECK(std::memcmp(out, x.data(), sizeof(out)) == 0);
}

TEST(dsp_ricampionatore_qualita) {
    // Da 8000, 11025, 16000, 22050, 32000, 44100 e 48000 Hz verso 48000 e 44100: ondulazione in banda fino a
    // 0,4 x la frequenza piu' bassa (toni da 0,02 a 0,4), immagini e alias (la riga piu' alta fuori dal tono, in
    // tutto lo spettro d'uscita) per gli stessi toni, guadagno in continua.
    const uint32_t ingressi[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000};
    const uint32_t uscite[] = {48000, 44100};
    const double toni[] = {0.02, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4};
    constexpr double A = 16384;  // -6 dBFS
    double peggioreRipple = 0, peggioreSpuria = -300, peggioreContinua = 0;
    for (const uint32_t hzOut : uscite) {
        for (const uint32_t hzIn : ingressi) {
            const double basso = std::min(hzIn, hzOut);
            double minDb = 1e9, maxDb = -1e9, spuriaDb = -300, rSpuria = 0, fSpuria = 0, a04 = 0;
            double spuria3[3] = {-300, -300, -300};  // ai toni 0,1, 0,25 e 0,4
            for (const double r : toni) {
                const Spettro s = misuraTono(hzIn, hzOut, r * basso, A);
                const double g = dB(s.ampiezza / A), sp = dB(s.spuria / s.ampiezza);
                minDb = std::min(minDb, g);
                maxDb = std::max(maxDb, g);
                if (r == 0.4) a04 = g;
                if (r == 0.1) spuria3[0] = sp;
                if (r == 0.25) spuria3[1] = sp;
                if (r == 0.4) spuria3[2] = sp;
                if (sp > spuriaDb) {
                    spuriaDb = sp;
                    rSpuria = r;
                    fSpuria = s.fSpuria;
                }
            }
            // Continua: 16384 costante, la media dopo l'assestamento.
            const std::vector<int16_t> y = ricampiona(hzIn, hzOut, std::vector<int16_t>(20000, 16384));
            double somma = 0;
            size_t n = 0;
            for (size_t i = 1000; i + 1000 < y.size(); i++, n++) somma += y[i];
            const double continua = n ? dB(somma / double(n) / 16384.0) : -300;
            std::printf("    %5u -> %5u: banda %+.3f..%+.3f dB (ondulazione %.3f, a 0,4: %+.3f), continua %+.4f dB, "
                        "immagini a 0,1/0,25/0,4: %.1f/%.1f/%.1f dB, peggiore %.1f dB (tono %.2f, riga a %.0f Hz)\n",
                        unsigned(hzIn), unsigned(hzOut), minDb, maxDb, maxDb - minDb, a04, continua, spuria3[0],
                        spuria3[1], spuria3[2], spuriaDb, rSpuria, fSpuria);
            CHECK(maxDb - minDb <= 0.5);
            CHECK(spuriaDb <= -60.0);
            CHECK(std::fabs(continua) <= 0.1);
            peggioreRipple = std::max(peggioreRipple, maxDb - minDb);
            peggioreSpuria = std::max(peggioreSpuria, spuriaDb);
            peggioreContinua = std::max(peggioreContinua, std::fabs(continua));
        }
    }
    std::printf("    in tutto: ondulazione massima %.3f dB (limite 0,5), immagini/alias al peggio %.1f dB sotto il "
                "tono (limite -60), continua entro %.4f dB (limite 0,1)\n",
                peggioreRipple, peggioreSpuria, peggioreContinua);
}

TEST(dsp_ricampionatore_alias_discesa) {
    // Da 48000 a 44100 cio' che sta sopra 22050 Hz si ripiega sotto: a 0,55 x 44100 = 24255 Hz non si arriva
    // (l'ingresso finisce a 24000), quindi si misura alla fine della banda d'ingresso. Solo per sapere quanto:
    // lo 0,45 di taglio lascia la zona 22,05-24 kHz nella transizione (finisce a 20,1-22,05 kHz, non udibile).
    for (const double f : {22500.0, 23000.0, 23500.0}) {
        constexpr size_t N = 16384, SALTA = 1024;
        const size_t nIn = size_t(double(N + SALTA) * 48000 / 44100) + 256;
        std::vector<int16_t> x(nIn);
        for (size_t i = 0; i < nIn; i++)
            x[i] = int16_t(std::lround(16384 * std::sin(2.0 * PI * f * double(i) / 48000)));
        const std::vector<int16_t> y = ricampiona(48000, 44100, x);
        CHECK(y.size() >= N + SALTA);
        if (y.size() < N + SALTA) continue;
        const Spettro s = analizza(y.data() + SALTA, N, 44100 - f, 44100);
        std::printf("    48000 -> 44100: un tono a %.0f Hz torna a %.0f Hz %.1f dB sotto\n", f, 44100 - f,
                    dB(s.ampiezza / 16384));
    }
}

TEST(dsp_ricampionatore_lungo_periodo) {
    // Un'ora a 44100 Hz verso 48000 deve dare 172 800 000 campioni (+-1): il contatore di fase e' razionale.
    // Anche il contrario e qualche altro rapporto. Solo il conto, a blocchi da 4096; ogni blocco deve entrare
    // tutto in un'uscita grande uscitaMassima(4096).
    struct Caso {
        uint32_t hzIn, hzOut, secondi;
    };
    // L'ora intera prende circa 2 s a -O2 e 9 s senza ottimizzazione (usb_audio_test); gli altri 10 minuti.
    const Caso casi[] = {{44100, 48000, 3600}, {48000, 44100, 600}, {8000, 44100, 600}, {22050, 48000, 600},
                         {32000, 44100, 600}};
    std::vector<int16_t> in = rumore(4096, 3, 20000);
    for (const Caso& k : casi) {
        Ricampionatore r;
        CHECK(r.configura(k.hzIn, k.hzOut, allocatoreStandard()));
        std::vector<int16_t> out(r.uscitaMassima(in.size()));
        const auto t0 = std::chrono::steady_clock::now();
        uint64_t resta = uint64_t(k.hzIn) * k.secondi, n = 0;
        bool tuttiPresi = true;
        while (resta) {
            const size_t b = resta < in.size() ? size_t(resta) : in.size();
            size_t letti = 0;
            n += r.elabora(in.data(), b, letti, out.data(), out.size());
            tuttiPresi = tuttiPresi && letti == b;
            resta -= b;
        }
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const uint64_t atteso = uint64_t(k.hzOut) * k.secondi;
        const int64_t scarto = int64_t(n) - int64_t(atteso);
        std::printf("    %5u -> %5u per %u s: %llu campioni (attesi %llu, scarto %lld) in %.1f s\n", unsigned(k.hzIn),
                    unsigned(k.hzOut), unsigned(k.secondi), static_cast<unsigned long long>(n),
                    static_cast<unsigned long long>(atteso), static_cast<long long>(scarto), s);
        CHECK(scarto >= -1 && scarto <= 1);
        CHECK(tuttiPresi);
    }
}

TEST(dsp_ricampionatore_blocchi) {
    // Stesso risultato, campione per campione, con blocchi d'ingresso da 1, 7, 256 e 4096 e uscite da 1, 5 e 4096.
    std::vector<int16_t> x = rumore(30000, 11, 20000);
    for (size_t i = 0; i < x.size(); i++) x[i] = int16_t(x[i] / 2 + std::lround(8000 * std::sin(0.01 * double(i))));
    const uint32_t coppie[][2] = {{44100, 48000}, {8000, 48000}, {16000, 44100}, {22050, 48000}, {48000, 44100},
                                  {32000, 48000}, {11025, 44100}};
    for (const auto& c : coppie) {
        const std::vector<int16_t> rif = ricampiona(c[0], c[1], x, x.size(), 100000);
        // Quanti ne devono uscire: tutti quelli che l'ingresso permette, ceil(n x hzOut / hzIn).
        CHECK_EQ(rif.size(), size_t((uint64_t(x.size()) * c[1] + c[0] - 1) / c[0]));
        bool uguali = true;
        for (const size_t b : {size_t(1), size_t(7), size_t(256), size_t(4096)})
            for (const size_t bo : {size_t(1), size_t(5), size_t(4096)})
                uguali = uguali && ricampiona(c[0], c[1], x, b, bo) == rif;
        CHECK(uguali);
        if (!uguali) std::printf("    %u -> %u: uscite diverse coi blocchi\n", unsigned(c[0]), unsigned(c[1]));
    }
}

TEST(dsp_ricampionatore_uscita_massima) {
    // Con un'uscita grande uscitaMassima(n) l'ingresso entra sempre tutto, in qualunque stato.
    std::vector<int16_t> x = rumore(5000, 5, 10000);
    std::mt19937 rng(9);
    bool ok = true;
    size_t peggiore = 0;  // quanto la stima supera il vero, al massimo
    for (const auto& c : {std::pair<uint32_t, uint32_t>{8000, 48000}, {44100, 48000}, {48000, 44100}, {8000, 44100}}) {
        Ricampionatore r;
        CHECK(r.configura(c.first, c.second, allocatoreStandard()));
        std::vector<int16_t> out(r.uscitaMassima(64));
        for (int k = 0; k < 20000; k++) {
            const size_t n = rng() % 65;
            size_t letti = 0;
            const size_t m = r.elabora(x.data() + rng() % 4000, n, letti, out.data(), r.uscitaMassima(n));
            ok = ok && letti == n && m <= r.uscitaMassima(n);
            peggiore = std::max(peggiore, r.uscitaMassima(n) - m);
        }
    }
    CHECK(ok);
    std::printf("    la stima supera il vero di %zu campioni al massimo\n", peggiore);
    CHECK(peggiore <= 8);
}

TEST(dsp_ricampionatore_saturazione) {
    // Onde quadre a fondo scala (+32767 / -32768, 100 Hz, un secondo): il filtro va oltre il fondo scala
    // (Gibbs, circa il 9%), e li' l'uscita deve fermarsi a 32767 / -32768, non ribaltarsi. Un ribaltamento
    // cambierebbe segno in mezzo a un tratto piatto: i cambi di segno devono essere quanti i fronti. Si conta
    // dal primo tratto alto: prima c'e' il gradino dal silenzio, che oscilla intorno a zero (Gibbs prima del
    // fronte) e cambia segno per conto suo.
    const uint32_t coppie[][2] = {{8000, 48000}, {16000, 48000}, {22050, 48000}, {44100, 48000}, {48000, 44100},
                                  {32000, 44100}, {8000, 44100}};
    for (const auto& c : coppie) {
        const size_t periodo = c[0] / 100;
        std::vector<int16_t> x(c[0]);
        size_t fronti = 0;
        for (size_t i = 0; i < x.size(); i++) {
            x[i] = (i % periodo) < periodo / 2 ? int16_t(32767) : int16_t(-32768);
            if (i && x[i] != x[i - 1]) fronti++;
        }
        const std::vector<int16_t> y = ricampiona(c[0], c[1], x);
        size_t alti = 0, bassi = 0, cambi = 0;
        int segno = 0;
        bool partito = false;
        for (const int16_t v : y) {
            alti += v == 32767;
            bassi += v == -32768;
            partito = partito || v > 16384;
            if (!partito) continue;
            const int s = v > 0 ? 1 : (v < 0 ? -1 : 0);
            if (s && segno && s != segno) cambi++;
            if (s) segno = s;
        }
        std::printf("    %5u -> %5u: %zu fronti, %zu cambi di segno, %zu campioni a +32767 e %zu a -32768\n",
                    unsigned(c[0]), unsigned(c[1]), fronti, cambi, alti, bassi);
        CHECK(cambi + 1 >= fronti && cambi <= fronti);
        CHECK(alti > 0 && bassi > 0);
    }
}

TEST(dsp_ricampionatore_azzera) {
    // Dopo azzera() si riparte dal silenzio: lo stesso di un ricampionatore appena configurato.
    const std::vector<int16_t> a = rumore(3000, 21, 30000), b = rumore(3000, 22, 30000);
    for (const auto& c : {std::pair<uint32_t, uint32_t>{44100, 48000}, {48000, 44100}, {8000, 48000}}) {
        Ricampionatore r1, r2;
        CHECK(r1.configura(c.first, c.second, allocatoreStandard()));
        CHECK(r2.configura(c.first, c.second, allocatoreStandard()));
        ricampiona(r1, a.data(), 1234, 100, 100);  // a meta' di un pezzo
        r1.azzera();
        const std::vector<int16_t> y1 = ricampiona(r1, b.data(), b.size(), 333, 4096);
        const std::vector<int16_t> y2 = ricampiona(r2, b.data(), b.size(), 4096, 4096);
        CHECK(y1 == y2);
        // E dal silenzio: dopo azzera() un ingresso di zeri da' solo zeri.
        r1.azzera();
        const std::vector<int16_t> zeri(500, 0);
        const std::vector<int16_t> y3 = ricampiona(r1, zeri.data(), zeri.size(), 500, 4096);
        bool tuttiZero = !y3.empty();
        for (const int16_t v : y3) tuttiZero = tuttiZero && v == 0;
        CHECK(tuttiZero);
        // configura() di nuovo con lo stesso rapporto vale come azzera().
        CHECK(r1.configura(c.first, c.second, allocatoreStandard()));
        CHECK(ricampiona(r1, b.data(), b.size(), 4096, 4096) == y2);
    }
}

// ---- coda ----

TEST(dsp_anello_base) {
    Conta c;
    {
        Anello a;
        CHECK(!a.crea(0, allocatoreConta(c)));
        CHECK(a.crea(10, allocatoreConta(c)));
        CHECK_EQ(a.capacita(), size_t(10));
        CHECK_EQ(a.pieni(), size_t(0));
        CHECK_EQ(a.liberi(), size_t(10));
        int16_t in[16], out[16];
        for (int i = 0; i < 16; i++) in[i] = int16_t(100 + i);
        CHECK_EQ(a.scrivi(in, 7), size_t(7));
        CHECK_EQ(a.scrivi(in + 7, 9), size_t(3));  // ci stanno solo 3
        CHECK_EQ(a.pieni(), size_t(10));
        CHECK_EQ(a.liberi(), size_t(0));
        CHECK_EQ(a.scrivi(in, 1), size_t(0));
        CHECK_EQ(a.leggi(out, 4), size_t(4));
        CHECK(out[0] == 100 && out[3] == 103);
        CHECK_EQ(a.scrivi(in + 10, 4), size_t(4));  // fa il giro dell'anello
        CHECK_EQ(a.leggi(out, 16), size_t(10));
        bool ordine = true;
        for (int i = 0; i < 10; i++) ordine = ordine && out[i] == int16_t(104 + i);
        CHECK(ordine);
        CHECK_EQ(a.leggi(out, 1), size_t(0));
        // scarta: butta tutto quello che c'e'.
        CHECK_EQ(a.scrivi(in, 5), size_t(5));
        a.scarta();
        CHECK_EQ(a.pieni(), size_t(0));
        CHECK_EQ(a.liberi(), size_t(10));
        CHECK_EQ(a.leggi(out, 1), size_t(0));
        CHECK_EQ(a.scrivi(in + 3, 2), size_t(2));
        CHECK_EQ(a.leggi(out, 5), size_t(2));
        CHECK(out[0] == 103 && out[1] == 104);
        // Tanti giri dell'anello con la capacita' che non e' una potenza di 2.
        bool giri = true;
        int16_t v = 0, atteso = 0;
        for (int k = 0; k < 10000; k++) {
            int16_t blocco[7];
            const size_t n = size_t(1 + k % 7);
            for (size_t i = 0; i < n; i++) blocco[i] = v++;
            giri = giri && a.scrivi(blocco, n) == n;
            giri = giri && a.leggi(out, n) == n;
            for (size_t i = 0; i < n; i++) giri = giri && out[i] == atteso++;
        }
        CHECK(giri);
        // Senza memoria.
        Conta niente;
        niente.fallisci = true;
        Anello b;
        CHECK(!b.crea(100, allocatoreConta(niente)));
        CHECK_EQ(b.scrivi(in, 3), size_t(0));
        CHECK_EQ(b.leggi(out, 3), size_t(0));
        CHECK_EQ(b.pieni(), size_t(0));
        CHECK_EQ(b.liberi(), size_t(0));
        b.scarta();
    }
    CHECK_EQ(c.allocazioni, 1);
    CHECK_EQ(c.liberazioni, 1);
}

namespace {
// Due thread: il produttore scrive 0, 1, 2, ... (troncati a 16 bit) a pezzi di lunghezza casuale, il
// consumatore li legge a pezzi casuali e controlla che arrivino tutti, in ordine, una volta sola. Con scarta,
// il consumatore ogni tanto butta quello che c'e': i salti devono stare fra 0 e la capacita', e letti +
// scartati deve fare il totale.
struct EsitoStress {
    uint64_t letti = 0, scartati = 0, scarti = 0;
    uint64_t errori = 0;      // campioni fuori sequenza
    bool troppoPieni = false; // pieni() oltre la capacita'
    bool bloccato = false;    // fermo per troppo tempo
    double secondi = 0;
};

EsitoStress stress(size_t cap, uint32_t totale, bool conScarta) {
    Anello a;
    EsitoStress e;
    if (!a.crea(cap, allocatoreStandard())) {
        e.bloccato = true;
        return e;
    }
    std::atomic<bool> fineProduttore{false}, troppoPieni{false};
    const auto t0 = std::chrono::steady_clock::now();
    std::thread produttore([&] {
        std::mt19937 rng(1);
        int16_t buf[600];
        uint32_t v = 0;
        while (v < totale) {
            size_t n = 1 + rng() % (cap < 600 ? cap + 2 : 600);
            if (n > totale - v) n = totale - v;
            for (size_t i = 0; i < n; i++) buf[i] = int16_t(uint16_t(v + i));
            const size_t fatti = a.scrivi(buf, n);
            if (a.pieni() > cap || a.liberi() > cap) troppoPieni = true;
            v += uint32_t(fatti);  // quelli che non ci stavano si riscrivono al giro dopo
            if (fatti == 0) std::this_thread::yield();
        }
        fineProduttore = true;
    });
    std::mt19937 rng(2);
    int16_t buf[600];
    uint64_t atteso = 0;     // indice (non troncato) del prossimo campione
    uint64_t scartiAperti = 0;  // scarta() dall'ultima lettura: ognuno butta al massimo la capacita'
    auto ultimo = std::chrono::steady_clock::now();
    for (;;) {
        const bool finito = fineProduttore.load();
        if (conScarta && !finito && rng() % 400 == 0) {
            a.scarta();
            e.scarti++;
            scartiAperti++;
            continue;
        }
        const size_t n = a.leggi(buf, 1 + rng() % 600);
        if (a.pieni() > cap) troppoPieni = true;
        for (size_t i = 0; i < n; i++) {
            const uint16_t d = uint16_t(uint16_t(buf[i]) - uint16_t(atteso));
            if (scartiAperti) {
                // Il primo dopo scarta: saltati d campioni, d fra 0 e la capacita' (per ogni scarta).
                if (d > scartiAperti * cap) e.errori++;
                e.scartati += d;
                atteso += d;
                scartiAperti = 0;
            } else if (d != 0) {
                e.errori++;
            }
            atteso++;
            e.letti++;
        }
        const auto ora = std::chrono::steady_clock::now();
        if (n) {
            ultimo = ora;
        } else {
            if (finito && a.pieni() == 0) break;
            if (ora - ultimo > std::chrono::seconds(10)) {
                e.bloccato = true;
                break;
            }
            std::this_thread::yield();
        }
    }
    produttore.join();
    e.troppoPieni = troppoPieni;
    e.secondi = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    // Uno scarta() proprio alla fine (il produttore aveva gia' scritto tutto): la coda buttata non si vede da
    // nessuna lettura, ma deve stare nei limiti.
    if (scartiAperti && atteso <= totale && totale - atteso <= scartiAperti * cap) {
        e.scartati += totale - atteso;
        atteso = totale;
    }
    if (atteso != totale) e.errori++;
    return e;
}
}  // namespace

TEST(dsp_anello_due_thread) {
    constexpr uint32_t TOTALE = 10000000;
    for (const size_t cap : {size_t(9600), size_t(7)}) {
        for (const bool conScarta : {false, true}) {
            const EsitoStress e = stress(cap, TOTALE, conScarta);
            std::printf("    capacita' %4zu%s: %llu letti + %llu scartati (%llu scarta) = %llu, %llu fuori "
                        "sequenza, %.2f s\n",
                        cap, conScarta ? ", con scarta" : "            ", static_cast<unsigned long long>(e.letti),
                        static_cast<unsigned long long>(e.scartati), static_cast<unsigned long long>(e.scarti),
                        static_cast<unsigned long long>(e.letti + e.scartati),
                        static_cast<unsigned long long>(e.errori), e.secondi);
            CHECK(!e.bloccato);
            CHECK_EQ(e.errori, uint64_t(0));
            CHECK(!e.troppoPieni);
            CHECK_EQ(e.letti + e.scartati, uint64_t(TOTALE));
            if (!conScarta) CHECK_EQ(e.letti, uint64_t(TOTALE));
            if (conScarta) CHECK(e.scarti > 0);
        }
    }
}

// ---- pacchetti da 1 ms ----

TEST(dsp_pacchetti) {
    Pacchetti p;
    p.configura(44100);
    CHECK_EQ(p.massimo(), uint16_t(45));
    int da44 = 0, da45 = 0, altri = 0;
    for (int i = 0; i < 10; i++) {
        const uint16_t f = p.prossimo();
        da44 += f == 44;
        da45 += f == 45;
        altri += f != 44 && f != 45;
    }
    CHECK_EQ(da44, 9);
    CHECK_EQ(da45, 1);
    CHECK_EQ(altri, 0);
    // Un secondo e un'ora, per ogni frequenza: la somma e' esatta e nessun pacchetto supera massimo().
    for (const uint32_t hz : {8000u, 11025u, 16000u, 22050u, 24000u, 32000u, 44100u, 48000u}) {
        p.configura(hz);
        uint64_t somma = 0;
        uint16_t min = 0xFFFF, max = 0;
        for (int i = 0; i < 1000; i++) {
            const uint16_t f = p.prossimo();
            somma += f;
            min = std::min(min, f);
            max = std::max(max, f);
        }
        CHECK_EQ(somma, uint64_t(hz));
        CHECK(max <= p.massimo());
        CHECK_EQ(p.massimo(), uint16_t((hz + 999) / 1000));
        for (int i = 1000; i < 3600000; i++) somma += p.prossimo();
        CHECK_EQ(somma, uint64_t(hz) * 3600);
        if (hz == 44100 || hz == 48000 || hz == 22050)
            std::printf("    %u Hz: pacchetti da %u a %u frame, un secondo %u frame, un'ora %llu\n", unsigned(hz),
                        unsigned(min), unsigned(max), unsigned(hz), static_cast<unsigned long long>(somma));
    }
    // 48000: sempre 48.
    p.configura(48000);
    bool sempre48 = true;
    for (int i = 0; i < 100000; i++) sempre48 = sempre48 && p.prossimo() == 48;
    CHECK(sempre48);
    // configura e azzera ripartono dall'inizio della sequenza.
    p.configura(44100);
    p.prossimo();
    p.azzera();
    uint32_t s = 0;
    for (int i = 0; i < 10; i++) s += p.prossimo();
    CHECK_EQ(s, 441u);
}

// ---- volume e formato del dispositivo ----

TEST(dsp_volume) {
    const int32_t attesi[11] = {0, 327, 1310, 2949, 5242, 8192, 11796, 16056, 20971, 26542, 32768};
    for (uint8_t v = 0; v <= 10; v++) CHECK_EQ(guadagnoDaVolume(v), attesi[v]);
    CHECK_EQ(guadagnoDaVolume(11), GUADAGNO_UNO);
    CHECK_EQ(guadagnoDaVolume(255), GUADAGNO_UNO);
    std::printf("    10 = %.1f dB, 8 = %.1f, 6 = %.1f, 4 = %.1f, 2 = %.1f, 1 = %.1f\n",
                dB(guadagnoDaVolume(10) / 32768.0), dB(guadagnoDaVolume(8) / 32768.0),
                dB(guadagnoDaVolume(6) / 32768.0), dB(guadagnoDaVolume(4) / 32768.0),
                dB(guadagnoDaVolume(2) / 32768.0), dB(guadagnoDaVolume(1) / 32768.0));
}

TEST(dsp_guadagno_rampa) {
    Guadagno g;
    CHECK(g.fermo());
    CHECK_EQ(g.attuale(), GUADAGNO_UNO);
    CHECK_EQ(g.avanza(), GUADAGNO_UNO);
    // Da muto a 0 dB in 480 campioni (10 ms a 48 kHz): sale sempre, mai piu' di 32768/480 + 1 per campione,
    // il campione 480 e' esatto, e ogni campione e' entro 1 dalla retta ideale.
    g.subito(0);
    CHECK_EQ(g.attuale(), 0);
    g.imposta(GUADAGNO_UNO, 480);
    CHECK(!g.fermo());
    CHECK_EQ(g.obiettivo(), GUADAGNO_UNO);
    int32_t prima = 0, passoMax = 0, scartoMax = 0;
    bool sale = true, fermoPresto = false;
    for (int i = 1; i <= 480; i++) {
        const int32_t v = g.avanza();
        sale = sale && v >= prima;
        passoMax = std::max(passoMax, v - prima);
        scartoMax = std::max(scartoMax, int32_t(std::lround(std::fabs(v - 32768.0 * i / 480))));
        if (i < 480 && g.fermo()) fermoPresto = true;
        prima = v;
    }
    CHECK(sale);
    CHECK(!fermoPresto);
    CHECK(g.fermo());
    CHECK_EQ(prima, GUADAGNO_UNO);
    CHECK(passoMax <= 32768 / 480 + 1);
    CHECK(scartoMax <= 1);
    CHECK_EQ(g.avanza(), GUADAGNO_UNO);
    std::printf("    rampa 0 -> 32768 in 480 campioni: passo massimo %d, distanza massima dalla retta %d\n",
                int(passoMax), int(scartoMax));
    // Giu' a 0 in 192 campioni (4 ms): scende sempre e finisce a 0 esatto.
    g.imposta(0, 192);
    bool scende = true;
    prima = GUADAGNO_UNO;
    for (int i = 0; i < 192; i++) {
        const int32_t v = g.avanza();
        scende = scende && v <= prima;
        prima = v;
    }
    CHECK(scende);
    CHECK_EQ(prima, 0);
    CHECK(g.fermo());
    // Cambio di obiettivo a meta' rampa: riparte da dove e', senza salti.
    g.imposta(20000, 1000);
    for (int i = 0; i < 300; i++) prima = g.avanza();
    const int32_t meta = g.attuale();
    CHECK(meta > 5900 && meta < 6100);  // 300/1000 di 20000
    g.imposta(5000, 100);
    const int32_t dopo = g.avanza();
    CHECK(dopo <= meta && meta - dopo <= 12);  // un passo da (6000 - 5000) / 100
    for (int i = 0; i < 99; i++) g.avanza();
    CHECK_EQ(g.attuale(), 5000);
    CHECK(g.fermo());
    // 0 campioni = subito; obiettivi fuori da 0..32768 si limitano.
    g.imposta(1234, 0);
    CHECK_EQ(g.attuale(), 1234);
    CHECK(g.fermo());
    g.imposta(40000, 10);
    CHECK_EQ(g.obiettivo(), GUADAGNO_UNO);
    g.imposta(-5, 10);
    CHECK_EQ(g.obiettivo(), 0);
    g.subito(99999);
    CHECK_EQ(g.attuale(), GUADAGNO_UNO);
    // Obiettivo uguale a dove si e': ferma subito.
    g.imposta(GUADAGNO_UNO, 50);
    CHECK(g.fermo());
}

TEST(dsp_impacchetta_formati) {
    const int16_t x[4] = {0x1234, -2, 32767, -32768};
    uint8_t out[64];
    Guadagno g;  // 0 dB
    // 16 bit: identico, little endian.
    const uint8_t m16[] = {0x34, 0x12, 0xFE, 0xFF, 0xFF, 0x7F, 0x00, 0x80};
    std::memset(out, 0xAA, sizeof(out));
    CHECK_EQ(impacchetta(x, 4, 1, 2, g, out), size_t(8));
    CHECK(std::memcmp(out, m16, 8) == 0);
    CHECK_EQ(out[8], uint8_t(0xAA));
    const uint8_t s16[] = {0x34, 0x12, 0x34, 0x12, 0xFE, 0xFF, 0xFE, 0xFF,
                           0xFF, 0x7F, 0xFF, 0x7F, 0x00, 0x80, 0x00, 0x80};
    CHECK_EQ(impacchetta(x, 4, 2, 2, g, out), size_t(16));
    CHECK(std::memcmp(out, s16, 16) == 0);
    // 24 bit: x * 256.
    const uint8_t m24[] = {0x00, 0x34, 0x12, 0x00, 0xFE, 0xFF, 0x00, 0xFF, 0x7F, 0x00, 0x00, 0x80};
    std::memset(out, 0xAA, sizeof(out));
    CHECK_EQ(impacchetta(x, 4, 1, 3, g, out), size_t(12));
    CHECK(std::memcmp(out, m24, 12) == 0);
    CHECK_EQ(out[12], uint8_t(0xAA));
    const uint8_t s24[] = {0x00, 0x34, 0x12, 0x00, 0x34, 0x12, 0x00, 0xFE, 0xFF, 0x00, 0xFE, 0xFF,
                           0x00, 0xFF, 0x7F, 0x00, 0xFF, 0x7F, 0x00, 0x00, 0x80, 0x00, 0x00, 0x80};
    CHECK_EQ(impacchetta(x, 4, 2, 3, g, out), size_t(24));
    CHECK(std::memcmp(out, s24, 24) == 0);
    // 32 bit: x * 65536 (-32768 x 32768 x 2 = -2^31, senza traboccare).
    const uint8_t m32[] = {0x00, 0x00, 0x34, 0x12, 0x00, 0x00, 0xFE, 0xFF,
                           0x00, 0x00, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0x80};
    CHECK_EQ(impacchetta(x, 4, 1, 4, g, out), size_t(16));
    CHECK(std::memcmp(out, m32, 16) == 0);
    CHECK_EQ(impacchetta(x, 4, 2, 4, g, out), size_t(32));
    bool stereo32 = true;
    for (int i = 0; i < 4; i++)
        stereo32 = stereo32 && std::memcmp(out + 8 * i, m32 + 4 * i, 4) == 0 &&
                   std::memcmp(out + 8 * i + 4, m32 + 4 * i, 4) == 0;
    CHECK(stereo32);
    // Formati non validi: niente scritto, guadagno fermo dov'era.
    g.subito(0);
    g.imposta(GUADAGNO_UNO, 100);
    std::memset(out, 0xAA, sizeof(out));
    CHECK_EQ(impacchetta(x, 4, 0, 2, g, out), size_t(0));
    CHECK_EQ(impacchetta(x, 4, 3, 2, g, out), size_t(0));
    CHECK_EQ(impacchetta(x, 4, 2, 1, g, out), size_t(0));
    CHECK_EQ(impacchetta(x, 4, 2, 5, g, out), size_t(0));
    CHECK_EQ(out[0], uint8_t(0xAA));
    CHECK_EQ(g.attuale(), 0);
    // A 0 dB e 16 bit tutti i 65536 valori escono identici.
    std::vector<int16_t> tutti(65536);
    for (size_t i = 0; i < tutti.size(); i++) tutti[i] = int16_t(uint16_t(i));
    std::vector<uint8_t> b(65536 * 4);
    g.subito(GUADAGNO_UNO);
    CHECK_EQ(impacchetta(tutti.data(), tutti.size(), 2, 2, g, b.data()), size_t(65536 * 4));
    bool identici = true;
    for (size_t i = 0; i < tutti.size(); i++) {
        const int16_t l = int16_t(uint16_t(b[4 * i] | b[4 * i + 1] << 8));
        const int16_t r = int16_t(uint16_t(b[4 * i + 2] | b[4 * i + 3] << 8));
        identici = identici && l == tutti[i] && r == tutti[i];
    }
    CHECK(identici);
    // Meta' guadagno a 16 bit: (x * 16384 + 16384) >> 15, arrotondato.
    const int16_t y[5] = {1, -1, 3, -3, -32768};
    g.subito(16384);
    CHECK_EQ(impacchetta(y, 5, 1, 2, g, out), size_t(10));
    const int16_t meta[5] = {1, 0, 2, -1, -16384};
    bool okMeta = true;
    for (int i = 0; i < 5; i++) okMeta = okMeta && int16_t(uint16_t(out[2 * i] | out[2 * i + 1] << 8)) == meta[i];
    CHECK(okMeta);
}

TEST(dsp_impacchetta_rampa_senza_clic) {
    // Un tono a 1 kHz a fondo scala (48 kHz, 16 bit stereo, a trasferimenti da 8 pacchetti = 384 frame): entra
    // da muto in 10 ms (480 campioni), poi a 0 dB, poi esce in 4 ms (192). Ogni campione deve essere x * g con g
    // sulla retta della rampa (entro 1 LSB dalla rampa ideale in double), sinistra = destra, e il guadagno non
    // salta mai piu' di un passo: niente clic, niente "zip".
    constexpr size_t N = 384 * 5;
    std::vector<int16_t> x(N);
    for (size_t i = 0; i < N; i++) x[i] = int16_t(std::lround(32767 * std::sin(2.0 * PI * 1000.0 * double(i) / 48000)));
    std::vector<uint8_t> b(N * 4);
    Guadagno g;
    g.subito(0);
    g.imposta(GUADAGNO_UNO, 480);
    size_t fatto = 0;
    const size_t inizioUscita = 1200;  // a meta' del quarto trasferimento
    for (size_t t = 0; t < N; t += 384) {
        size_t k = 384;
        if (t <= inizioUscita && inizioUscita < t + 384) {
            k = inizioUscita - t;
            fatto += impacchetta(x.data() + t, k, 2, 2, g, b.data() + 4 * t);
            g.imposta(0, 192);
            fatto += impacchetta(x.data() + t + k, 384 - k, 2, 2, g, b.data() + 4 * (t + k));
            continue;
        }
        fatto += impacchetta(x.data() + t, k, 2, 2, g, b.data() + 4 * t);
    }
    CHECK_EQ(fatto, N * 4);
    double scartoMax = 0, saltoMax = 0;
    bool lr = true;
    for (size_t i = 0; i < N; i++) {
        double gi;  // la rampa ideale
        if (i < 480) {
            gi = (double(i) + 1) / 480;
        } else if (i < inizioUscita) {
            gi = 1;
        } else if (i < inizioUscita + 192) {
            gi = 1 - (double(i - inizioUscita) + 1) / 192;
        } else {
            gi = 0;
        }
        const int16_t l = int16_t(uint16_t(b[4 * i] | b[4 * i + 1] << 8));
        const int16_t r = int16_t(uint16_t(b[4 * i + 2] | b[4 * i + 3] << 8));
        lr = lr && l == r;
        scartoMax = std::max(scartoMax, std::fabs(double(l) - double(x[i]) * gi));
        if (i) {
            // Il salto fra due campioni, tolto quello del tono: al massimo un passo della rampa per il fondo scala.
            const int16_t l0 = int16_t(uint16_t(b[4 * (i - 1)] | b[4 * (i - 1) + 1] << 8));
            saltoMax = std::max(saltoMax, std::fabs(double(l - l0) - double(x[i] - x[i - 1]) * gi));
        }
    }
    std::printf("    rampa su 1 kHz a fondo scala: distanza massima dalla rampa ideale %.2f LSB, salto massimo oltre "
                "il tono %.1f LSB (un passo della rampa da 10 ms vale %.1f)\n",
                scartoMax, saltoMax, 32767.0 / 480);
    CHECK(lr);
    CHECK(scartoMax <= 3.0);
    CHECK(saltoMax <= 32767.0 / 192 + 4);
    // Alla fine e' muto davvero.
    bool muto = true;
    for (size_t i = inizioUscita + 192; i < N; i++) muto = muto && b[4 * i] == 0 && b[4 * i + 1] == 0;
    CHECK(muto);
}

TEST(dsp_silenzio) {
    uint8_t buf[48 * 2 * 4];
    std::memset(buf, 0x55, sizeof(buf));
    CHECK_EQ(silenzio(48, 2, 2, buf), size_t(192));
    bool zeri = true;
    for (size_t i = 0; i < 192; i++) zeri = zeri && buf[i] == 0;
    CHECK(zeri);
    CHECK_EQ(buf[192], uint8_t(0x55));        // oltre non scrive
    CHECK_EQ(silenzio(45, 1, 3, buf), size_t(135));
    CHECK_EQ(silenzio(10, 3, 2, buf), size_t(0));  // 3 canali: non valido
    CHECK_EQ(silenzio(10, 2, 1, buf), size_t(0));  // 1 byte per campione: non valido
}
