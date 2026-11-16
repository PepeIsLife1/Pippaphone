// Fuzzer di uac_desc. Per qualche secondo (variabile d'ambiente UAC_FUZZ_MS, 3000 di default) prende i
// descrittori veri e alcuni costruiti a mano, li rovina a caso (bit, byte, bLength, wTotalLength, tagli, pezzi
// tolti, ripetuti o presi da un altro descrittore) e li fa leggere ad analizza(); poi passa il risultato a
// tutte le altre funzioni (clock, frequenze, scelta, strada, descrizione). Controlla che:
//  - non si legga mai fuori dal buffer: sta contro una pagina proibita (in fondo o in testa, a turno), e una
//    lettura fuori fa cadere il programma (al posto di AddressSanitizer, che qui non c'e'; con UBSan in modo
//    trap anche gli indici degli array e gli overflow con segno fermano tutto);
//  - la stessa lettura su memoria sporca (0x00 e 0xA5) dia la stessa configurazione;
//  - ogni risultato rispetti gli invarianti (aiuti::controllaConfigurazione, e quelli della Scelta).
// Lo stesso per GET RANGE (frequenze e volume) e per i descrittori stringa. Il seme e' PN_SEED (o 12345).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "aiuti.h"
#include "descrittori_veri.h"
#include "testing.h"
#include "usb_audio/uac_desc.h"

using namespace uac;
using aiuti::Costruttore;

namespace {

constexpr size_t MASSIMO = 4096;  // byte di un descrittore rovinato

long durataMs() {
    const char* s = std::getenv("UAC_FUZZ_MS");
    const long ms = s ? std::strtol(s, nullptr, 10) : 3000;
    return ms > 0 ? ms : 3000;
}

uint32_t a(std::mt19937& g, uint32_t n) { return n ? uint32_t(g() % n) : 0; }

// I semi: i descrittori veri e qualche costruito con le parti che i veri non hanno (moltiplicatori, giri di
// selettori, feedback UAC2, due funzioni senza IAD, frequenze continue).
std::vector<std::vector<uint8_t>> semi() {
    std::vector<std::vector<uint8_t>> s;
    s.emplace_back(veri::APPLE_110A, veri::APPLE_110A + sizeof(veri::APPLE_110A));
    s.emplace_back(veri::GOOGLE_5034, veri::GOOGLE_5034 + sizeof(veri::GOOGLE_5034));
    s.emplace_back(veri::SAMSUNG_A051, veri::SAMSUNG_A051 + sizeof(veri::SAMSUNG_A051));
    s.emplace_back(veri::CMEDIA_000C, veri::CMEDIA_000C + sizeof(veri::CMEDIA_000C));
    s.emplace_back(veri::EPLUS_9023, veri::EPLUS_9023 + sizeof(veri::EPLUS_9023));
    {
        Costruttore k;
        k.iad(0, 2).interfaccia(0, 0, 0, 0x01, 0x01, 0x20).header2().clock2(9).clock2(10);
        k.selettore2(11, {9, 10, 12}).selettore2(12, {11}).moltiplicatore2(13, 11);
        k.it2(1, 0x0101, 13).fu2(2, 1, {0x3, 0xC, 0xC}).d({0x0E, 0x24, 0x07, 0x05, 0x01, 0x00, 0x02, 0x00, 0x00,
                                                             0x00, 0x00, 0x00, 0x00, 0x00});
        k.d({0x08, 0x24, 0x0D, 0x06, 0x05, 0x09, 0x0A, 0x00}).ot2(3, 0x0302, 6, 13);
        k.interfaccia(1, 0, 0, 0x01, 0x02, 0x20).interfaccia(1, 1, 2, 0x01, 0x02, 0x20).general2(1, 2);
        k.formato2(2, 16).ep(0x01, 0x05, 196).csep2().ep(0x81, 0x11, 4, 4);
        k.interfaccia(1, 2, 1, 0x01, 0x02, 0x20).general2(1, 2).formato2(3, 24).ep(0x01, 0x0D, 288).csep2();
        s.push_back(k.fine());
    }
    {
        Costruttore k;
        k.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1, 2}).it1(1, 0x0101).it1(4, 0x0201, 1);
        k.d({0x0D, 0x24, 0x04, 0x05, 0x02, 0x01, 0x06, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00});
        k.fu1(6, 4, {0x03}).fu1(2, 5, {0x01, 0x02, 0x02}).ot1(3, 0x0302, 2).ot1(7, 0x0101, 6);
        k.d({0x0D, 0x24, 0x07, 0x08, 0x01, 0x00, 0x01, 0x02, 0x02, 0x03, 0x00, 0x00, 0x00});
        k.interfaccia(1, 0, 0, 0x01, 0x02, 0x00).interfaccia(1, 1, 1, 0x01, 0x02, 0x00).general1(1);
        k.d({0x0E, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x00, 0x40, 0x1F, 0x00, 0x00, 0x77, 0x01});
        k.ep1(0x01, 0x05, 200, 1, 0, 0x82).csep1(0x01).ep1(0x82, 0x01, 3, 1, 2, 0);
        k.interfaccia(2, 0, 0, 0x01, 0x02, 0x00).interfaccia(2, 1, 1, 0x01, 0x02, 0x00).general1(7);
        k.formato1(1, 2, 16, {48000, 44100, 32000, 16000}).ep1(0x83, 0x05, 96).csep1(0x01);
        s.push_back(k.fine());
    }
    {
        Costruttore k;  // due funzioni UAC2 senza IAD, la prima solo in ingresso
        k.interfaccia(0, 0, 0, 0x01, 0x01, 0x20).header2().clock2(9).it2(1, 0x0201, 9).ot2(2, 0x0101, 1, 9);
        k.interfaccia(1, 0, 0, 0x01, 0x02, 0x20).interfaccia(1, 1, 1, 0x01, 0x02, 0x20).general2(2, 1);
        k.formato2(2, 16).ep(0x81, 0x0D, 96).csep2();
        k.interfaccia(2, 0, 0, 0x01, 0x01, 0x20).header2().clock2(9).it2(1, 0x0101, 9).ot2(3, 0x0301, 1, 9);
        k.interfaccia(3, 0, 0, 0x01, 0x02, 0x20).interfaccia(3, 1, 1, 0x01, 0x02, 0x20).general2(1, 2);
        k.formato2(2, 16).ep(0x03, 0x09, 192).csep2();
        s.push_back(k.fine());
    }
    return s;
}

