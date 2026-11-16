// Test sul PC del pilota del GPS del T-Deck (src/tdeck/gnss_pilota.cpp) con un ricevitore finto (finto.h):
// riconoscimento della velocita', standby e sveglia coi loro tempi, PMREQ ignorato o perso, cane da
// guardia, dati che scadono, ripartenza da freddo, versione, prova al banco, storia, un solo scrittore.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "finto.h"
#include "flussi.h"
#include "gnss_pilota.h"
#include "testing.h"

using scheda::Fix;
using scheda::Gnss;
using scheda::gps::Pilota;
using Ev = Gnss::Ev;
using St = Gnss::Stato;

#define CONTROLLA(cond, ...)                                                       \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::printf("    FALLITO %s:%d  %s  -- ", __FILE__, __LINE__, #cond);  \
            std::printf(__VA_ARGS__);                                              \
            std::printf("\n");                                                     \
            g_failures++;                                                          \
        }                                                                          \
    } while (0)

namespace {
// I frame che il T-Deck deve mandare, byte per byte (UBX-21035062; il PMREQ come l'ha ricostruito
// l'indagine: checksum 5d 4b con flag 0x02, 61 6b con 0x06).
const std::vector<uint8_t> PMREQ_02 = {0xB5, 0x62, 0x02, 0x41, 0x10, 0x00, 0, 0, 0, 0, 0, 0, 0, 0,
                                       0x02, 0, 0, 0, 0x08, 0, 0, 0, 0x5D, 0x4B};
const std::vector<uint8_t> PMREQ_06 = {0xB5, 0x62, 0x02, 0x41, 0x10, 0x00, 0, 0, 0, 0, 0, 0, 0, 0,
                                       0x06, 0, 0, 0, 0x08, 0, 0, 0, 0x61, 0x6B};
const std::vector<uint8_t> CFG_RST = {0xB5, 0x62, 0x06, 0x04, 0x04, 0x00, 0xFF, 0xFF, 0x02, 0x00, 0x0E, 0x61};
const std::vector<uint8_t> MON_VER = {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};
const std::vector<uint8_t> MON_HW3 = {0xB5, 0x62, 0x0A, 0x37, 0x00, 0x00, 0x41, 0xCD};

// Il ricevitore finto e il pilota, accesi come fa Gnss::begin() (voluto acceso, poi la ricerca).
struct Banco {
    Finto f;
    Fix fix;
    Pilota p{f, fix};
    void accendi(const std::vector<std::string>& scena = ubloxFix()) {
        f.scena = scena;
        p.vuoi(true, "avvio");
        p.avvio();
    }
    Fix leggi() {
        f.blocca();
        const Fix x = fix;
        f.sblocca();
        return x;
    }
    // Fino a che lo stato e' s (al massimo ms): true se ci arriva.
    bool aspetta(St s, uint64_t ms) {
        const uint64_t fine = f.t + ms;
        while (f.t < fine && p.stato() != s) p.giro();
        return p.stato() == s;
    }
    // In standby col PMREQ rispettato, partendo da acceso.
    void inStandby() {
        gira(p, f, 3000);
        p.vuoi(false, "prova");
        aspetta(St::STANDBY, 10000);
    }
    bool pulito() const { return f.doppi == 0 && !f.bloccato; }
};

// L'istante del primo frame cls/id scritto da t in poi (0 se non c'e').
uint64_t primoDa(const Finto& f, uint8_t cls, uint8_t id, uint64_t t) {
    for (const auto& s : f.scritti)
        if (s.t >= t && s.b.size() >= 8 && s.b[0] == 0xB5 && s.b[2] == cls && s.b[3] == id) return s.t;
    return 0;
}
}  // namespace

TEST(gps_rileva_ublox) {
    Banco b;
    b.accendi();
    CHECK(b.p.stato() == St::ACCESO);
    CHECK(std::strcmp(b.p.modulo(), "u-blox") == 0 && b.p.ublox());
    CHECK(b.f.portaBaud == 38400);
    CHECK(b.f.contaScritti(0xFF, 0) == 1);  // lo 0xFF della ricerca (sveglia un u-blox rimasto in standby)
    const Gnss::Evento e = ultimo(b.p, Ev::RILEVATO);
    CONTROLLA(e.a == 1 && e.b == 38400 && e.c >= 0 && e.c < 1500, "a=%ld b=%ld c=%ld", long(e.a), long(e.b), long(e.c));
    gira(b.p, b.f, 3000);
    const Fix x = b.leggi();
    CHECK(x.valid && x.sats == 8 && x.conSegnale == 11 && x.inVista == 16 && x.snrMax == 44);
    CHECK(x.frasiOk > 20 && x.frasiKo <= 1);  // al massimo la prima frase, letta a meta' durante la ricerca
    CHECK(b.p.risponde());
    CONTROLLA(b.p.silenzioMs() < 1100, "silenzio=%lu", (unsigned long)b.p.silenzioMs());
    CHECK(conta(b.p, Ev::PRIMO_FIX) == 1 && conta(b.p, Ev::VOLUTO) == 1);
    CHECK(b.p.diagnosi().snrMaxAcceso == 44);
    CHECK(b.pulito());
}

