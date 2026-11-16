// Tempi del DSP delle cuffie USB-C (usb_audio/dsp.h) sul PC: nanosecondi per campione d'uscita del
// ricampionatore nei rapporti che capitano davvero, per frame di impacchetta() e per campione della coda.
// Sul PC i tempi non sono quelli dell'ESP32-S3 (la stima in cicli per l'S3 e' in dsp.cpp, contata sulle
// istruzioni del compilatore xtensa): qui servono a confrontare i casi fra loro e a vedere se si peggiora.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "usb_audio/dsp.h"

using namespace uac;
using Orologio = std::chrono::steady_clock;

namespace {
volatile int g_pozzo;  // cosi' il compilatore non butta via i risultati

double secondiDa(Orologio::time_point t0) { return std::chrono::duration<double>(Orologio::now() - t0).count(); }

// ns per campione d'uscita, con 20 secondi d'audio a blocchi da 4096.
double ricampionatore(uint32_t hzIn, uint32_t hzOut) {
    Ricampionatore r;
    if (!r.configura(hzIn, hzOut, allocatoreStandard())) return -1;
    std::vector<int16_t> in(4096), out(r.uscitaMassima(in.size()));
    for (size_t i = 0; i < in.size(); i++) in[i] = int16_t(int32_t((i * 7919) % 20000) - 10000);
    const uint64_t totale = uint64_t(hzIn) * 20;
    uint64_t n = 0;
    const auto t0 = Orologio::now();
    for (uint64_t fatto = 0; fatto < totale; fatto += in.size()) {
        size_t letti = 0;
        n += r.elabora(in.data(), in.size(), letti, out.data(), out.size());
        g_pozzo = g_pozzo + out[0];
    }
    return secondiDa(t0) * 1e9 / double(n);
}

// ns per frame di impacchetta(), a trasferimenti da 384 frame (8 pacchetti da 48), col guadagno fermo o in rampa.
double pacchetti(uint8_t canali, uint8_t byteCampione, bool rampa) {
    std::vector<int16_t> mono(384);
    for (size_t i = 0; i < mono.size(); i++) mono[i] = int16_t(int32_t((i * 7919) % 60000) - 30000);
    std::vector<uint8_t> out(384 * 2 * 4);
    Guadagno g;
    g.subito(guadagnoDaVolume(4));
    const int giri = 200000;
    const auto t0 = Orologio::now();
    for (int k = 0; k < giri; k++) {
        if (rampa && k % 2 == 0) g.imposta(k % 4 ? guadagnoDaVolume(4) : guadagnoDaVolume(6), 768);
        impacchetta(mono.data(), mono.size(), canali, byteCampione, g, out.data());
        g_pozzo = g_pozzo + out[5];
    }
    return secondiDa(t0) * 1e9 / (double(giri) * 384);
}

// ns per campione della coda: scrivi 384 e leggi 384, un thread solo.
double coda() {
    Anello a;
    if (!a.crea(9600, allocatoreStandard())) return -1;
    std::vector<int16_t> buf(384, 7);
    const int giri = 400000;
    const auto t0 = Orologio::now();
    for (int k = 0; k < giri; k++) {
        a.scrivi(buf.data(), buf.size());
        a.leggi(buf.data(), buf.size());
        g_pozzo = g_pozzo + buf[3];
    }
    return secondiDa(t0) * 1e9 / (double(giri) * 384);
}
}  // namespace

int main() {
    std::printf("Ricampionatore sul PC: ns per campione d'uscita (e parte di un core del PC per stare al passo):\n");
    const uint32_t coppie[][2] = {{44100, 48000}, {48000, 44100}, {8000, 48000}, {16000, 48000},
                                  {32000, 48000}, {22050, 48000}, {8000, 44100}, {48000, 48000}};
    for (const auto& c : coppie) {
        const double ns = ricampionatore(c[0], c[1]);
        std::printf("  %5u -> %5u: %6.2f ns  (%.3f%%)\n", unsigned(c[0]), unsigned(c[1]), ns, ns * c[1] * 1e-7);
    }
    std::printf("impacchetta, ns per frame: 16 bit stereo %.2f (in rampa %.2f), 24 bit stereo %.2f, 16 bit mono "
                "%.2f\n",
                pacchetti(2, 2, false), pacchetti(2, 2, true), pacchetti(2, 3, false), pacchetti(1, 2, false));
    std::printf("Anello, ns per campione (scrivi + leggi a blocchi da 384): %.3f\n", coda());
    return 0;
}
