// Aiuti per i test di uac_desc sul PC (vedi aiuti.h).
#include "aiuti.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "testing.h"

using namespace uac;

// Chiamata da test/main.cpp prima di ogni test.
void seedRandom(uint32_t seed) { aiuti::caso().seed(seed); }

namespace aiuti {

std::mt19937& caso() {
    static std::mt19937 g(12345);
    return g;
}

// ---- memoria protetta ----

Protetta::Protetta(size_t massimo) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    pagina_ = si.dwPageSize;
#else
    pagina_ = size_t(sysconf(_SC_PAGESIZE));
#endif
    utile_ = (massimo + pagina_ - 1) / pagina_ * pagina_;
    if (utile_ == 0) utile_ = pagina_;
    const size_t totale = utile_ + 2 * pagina_;
#ifdef _WIN32
    base_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, totale, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    DWORD vecchio = 0;
    if (base_ == nullptr || !VirtualProtect(base_, pagina_, PAGE_NOACCESS, &vecchio) ||
        !VirtualProtect(base_ + pagina_ + utile_, pagina_, PAGE_NOACCESS, &vecchio)) {
        std::printf("memoria protetta non disponibile\n");
        std::abort();
    }
#else
    void* p = mmap(nullptr, totale, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    base_ = p == MAP_FAILED ? nullptr : static_cast<uint8_t*>(p);
    if (base_ == nullptr || mprotect(base_, pagina_, PROT_NONE) != 0 ||
        mprotect(base_ + pagina_ + utile_, pagina_, PROT_NONE) != 0) {
        std::printf("memoria protetta non disponibile\n");
        std::abort();
    }
#endif
}

Protetta::~Protetta() {
#ifdef _WIN32
    VirtualFree(base_, 0, MEM_RELEASE);
#else
    munmap(base_, utile_ + 2 * pagina_);
#endif
}

uint8_t* Protetta::metti(const uint8_t* dati, size_t n, bool inFondo) {
    if (n > utile_) {
        std::printf("memoria protetta troppo piccola (%zu > %zu)\n", n, utile_);
        std::abort();
    }
    uint8_t* d = inFondo ? base_ + pagina_ + utile_ - n : base_ + pagina_;
    if (n) std::memcpy(d, dati, n);
    return d;
}

uint8_t* Protetta::spazioInFondo(size_t n) {
    if (n > utile_) std::abort();
    return base_ + pagina_ + utile_ - n;
}

// ---- costruttore di descrittori ----

namespace {
void metti16(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(x >> 8));
}
void metti24(std::vector<uint8_t>& v, uint32_t x) {
    metti16(v, x);
    v.push_back(uint8_t(x >> 16));
}
void metti32(std::vector<uint8_t>& v, uint32_t x) {
    metti16(v, x);
    metti16(v, x >> 16);
}
}  // namespace

Costruttore::Costruttore() { b_ = {0x09, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x80, 0x32}; }

Costruttore& Costruttore::d(std::initializer_list<uint8_t> byte) {
    b_.insert(b_.end(), byte.begin(), byte.end());
    return *this;
}

Costruttore& Costruttore::d(const std::vector<uint8_t>& byte) {
    b_.insert(b_.end(), byte.begin(), byte.end());
    return *this;
}

Costruttore& Costruttore::interfaccia(uint8_t numero, uint8_t alt, uint8_t nEndpoint, uint8_t classe,
                                      uint8_t sottoclasse, uint8_t protocollo) {
    return d({0x09, 0x04, numero, alt, nEndpoint, classe, sottoclasse, protocollo, 0x00});
}

Costruttore& Costruttore::iad(uint8_t primo, uint8_t quante, uint8_t classe, uint8_t protocollo) {
    return d({0x08, 0x0B, primo, quante, classe, 0x00, protocollo, 0x00});
}

Costruttore& Costruttore::header1(std::initializer_list<uint8_t> interfacce) {
    std::vector<uint8_t> v = {uint8_t(8 + interfacce.size()), 0x24, 0x01, 0x00, 0x01, 0x00, 0x00,
                              uint8_t(interfacce.size())};
    v.insert(v.end(), interfacce.begin(), interfacce.end());
    return d(v);
}

Costruttore& Costruttore::it1(uint8_t id, uint16_t tipo, uint8_t canali) {
    return d({0x0C, 0x24, 0x02, id, uint8_t(tipo), uint8_t(tipo >> 8), 0x00, canali, 0x03, 0x00, 0x00, 0x00});
}