TEST(gps_rileva_l76k) {
    Banco b;
    b.f.baud = 9600;
    b.f.ublox = false;
    b.accendi(l76kFix());
    CHECK(b.p.stato() == St::ACCESO && std::strcmp(b.p.modulo(), "L76K") == 0 && !b.p.ublox());
    CHECK(b.f.portaBaud == 9600);
    const Gnss::Evento e = ultimo(b.p, Ev::RILEVATO);
    CONTROLLA(e.a == 2 && e.b == 9600 && e.c >= 1500 && e.c < 3000, "a=%ld b=%ld c=%ld", long(e.a), long(e.b), long(e.c));
    b.p.vuoi(false, "prova");
    gira(b.p, b.f, 10000);
    CHECK(b.f.contaScritti(0x02, 0x41) == 0);  // l'L76K resta acceso
    CHECK(b.p.stato() == St::ACCESO && !b.p.inStandby());
    CHECK(b.leggi().valid && b.leggi().sats == 6);
    CHECK(b.pulito());
}

TEST(gps_muto_poi_trovato) {
    Banco b;
    b.f.vivo = false;
    b.accendi();
    CHECK(b.p.stato() == St::MUTO && std::strcmp(b.p.modulo(), "muto") == 0 && !b.p.ublox());
    CHECK(b.f.portaBaud == 9600 && b.f.contaScritti(0xFF, 0) == 3);  // tre giri, 38400 col byte poi 9600
    const Gnss::Evento m = ultimo(b.p, Ev::MUTO);
    CONTROLLA(m.c >= 8900 && m.c <= 9500, "cercato per %ld ms", long(m.c));
    // Voluto acceso: si riprova ogni 30 s (non prima).
    gira(b.p, b.f, 25000);
    CHECK(b.f.contaScritti(0xFF, 0) == 3 && b.p.stato() == St::MUTO);
    gira(b.p, b.f, 6000);
    CHECK(b.f.contaScritti(0xFF, 0) == 6 && conta(b.p, Ev::MUTO) == 2);
    // Il ricevitore si fa vivo (a 38400 mentre la UART e' rimasta a 9600): lo trova al giro dopo.
    b.f.vivo = true;
    b.f.prossimaEpoca = b.f.t;
    CHECK(b.aspetta(St::ACCESO, 32000));
    CHECK(std::strcmp(b.p.modulo(), "u-blox") == 0 && b.f.portaBaud == 38400);
    gira(b.p, b.f, 2500);
    CHECK(b.leggi().valid && b.p.risponde());

    // Non voluto e muto: niente tentativi; voluto di nuovo: si riprova subito.
    Banco c;
    c.f.vivo = false;
    c.f.scena = ubloxFix();
    c.p.vuoi(false, "prova");
    c.p.avvio();
    CHECK(c.p.stato() == St::MUTO);
    gira(c.p, c.f, 60000);
    CHECK(c.f.contaScritti(0xFF, 0) == 3);
    const uint64_t voluto = c.f.t;
    c.p.vuoi(true, "prova");
    gira(c.p, c.f, 200);  // la ricerca blocca il giro per 9 s: tre 0xFF in piu'
    CHECK(c.f.contaScritti(0xFF, 0) == 6 && c.f.quando(0xFF, 0, 3) - voluto < 100);
    CHECK(b.pulito() && c.pulito());
}

TEST(gps_standby_e_sveglia) {
    Banco b;
    b.accendi();
    gira(b.p, b.f, 3000);
    const Fix prima = b.leggi();
    CHECK(prima.valid);
    const uint64_t spento = b.f.t;
    b.p.vuoi(false, "prova");
    gira(b.p, b.f, 200);
    CHECK(b.f.contaScritti(0x02, 0x41) == 1 && b.f.frame(0x02, 0x41) == PMREQ_06);
    CONTROLLA(b.f.quando(0x02, 0x41) - spento < 150, "PMREQ dopo %lu ms", (unsigned long)(b.f.quando(0x02, 0x41) - spento));
    CHECK(b.p.stato() == St::ADDORMENTA && !b.p.inStandby());
    CHECK(b.aspetta(St::STANDBY, 5000));
    CHECK(b.p.inStandby() && b.f.dorme && b.f.pmreqRicevuti == 1);
    CONTROLLA(b.f.t - b.f.quando(0x02, 0x41) <= 1800, "standby dopo %lu ms", (unsigned long)(b.f.t - b.f.quando(0x02, 0x41)));
    const Gnss::Evento ok = ultimo(b.p, Ev::PMREQ_OK);
    CHECK(ok.ms && ok.b == 0x06 && ok.c == 1 && ok.a >= 0 && ok.a < 300);
    CHECK(b.p.diagnosi().pmreq == 1 && b.p.diagnosi().standby);
    // In standby: niente posizione valida ne' satelliti; l'ultima posizione resta.
    const Fix x = b.leggi();
    CHECK(!x.valid && x.sats == 0 && x.inVista == 0 && x.conSegnale == 0 && x.snrMax == 0 && x.hdop == 99);
    CHECK(x.lat == prima.lat && x.lon == prima.lon && x.atMs == prima.atMs && x.timeValid);
    CHECK(b.p.silenzioMs() == 0);  // non lo si vuole acceso
    gira(b.p, b.f, 60000);
    CHECK(b.f.contaScritti(0x02, 0x41) == 1 && b.f.contaScritti(0xFF, 0) == 1 && b.p.stato() == St::STANDBY);

    // Sveglia: 0xFF, frase dopo 300 ms (latenza del finto) e i dati tornano.
    const uint64_t acceso = b.f.t;
    b.p.vuoi(true, "prova");
    gira(b.p, b.f, 100);
    CHECK(b.f.contaScritti(0xFF, 0) == 2 && b.f.quando(0xFF, 0, 1) - acceso < 100);
    CHECK(b.p.stato() == St::SVEGLIA && b.p.silenzioMs() < 200);  // la sveglia appena chiesta non e' silenzio
    CHECK(b.aspetta(St::ACCESO, 3000));
    const Gnss::Evento s = ultimo(b.p, Ev::SVEGLIO);
    CONTROLLA(s.a >= 300 && s.a <= 450 && s.b == 1, "sveglio in %ld ms con %ld tentativi", long(s.a), long(s.b));
    CHECK(b.p.diagnosi().svegliaMs == s.a && b.p.diagnosi().svegliaTentativi == 1);
    gira(b.p, b.f, 2000);
    CHECK(b.leggi().valid && b.leggi().conSegnale == 11 && b.p.risponde() && !b.p.inStandby());
    CHECK(b.pulito());
}

