// Mappe dal computer a flusso (telefono/main/app/flusso_mappe.h): il lettore dei byte della pagina del
// flasher, coi byte spezzati a caso come arrivano dalla USB, i comandi sbagliati che rompono il flusso e,
// con PN_FLUSSO=<file>, un flusso vero registrato dalla prova della pagina (provaMappe).
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "app/flusso_mappe.h"
#include "testing.h"

namespace {

using app::flusso::Domanda;
using app::flusso::Lettore;
using app::flusso::Tile;

// Chi usa il lettore: tiene tile, domande e fine come arrivano.
struct Raccolta : app::flusso::Azioni {
    struct Arrivata {
        Tile t;
        std::vector<uint8_t> dati;
    };
    std::vector<Arrivata> arrivate;
    std::vector<Domanda> domande;
    int fini = 0;
    bool senzaPosto = false;
    std::vector<std::vector<uint8_t>> posti;  // come la coda in PSRAM: un posto per tile
    std::string log;                          // in ordine di arrivo, come lo scrive il telefono finto della pagina
    uint8_t* posto(const Tile& t) override {
        if (senzaPosto) return nullptr;
        posti.emplace_back(t.byte, 0xEE);
        return posti.back().data();
    }
    void tile(const Tile& t, uint8_t* dati) override {
        CHECK(!posti.empty() && dati == posti.back().data());
        arrivate.push_back({t, std::vector<uint8_t>(dati, dati + t.byte)});
        const bool buono = app::flusso::crc32(dati, t.byte) == t.crc;
        log += "T " + std::to_string(t.id) + " " + std::to_string(t.z) + "/" + std::to_string(t.x) + "/" +
               std::to_string(t.y) + " " + std::to_string(t.byte) + (buono ? "" : " rovinata") + "\n";
    }
    void domanda(const Domanda& d) override {
        domande.push_back(d);
        log += "C " + std::to_string(d.id) + " " + std::to_string(d.z) + " " + std::to_string(d.x0) + " " +
               std::to_string(d.y0) + " " + std::to_string(d.x1) + " " + std::to_string(d.y1) + "\n";
    }
    void fine() override { fini++; }
};

std::vector<uint8_t> byte(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

void aggiungi(std::vector<uint8_t>& v, const std::string& s) { v.insert(v.end(), s.begin(), s.end()); }

std::string esa8(uint32_t v) {
    char b[16];
    snprintf(b, sizeof(b), "%08x", unsigned(v));
    return b;
}

// Una tile finta: byte qualunque, anche "\n" e "@FINE\n" in mezzo (non devono contare come righe).
std::vector<uint8_t> png(size_t n, uint32_t seme) {
    std::mt19937 g(seme);
    std::vector<uint8_t> v(n);
    for (auto& b : v) b = uint8_t(g());
    if (n > 20) std::memcpy(v.data() + n / 2, "\n@FINE\nC 1 0 0 0 0 0\n", 20);
    return v;
}

void tileNelFlusso(std::vector<uint8_t>& f, uint32_t id, const std::string& nome, const std::vector<uint8_t>& dati) {
    aggiungi(f, "T " + std::to_string(id) + " " + nome + " " + std::to_string(dati.size()) + " " +
                    esa8(app::flusso::crc32(dati.data(), dati.size())) + "\n");
    f.insert(f.end(), dati.begin(), dati.end());
}

// Dà il flusso al lettore a pezzi lunghi da 1 a max byte; ritorna quanti ne ha usati in tutto.
size_t aPezzi(Lettore& l, const std::vector<uint8_t>& f, std::mt19937& g, size_t max) {
    size_t i = 0, usati = 0;
    while (i < f.size()) {
        const size_t n = std::min<size_t>(f.size() - i, 1 + g() % max);
        const size_t u = l.consuma(f.data() + i, n);
        usati += u;
        if (u < n) break;  // finito o rotto: il resto non e' suo
        i += n;
    }
    return usati;
}

// Il motivo per cui il flusso fatto di una riga sola si rompe ("" se non si rompe).
std::string rotto(const std::string& riga) {
    Raccolta r;
    Lettore l(r);
    const auto f = byte(riga + "\n");
    l.consuma(f.data(), f.size());
    return l.stato() == Lettore::Stato::ROTTO ? l.motivo() : "";
}

}  // namespace

TEST(flusso_crc_e_percorsi) {
    const auto s = byte("123456789");
    CHECK_EQ(app::flusso::crc32(s.data(), s.size()), 0xCBF43926u);
    CHECK_EQ(app::flusso::crc32(nullptr, 0), 0u);
    int z;
    long x, y;
    CHECK(app::flusso::percorso("14/8707/6000.png", z, x, y) && z == 14 && x == 8707 && y == 6000);
    CHECK(app::flusso::percorso("0/0/0.png", z, x, y));
    CHECK(app::flusso::percorso("20/1048575/1048575.png", z, x, y));
    for (const char* no : {"14/08707/6000.png", "14/8707/6000.PNG", "-1/0/0.png", "21/0/0.png", "14/16384/0.png",
                           "1/2/0.png", "14/8707/6000.pngx", "14/../6000.png", "14/8707/6000", " 14/8707/6000.png",
                           "14/+8707/6000.png", ""})
        CHECK(!app::flusso::percorso(no, z, x, y));
}

TEST(flusso_tile_domande_e_fine_a_pezzi) {
    std::vector<uint8_t> f;
    const auto a = png(37000, 1), b = png(1, 2), c = png(4096, 3);
    tileNelFlusso(f, 1, "14/8707/6000.png", a);
    aggiungi(f, "C 2 14 8704 6000 8707 6003\n\n\r\n");
    tileNelFlusso(f, 3, "14/8707/6001.png", b);
    tileNelFlusso(f, 4294967295u, "0/0/0.png", c);
    aggiungi(f, "C 5 16 0 65535 0 65535\r\n@FINE\n");
    const size_t fineFlusso = f.size();
    aggiungi(f, "@PIPPA?\n");  // dopo @FINE: non e' del flusso
    for (int giro = 0; giro < 200; giro++) {
        std::mt19937 g(giro);
        Raccolta r;
        Lettore l(r);
        const size_t usati = aPezzi(l, f, g, giro < 100 ? 7 : 5000);
        CHECK(l.stato() == Lettore::Stato::FINE);
        CHECK_EQ(usati, fineFlusso);
        CHECK_EQ(r.fini, 1);
        CHECK_EQ(r.arrivate.size(), size_t(3));
        if (r.arrivate.size() != 3) continue;
        CHECK(r.arrivate[0].t.id == 1 && r.arrivate[0].t.z == 14 && r.arrivate[0].t.x == 8707 && r.arrivate[0].t.y == 6000);
        CHECK(r.arrivate[0].dati == a && r.arrivate[1].dati == b && r.arrivate[2].dati == c);
        CHECK_EQ(r.arrivate[0].t.crc, app::flusso::crc32(a.data(), a.size()));
        CHECK_EQ(r.arrivate[2].t.id, 4294967295u);
        CHECK_EQ(r.domande.size(), size_t(2));
        if (r.domande.size() != 2) continue;
        CHECK(r.domande[0].id == 2 && r.domande[0].z == 14 && r.domande[0].x0 == 8704 && r.domande[0].y0 == 6000 &&
              r.domande[0].x1 == 8707 && r.domande[0].y1 == 6003);
        CHECK_EQ(r.domande[0].quante(), 16);
        CHECK(r.domande[1].z == 16 && r.domande[1].x1 == 0 && r.domande[1].y0 == 65535 && r.domande[1].quante() == 1);
    }
}

TEST(flusso_comandi_sbagliati) {
    CHECK_EQ(rotto(""), std::string());
    CHECK_EQ(rotto("ciao"), std::string("comando sconosciuto"));
    CHECK_EQ(rotto("@FILE 14/1/1.png 10 00000000"), std::string("comando sconosciuto"));
    CHECK_EQ(rotto("TX 1 14/1/1.png 10 00000000"), std::string("comando sconosciuto"));
    CHECK_EQ(rotto("T 1 14/1/1.png 10"), std::string("riga T sbagliata"));
    CHECK_EQ(rotto("T 01 14/1/1.png 10 00000000"), std::string("riga T sbagliata"));
    CHECK_EQ(rotto("T 4294967296 14/1/1.png 10 00000000"), std::string("riga T sbagliata"));
    CHECK_EQ(rotto("T  1 14/1/1.png 10 00000000"), std::string("riga T sbagliata"));
    CHECK_EQ(rotto("T 1 14/01/1.png 10 00000000"), std::string("percorso sbagliato"));
    CHECK_EQ(rotto("T 1 14/1/1.png 0 00000000"), std::string("grandezza sbagliata"));
    CHECK_EQ(rotto("T 1 14/1/1.png 1048577 00000000"), std::string("grandezza sbagliata"));
    CHECK_EQ(rotto("T 1 14/1/1.png -5 00000000"), std::string("grandezza sbagliata"));
    CHECK_EQ(rotto("T 1 14/1/1.png 10 0000000g"), std::string("CRC sbagliato nella riga"));
    CHECK_EQ(rotto("T 1 14/1/1.png 10 000000000"), std::string("CRC sbagliato nella riga"));
    CHECK_EQ(rotto("C 1 14 0 0 7 8"), std::string("rettangolo sbagliato"));
    CHECK_EQ(rotto("C 1 14 5 0 4 0"), std::string("rettangolo sbagliato"));
    CHECK_EQ(rotto("C 1 14 0 0 16384 0"), std::string("riga C sbagliata"));
    CHECK_EQ(rotto("C 1 21 0 0 0 0"), std::string("riga C sbagliata"));
    CHECK_EQ(rotto("C 1 14 0 0 0"), std::string("riga C sbagliata"));
    CHECK_EQ(rotto(std::string(200, 'x')), std::string("riga troppo lunga"));
    // Il lettore rotto non legge piu' niente, nemmeno una riga giusta.
    Raccolta r;
    Lettore l(r);
    const auto f = byte("bah\nC 1 14 0 0 0 0\n@FINE\n");
    CHECK_EQ(l.consuma(f.data(), f.size()), size_t(4));
    CHECK(r.domande.empty() && !r.fini);
    CHECK_EQ(l.consuma(f.data() + 4, f.size() - 4), size_t(0));
}

TEST(flusso_rettangolo_di_64) {
    // 64 tile vanno (8x8 e 1x64), 65 no.
    Raccolta r;
    Lettore l(r);
    const auto f = byte("C 7 14 0 0 7 7\nC 8 14 3 100 3 163\n");
    CHECK_EQ(l.consuma(f.data(), f.size()), f.size());
    CHECK(l.stato() == Lettore::Stato::RIGA);
    CHECK(r.domande.size() == 2 && r.domande[0].quante() == 64 && r.domande[1].quante() == 64);
    CHECK_EQ(rotto("C 1 14 3 100 3 164"), std::string("rettangolo sbagliato"));
}

TEST(flusso_tile_a_meta) {
    // Il flusso finisce in mezzo ai byte di una tile: il posto torna a chi l'aveva dato, una volta sola.
    Raccolta r;
    Lettore l(r);
    std::vector<uint8_t> f;
    tileNelFlusso(f, 1, "14/1/1.png", png(1000, 4));
    CHECK_EQ(l.consuma(f.data(), 500), size_t(500));
    CHECK(l.stato() == Lettore::Stato::DATI);
    CHECK(r.arrivate.empty() && r.posti.size() == 1);
    CHECK(l.lasciaPosto() == r.posti[0].data());
    CHECK(l.lasciaPosto() == nullptr);
    Lettore vuoto(r);
    CHECK(vuoto.lasciaPosto() == nullptr);
}

TEST(flusso_senza_memoria) {
    Raccolta r;
    r.senzaPosto = true;
    Lettore l(r);
    std::vector<uint8_t> f;
    tileNelFlusso(f, 1, "14/1/1.png", png(100, 9));
    CHECK(l.consuma(f.data(), f.size()) < f.size());
    CHECK(l.stato() == Lettore::Stato::ROTTO);
    CHECK_EQ(std::string(l.motivo()), std::string("memoria finita"));
    CHECK(r.arrivate.empty());
}

// Un flusso vero, registrato dalla pagina del flasher con la sua prova (provaMappe col telefono finto a
// flusso): tutto quello che la pagina ha scritto sulla porta dopo "@FLUSSO", sessione per sessione. Ogni
// sessione deve finire con @FINE e ogni tile avere il suo CRC; lo stesso file ha accanto (.atteso) l'elenco
// delle tile e delle domande come le ha viste il telefono finto della pagina.
TEST(flusso_registrato_dalla_pagina) {
    const char* nome = std::getenv("PN_FLUSSO");
    if (!nome) {
        std::printf("(senza PN_FLUSSO) ");
        return;
    }
    std::ifstream in(nome, std::ios::binary);
    std::vector<uint8_t> f((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(!f.empty());
    std::ifstream at(std::string(nome) + ".atteso");
    std::string atteso((std::istreambuf_iterator<char>(at)), std::istreambuf_iterator<char>());
    CHECK(!atteso.empty());
    std::string visto;
    std::mt19937 g(1);
    size_t i = 0;
    int sessioni = 0;
    while (i < f.size()) {
        Raccolta r;
        Lettore l(r);
        const size_t u = aPezzi(l, std::vector<uint8_t>(f.begin() + long(i), f.end()), g, 3000);
        if (l.stato() != Lettore::Stato::FINE) {
            std::printf("\n    sessione %d: %s\n", sessioni + 1, l.stato() == Lettore::Stato::ROTTO ? l.motivo() : "senza @FINE");
            CHECK(false);
            break;
        }
        visto += r.log + "@FINE\n";
        sessioni++;
        i += u;
    }
    std::printf("(%d sessioni, %zu byte) ", sessioni, f.size());
    CHECK(visto == atteso);
    if (visto != atteso) {
        std::ofstream o(std::string(nome) + ".visto");
        o << visto;
        std::printf("\n    diverso dall'atteso: vedi %s.visto\n", nome);
    }
}
