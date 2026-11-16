// Test sul PC dell'analizzatore NMEA del T-Deck (scheda::NmeaParser, src/tdeck/gnss_pilota.cpp): i campi
// di sempre (posizione, ora, usati, hdop, quota: come l'analizzatore della PCB), i satelliti con segnale e
// il C/N0 migliore dalle serie GSV per costellazione e segnale, le scadenze, i contatori della diagnosi.
// Dalle prove dell'indagine sul GPS del 15/11/2026 (59 verifiche), con la posizione solo dalla RMC.
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "flussi.h"
#include "scheda/gnss.h"
#include "testing.h"

using scheda::Fix;
using scheda::NmeaParser;

#define CONTROLLA(cond, ...)                                                       \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::printf("    FALLITO %s:%d  %s  -- ", __FILE__, __LINE__, #cond);  \
            std::printf(__VA_ARGS__);                                              \
            std::printf("\n");                                                     \
            g_failures++;                                                          \
        }                                                                          \
    } while (0)

#define CIELO(f, e, s, m)                                                                        \
    CONTROLLA((f).inVista == (e) && (f).conSegnale == (s) && (f).snrMax == (m),                   \
              "inVista=%u conSegnale=%u snrMax=%u (attesi %u %u %u)", unsigned((f).inVista),     \
              unsigned((f).conSegnale), unsigned((f).snrMax), unsigned(e), unsigned(s), unsigned(m))

namespace {
void alimenta(NmeaParser& p, Fix& f, const std::string& s, uint64_t now) {
    for (char c : s) p.feed(c, f, now);
    p.scadenze(f, now);
}
}  // namespace

// I campi che c'erano gia', con gli stessi valori dell'analizzatore della PCB (gnss.cpp).
TEST(nmea_campi_di_sempre) {
    NmeaParser p;
    Fix f;
    alimenta(p, f, flusso(ubloxFix()), 1000);
    CHECK(f.valid);
    CONTROLLA(std::fabs(f.lat - (47 + 17.11437 / 60)) < 1e-9, "lat=%.9f", f.lat);
    CONTROLLA(std::fabs(f.lon - (8 + 33.91522 / 60)) < 1e-9, "lon=%.9f", f.lon);
    CHECK(f.sats == 8);
    CHECK(std::fabs(f.hdop - 1.01f) < 1e-6f);
    CHECK(std::fabs(f.altM - 499.6f) < 1e-4f);
    CONTROLLA(f.timeValid && f.unixTime == 1794737745u, "unix=%lu", (unsigned long)f.unixTime);  // 15/11/2026 10:15:45
    CHECK(f.atMs == 1000 && f.timeAtMs == 1000);

    NmeaParser q;
    Fix g;
    alimenta(q, g, flusso(l76kFix()), 2000);
    CHECK(g.valid && g.sats == 6 && std::fabs(g.hdop - 1.6f) < 1e-6f && g.unixTime == 1794737750u);
    CONTROLLA(std::fabs(g.lat - (47 + 17.1144 / 60)) < 1e-9, "lat=%.9f", g.lat);

    NmeaParser r;
    Fix h;
    alimenta(r, h, flusso(at6558Fix()), 3000);
    CHECK(h.valid && h.sats == 5 && std::fabs(h.hdop - 2.1f) < 1e-6f);

    // Date fuori dal 2024-2079 (ricevitore appena acceso): l'ora non vale.
    NmeaParser s;
    Fix k;
    alimenta(s, k, frase("GPRMC,083559.00,A,4717.11437,N,00833.91522,E,0.004,77.52,091202,,,A,V"), 1000);  // 2002
    CHECK(k.valid && !k.timeValid);
    alimenta(s, k, frase("GNRMC,000003.000,V,,,,,,,060180,,,N,V"), 1100);  // 1980
    CHECK(!k.timeValid && !k.valid);
    // L'esempio di RMC della documentazione u-blox ha il checksum sbagliato (*57, il giusto e' 2D): scartato.
    NmeaParser d;
    Fix j;
    alimenta(d, j, DOC_UBLOX[6], 1000);
    CHECK(!j.valid && j.frasiKo == 1 && j.frasiOk == 0);
}

// feed() dice true esattamente per RMC, GGA e GSV valide, come oggi: la ricerca della velocita' se ne fida.
TEST(nmea_feed_vero) {
    NmeaParser p;
    Fix f;
    int veri = 0;
    for (char c : flusso(ubloxFix())) veri += p.feed(c, f, 1000);
    CHECK(veri == 8);  // RMC, GGA, 3 GPGSV, 2 GAGSV, GQGSV (non VTG, GSA, GLL)
    veri = 0;
    for (char c : flusso(ubloxAvvio()) + "$GNGGA,rotta*00\r\n" + frase("GPGSV,3,2,09,15,,,44,1")) veri += p.feed(c, f, 1000);
    CHECK(veri == 1);  // la GSV (anche fuori serie e' valida come frase)
    veri = 0;
    for (char c : flusso(l76kFix()) + flusso(at6558Fix())) veri += p.feed(c, f, 1000);
    CHECK(veri == 6 + 6);
}