TEST(gps_sveglia_con_tentativi) {
    Banco b;
    b.accendi();
    b.inStandby();
    CHECK(b.p.stato() == St::STANDBY);
    b.f.frontiPersi = 2;  // i primi due 0xFF non lo svegliano
    const int ff = b.f.contaScritti(0xFF, 0);
    b.p.vuoi(true, "prova");
    CHECK(b.aspetta(St::ACCESO, 12000));
    const Gnss::Evento s = ultimo(b.p, Ev::SVEGLIO);
    CONTROLLA(s.b == 3 && s.a >= 4300 && s.a <= 4500, "sveglio in %ld ms con %ld tentativi", long(s.a), long(s.b));
    CHECK(b.f.contaScritti(0xFF, 0) - ff == 3);
    // I tentativi sono a 2 s l'uno dall'altro.
    const uint64_t t1 = b.f.quando(0xFF, 0, ff), t2 = b.f.quando(0xFF, 0, ff + 1), t3 = b.f.quando(0xFF, 0, ff + 2);
    CONTROLLA(t2 - t1 >= 2000 && t2 - t1 < 2100 && t3 - t2 >= 2000 && t3 - t2 < 2100, "%lu %lu",
              (unsigned long)(t2 - t1), (unsigned long)(t3 - t2));
    CHECK(b.pulito());
}

TEST(gps_sveglia_fallita_poi_rileva) {
    Banco b;
    b.accendi();
    b.inStandby();
    b.f.vivo = false;  // non si sveglia piu' (staccato)
    const int ff = b.f.contaScritti(0xFF, 0);
    b.p.vuoi(true, "prova");
    CHECK(b.aspetta(St::MUTO, 25000));
    CHECK(conta(b.p, Ev::SVEGLIA_FALLITA) == 1 && ultimo(b.p, Ev::SVEGLIA_FALLITA).a == 5);
    CHECK(b.f.contaScritti(0xFF, 0) - ff == 5 + 3);  // 5 sveglie, poi i 3 giri della ricerca
    // Torna (riacceso, sveglio): la ricerca ogni 30 s lo ritrova.
    b.f.vivo = true;
    b.f.dorme = false;
    b.f.prossimaEpoca = b.f.t;
    CHECK(b.aspetta(St::ACCESO, 32000));
    CHECK(std::strcmp(b.p.modulo(), "u-blox") == 0);
    CHECK(b.pulito());
}

TEST(gps_pmreq_ignorato) {
    Banco b;
    b.f.ignoraPmreq = true;
    b.accendi();
    gira(b.p, b.f, 3000);
    b.p.vuoi(false, "prova");
    gira(b.p, b.f, 6000);
    CHECK(conta(b.p, Ev::PMREQ_IGNORATO) == 1 && conta(b.p, Ev::PMREQ_OK) == 0);
    CHECK(b.p.stato() == St::ACCESO && !b.p.inStandby() && b.p.diagnosi().pmreq == -1);
    CHECK(b.f.contaScritti(0x02, 0x41) == 1);
    // Da qui in poi niente piu' PMREQ: resta sempre acceso, e i suoi dati sono veri.
    for (int i = 0; i < 3; i++) {
        b.p.vuoi(true, "prova");
        gira(b.p, b.f, 2000);
        b.p.vuoi(false, "prova");
        gira(b.p, b.f, 8000);
    }
    CHECK(b.f.contaScritti(0x02, 0x41) == 1 && conta(b.p, Ev::PMREQ_IGNORATO) == 1);
    CHECK(b.leggi().valid && b.p.risponde());
    bool avviso = false;
    for (const auto& r : b.f.righe) avviso |= r.rfind("W PMREQ ignorato", 0) == 0;
    CHECK(avviso);
    CHECK(b.pulito());
}

// Taceva da piu' di 1,5 s quando lo si vuole spento: prima 0xFF e 500 ms, poi il PMREQ.
TEST(gps_tace_prima_del_pmreq) {
    Banco b;
    b.accendi();
    gira(b.p, b.f, 3000);
    b.f.dorme = true;  // si e' addormentato da solo (il pilota non lo sa)
    gira(b.p, b.f, 2500);
    CHECK(b.p.stato() == St::ACCESO && conta(b.p, Ev::SILENZIO) == 0);
    const int ff = b.f.contaScritti(0xFF, 0);
    b.p.vuoi(false, "prova");
    CHECK(b.aspetta(St::STANDBY, 6000));
    CHECK(b.f.contaScritti(0xFF, 0) == ff + 1);
    const uint64_t tff = b.f.quando(0xFF, 0, ff), tpm = b.f.quando(0x02, 0x41);
    CONTROLLA(tpm > tff && tpm - tff >= 500 && tpm - tff < 600, "PMREQ %lu ms dopo lo 0xFF", (unsigned long)(tpm - tff));
    CHECK(b.f.pmreqRicevuti == 1 && b.f.pmreqPersi == 0);
    CHECK(ultimo(b.p, Ev::PMREQ_OK).c == 1 && b.p.diagnosi().pmreq == 1);
    CHECK(b.pulito());
}

