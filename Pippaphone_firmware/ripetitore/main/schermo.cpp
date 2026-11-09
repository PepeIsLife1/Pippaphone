#include "schermo.h"

#include <cstdio>
#include <cstring>

namespace schermo {

namespace {

// Carattere 5x7, ASCII 32-126: 5 colonne per carattere, bit 0 = riga in alto.
const uint8_t FONT[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00},  // ' '
    {0x00, 0x00, 0x5F, 0x00, 0x00},  // !
    {0x00, 0x07, 0x00, 0x07, 0x00},  // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14},  // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12},  // $
    {0x23, 0x13, 0x08, 0x64, 0x62},  // %
    {0x36, 0x49, 0x55, 0x22, 0x50},  // &
    {0x00, 0x05, 0x03, 0x00, 0x00},  // '
    {0x00, 0x1C, 0x22, 0x41, 0x00},  // (
    {0x00, 0x41, 0x22, 0x1C, 0x00},  // )
    {0x14, 0x08, 0x3E, 0x08, 0x14},  // *
    {0x08, 0x08, 0x3E, 0x08, 0x08},  // +
    {0x00, 0x50, 0x30, 0x00, 0x00},  // ,
    {0x08, 0x08, 0x08, 0x08, 0x08},  // -
    {0x00, 0x60, 0x60, 0x00, 0x00},  // .
    {0x20, 0x10, 0x08, 0x04, 0x02},  // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0x00, 0x36, 0x36, 0x00, 0x00},  // :
    {0x00, 0x56, 0x36, 0x00, 0x00},  // ;
    {0x08, 0x14, 0x22, 0x41, 0x00},  // <
    {0x14, 0x14, 0x14, 0x14, 0x14},  // =
    {0x00, 0x41, 0x22, 0x14, 0x08},  // >
    {0x02, 0x01, 0x51, 0x09, 0x06},  // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E},  // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x07, 0x08, 0x70, 0x08, 0x07},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
    {0x00, 0x7F, 0x41, 0x41, 0x00},  // [
    {0x02, 0x04, 0x08, 0x10, 0x20},  // backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00},  // ]
    {0x04, 0x02, 0x01, 0x02, 0x04},  // ^
    {0x40, 0x40, 0x40, 0x40, 0x40},  // _
    {0x00, 0x01, 0x02, 0x04, 0x00},  // `
    {0x20, 0x54, 0x54, 0x54, 0x78},  // a
    {0x7F, 0x48, 0x44, 0x44, 0x38},  // b
    {0x38, 0x44, 0x44, 0x44, 0x20},  // c
    {0x38, 0x44, 0x44, 0x48, 0x7F},  // d
    {0x38, 0x54, 0x54, 0x54, 0x18},  // e
    {0x08, 0x7E, 0x09, 0x01, 0x02},  // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E},  // g
    {0x7F, 0x08, 0x04, 0x04, 0x78},  // h
    {0x00, 0x44, 0x7D, 0x40, 0x00},  // i
    {0x20, 0x40, 0x44, 0x3D, 0x00},  // j
    {0x7F, 0x10, 0x28, 0x44, 0x00},  // k
    {0x00, 0x41, 0x7F, 0x40, 0x00},  // l
    {0x7C, 0x04, 0x18, 0x04, 0x78},  // m
    {0x7C, 0x08, 0x04, 0x04, 0x78},  // n
    {0x38, 0x44, 0x44, 0x44, 0x38},  // o
    {0x7C, 0x14, 0x14, 0x14, 0x08},  // p
    {0x08, 0x14, 0x14, 0x18, 0x7C},  // q
    {0x7C, 0x08, 0x04, 0x04, 0x08},  // r
    {0x48, 0x54, 0x54, 0x54, 0x20},  // s
    {0x04, 0x3F, 0x44, 0x40, 0x20},  // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C},  // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C},  // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C},  // w
    {0x44, 0x28, 0x10, 0x28, 0x44},  // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C},  // y
    {0x44, 0x64, 0x54, 0x4C, 0x44},  // z
    {0x00, 0x08, 0x36, 0x41, 0x00},  // {
    {0x00, 0x00, 0x7F, 0x00, 0x00},  // |
    {0x00, 0x41, 0x36, 0x08, 0x00},  // }
    {0x08, 0x04, 0x08, 0x10, 0x08},  // ~
};

int larghezza(const char* s, int scale) {
    const int n = int(strlen(s));
    return n ? n * 6 * scale - scale : 0;
}

// Numero con la virgola decimale, all'italiana.
void decimale(char* o, size_t n, float v, int cifre, const char* unita) {
    snprintf(o, n, "%.*f%s", cifre, double(v), unita);
    for (char* p = o; *p; p++)
        if (*p == '.') *p = ',';
}

// Durata breve: 45s, 12m, 3h, 2g.
void breve(char* o, size_t n, uint32_t s) {
    if (s < 60) snprintf(o, n, "%lus", (unsigned long)s);
    else if (s < 3600) snprintf(o, n, "%lum", (unsigned long)(s / 60));
    else if (s < 86400) snprintf(o, n, "%luh", (unsigned long)(s / 3600));
    else snprintf(o, n, "%lug", (unsigned long)(s / 86400));
}

// Durata lunga: 45s, 12m 05s, 4h 12m, 3g 04h.
void lungo(char* o, size_t n, uint32_t s) {
    if (s < 60) snprintf(o, n, "%lus", (unsigned long)s);
    else if (s < 3600) snprintf(o, n, "%lum %02lus", (unsigned long)(s / 60), (unsigned long)(s % 60));
    else if (s < 86400) snprintf(o, n, "%luh %02lum", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
    else snprintf(o, n, "%lug %02luh", (unsigned long)(s / 86400), (unsigned long)(s / 3600 % 24));
}

void nome(char* o, size_t n, uint16_t a) {
    if (a >= 0xF001 && a <= 0xFFFE) snprintf(o, n, "R%u", unsigned(a - 0xF000));
    else snprintf(o, n, "%u", unsigned(a));
}

// Riga con etichetta a sinistra e valore a destra.
void riga(Framebuffer& fb, int y, const char* etichetta, const char* valore) {
    fb.text(0, y, etichetta);
    fb.textRight(W - 1, y, valore);
}

void paginaStato(Framebuffer& fb, const Stato& s) {
    char t[48], u[24];
    snprintf(t, sizeof t, "R%u", unsigned(s.indirizzo - 0xF000));
    fb.text(0, 0, t, 2);

    if (!s.radioOk) {
        fb.textRight(W - 1, 0, "RADIO KO");
    } else if (s.canaleVoce) {
        fb.textRight(W - 1, 0, "VOCE");
    } else {
        snprintf(t, sizeof t, "%d dBm", s.potenza);
        fb.textRight(W - 1, 0, t);
    }
    if (s.batteriaMv > 0) {
        decimale(u, sizeof u, float(s.batteriaMv) / 1000.0f, 2, "V");
        if (s.batteriaPercento >= 0) snprintf(t, sizeof t, "%d%% %s", s.batteriaPercento, u);
        else snprintf(t, sizeof t, "%s", u);
        fb.textRight(W - 1, 8, t);
    }

    if (!s.radioOk) {
        fb.text(10, 26, "RADIO NON RISPONDE");
        fb.text(10, 40, "controlla l'SX1262");
        return;
    }

    snprintf(t, sizeof t, "%lu", (unsigned long)s.ripetuti);
    riga(fb, 16, "Ripetuti", t);
    snprintf(t, sizeof t, "%lu", (unsigned long)s.ricevuti);
    riga(fb, 24, "Ricevuti", t);
    if (s.haUltimo) {
        breve(u, sizeof u, s.ultimoSecondi);
        snprintf(t, sizeof t, "%s fa", u);
        riga(fb, 32, "Ultimo", t);
        decimale(u, sizeof u, s.snr, 1, "dB");
        snprintf(t, sizeof t, "%ddBm %s", s.rssi, u);
        riga(fb, 40, "Segnale", t);
    } else {
        riga(fb, 32, "Ultimo", "nessuno");
        riga(fb, 40, "Segnale", "-");
    }
    snprintf(t, sizeof t, "Vicini %d", s.nVicini);
    snprintf(u, sizeof u, "strade %d", s.strade);
    riga(fb, 48, t, u);
    lungo(t, sizeof t, s.accesoSecondi);
    riga(fb, 56, "Acceso da", t);
}

void paginaVicini(Framebuffer& fb, const Stato& s) {
    char t[48], u[24];
    fb.text(0, 0, "Vicini diretti");
    snprintf(t, sizeof t, "%d", s.nVicini);
    fb.textRight(W - 1, 0, t);
    if (s.nElenco == 0) {
        fb.text(0, 24, "Nessun vicino sentito");
        fb.text(0, 34, "negli ultimi 30 min");
        return;
    }
    const int righe = 7;
    const int altri = s.nVicini - s.nElenco;
    const int mostra = (altri > 0 && s.nElenco >= righe) ? righe - 1 : (s.nElenco < righe ? s.nElenco : righe);
    for (int i = 0; i < mostra; i++) {
        const Vicino& v = s.vicini[i];
        const int y = 8 + 8 * i;
        nome(t, sizeof t, v.addr);
        fb.text(0, y, t);
        decimale(u, sizeof u, v.snr, 1, "dB");
        fb.textRight(90, y, u);
        breve(u, sizeof u, v.secondi);
        fb.textRight(W - 1, y, u);
    }
    if (mostra < s.nVicini) {
        snprintf(t, sizeof t, "e altri %d", s.nVicini - mostra);
        fb.text(0, 8 + 8 * mostra, t);
    }
}

void paginaRadio(Framebuffer& fb, const Stato& s) {
    char t[48], u[24];
    decimale(u, sizeof u, s.mhz, 3, " MHz");
    fb.text(0, 0, u);
    snprintf(t, sizeof t, "SF%d", s.sf);
    fb.textRight(W - 1, 0, t);
    decimale(u, sizeof u, s.khz, 0, " kHz");
    snprintf(t, sizeof t, "%d dBm", s.potenza);
    riga(fb, 8, u, t);
    snprintf(t, sizeof t, "%lu", (unsigned long)s.trasmessi);
    riga(fb, 17, "Trasmessi", t);
    lungo(t, sizeof t, s.secondiInAria);
    riga(fb, 25, "In aria", t);
    snprintf(t, sizeof t, "%lu", (unsigned long)s.soppressi);
    riga(fb, 33, "Soppressi", t);
    snprintf(t, sizeof t, "%lu", (unsigned long)s.recuperati);
    riga(fb, 41, "Recuperati", t);
    snprintf(t, sizeof t, "%lu", (unsigned long)s.scartati);
    riga(fb, 49, "Scartati", t);
    snprintf(t, sizeof t, "%lu", (unsigned long)s.erroriRadio);
    riga(fb, 57, "Errori radio", t);
}

}  // namespace

void Framebuffer::clear() { memset(buf, 0, sizeof(buf)); }

void Framebuffer::pixel(int x, int y, bool on) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    uint8_t& b = buf[(y / 8) * W + x];
    const uint8_t m = uint8_t(1u << (y % 8));
    b = on ? uint8_t(b | m) : uint8_t(b & ~m);
}

