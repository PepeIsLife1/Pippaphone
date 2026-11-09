// PNG senza compressione (blocchi "stored" di zlib): basta per gli screenshot del simulatore.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "sim.h"

namespace {
uint32_t crcTable[256];

void initCrc() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crcTable[n] = c;
    }
}

uint32_t crc(const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) {
    for (size_t i = 0; i < n; i++) c = crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

void be32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 3; i >= 0; i--) v.push_back(uint8_t(x >> (8 * i)));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc(td.data(), td.size()) ^ 0xFFFFFFFFu);
}
}  // namespace

namespace sim {

bool savePngRgb(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb) {
    static bool init = false;
    if (!init) {
        initCrc();
        init = true;
    }
    // Righe RGB a 8 bit, ognuna preceduta dal filtro 0.
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (1 + size_t(w) * 3));
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), rgb.begin() + long(size_t(y) * w * 3), rgb.begin() + long(size_t(y + 1) * w * 3));
    }
    // zlib: intestazione, blocchi stored da 65535 byte, adler32.
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    for (size_t i = 0; i < raw.size(); i += 65535) {
        const size_t n = std::min<size_t>(65535, raw.size() - i);
        z.push_back(i + n >= raw.size() ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + long(i), raw.begin() + long(i + n));
    }
    be32(z, (b << 16) | a);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(w));
    be32(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool savePng(const std::string& path) {
    const int w = width(), h = height();
    const uint16_t* px = pixels();
    std::vector<uint8_t> rgb;
    rgb.reserve(size_t(w) * size_t(h) * 3);
    for (int i = 0; i < w * h; i++) {
        const uint16_t c = px[i];
        const uint8_t r = uint8_t((c >> 11) & 0x1F), g = uint8_t((c >> 5) & 0x3F), b = uint8_t(c & 0x1F);
        rgb.push_back(uint8_t((r << 3) | (r >> 2)));
        rgb.push_back(uint8_t((g << 2) | (g >> 4)));
        rgb.push_back(uint8_t((b << 3) | (b >> 2)));
    }
    return savePngRgb(path, w, h, rgb);
}

}  // namespace sim