Costruttore& Costruttore::ot1(uint8_t id, uint16_t tipo, uint8_t sorgente) {
    return d({0x09, 0x24, 0x03, id, uint8_t(tipo), uint8_t(tipo >> 8), 0x00, sorgente, 0x00});
}

Costruttore& Costruttore::fu1(uint8_t id, uint8_t sorgente, std::initializer_list<uint8_t> controlli) {
    std::vector<uint8_t> v = {uint8_t(7 + controlli.size()), 0x24, 0x06, id, sorgente, 0x01};
    v.insert(v.end(), controlli.begin(), controlli.end());
    v.push_back(0x00);
    return d(v);
}

Costruttore& Costruttore::general1(uint8_t terminale, uint16_t formato) {
    return d({0x07, 0x24, 0x01, terminale, 0x01, uint8_t(formato), uint8_t(formato >> 8)});
}

Costruttore& Costruttore::formato1(uint8_t canali, uint8_t byte, uint8_t bit, std::initializer_list<uint32_t> hz) {
    std::vector<uint8_t> v = {uint8_t(8 + 3 * hz.size()), 0x24, 0x02, 0x01, canali, byte, bit, uint8_t(hz.size())};
    for (uint32_t f : hz) metti24(v, f);
    return d(v);
}

Costruttore& Costruttore::ep1(uint8_t indirizzo, uint8_t attributi, uint16_t mps, uint8_t intervallo, uint8_t refresh,
                              uint8_t sincro) {
    return d({0x09, 0x05, indirizzo, attributi, uint8_t(mps), uint8_t(mps >> 8), intervallo, refresh, sincro});
}

Costruttore& Costruttore::csep1(uint8_t attributi) { return d({0x07, 0x25, 0x01, attributi, 0x00, 0x00, 0x00}); }

Costruttore& Costruttore::header2() { return d({0x09, 0x24, 0x01, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00}); }

Costruttore& Costruttore::it2(uint8_t id, uint16_t tipo, uint8_t clock, uint8_t canali) {
    return d({0x11, 0x24, 0x02, id, uint8_t(tipo), uint8_t(tipo >> 8), 0x00, clock, canali, 0x03, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00});
}

Costruttore& Costruttore::ot2(uint8_t id, uint16_t tipo, uint8_t sorgente, uint8_t clock) {
    return d({0x0C, 0x24, 0x03, id, uint8_t(tipo), uint8_t(tipo >> 8), 0x00, sorgente, clock, 0x00, 0x00, 0x00});
}

Costruttore& Costruttore::fu2(uint8_t id, uint8_t sorgente, std::initializer_list<uint32_t> controlli) {
    std::vector<uint8_t> v = {uint8_t(6 + 4 * controlli.size()), 0x24, 0x06, id, sorgente};
    for (uint32_t c : controlli) metti32(v, c);
    v.push_back(0x00);
    return d(v);
}

Costruttore& Costruttore::clock2(uint8_t id, uint8_t attributi, uint8_t controlli) {
    return d({0x08, 0x24, 0x0A, id, attributi, controlli, 0x00, 0x00});
}

Costruttore& Costruttore::selettore2(uint8_t id, std::initializer_list<uint8_t> ingressi) {
    std::vector<uint8_t> v = {uint8_t(7 + ingressi.size()), 0x24, 0x0B, id, uint8_t(ingressi.size())};
    v.insert(v.end(), ingressi.begin(), ingressi.end());
    v.push_back(0x03);
    v.push_back(0x00);
    return d(v);
}

Costruttore& Costruttore::moltiplicatore2(uint8_t id, uint8_t sorgente) {
    return d({0x07, 0x24, 0x0C, id, sorgente, 0x00, 0x00});
}

Costruttore& Costruttore::general2(uint8_t terminale, uint8_t canali, uint32_t formati, uint8_t tipoFormato) {
    std::vector<uint8_t> v = {0x10, 0x24, 0x01, terminale, 0x00, tipoFormato};
    metti32(v, formati);
    v.push_back(canali);
    metti32(v, 0x00000003);
    v.push_back(0x00);
    return d(v);
}

Costruttore& Costruttore::formato2(uint8_t byte, uint8_t bit) { return d({0x06, 0x24, 0x02, 0x01, byte, bit}); }

Costruttore& Costruttore::ep(uint8_t indirizzo, uint8_t attributi, uint16_t mps, uint8_t intervallo) {
    return d({0x07, 0x05, indirizzo, attributi, uint8_t(mps), uint8_t(mps >> 8), intervallo});
}

Costruttore& Costruttore::csep2() { return d({0x08, 0x25, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}); }