TEST(nmea_ublox_cielo) {
    NmeaParser p;
    Fix f;
    uint64_t t = 1000;
    alimenta(p, f, flusso(ubloxAvvio()) + flusso(ubloxFreddo()), t);
    CIELO(f, 0, 0, 0);
    alimenta(p, f, flusso(ubloxRicerca()), t += 1000);
    CIELO(f, 9, 4, 24);
    alimenta(p, f, flusso(ubloxScrivania()), t += 1000);
    CIELO(f, 18, 2, 14);  // come @GPS? alla scrivania: 18 "in vista", solo 2 sentiti
    CHECK(f.sats == 0 && !f.valid && !f.timeValid);
    alimenta(p, f, flusso(ubloxOraSenzaFix()), t += 1000);
    CHECK(f.timeValid && !f.valid);
    alimenta(p, f, flusso(ubloxFix()), t += 1000);
    CIELO(f, 16, 11, 44);
    CHECK(f.valid && f.sats == 8 && f.unixTime == 1794737745u);
    CHECK(f.frasiAtMs == t && f.frasiKo == 0);
    alimenta(p, f, flusso(ubloxGloBds()), t += 100);
    CIELO(f, 24, 15, 44);
}

// Due segnali della stessa costellazione (u-blox a doppia banda): per costellazione il massimo, non la somma.
TEST(nmea_due_segnali) {
    NmeaParser p;
    Fix f;
    std::string doc;
    for (int i = 0; i < 5; i++) doc += DOC_UBLOX[i];
    alimenta(p, f, doc, 1000);
    CIELO(f, 9, 9, 50);  // l'unione vera e' 10 (il 32 solo su L2): limite noto, solo per i doppia banda
}

TEST(nmea_serie_incomplete) {
    NmeaParser p;
    Fix f;
    const auto fix = ubloxFix();  // [5..7] GPGSV 3 messaggi, [8..9] GAGSV 2 messaggi
    alimenta(p, f, frase(fix[5]) + frase(fix[7]), 1000);  // manca il 2
    CIELO(f, 0, 0, 0);
    alimenta(p, f, frase(fix[5]) + frase(fix[6]) + frase(fix[7]), 2000);
    CIELO(f, 10, 7, 44);
    alimenta(p, f, frase(fix[5]) + frase(fix[7]), 3000);  // di nuovo senza il 2: resta la serie buona
    CIELO(f, 10, 7, 44);
    alimenta(p, f, frase(fix[6]) + frase(fix[5]) + frase(fix[6]) + frase(fix[7]), 3500);  // 2 isolato, poi intera
    CIELO(f, 10, 7, 44);
    alimenta(p, f, frase("GPGSV,2,1,05,05,67,301,38,13,55,089,41,15,33,150,35,18,08,270,,1") + frase(fix[7]), 3600);
    CIELO(f, 10, 7, 44);  // numMsg cambia a meta' (2 poi 3): scartata
    alimenta(p, f, frase("GPGSV,1,2,05,05,67,301,38,1") + frase("GPGSV,0,0,05,1") + frase("GPGSV,x,y,z,1"), 3700);
    CIELO(f, 10, 7, 44);  // numeri impossibili: ignorate
    alimenta(p, f, frase("GPGSV,1,1,00,1"), 3800);  // GPS senza satelliti
    CIELO(f, 0, 0, 0);
}

// Il GPS tace: posizione e satelliti scadono dopo 5 s; restano l'ultima posizione, atMs e l'ora.
TEST(nmea_scadenze) {
    NmeaParser p;
    Fix f;
    const uint64_t t0 = 10000;
    alimenta(p, f, flusso(ubloxFix()), t0);
    CHECK(f.valid);
    p.scadenze(f, t0 + 5000);
    CHECK(f.valid && f.conSegnale == 11 && f.sats == 8);
    p.scadenze(f, t0 + 5001);
    CHECK(!f.valid && f.conSegnale == 0 && f.inVista == 0 && f.snrMax == 0 && f.sats == 0 && f.hdop == 99);
    CHECK(f.lat != 0 && f.atMs == t0 && f.timeValid);
    alimenta(p, f, flusso(ubloxFix()), t0 + 6000);
    CHECK(f.valid && f.conSegnale == 11);

    // A 1 Hz con scadenze() ogni 100 ms per 30 s i conti non sfarfallano.
    NmeaParser q;
    Fix g;
    int sbagli = 0;
    for (uint64_t t = 1000; t < 31000; t += 100) {
        if (t % 1000 == 0)
            for (char c : flusso(ubloxFix())) q.feed(c, g, t);
        q.scadenze(g, t);
        if (!g.valid || g.conSegnale != 11 || g.inVista != 16 || g.snrMax != 44 || g.sats != 8) sbagli++;
    }
    CONTROLLA(sbagli == 0, "%d istanti con valori diversi", sbagli);

    // Solo la RMC 'A' tiene valida la posizione: con le sole GSV scade anche se il GPS parla.
    NmeaParser r;
    Fix h;
    alimenta(r, h, flusso(ubloxFix()), 1000);
    for (uint64_t t = 2000; t <= 7000; t += 1000) alimenta(r, h, flusso(ubloxRicerca()), t);
    CHECK(!h.valid && h.inVista == 9 && h.conSegnale == 4);
}