// Si sveglia piu' lento dei 500 ms: il PMREQ si perde. Non e' "ignorato": si rimanda.
TEST(gps_pmreq_perso_mentre_si_sveglia) {
    Banco b;
    b.f.latenzaSveglia = 800;
    b.accendi();
    gira(b.p, b.f, 3000);
    b.f.dorme = true;
    gira(b.p, b.f, 2500);
    b.p.vuoi(false, "prova");
    CHECK(b.aspetta(St::STANDBY, 12000));
    CHECK(b.f.pmreqPersi == 1 && b.f.pmreqRicevuti == 1 && b.f.contaScritti(0x02, 0x41) == 2);
    CHECK(conta(b.p, Ev::PMREQ_RIPROVA) == 1 && conta(b.p, Ev::PMREQ_IGNORATO) == 0);
    CHECK(b.p.diagnosi().pmreq == 1);
    CHECK(b.pulito());
}

// Voluto spento mentre si sveglia (dopo il terzo 0xFF): tace da 1,5 s, quindi 0xFF, 500 ms e PMREQ, che arriva
// a un ricevitore sveglio da meno di 1 s. Se lo perde non e' "ignorato" (sarebbe per sempre): si rimanda, a
// ricevitore sveglio da 1 s. Col ricevitore normale e con uno che appena sveglio non sente i comandi UBX.
TEST(gps_sveglia_interrotta_non_ignorato) {
    for (int sordo = 0; sordo <= 1; sordo++) {
        Banco b;
        b.f.sordoDopoSveglia = sordo ? 500 : 0;
        b.accendi();
        b.inStandby();
        CHECK(b.p.stato() == St::STANDBY && b.p.diagnosi().pmreq == 1);
        b.f.frontiPersi = 2;  // servono tre 0xFF
        const int ff = b.f.contaScritti(0xFF, 0);
        b.p.vuoi(true, "prova");
        for (int i = 0; i < 100000 && b.f.contaScritti(0xFF, 0) < ff + 3; i++) b.p.giro();
        gira(b.p, b.f, 50);  // si sveglia 300 ms dopo il terzo 0xFF: prima lo si vuole spento
        b.p.vuoi(false, "prova");
        CHECK(b.aspetta(St::STANDBY, 12000));
        const uint64_t sveglio = b.f.sveglioA, tpm = primoDa(b.f, 0x02, 0x41, sveglio);
        CONTROLLA(tpm && tpm - sveglio < Pilota::SVEGLIO_DA_MS, "sordo %d: primo PMREQ %ld ms dopo la sveglia", sordo,
                  tpm ? long(tpm - sveglio) : -1L);
        CONTROLLA(b.p.diagnosi().pmreq == 1 && conta(b.p, Ev::PMREQ_IGNORATO) == 0 && b.p.inStandby(),
                  "sordo %d: pmreq %d, stato %s", sordo, int(b.p.diagnosi().pmreq), Gnss::nomeStato(b.p.stato()));
        CHECK(conta(b.p, Ev::PMREQ_RIPROVA) == sordo && b.f.pmreqPersi == sordo);
        CHECK(b.pulito());
    }
}

// Ripartenza da freddo e voluto spento nello stesso giro: il PMREQ non parte subito dopo il CFG-RST, ma 1 s dopo
// (come dopo una sveglia).
TEST(gps_pmreq_dopo_freddo_aspetta) {
    Banco b;
    b.accendi();
    gira(b.p, b.f, 3000);
    b.p.freddo();
    b.p.vuoi(false, "prova");
    CHECK(b.aspetta(St::STANDBY, 6000));
    const uint64_t rst = b.f.quando(0x06, 0x04), pm = b.f.quando(0x02, 0x41);
    CHECK(b.f.freddi == 1 && b.f.pmreqRicevuti == 1 && b.p.diagnosi().pmreq == 1);
    CONTROLLA(rst && pm >= rst + Pilota::SVEGLIO_DA_MS, "CFG-RST a %lu ms, PMREQ a %lu ms", (unsigned long)rst,
              (unsigned long)pm);
    CHECK(b.pulito());
}

TEST(gps_cane_da_guardia) {
    Banco b;
    b.accendi();
    gira(b.p, b.f, 3000);
    const Fix prima = b.leggi();
    CHECK(prima.valid);
    b.f.vivo = false;  // tace (cavo, alimentazione...): il pilota non lo sa
    const uint64_t zitto = b.f.t;
    gira(b.p, b.f, 4500);
    CHECK(b.p.stato() == St::ACCESO && b.f.contaScritti(0xFF, 0) == 1);
    gira(b.p, b.f, 1500);
    CHECK(conta(b.p, Ev::SILENZIO) == 1 && b.p.stato() == St::SVEGLIA);
    const uint64_t tff = b.f.quando(0xFF, 0, 1);
    CONTROLLA(tff - zitto >= 4800 && tff - zitto <= 5600, "sveglia %lu ms dopo l'ultima frase", (unsigned long)(tff - zitto));
    CONTROLLA(b.p.silenzioMs() > 5000, "silenzio=%lu", (unsigned long)b.p.silenzioMs());
    // Dati vecchi: dopo 5 s senza frasi niente posizione valida, usati, in vista, con segnale.
    const Fix x = b.leggi();
    CHECK(!x.valid && x.sats == 0 && x.inVista == 0 && x.conSegnale == 0 && x.snrMax == 0);
    CHECK(x.lat == prima.lat && x.atMs == prima.atMs);
    CHECK(!b.p.risponde());
    b.f.vivo = true;
    b.f.prossimaEpoca = b.f.t + 500;
    CHECK(b.aspetta(St::ACCESO, 3000));
    gira(b.p, b.f, 1500);
    CHECK(b.leggi().valid && b.p.risponde());
    CHECK(b.pulito());
}