// Gli inizi dei descrittori di v (finche' la catena regge).
std::vector<size_t> inizi(const std::vector<uint8_t>& v) {
    std::vector<size_t> r;
    for (size_t i = 0; i < v.size() && v[i] >= 2; i += v[i]) r.push_back(i);
    return r;
}

void muta(std::vector<uint8_t>& v, const std::vector<std::vector<uint8_t>>& semi, std::mt19937& g) {
    static const uint8_t INTERESSANTI[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
                                           0x0C, 0x0D, 0x10, 0x11, 0x18, 0x20, 0x24, 0x25, 0x30, 0x7F, 0x80, 0x81,
                                           0xBB, 0xC0, 0xFE, 0xFF};
    const int quante = 1 + int(a(g, 6));
    for (int m = 0; m < quante && !v.empty(); m++) {
        const std::vector<size_t> ii = inizi(v);
        switch (a(g, 12)) {
        case 0:  // un bit
            v[a(g, uint32_t(v.size()))] ^= uint8_t(1u << a(g, 8));
            break;
        case 1:  // un byte qualunque
            v[a(g, uint32_t(v.size()))] = uint8_t(g());
            break;
        case 2:  // un valore che conta (tipi, sottotipi, id, limiti)
            v[a(g, uint32_t(v.size()))] = INTERESSANTI[a(g, sizeof(INTERESSANTI))];
            break;
        case 3: {  // il bLength di un descrittore
            if (ii.empty()) break;
            const size_t i = ii[a(g, uint32_t(ii.size()))];
            const int delta[] = {-3, -2, -1, 1, 2, 3};
            const int x = a(g, 3) == 0 ? int(g() % 256) : int(v[i]) + delta[a(g, 6)];
            v[i] = uint8_t(x);
            break;
        }
        case 4:  // tagliato
            v.resize(a(g, uint32_t(v.size()) + 1));
            break;
        case 5: {  // un pezzo tolto
            const size_t i = a(g, uint32_t(v.size()));
            const size_t n = 1 + a(g, 40);
            v.erase(v.begin() + long(i), v.begin() + long(std::min(v.size(), i + n)));
            break;
        }
        case 6: {  // un pezzo ripetuto (spesso un descrittore intero)
            if (ii.empty() || v.size() > MASSIMO - 300) break;
            const size_t i = ii[a(g, uint32_t(ii.size()))];
            const size_t n = std::min<size_t>(v[i], v.size() - i);
            const std::vector<uint8_t> pezzo(v.begin() + long(i), v.begin() + long(i + n));
            const size_t dove = ii[a(g, uint32_t(ii.size()))];
            v.insert(v.begin() + long(dove), pezzo.begin(), pezzo.end());
            break;
        }
        case 7: {  // un descrittore preso da un altro seme
            const std::vector<uint8_t>& s = semi[a(g, uint32_t(semi.size()))];
            const std::vector<size_t> js = inizi(s);
            if (js.empty() || ii.empty() || v.size() > MASSIMO - 300) break;
            const size_t j = js[a(g, uint32_t(js.size()))];
            const size_t n = std::min<size_t>(s[j], s.size() - j);
            const size_t dove = ii[a(g, uint32_t(ii.size()))];
            v.insert(v.begin() + long(dove), s.begin() + long(j), s.begin() + long(j + n));
            break;
        }
        case 8: {  // wTotalLength
            if (v.size() < 4) break;
            const uint32_t t = a(g, 2) ? uint32_t(int(v.size()) + int(a(g, 7)) - 3) : uint32_t(g() % 65536);
            v[2] = uint8_t(t);
            v[3] = uint8_t(t >> 8);
            break;
        }
        case 9: {  // un id piccolo in un campo a caso di un descrittore di classe: strade e clock che si
                   // incrociano, giri, sorgenti che non ci sono
            if (ii.empty()) break;
            const size_t i = ii[a(g, uint32_t(ii.size()))];
            if (v[i] < 4 || i + v[i] > v.size()) break;
            v[i + 3 + a(g, v[i] - 3)] = uint8_t(a(g, 16));
            break;
        }
        case 10: {  // due descrittori scambiati
            if (ii.size() < 3) break;
            const size_t x = 1 + a(g, uint32_t(ii.size() - 1));
            if (x + 1 >= ii.size()) break;
            const size_t i = ii[x], j = ii[x + 1];
            const size_t fine = (x + 2 < ii.size()) ? ii[x + 2] : v.size();
            if (j > v.size() || fine > v.size() || i >= j || j >= fine) break;
            std::vector<uint8_t> nuovo(v.begin() + long(j), v.begin() + long(fine));
            nuovo.insert(nuovo.end(), v.begin() + long(i), v.begin() + long(j));
            std::copy(nuovo.begin(), nuovo.end(), v.begin() + long(i));
            break;
        }
        default: {  // il numero d'interfaccia, l'alternativa o la classe di un'interfaccia
            for (size_t i : ii) {
                if (i + 9 > v.size() || v[i + 1] != 0x04 || a(g, 3) != 0) continue;
                v[i + 2 + a(g, 6)] = uint8_t(a(g, 4) == 0 ? g() : a(g, 4));
                break;
            }
            break;
        }
        }
    }
    if (v.size() > MASSIMO) v.resize(MASSIMO);
}