TEST(nmea_costellazione_sparita) {
    NmeaParser p;
    Fix f;
    uint64_t t = 1000;
    alimenta(p, f, flusso(ubloxFix()) + flusso(ubloxGloBds()), t);
    CIELO(f, 24, 15, 44);
    for (int i = 0; i < 6; i++) alimenta(p, f, flusso(ubloxFix()), t += 1000);  // solo GP, GA, GQ
    CIELO(f, 16, 11, 44);
    p.azzera();
    f = Fix();
    alimenta(p, f, frase("GPGSV,1,1,00,1"), t += 1000);
    CIELO(f, 0, 0, 0);
}

// Posizione solo dalla RMC (come la PCB): la GGA da' usati, hdop e quota; RMC 'A' senza coordinate no.
TEST(nmea_rmc_e_gga) {
    NmeaParser p;
    Fix f;
    alimenta(p, f, frase("GNGGA,101545.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,"), 1000);
    CHECK(!f.valid && f.sats == 8 && std::fabs(f.altM - 499.6f) < 1e-4f && f.ggaAtMs == 1000);
    alimenta(p, f, frase("GNRMC,101548.00,A,,,,,0.004,77.52,151126,,,A,V"), 2000);
    CHECK(!f.valid && f.timeValid && f.rmcAtMs == 2000);  // niente posizione 0,0, l'ora si'
    CHECK(f.lat == 0 && f.lon == 0);
    alimenta(p, f, frase(ubloxFix()[0]), 3000);
    CHECK(f.valid && f.atMs == 3000);
    alimenta(p, f, frase("GNRMC,101549.00,V,,,,,,,151126,,,N,V"), 3500);
    CHECK(!f.valid && f.atMs == 3000 && f.lat != 0);  // la RMC 'V' la toglie subito, l'ultima resta
}

TEST(nmea_l76k_at6558) {
    NmeaParser p;
    Fix f;
    alimenta(p, f, flusso(l76kFreddo()), 1000);
    CIELO(f, 0, 0, 0);
    alimenta(p, f, flusso(l76kRicerca()), 2000);
    CIELO(f, 11, 5, 30);
    alimenta(p, f, flusso(l76kFix()), 3000);
    CIELO(f, 11, 7, 38);
    CHECK(f.valid && f.sats == 6);
    NmeaParser q;
    Fix g;
    alimenta(q, g, flusso(at6558Fix()), 1000);
    CIELO(g, 13, 7, 38);  // NMEA 3.0, senza signalId
    CHECK(g.valid && g.sats == 5);
    NmeaParser r;
    Fix h;
    alimenta(r, h, frase("GPGSV,1,1,02,30,15,120,,31,05,010"), 1000);  // ultimo C/N0 omesso del tutto
    CIELO(h, 2, 0, 0);
    alimenta(r, h, frase("GNGSV,1,1,03,05,67,301,38,13,55,089,41,70,40,100,30,1"), 1100);  // talker GN
    CIELO(h, 5, 3, 41);
    alimenta(r, h, frase("GLGSV,1,1,02,,,,27,66,60,200,,1"), 1200);  // GLONASS senza numero ("R?")
    CIELO(h, 7, 4, 41);
}