std::vector<uint8_t> Costruttore::fine() const {
    std::vector<uint8_t> v = b_;
    v[2] = uint8_t(v.size());
    v[3] = uint8_t(v.size() >> 8);
    // bNumInterfaces: i numeri d'interfaccia diversi.
    bool visto[256] = {};
    unsigned n = 0;
    for (size_t i = 0; i + 1 < v.size() && v[i] >= 2; i += v[i])
        if (v[i + 1] == 0x04 && v[i] >= 3 && !visto[v[i + 2]]) {
            visto[v[i + 2]] = true;
            n++;
        }
    v[4] = uint8_t(n);
    return v;
}

std::vector<uint8_t> rangeHz(std::initializer_list<uint32_t> triple) {
    std::vector<uint8_t> v;
    metti16(v, uint32_t(triple.size() / 3));
    for (uint32_t x : triple) metti32(v, x);
    return v;
}

std::vector<uint8_t> rangeVolume(std::initializer_list<int16_t> triple) {
    std::vector<uint8_t> v;
    metti16(v, uint32_t(triple.size() / 3));
    for (int16_t x : triple) metti16(v, uint16_t(x));
    return v;
}

std::vector<uint8_t> stringa(std::initializer_list<uint16_t> unita) {
    std::vector<uint8_t> v = {uint8_t(2 + 2 * unita.size()), 0x03};
    for (uint16_t u : unita) metti16(v, u);
    return v;
}

std::vector<uint8_t> stringa(const char* ascii) {
    std::vector<uint8_t> v = {uint8_t(2 + 2 * std::strlen(ascii)), 0x03};
    for (const char* p = ascii; *p; p++) metti16(v, uint8_t(*p));
    return v;
}

namespace {
void unaRiga(void* ctx, const char* testo) { static_cast<std::vector<std::string>*>(ctx)->push_back(testo); }
}  // namespace

std::vector<std::string> righe(const Configurazione& c) {
    std::vector<std::string> r;
    descrivi(c, unaRiga, &r);
    return r;
}

Esito simulaApri(Configurazione& c, uint32_t quirk, const std::vector<uint8_t>& range, Scelta& scelta,
                 PinSelettore pin, void* ctx) {
    if (c.versione == Versione::UAC2) {
        bool fatto[256] = {};
        for (uint8_t i = 0; i < c.nStreaming && i < MAX_STREAMING; i++) {
            const Streaming& s = c.streaming[i];
            for (uint8_t j = 0; j < s.nAlt && j < MAX_ALT; j++) {
                const Alternativa& a = s.alt[j];
                if ((a.endpoint & 0x80) != 0 || fatto[a.terminale]) continue;
                fatto[a.terminale] = true;
                Frequenze f;
                bool ok = false;
                const Entita* t = trovaEntita(c, a.terminale);
                if (t && sorgenteClock(c, t->clock, pin, ctx) != 0 && !range.empty())
                    ok = analizzaRangeFrequenze(range.data(), range.size(), f);
                if (!ok) {
                    f.n = 0;
                    f.aggiungi(48000, 48000);
                }
                impostaFrequenzeClock(c, a.terminale, f);
            }
        }
    }
    return scegli(c, quirk, scelta);
}

bool catenaEsatta(const uint8_t* v, size_t n) {
    if (n < 9 || v[0] != 9 || v[1] != 0x02 || size_t(v[2] | (v[3] << 8)) != n) return false;
    size_t i = 0;
    while (i < n) {
        if (v[i] < 2 || i + v[i] > n) return false;
        i += v[i];
    }
    return i == n;
}

namespace {
bool stesseFrequenze(const Frequenze& a, const Frequenze& b) {
    if (a.n != b.n) return false;
    for (uint8_t i = 0; i < a.n && i < MAX_FREQ; i++)
        if (a.voci[i].min != b.voci[i].min || a.voci[i].max != b.voci[i].max || a.voci[i].passo != b.voci[i].passo)
            return false;
    return true;
}

bool stessaAlternativa(const Alternativa& a, const Alternativa& b) {
    return a.numero == b.numero && a.terminale == b.terminale && a.pcm == b.pcm && a.canali == b.canali &&
           a.byteCampione == b.byteCampione && a.bit == b.bit && stesseFrequenze(a.frequenze, b.frequenze) &&
           a.endpoint == b.endpoint && a.maxPacchetto == b.maxPacchetto && a.intervallo == b.intervallo &&
           a.sync == b.sync && a.endpointFeedback == b.endpointFeedback &&
           a.intervalloFeedback == b.intervalloFeedback && a.frequenzaSuEndpoint == b.frequenzaSuEndpoint;
}

bool stessaEntita(const Entita& a, const Entita& b) {
    if (a.id != b.id || a.tipo != b.tipo || a.tipoTerminale != b.tipoTerminale || a.clock != b.clock ||
        a.nSorgenti != b.nSorgenti || a.attributiClock != b.attributiClock || a.controlliClock != b.controlliClock ||
        a.canali != b.canali || a.muto != b.muto || a.volume != b.volume)
        return false;
    for (uint8_t i = 0; i < a.nSorgenti && i < MAX_SORGENTI; i++)
        if (a.sorgenti[i] != b.sorgenti[i]) return false;
    return true;
}
}  // namespace

