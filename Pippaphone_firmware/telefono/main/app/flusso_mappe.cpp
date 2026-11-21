#include "app/flusso_mappe.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace app::flusso {

namespace {

// La tabella del CRC fatta dal compilatore: nessuna inizializzazione a meta' fra due compiti.
struct TabellaCrc {
    uint32_t v[256];
    constexpr TabellaCrc() : v() {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            v[i] = c;
        }
    }
};
constexpr TabellaCrc TAB;

// Le parole della riga separate da uno spazio solo (la riga viene modificata); max + 1 se sono di piu'.
int parole(char* s, char** out, int max) {
    int n = 0;
    while (*s) {
        if (n == max) return max + 1;
        out[n++] = s;
        char* sp = std::strchr(s, ' ');
        if (!sp) break;
        *sp = 0;
        s = sp + 1;
    }
    return n;
}

// Un numero decimale senza segno, senza zeri davanti (tranne "0"), fino a max.
bool numero(const char* s, unsigned long max, unsigned long& v) {
    if (!*s || (s[0] == '0' && s[1]) || *s < '0' || *s > '9') return false;
    char* fine = nullptr;
    errno = 0;
    v = std::strtoul(s, &fine, 10);
    return !*fine && errno != ERANGE && v <= max;
}

// Il CRC: esattamente 8 cifre esadecimali (anche maiuscole).
bool esa8(const char* s, uint32_t& v) {
    if (std::strlen(s) != 8) return false;
    for (const char* p = s; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) return false;
    v = uint32_t(std::strtoul(s, nullptr, 16));
    return true;
}

}  // namespace

uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = TAB.v[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

bool percorso(const char* rel, int& z, long& x, long& y) {
    char g = 0;
    if (std::sscanf(rel, "%d/%ld/%ld.pn%c", &z, &x, &y, &g) != 4 || g != 'g') return false;
    char b[48];
    snprintf(b, sizeof(b), "%d/%ld/%ld.png", z, x, y);
    if (std::strcmp(b, rel) != 0) return false;
    return z >= 0 && z <= 20 && x >= 0 && y >= 0 && x < (1L << z) && y < (1L << z);
}

size_t Lettore::consuma(const uint8_t* p, size_t n) {
    size_t i = 0;
    while (i < n && (stato_ == Stato::RIGA || stato_ == Stato::DATI)) {
        if (stato_ == Stato::DATI) {
            const size_t k = std::min<size_t>(n - i, tile_.byte - preso_);
            std::memcpy(dati_ + preso_, p + i, k);
            preso_ += uint32_t(k);
            i += k;
            if (preso_ == tile_.byte) {
                uint8_t* d = dati_;
                dati_ = nullptr;
                stato_ = Stato::RIGA;
                a_.tile(tile_, d);
            }
            continue;
        }
        const char c = char(p[i++]);
        if (c == '\n') {
            riga_[lung_] = 0;
            lung_ = 0;
            esegui();
        } else if (lung_ + 1 < MAX_RIGA) {
            riga_[lung_++] = c;
        } else {
            rompi("riga troppo lunga");
        }
    }
    return i;
}

void Lettore::rompi(const char* motivo) {
    stato_ = Stato::ROTTO;
    motivo_ = motivo;
}

void Lettore::esegui() {
    size_t l = std::strlen(riga_);
    if (l && riga_[l - 1] == '\r') riga_[--l] = 0;
    if (!l) return;  // riga vuota: la pagina tiene viva la sessione
    if (!std::strcmp(riga_, "@FINE")) {
        stato_ = Stato::FINE;
        a_.fine();
        return;
    }
    char* w[8];
    const int n = parole(riga_, w, 7);
    unsigned long id = 0;
    if (riga_[0] == 'T' && !riga_[1]) {
        unsigned long byte = 0;
        if (n != 5 || !numero(w[1], 0xFFFFFFFFul, id)) return rompi("riga T sbagliata");
        Tile t = {};
        t.id = uint32_t(id);
        if (!percorso(w[2], t.z, t.x, t.y)) return rompi("percorso sbagliato");
        if (!numero(w[3], MAX_FILE, byte) || !byte) return rompi("grandezza sbagliata");
        if (!esa8(w[4], t.crc)) return rompi("CRC sbagliato nella riga");
        t.byte = uint32_t(byte);
        tile_ = t;
        preso_ = 0;
        dati_ = a_.posto(tile_);
        if (!dati_) return rompi("memoria finita");
        stato_ = Stato::DATI;
        return;
    }
    if (riga_[0] == 'C' && !riga_[1]) {
        unsigned long v[5];
        if (n != 7 || !numero(w[1], 0xFFFFFFFFul, id) || !numero(w[2], 20, v[0])) return rompi("riga C sbagliata");
        const unsigned long lato = 1ul << v[0];
        for (int k = 1; k < 5; k++)
            if (!numero(w[2 + k], lato - 1, v[k])) return rompi("riga C sbagliata");
        Domanda d = {uint32_t(id), int(v[0]), long(v[1]), long(v[2]), long(v[3]), long(v[4])};
        if (d.x1 < d.x0 || d.y1 < d.y0 || (d.x1 - d.x0 + 1) * (d.y1 - d.y0 + 1) > MAX_DOMANDA)
            return rompi("rettangolo sbagliato");
        a_.domanda(d);
        return;
    }
    rompi("comando sconosciuto");
}

}  // namespace app::flusso
