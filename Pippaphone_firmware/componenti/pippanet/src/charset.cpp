#include "pippanet/charset.h"

namespace pn {
namespace {

// Codici extra (valori Unicode). L'ordine e' parte del protocollo: non cambiarlo.
struct Extra {
    uint8_t code;
    uint32_t cp;
};
constexpr Extra EXTRAS[] = {
    {0x01, 0x00E0},  // à
    {0x02, 0x00E8},  // è
    {0x03, 0x00E9},  // é
    {0x04, 0x00EC},  // ì
    {0x05, 0x00F2},  // ò
    {0x06, 0x00F9},  // ù
    {0x07, 0x00C0},  // À
    {0x08, 0x00C8},  // È
    {0x09, 0x00C9},  // É
    {0x0B, 0x00CC},  // Ì
    {0x0C, 0x00D2},  // Ò
    {0x0D, 0x00D9},  // Ù
    {0x0E, 0x20AC},  // €
    {0x0F, 0x00A3},  // £
    {0x10, 0x00B0},  // °
    {0x11, 0x00E7},  // ç
    {0x12, 0x00F1},  // ñ
    {0x13, 0x00E4},  // ä
    {0x14, 0x00F6},  // ö
    {0x15, 0x00FC},  // ü
    {0x16, 0x00DF},  // ß
    {0x17, 0x00AB},  // «
    {0x18, 0x00BB},  // »
    {0x19, 0x00BF},  // ¿
    {0x1A, 0x00A1},  // ¡
    {0x1B, 0x00A7},  // §
    {0x1C, 0x00B7},  // ·
    {0x1D, 0x2026},  // …
    {0x1E, 0x2019},  // ’
    {0x1F, 0x201C},  // “
    {0x7F, 0x201D},  // ”
};

int codeFor(uint32_t cp) {
    if (cp == '\n') return 0x0A;
    if (cp >= 0x20 && cp <= 0x7E) return int(cp);
    for (const auto& e : EXTRAS)
        if (e.cp == cp) return e.code;
    // Sostituzioni comode per caratteri simili.
    switch (cp) {
        case '\r': return -2;             // ignorato
        case '\t': return ' ';
        case 0x2018: return '\'';          // ‘
        case 0x00A0: return ' ';           // spazio non separabile
        case 0x2013: case 0x2014: return '-';  // trattini lunghi
        case 0x00C1: return 'A'; case 0x00E1: return 0x01;  // á -> à
        case 0x00CD: return 'I'; case 0x00ED: return 0x04;  // í -> ì
        case 0x00D3: return 'O'; case 0x00F3: return 0x05;  // ó -> ò
        case 0x00DA: return 'U'; case 0x00FA: return 0x06;  // ú -> ù
        default: return -1;
    }
}

uint32_t cpFor(uint8_t code) {
    if (code == 0x0A) return '\n';
    if (code >= 0x20 && code <= 0x7E) return code;
    for (const auto& e : EXTRAS)
        if (e.code == code) return e.cp;
    return '?';
}

void appendUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) {
        s.push_back(char(cp));
    } else if (cp < 0x800) {
        s.push_back(char(0xC0 | (cp >> 6)));
        s.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        s.push_back(char(0xE0 | (cp >> 12)));
        s.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        s.push_back(char(0xF0 | (cp >> 18)));
        s.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(char(0x80 | (cp & 0x3F)));
    }
}

// Legge un carattere UTF-8 a partire da i; restituisce il codepoint e avanza i.
uint32_t nextCp(const std::string& s, size_t& i) {
    uint8_t c = uint8_t(s[i++]);
    if (c < 0x80) return c;
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    if (extra == 0) return 0xFFFD;
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra; k++) {
        if (i >= s.size()) return 0xFFFD;
        uint8_t cc = uint8_t(s[i]);
        if ((cc & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (cc & 0x3F);
        i++;
    }
    return cp;
}

}  // namespace

bool isEncodable(uint32_t cp) { return codeFor(cp) >= 0; }

size_t utf8ToCodes(const std::string& utf8, uint8_t* codes, size_t maxChars) {
    size_t n = 0, i = 0;
    while (i < utf8.size() && n < maxChars) {
        int c = codeFor(nextCp(utf8, i));
        if (c == -2) continue;
        codes[n++] = uint8_t(c < 0 ? '?' : c);
    }
    return n;
}

size_t textLength(const std::string& utf8) {
    size_t n = 0, i = 0;
    while (i < utf8.size()) {
        if (codeFor(nextCp(utf8, i)) != -2) n++;
    }
    return n;
}

std::string codesToUtf8(const uint8_t* codes, size_t n) {
    std::string s;
    s.reserve(n + n / 4);
    for (size_t i = 0; i < n; i++) {
        if (codes[i] == 0) continue;
        appendUtf8(s, cpFor(codes[i] & 0x7F));
    }
    return s;
}

size_t pack7(const uint8_t* codes, size_t n, uint8_t* out, size_t outCap) {
    size_t bytes = (n * 7 + 7) / 8;
    if (bytes > outCap) return 0;
    uint32_t acc = 0;
    int bits = 0;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        acc |= uint32_t(codes[i] & 0x7F) << bits;
        bits += 7;
        while (bits >= 8) {
            out[o++] = uint8_t(acc);
            acc >>= 8;
            bits -= 8;
        }
    }
    if (bits > 0) out[o++] = uint8_t(acc);
    return o;
}

size_t unpack7(const uint8_t* in, size_t len, uint8_t* codes, size_t maxCodes) {
    uint32_t acc = 0;
    int bits = 0;
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        acc |= uint32_t(in[i]) << bits;
        bits += 8;
        while (bits >= 7 && n < maxCodes) {
            codes[n++] = uint8_t(acc & 0x7F);
            acc >>= 7;
            bits -= 7;
        }
    }
    // Il riempimento finale e' fatto di zeri: toglie gli eventuali codici 0 in coda.
    while (n > 0 && codes[n - 1] == 0) n--;
    return n;
}

size_t encodeText(const std::string& utf8, uint8_t* out, size_t outCap, size_t maxChars) {
    uint8_t codes[MAX_FRAME * 8 / 7 + 1];
    if (maxChars > sizeof(codes)) maxChars = sizeof(codes);
    size_t n = utf8ToCodes(utf8, codes, maxChars);
    return pack7(codes, n, out, outCap);
}

std::string decodeText(const uint8_t* in, size_t len) {
    uint8_t codes[MAX_FRAME * 8 / 7 + 1];
    size_t n = unpack7(in, len, codes, sizeof(codes));
    return codesToUtf8(codes, n);
}

}  // namespace pn