struct Conti {
    long prove = 0;
    long esiti[7] = {};
    long troncate = 0;
    long scelte = 0;
    long strade = 0;
    long clock = 0;
    long range = 0;
    long stringhe = 0;
    long falliti = 0;
};

uint8_t pinCasuale(void* ctx, uint8_t) {
    std::mt19937& g = *static_cast<std::mt19937*>(ctx);
    return uint8_t(a(g, 5) == 0 ? g() : a(g, 4));
}

// Il risultato di analizza() passato a tutto il resto, con i controlli.
void esercita(Configurazione& c, std::mt19937& g, Conti& conti) {
    // Strada e clock da ogni entita' (non si devono bloccare ne' leggere fuori).
    for (uint8_t i = 0; i < c.nEntita; i++) {
        Percorso p;
        const uint8_t id = c.entita[i].id;
        if (percorso(c, id, p)) {
            conti.strade++;
            const Entita* ot = trovaEntita(c, p.terminaleUscita);
            const Entita* fu = p.featureUnit ? trovaEntita(c, p.featureUnit) : nullptr;
            const bool ok = ot && ot->tipo == TipoEntita::TERMINALE_USCITA &&
                            ot->tipoTerminale != TERMINALE_STREAMING_USB && ot->tipoTerminale == p.tipoUscita &&
                            (p.featureUnit == 0 || (fu && fu->tipo == TipoEntita::FEATURE)) &&
                            c.entita[i].tipo == TipoEntita::TERMINALE_INGRESSO;
            if (!ok) conti.falliti++;
            CHECK(ok);
        }
        const uint8_t s = sorgenteClock(c, id, a(g, 2) ? pinCasuale : nullptr, &g);
        if (s != 0) {
            conti.clock++;
            const Entita* e = trovaEntita(c, s);
            CHECK(e && e->tipo == TipoEntita::CLOCK_SORGENTE);
        }
    }
    // GET RANGE a caso: buona, rotta o vuota.
    std::vector<uint8_t> range;
    switch (a(g, 4)) {
    case 0:
        range = aiuti::rangeHz({48000, 48000, 0});
        break;
    case 1:
        range = aiuti::rangeHz({8000, 96000, a(g, 3) * 1000, 44100, 44100, 0});
        break;
    case 2:
        range.resize(a(g, 40));
        for (uint8_t& b : range) b = uint8_t(g());
        if (range.size() >= 2) range[1] = 0;
        break;
    default:
        break;
    }
    const uint32_t quirk = a(g, 2) ? uint32_t(QUIRK_SOLO_48K) : 0u;
    Scelta sc;
    const Esito e = aiuti::simulaApri(c, quirk, range, sc, a(g, 2) ? pinCasuale : nullptr, &g);
    if (e == Esito::OK) {
        conti.scelte++;
        bool ok = sc.indiceStreaming < c.nStreaming && sc.indiceAlt < c.streaming[sc.indiceStreaming].nAlt;
        if (ok) {
            const Streaming& s = c.streaming[sc.indiceStreaming];
            const Alternativa& x = s.alt[sc.indiceAlt];
            const uint32_t serve = (sc.hz + 999) / 1000 * sc.canali * sc.byteCampione;
            ok = s.interfaccia == sc.interfaccia && x.numero == sc.alt && x.endpoint == sc.endpoint &&
                 x.terminale == sc.terminale && x.maxPacchetto == sc.maxPacchetto && x.pcm && x.intervallo == 1 &&
                 (sc.endpoint & 0x80) == 0 && (sc.endpoint & 0x0F) != 0 && (sc.canali == 1 || sc.canali == 2) &&
                 sc.byteCampione >= 2 && sc.byteCampione <= 4 && sc.bit >= 16 && sc.bit <= 8 * sc.byteCampione &&
                 (sc.hz == 48000 || (sc.hz == 44100 && !(quirk & QUIRK_SOLO_48K))) &&
                 x.frequenze.contiene(sc.hz) && serve <= sc.maxPacchetto && sc.maxPacchetto <= MAX_PACCHETTO;
        }
        if (!ok) conti.falliti++;
        CHECK(ok);
    } else {
        CHECK(e == Esito::FORMATO || e == Esito::PACCHETTI_GRANDI || e == Esito::NESSUNA_USCITA);
    }
    // La descrizione: una riga per la funzione e una per alternativa, mai oltre il buffer.
    const std::vector<std::string> r = aiuti::righe(c);
    size_t alt = 0;
    for (uint8_t i = 0; i < c.nStreaming; i++) alt += c.streaming[i].nAlt;
    CHECK_EQ(r.size(), 1 + alt);
    for (const std::string& s : r) CHECK(s.size() < 200);
}