TEST(nmea_diagnosi) {
    NmeaParser p;
    Fix f;
    alimenta(p, f, frase("GNTXT,01,01,02,ANTSTATUS=OK"), 500);
    CHECK(f.frasiAtMs == 500 && f.frasiOk == 1 && !f.rmcAtMs && !f.ggaAtMs && !f.gsvAtMs);
    alimenta(p, f, frase(ubloxFix()[0], true), 600);  // checksum minuscolo: scartata (come sulla PCB)
    std::string rotta = frase(ubloxFix()[2]);
    rotta[25] ^= 1;
    alimenta(p, f, rotta, 700);
    alimenta(p, f, "$GNGGA,1015" + frase(ubloxFix()[2]), 800);  // interrotta da un altro '$'
    alimenta(p, f, "$" + std::string(120, 'A') + "\r\n", 900);   // troppo lunga
    alimenta(p, f, "$GNGGA,senza,asterisco\r\n", 1000);
    CONTROLLA(f.frasiKo == 5 && f.frasiOk == 2 && f.sats == 8, "ko=%u ok=%u", unsigned(f.frasiKo), unsigned(f.frasiOk));
    CHECK(f.ggaAtMs == 800 && f.frasiAtMs == 800);

    // Le tre situazioni che prima davano la stessa risposta @GPS? ora si distinguono.
    NmeaParser pa, pb, pc;
    Fix fa, fb, fc;
    alimenta(pa, fa, flusso(ubloxScrivania()), 1000);
    std::vector<std::string> soloGsv;
    std::string rotte;
    for (const auto& s : ubloxScrivania()) {
        std::string x = frase(s);
        if (s.find("GSV") != std::string::npos) soloGsv.push_back(s);
        else x[8] ^= 1;
        rotte += x;
    }
    alimenta(pb, fb, flusso(soloGsv), 1000);
    alimenta(pc, fc, rotte, 1000);
    CHECK(fa.rmcAtMs && fa.ggaAtMs && fa.gsvAtMs && !fa.frasiKo);  // arrivano, senza posizione
    CHECK(!fb.rmcAtMs && !fb.ggaAtMs && fb.gsvAtMs && !fb.frasiKo);  // arrivano solo le GSV
    CHECK(!fc.rmcAtMs && !fc.ggaAtMs && fc.frasiKo == 2);            // RMC e GGA rovinate
}

// Piu' serie dei 12 posti: si ricicla la piu' vecchia, i conti restano sensati.
TEST(nmea_molte_serie) {
    NmeaParser p;
    Fix f;
    const char* talker[] = {"GP", "GL", "GA", "GB", "GQ", "GI", "BD", "GN"};
    uint64_t t = 1000;
    for (int i = 0; i < 8; i++)
        for (const char* sig : {"1", "7"}) {
            alimenta(p, f, frase(std::string(talker[i]) + "GSV,1,1,01,05,40,100,30," + sig), t);
            t += 10;
        }
    CONTROLLA(f.conSegnale <= 8 && f.conSegnale >= 6 && f.snrMax == 30, "conSegnale=%u", unsigned(f.conSegnale));
}

TEST(nmea_azzera) {
    NmeaParser p;
    Fix f;
    alimenta(p, f, flusso(ubloxFix()), 1000);
    CIELO(f, 16, 11, 44);
    for (char c : std::string("$GNRMC,1015")) p.feed(c, f, 1100);  // riga a meta'
    p.azzera();
    p.scadenze(f, 1200);
    CIELO(f, 0, 0, 0);  // satelliti dimenticati subito (standby, freddo)
    const uint32_t ko = f.frasiKo;
    alimenta(p, f, frase(ubloxFix()[0]), 1300);
    CHECK(f.frasiKo == ko && f.valid && f.atMs == 1300);  // la riga a meta' non conta come interrotta
}

// Dati a caso: niente crash, niente valori fuori scala.
TEST(nmea_dati_a_caso) {
    std::mt19937 rng(12345);
    const std::string base = flusso(ubloxFix()) + flusso(l76kFix()) + flusso(ubloxGloBds());
    NmeaParser p;
    Fix f;
    uint64_t t = 0;
    int validi = 0, incoerenti = 0;
    for (int giro = 0; giro < 20000; giro++) {
        std::string s = base;
        const int mut = int(rng() % 6);
        for (int k = 0; k < mut; k++) s[rng() % s.size()] = char(rng() % 256);
        if (rng() % 4 == 0) s.erase(rng() % s.size(), rng() % 40);
        for (char c : s) p.feed(c, f, t);
        p.scadenze(f, t);
        t += 1000;
        if (f.valid) validi++;
        if (f.conSegnale > f.inVista || f.snrMax > 99) incoerenti++;
    }
    CONTROLLA(validi > 1000, "validi=%d", validi);
    CONTROLLA(incoerenti == 0, "incoerenti=%d", incoerenti);
    for (int giro = 0; giro < 2000; giro++) {
        std::string s(300, 0);
        for (char& c : s) c = char(rng() % 256);
        if (giro % 3 == 0) s[rng() % 300] = '$';
        for (char c : s) p.feed(c, f, t);
        p.scadenze(f, t);
        t += 100;
    }
    CHECK(f.conSegnale <= f.inVista && f.snrMax <= 99);
}