bool stessaConfigurazione(const Configurazione& a, const Configurazione& b) {
    if (a.versione != b.versione || a.bcdADC != b.bcdADC || a.interfacciaControllo != b.interfacciaControllo ||
        a.nEntita != b.nEntita || a.nStreaming != b.nStreaming || a.troncata != b.troncata)
        return false;
    for (uint8_t i = 0; i < a.nEntita && i < MAX_ENTITA; i++)
        if (!stessaEntita(a.entita[i], b.entita[i])) return false;
    for (uint8_t i = 0; i < a.nStreaming && i < MAX_STREAMING; i++) {
        const Streaming& x = a.streaming[i];
        const Streaming& y = b.streaming[i];
        if (x.interfaccia != y.interfaccia || x.uscita != y.uscita || x.nAlt != y.nAlt) return false;
        for (uint8_t j = 0; j < x.nAlt && j < MAX_ALT; j++)
            if (!stessaAlternativa(x.alt[j], y.alt[j])) return false;
    }
    return true;
}

std::string controllaConfigurazione(const Configurazione& c, Esito e) {
    char b[160];
    if (e != Esito::OK) {
        if (c.nEntita != 0 || c.nStreaming != 0) return "esito non OK ma configurazione non vuota";
        return "";
    }
    if (c.nEntita > MAX_ENTITA) return "troppe entita'";
    if (c.nStreaming < 1 || c.nStreaming > MAX_STREAMING) return "numero di streaming fuori dai limiti";
    bool uscita = false;
    for (uint8_t i = 0; i < c.nEntita; i++) {
        const Entita& x = c.entita[i];
        if (x.id == 0) return "entita' con id 0";
        for (uint8_t k = 0; k < i; k++)
            if (c.entita[k].id == x.id) return "id ripetuto";
        if (x.nSorgenti > MAX_SORGENTI) return "troppe sorgenti";
        if (x.canali > MAX_CANALI_FU) return "troppi canali";
        const uint32_t maschera = (1u << (x.canali + 1)) - 1;
        if ((x.muto & ~maschera) != 0 || (x.volume & ~maschera) != 0) return "controlli oltre i canali";
        if (x.tipo != TipoEntita::FEATURE && (x.canali != 0 || x.muto != 0 || x.volume != 0))
            return "controlli su un'entita' che non e' una Feature Unit";
        if (c.versione == Versione::UAC1 && x.clock != 0) return "clock in UAC1";
    }
    for (uint8_t i = 0; i < c.nStreaming; i++) {
        const Streaming& s = c.streaming[i];
        if (s.nAlt < 1 || s.nAlt > MAX_ALT) return "numero di alternative fuori dai limiti";
        for (uint8_t k = 0; k < i; k++)
            if (c.streaming[k].interfaccia == s.interfaccia) return "interfaccia ripetuta";
        bool out = false;
        for (uint8_t j = 0; j < s.nAlt; j++) {
            const Alternativa& a = s.alt[j];
            std::snprintf(b, sizeof(b), "if %u alt %u: ", unsigned(s.interfaccia), unsigned(a.numero));
            if (a.numero == 0) return std::string(b) + "alternativa 0";
            if ((a.endpoint & 0x0F) == 0) return std::string(b) + "endpoint 0";
            if (a.maxPacchetto > 0x7FF) return std::string(b) + "wMaxPacketSize oltre 11 bit";
            if (a.frequenze.n > MAX_FREQ) return std::string(b) + "troppe frequenze";
            if (c.versione == Versione::UAC2 && (a.frequenze.n != 0 || a.frequenzaSuEndpoint))
                return std::string(b) + "UAC2 con frequenze nei descrittori";
            if (uint8_t(a.sync) > 3) return std::string(b) + "sync fuori dai limiti";
            if ((a.endpoint & 0x80) == 0) out = true;
        }
        if (out != s.uscita) return "uscita sbagliata";
        uscita = uscita || out;
    }
    if (!uscita) return "OK senza uscita";
    return "";
}

}  // namespace aiuti