bool utf8Buono(const char* s, size_t n) {
    for (size_t i = 0; i < n;) {
        const uint8_t c = uint8_t(s[i]);
        size_t k = 1;
        if (c < 0x80) {
            if (c < 0x20) return false;
        } else if ((c & 0xE0) == 0xC0) {
            k = 2;
        } else if ((c & 0xF0) == 0xE0) {
            k = 3;
        } else {
            return false;
        }
        if (i + k > n) return false;
        for (size_t j = 1; j < k; j++)
            if ((uint8_t(s[i + j]) & 0xC0) != 0x80) return false;
        i += k;
    }
    return n == 0 || s[n - 1] != ' ';
}

// GET RANGE e descrittori stringa a caso, contro la pagina proibita.
void fuzzPiccoli(aiuti::Protetta& mem, std::mt19937& g, Conti& conti) {
    std::vector<uint8_t> v(a(g, 80));
    for (uint8_t& b : v) b = uint8_t(g());
    if (v.size() >= 2 && a(g, 2)) {  // spesso un numero di voci piccolo e plausibile
        v[0] = uint8_t(a(g, 8));
        v[1] = 0;
    }
    const bool inFondo = a(g, 2) != 0;
    Frequenze f;
    if (analizzaRangeFrequenze(mem.metti(v, inFondo), v.size(), f)) {
        conti.range++;
        bool ok = f.n >= 1 && f.n <= MAX_FREQ;
        for (uint8_t i = 0; ok && i < f.n; i++) ok = f.voci[i].min <= f.voci[i].max;
        CHECK(ok);
    } else {
        CHECK_EQ(f.n, 0);
    }
    RangeVolume r;
    if (analizzaRangeVolume(mem.metti(v, inFondo), v.size(), r)) {
        const int16_t z = volumeZeroDb(r);
        const bool ok = r.max < 0 ? z == r.max : r.min > 0 ? z == r.min : (z <= 0 && z >= r.min);
        CHECK(ok);
    }
    // Descrittore stringa: a volte giusto nella testa, con unita' UTF-16 qualunque.
    if (v.size() >= 2 && a(g, 3) != 0) {
        v[0] = uint8_t(a(g, 4) == 0 ? g() : v.size());
        v[1] = 0x03;
    }
    const size_t cap = a(g, 40);
    char* out = reinterpret_cast<char*>(mem.spazioInFondo(cap + 1)) + 1;  // cap byte contro la pagina
    static aiuti::Protetta ingresso(4096);
    const size_t k = testoStringa(ingresso.metti(v, inFondo), v.size(), cap ? out : nullptr, cap);
    if (cap == 0) {
        CHECK_EQ(k, size_t(0));
    } else {
        conti.stringhe++;
        const bool ok = k < cap && out[k] == '\0' && std::strlen(out) == k && utf8Buono(out, k);
        CHECK(ok);
    }
}

}  // namespace