TEST(gps_freddo_aspetta_sveglio) {
    Banco b;
    b.accendi();
    b.inStandby();
    b.p.freddo();
    gira(b.p, b.f, 3000);
    CHECK(b.f.contaScritti(0x06, 0x04) == 0);  // dorme: il comando si perderebbe
    b.f.scena = ubloxFreddo();
    b.p.vuoi(true, "prova");
    CHECK(b.aspetta(St::ACCESO, 3000));
    const uint64_t sveglio = b.f.t;
    gira(b.p, b.f, 2000);
    CHECK(b.f.contaScritti(0x06, 0x04) == 1 && b.f.frame(0x06, 0x04) == CFG_RST && b.f.freddi == 1);
    CONTROLLA(b.f.quando(0x06, 0x04) - sveglio >= 900, "CFG-RST %lu ms dopo la sveglia",
              (unsigned long)(b.f.quando(0x06, 0x04) - sveglio));
    const Fix x = b.leggi();
    CHECK(!x.valid && !x.timeValid && x.unixTime == 0 && x.lat == 0);  // come dopo l'interruttore
    CHECK(x.frasiOk > 20);                                             // la diagnosi resta
    CHECK(b.p.diagnosi().freddi == 1 && conta(b.p, Ev::FREDDO) == 1);
    // Una richiesta che non trova il ricevitore sveglio entro 60 s cade.
    b.inStandby();
    b.p.freddo();
    gira(b.p, b.f, 61000);
    CHECK(conta(b.p, Ev::FREDDO_SCADUTO) == 1);
    b.p.vuoi(true, "prova");
    gira(b.p, b.f, 5000);
    CHECK(b.f.contaScritti(0x06, 0x04) == 1 && b.p.diagnosi().freddi == 1);
    CHECK(b.pulito());
}

TEST(gps_versione) {
    Banco b;
    b.accendi();
    gira(b.p, b.f, 1500);
    const uint32_t ko = b.leggi().frasiKo;
    b.p.chiediVersione();
    gira(b.p, b.f, 1500);
    CHECK(b.f.frame(0x0A, 0x04) == MON_VER && b.f.frame(0x0A, 0x37) == MON_HW3 && b.f.poll == 2);
    const Gnss::Versione v = b.p.versione();
    CHECK(v.risposteVer == 1 && v.risposteHw3 == 1);
    CHECK(!std::strcmp(v.sw, "ROM SPG 5.10 (7b202e)") && !std::strcmp(v.hw, "000A0000"));
    CHECK(v.nEstensioni == 5 && !std::strcmp(v.estensioni[0], "FWVER=SPG 5.10") &&
          !std::strcmp(v.estensioni[2], "MOD=MIA-M10Q") && !std::strcmp(v.estensioni[4], "SBAS;QZSS"));
    CHECK(v.hw3Flags == 0x04 && v.hw3Pin == 2 && v.hw3Len == 34 && v.hw3Dati[2] == 0x04);
    // Nei frame c'erano '$' e a capo: l'analizzatore NMEA non li ha visti.
    CONTROLLA(b.leggi().frasiKo == ko, "ko %u -> %u", unsigned(ko), unsigned(b.leggi().frasiKo));
    // In standby la richiesta aspetta la sveglia; sveglio da almeno 1 s.
    b.inStandby();
    b.p.chiediVersione();
    gira(b.p, b.f, 3000);
    CHECK(b.f.poll == 2);
    b.p.vuoi(true, "prova");
    CHECK(b.aspetta(St::ACCESO, 3000));
    const uint64_t sveglio = b.f.t;
    gira(b.p, b.f, 2000);
    CHECK(b.f.poll == 4 && b.p.versione().risposteVer == 2 && b.f.quando(0x0A, 0x04, 1) - sveglio >= 900);
    // L'L76K non parla UBX: la richiesta cade senza scrivere niente.
    Banco c;
    c.f.baud = 9600;
    c.f.ublox = false;
    c.accendi(l76kFix());
    const size_t scritti = c.f.scritti.size();
    c.p.chiediVersione();
    gira(c.p, c.f, 3000);
    CHECK(c.f.scritti.size() == scritti && c.p.versione().risposteVer == 0);
    CHECK(b.pulito() && c.pulito());
}