bool Framebuffer::get(int x, int y) const {
    if (x < 0 || x >= W || y < 0 || y >= H) return false;
    return buf[(y / 8) * W + x] & (1u << (y % 8));
}

int Framebuffer::text(int x, int y, const char* s, int scale) {
    for (; *s; s++) {
        unsigned c = static_cast<unsigned char>(*s);
        if (c < 32 || c > 126) c = '?';
        const uint8_t* g = FONT[c - 32];
        for (int cx = 0; cx < 5; cx++)
            for (int cy = 0; cy < 7; cy++)
                if (g[cx] & (1u << cy))
                    for (int dx = 0; dx < scale; dx++)
                        for (int dy = 0; dy < scale; dy++) pixel(x + cx * scale + dx, y + cy * scale + dy);
        x += 6 * scale;
    }
    return x;
}

void Framebuffer::textRight(int xDestra, int y, const char* s, int scale) {
    text(xDestra - larghezza(s, scale) + 1, y, s, scale);
}

void disegna(Framebuffer& fb, const Stato& s, int pagina) {
    fb.clear();
    switch (pagina) {
        case 1: paginaVicini(fb, s); break;
        case 2: paginaRadio(fb, s); break;
        default: paginaStato(fb, s); break;
    }
}

void avviso(Framebuffer& fb, const char* titolo, const char* riga1, const char* riga2) {
    fb.clear();
    fb.text((W - larghezza(titolo, 2)) / 2, 6, titolo, 2);
    fb.text((W - larghezza(riga1, 1)) / 2, 32, riga1);
    fb.text((W - larghezza(riga2, 1)) / 2, 44, riga2);
}

}  // namespace schermo