TEST(uac_fuzz) {
    const long ms = durataMs();
    std::mt19937& g = aiuti::caso();
    const std::vector<std::vector<uint8_t>> s = semi();
    for (const std::vector<uint8_t>& x : s) CHECK(aiuti::catenaEsatta(x.data(), x.size()));
    aiuti::Protetta mem(MASSIMO + 4096);
    std::unique_ptr<Configurazione> pulita(new Configurazione), sporca(new Configurazione);
    Conti conti;
    const auto inizio = std::chrono::steady_clock::now();
    std::vector<uint8_t> v;
    v.reserve(MASSIMO + 512);
    const int prima = g_failures;
    for (;;) {
        if ((conti.prove & 255) == 0 &&
            std::chrono::steady_clock::now() - inizio > std::chrono::milliseconds(ms))
            break;
        if (g_failures - prima > 20) break;  // e' gia' chiaro che qualcosa non va
        v = s[a(g, uint32_t(s.size()))];
        muta(v, s, g);
        // A volte n e' piu' corto del vettore: si legge solo quello (e solo quello c'e' prima della pagina).
        const size_t n = a(g, 8) == 0 ? a(g, uint32_t(v.size()) + 1) : v.size();
        const uint8_t* d = mem.metti(v.data(), n, (conti.prove & 1) == 0);
        std::memset(static_cast<void*>(pulita.get()), 0x00, sizeof(Configurazione));
        std::memset(static_cast<void*>(sporca.get()), 0xA5, sizeof(Configurazione));
        const Esito e = analizza(d, n, *pulita);
        const Esito e2 = analizza(d, n, *sporca);
        conti.prove++;
        if (uint8_t(e) < 7) conti.esiti[uint8_t(e)]++;
        CHECK(uint8_t(e) < 7);
        CHECK(e == e2);
        CHECK(aiuti::stessaConfigurazione(*pulita, *sporca));
        const std::string problema = aiuti::controllaConfigurazione(*pulita, e);
        if (!problema.empty()) {
            std::printf("\n    prova %ld: %s", conti.prove, problema.c_str());
            conti.falliti++;
        }
        CHECK(problema.empty());
        if (e == Esito::OK) {
            if (pulita->troncata) conti.troncate++;
            esercita(*pulita, g, conti);
        }
        fuzzPiccoli(mem, g, conti);
    }
    const double secondi =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - inizio).count();
    std::printf("\n    %ld descrittori rovinati in %.1f s (%.0f al secondo): OK %ld, MALFORMATO %ld, NON_AUDIO %ld, "
                "SOLO_UAC3 %ld, NESSUNA_USCITA %ld (troncate %ld)",
                conti.prove, secondi, conti.prove / (secondi > 0 ? secondi : 1), conti.esiti[0], conti.esiti[1],
                conti.esiti[2], conti.esiti[3], conti.esiti[4], conti.troncate);
    std::printf("\n    scelte riuscite %ld, strade %ld, clock trovati %ld, range buone %ld, stringhe %ld, "
                "invarianti violati %ld ",
                conti.scelte, conti.strade, conti.clock, conti.range, conti.stringhe, conti.falliti);
    CHECK_EQ(conti.falliti, 0L);
    CHECK(conti.prove > 1000);
    CHECK(conti.esiti[0] > 0 && conti.esiti[1] > 0);
}