TEST(gps_prova_banco) {
    Banco b;
    b.accendi();
    gira(b.p, b.f, 2000);
    b.p.prova(3, false);
    CHECK(b.p.provaStato().inCorso && b.p.provaStato().cicli == 3);
    const uint32_t numero = b.p.provaStato().numero;
    gira(b.p, b.f, 200);
    CHECK(b.p.stato() == St::PROVA);
    for (int i = 0; i < 600 && b.p.provaStato().inCorso; i++) gira(b.p, b.f, 100);
    const Gnss::Prova r = b.p.provaStato();
    CHECK(!r.inCorso && r.fatti == 3 && !r.errore && r.numero == numero && !r.forza);
    for (int i = 0; i < 3; i++) {
        const Gnss::CicloProva& c = r.ciclo[i];
        CONTROLLA(c.esito == 1 && c.fermeMs >= 0 && c.fermeMs < 1000 && c.frasiNelSonno == 0 && c.sveglioMs >= 300 &&
                      c.sveglioMs <= 450 && c.tentativi == 1,
                  "ciclo %d: esito %d ferme %d sonno %u sveglio %d tentativi %u", i, int(c.esito), int(c.fermeMs),
                  unsigned(c.frasiNelSonno), int(c.sveglioMs), unsigned(c.tentativi));
    }
    CHECK(b.f.contaScritti(0x02, 0x41) == 3 && b.f.frame(0x02, 0x41, 2) == PMREQ_06);
    CHECK(conta(b.p, Ev::PROVA_CICLO) == 3 && conta(b.p, Ev::PROVA_FINE) == 1);
    // Torna com'era (voluto acceso): sveglio e senza standby; il PMREQ normale non e' "provato".
    gira(b.p, b.f, 1500);
    CHECK(b.p.stato() == St::ACCESO && b.p.risponde() && b.p.diagnosi().pmreq == 0);

    // Col flag force e da spento: parte svegliandolo, finisce di nuovo in standby (flag normali).
    b.inStandby();
    const int pm = b.f.contaScritti(0x02, 0x41);
    b.p.prova(2, true);
    for (int i = 0; i < 600 && b.p.provaStato().inCorso; i++) gira(b.p, b.f, 100);
    const Gnss::Prova r2 = b.p.provaStato();
    CHECK(r2.fatti == 2 && r2.forza && !r2.errore && r2.ciclo[0].esito == 1 && r2.ciclo[1].esito == 1);
    CHECK(b.f.frame(0x02, 0x41, pm) == PMREQ_06 && b.f.frame(0x02, 0x41, pm + 1) == PMREQ_06);
    CHECK(b.aspetta(St::STANDBY, 6000));
    CHECK(b.f.frame(0x02, 0x41, pm + 2) == PMREQ_06 && b.f.ultimoFlag == 0x06);
    CHECK(b.pulito());
}

TEST(gps_prova_ignorato_e_errori) {
    Banco b;
    b.f.ignoraPmreq = true;
    b.accendi();
    gira(b.p, b.f, 2000);
    b.p.prova(2, false);
    for (int i = 0; i < 600 && b.p.provaStato().inCorso; i++) gira(b.p, b.f, 100);
    const Gnss::Prova r = b.p.provaStato();
    CHECK(r.fatti == 2 && !r.errore && r.ciclo[0].esito == 0 && r.ciclo[1].esito == 0 && r.ciclo[0].fermeMs == -1);
    CHECK(b.p.diagnosi().pmreq == 0 && b.p.stato() == St::ACCESO);
    // Una seconda richiesta mentre una prova e' in corso non la ricomincia.
    b.p.prova(4, false);
    const uint32_t n = b.p.provaStato().numero;
    b.p.prova(1, false);
    CHECK(b.p.provaStato().numero == n && b.p.provaStato().cicli == 4);
    for (int i = 0; i < 900 && b.p.provaStato().inCorso; i++) gira(b.p, b.f, 100);
    CHECK(b.p.provaStato().fatti == 4);

    // L76K: non parte e non cambia niente.
    Banco c;
    c.f.baud = 9600;
    c.f.ublox = false;
    c.accendi(l76kFix());
    c.p.prova(3, false);
    gira(c.p, c.f, 200);
    CHECK(!c.p.provaStato().inCorso && c.p.provaStato().errore && !std::strcmp(c.p.provaStato().errore, "solo con l'u-blox"));
    CHECK(c.p.stato() == St::ACCESO && c.p.provaStato().fatti == 0);
    // Ricevitore muto: idem.
    Banco d;
    d.f.vivo = false;
    d.accendi();
    d.p.prova(3, false);
    gira(d.p, d.f, 200);
    CHECK(!d.p.provaStato().inCorso && d.p.provaStato().errore &&
          !std::strcmp(d.p.provaStato().errore, "il GPS non risponde"));
    CHECK(b.pulito() && c.pulito() && d.pulito());
}

// Prova chiesta appena dopo una sveglia: anche il primo PMREQ della prova aspetta che sia sveglio da 1 s (un M10
// appena sveglio puo' perderlo, e il primo ciclo direbbe "ignorato" invece di "standby").
TEST(gps_prova_appena_sveglio) {
    for (int sordo = 0; sordo <= 1; sordo++) {
        Banco b;
        b.f.sordoDopoSveglia = sordo ? 500 : 0;
        b.accendi();
        b.inStandby();
        b.p.vuoi(true, "prova");
        CHECK(b.aspetta(St::ACCESO, 3000));
        const uint64_t sveglio = b.f.sveglioA;
        b.p.prova(1, false);
        for (int i = 0; i < 300 && b.p.provaStato().inCorso; i++) gira(b.p, b.f, 100);
        const uint64_t tpm = primoDa(b.f, 0x02, 0x41, sveglio);
        CONTROLLA(tpm >= sveglio + Pilota::SVEGLIO_DA_MS, "sordo %d: PMREQ %ld ms dopo la sveglia", sordo,
                  long(tpm - sveglio));
        const Gnss::Prova r = b.p.provaStato();
        CONTROLLA(r.fatti == 1 && r.ciclo[0].esito == 1 && !r.errore, "sordo %d: esito %d", sordo, int(r.ciclo[0].esito));
        CHECK(b.f.ubxPersiDaSveglio == 0);
        CHECK(b.pulito());
    }
}

// Prova al banco col GPS non voluto (di solito al banco e' cosi'): finita la prova torna in standby, col PMREQ
// almeno 1 s dopo la sveglia dell'ultimo ciclo. Prima partiva subito: un M10 appena sveglio puo' perderlo, e il
// pilota concludeva "PMREQ ignorato" (sempre acceso fino al riavvio) contro quello che la prova aveva appena detto.
TEST(gps_dopo_prova_pmreq_dopo_1_s) {
    for (int sordo = 0; sordo <= 1; sordo++) {
        Banco b;
        b.f.sordoDopoSveglia = sordo ? 500 : 0;
        b.accendi();
        b.inStandby();
        CHECK(b.p.diagnosi().pmreq == 1);
        b.p.prova(2, false);
        for (int i = 0; i < 600 && b.p.provaStato().inCorso; i++) gira(b.p, b.f, 100);
        const Gnss::Prova r = b.p.provaStato();
        CHECK(!r.inCorso && r.fatti == 2 && !r.errore && r.ciclo[0].esito == 1 && r.ciclo[1].esito == 1);
        CHECK(b.aspetta(St::STANDBY, 10000));
        const uint64_t sveglio = b.f.sveglioA, tpm = primoDa(b.f, 0x02, 0x41, sveglio);
        CONTROLLA(tpm >= sveglio + Pilota::SVEGLIO_DA_MS, "sordo %d: PMREQ %ld ms dopo la sveglia", sordo,
                  long(tpm - sveglio));
        gira(b.p, b.f, 8000);
        CONTROLLA(b.p.diagnosi().pmreq == 1 && conta(b.p, Ev::PMREQ_IGNORATO) == 0 && b.p.stato() == St::STANDBY,
                  "sordo %d: pmreq %d, stato %s", sordo, int(b.p.diagnosi().pmreq), Gnss::nomeStato(b.p.stato()));
        CHECK(b.f.ubxPersiDaSveglio == 0);
        CHECK(b.pulito());
    }
}

// In standby si risveglia da solo: di nuovo PMREQ, ma al massimo 4 volte; poi resta acceso.
TEST(gps_da_solo_poi_rinuncia) {
    Banco b;
    b.f.svegliaDaSolo = 3000;
    b.accendi();
    gira(b.p, b.f, 3000);
    b.p.vuoi(false, "prova");
    gira(b.p, b.f, 60000);
    CHECK(conta(b.p, Ev::DA_SOLO) == 4 && conta(b.p, Ev::RINUNCIA) == 1);
    CHECK(b.f.contaScritti(0x02, 0x41) == Pilota::TENTATIVI_DORMI);
    CHECK(b.p.stato() == St::ACCESO && !b.p.inStandby());
    gira(b.p, b.f, 30000);
    CHECK(b.f.contaScritti(0x02, 0x41) == Pilota::TENTATIVI_DORMI);
    // Riacceso e rispento: ci riprova.
    b.p.vuoi(true, "prova");
    gira(b.p, b.f, 2000);
    b.p.vuoi(false, "prova");
    gira(b.p, b.f, 2000);
    CHECK(b.f.contaScritti(0x02, 0x41) == Pilota::TENTATIVI_DORMI + 1);
    CHECK(b.pulito());
}

TEST(gps_sessione_e_ricerca) {
    Banco b;
    b.accendi(ubloxRicerca());
    gira(b.p, b.f, 125000);
    CONTROLLA(conta(b.p, Ev::RICERCA) == 2, "%d", conta(b.p, Ev::RICERCA));
    const Gnss::Evento r = ultimo(b.p, Ev::RICERCA);
    CHECK(r.a == 4 && r.b == 24 && r.c == 9);
    CHECK(conta(b.p, Ev::PRIMA_ORA) == 0 && conta(b.p, Ev::PRIMO_FIX) == 0);
    b.f.scena = ubloxOraSenzaFix();
    gira(b.p, b.f, 2000);
    const Gnss::Evento o = ultimo(b.p, Ev::PRIMA_ORA);
    CONTROLLA(conta(b.p, Ev::PRIMA_ORA) == 1 && o.a > 125000 && o.a < 128000, "%ld", long(o.a));
    b.f.scena = ubloxFix();
    gira(b.p, b.f, 3000);
    const Gnss::Evento p = ultimo(b.p, Ev::PRIMO_FIX);
    CHECK(conta(b.p, Ev::PRIMO_FIX) == 1 && p.b == 8 && p.c == 11 && p.a > o.a);
    gira(b.p, b.f, 70000);
    CHECK(conta(b.p, Ev::RICERCA) == 2 && conta(b.p, Ev::PRIMO_FIX) == 1);  // con la posizione non cerca
    char t[128];
    Gnss::testoEvento(p, t, sizeof(t));
    CONTROLLA(std::strstr(t, "(8 usati, 11 con segnale)") != nullptr, "%s", t);
    CHECK(b.pulito());
}

TEST(gps_silenzio_ms) {
    Banco b;
    b.accendi();
    b.inStandby();
    gira(b.p, b.f, 60000);
    CHECK(b.p.silenzioMs() == 0);  // spento
    b.f.vivo = false;
    b.p.vuoi(true, "prova");
    gira(b.p, b.f, 1000);
    CONTROLLA(b.p.silenzioMs() >= 900 && b.p.silenzioMs() < 1200, "%lu", (unsigned long)b.p.silenzioMs());
    gira(b.p, b.f, 5000);
    CONTROLLA(b.p.silenzioMs() > 5000, "%lu", (unsigned long)b.p.silenzioMs());
    CHECK(b.pulito());
}

// Le richieste non scrivono: lo fa solo il compito del GPS (giro()), un frame intero per volta.
TEST(gps_un_solo_scrittore) {
    Banco b;
    b.accendi();
    gira(b.p, b.f, 2000);
    const size_t n = b.f.scritti.size();
    b.p.vuoi(false, "prova");
    b.p.freddo();
    b.p.chiediVersione();
    b.p.prova(2, false);
    b.p.vuoi(true, "prova");
    CHECK(b.f.scritti.size() == n);
    gira(b.p, b.f, 40000);
    for (const auto& s : b.f.scritti) {
        const bool ff = s.b.size() == 1 && s.b[0] == 0xFF;
        const bool ubx = s.b.size() >= 8 && s.b[0] == 0xB5 && s.b[1] == 0x62 && s.b.size() == size_t(s.b[4] | s.b[5] << 8) + 8;
        CHECK(ff || ubx);
    }
    CHECK(b.pulito());
}

TEST(gps_storia) {
    Banco b;
    b.accendi();
    for (int i = 0; i < 70; i++) {
        b.p.vuoi(i % 2 == 0 ? false : true, i % 2 ? "su" : "giu");
        gira(b.p, b.f, 150);
    }
    Gnss::Evento ev[Gnss::STORIA];
    const int n = b.p.storia(ev, Gnss::STORIA);
    CHECK(n == Gnss::STORIA);
    bool ordine = true;
    for (int i = 1; i < n; i++) ordine &= ev[i].ms >= ev[i - 1].ms;
    CHECK(ordine);
    Gnss::Evento pochi[3];
    CHECK(b.p.storia(pochi, 3) == 3 && pochi[2].ms == ev[n - 1].ms && pochi[0].ms == ev[n - 3].ms);
    // Ogni tipo ha il suo testo.
    for (int t = 0; t <= int(Ev::PROVA_FINE); t++) {
        Gnss::Evento e;
        e.tipo = Ev(t);
        e.a = 1;
        e.b = 2;
        e.c = 3;
        char s[128];
        Gnss::testoEvento(e, s, sizeof(s));
        CONTROLLA(s[0] && std::strncmp(s, "evento ", 7) != 0, "tipo %d: '%s'", t, s);
    }
    Gnss::Evento e;
    e.tipo = Ev::VOLUTO;
    e.a = 1;
    e.perche = "mappa";
    char s[64];
    Gnss::testoEvento(e, s, sizeof(s));
    CHECK(!std::strcmp(s, "voluto acceso (mappa)"));
    char corto[8];
    Gnss::testoEvento(e, corto, sizeof(corto));
    CHECK(std::strlen(corto) == 7);
    CHECK(!std::strcmp(Gnss::nomeStato(St::ADDORMENTA), "addormenta") && !std::strcmp(Gnss::nomeStato(St::STANDBY), "standby"));
    CHECK(b.pulito());
}

TEST(gps_frame_ubx) {
    uint8_t b[64];
    const uint8_t pm[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0x02, 0, 0, 0, 0x08, 0, 0, 0};
    size_t n = scheda::gps::frameUbx(0x02, 0x41, pm, 16, b, sizeof(b));
    CHECK(n == 24 && std::vector<uint8_t>(b, b + n) == PMREQ_02);
    n = scheda::gps::frameUbx(0x0A, 0x04, nullptr, 0, b, sizeof(b));
    CHECK(n == 8 && std::vector<uint8_t>(b, b + n) == MON_VER);
    CHECK(scheda::gps::frameUbx(0x02, 0x41, pm, 16, b, 23) == 0);  // non ci sta
    char t[32];
    CHECK(scheda::gps::fraseNmea("PCAS10,2", t, sizeof(t)) == 14 && !std::strcmp(t, "$PCAS10,2*1E\r\n"));
    CHECK(scheda::gps::fraseNmea("PCAS10,2", t, 10) == 0);

    // Lettore UBX: un frame a pezzi, uno col checksum sbagliato, uno lunghissimo (non si mangia il resto).
    scheda::gps::LettoreUbx l;
    int fatti = 0;
    for (uint8_t c : MON_HW3) {
        CHECK(l.byte(c));
        fatti += l.fatto();
    }
    CHECK(fatti == 1 && l.classe() == 0x0A && l.id() == 0x37 && l.lunghezza() == 0);
    std::vector<uint8_t> rotto = MON_VER;
    rotto.back() ^= 1;
    for (uint8_t c : rotto) l.byte(c);
    CHECK(!l.fatto() && l.scartati() == 1);
    const uint8_t lungo[6] = {0xB5, 0x62, 0x01, 0x07, 0xFF, 0xFF};
    for (uint8_t c : lungo) l.byte(c);
    CHECK(l.scartati() == 2);
    scheda::NmeaParser p;
    Fix f;
    for (char c : frase(ubloxFix()[0]))
        if (!l.byte(uint8_t(c))) p.feed(c, f, 1000);
    CHECK(f.valid && f.frasiOk == 1 && f.frasiKo == 0);
    // 0xB5 seguito da altro: quell'altro va all'NMEA.
    CHECK(l.byte(0xB5) && !l.byte('$'));
}
