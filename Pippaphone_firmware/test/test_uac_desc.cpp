// Test dei descrittori USB Audio (componenti/usb_audio, uac_desc.h e quirk.cpp): cuffie USB-C del T-Deck.
//
// I test completi sono in componenti/usb_audio/test_host, con il loro CMakeLists: i descrittori veri di cuffie e
// adattatori (byte per byte dai dump di lsusb -v), i casi rotti, l'ordine della scelta, GET RANGE, i nomi, i
// quirk e un fuzzer con la memoria protetta. Qui, dentro usb_audio_test, solo un riassunto veloce sugli stessi
// descrittori veri, per accorgersi subito se qualcosa si rompe.
#include <cstring>

#include "../componenti/usb_audio/test_host/descrittori_veri.h"
#include "testing.h"
#include "usb_audio/uac_desc.h"

using namespace uac;

TEST(uac_frequenze_contiene) {
    Frequenze f;
    CHECK(f.aggiungi(44100, 44100));          // una frequenza sola
    CHECK(f.aggiungi(8000, 16000));           // intervallo continuo
    CHECK(f.aggiungi(32000, 96000, 16000));   // intervallo a passi: 32, 48, 64, 80, 96 kHz
    CHECK(f.contiene(44100));
    CHECK(f.contiene(11025));
    CHECK(f.contiene(48000));
    CHECK(!f.contiene(44000));
    CHECK(!f.contiene(40000));
    CHECK(!f.contiene(192000));
    while (f.n < MAX_FREQ) f.aggiungi(1000, 1000);
    CHECK(!f.aggiungi(2000, 2000));           // piena
}

TEST(uac_veri_in_breve) {
    struct Caso {
        const uint8_t* d;
        size_t n;
        uint16_t vid, pid;
        uint8_t interfaccia, alt;
        uint16_t maxPacchetto;
        Sync sync;
    };
    const Caso casi[] = {
        {veri::APPLE_110A, sizeof(veri::APPLE_110A), 0x05ac, 0x110a, 1, 2, 192, Sync::SINCRONA},
        {veri::GOOGLE_5034, sizeof(veri::GOOGLE_5034), 0x18d1, 0x5034, 2, 1, 384, Sync::SINCRONA},
        {veri::SAMSUNG_A051, sizeof(veri::SAMSUNG_A051), 0x04e8, 0xa051, 2, 1, 384, Sync::SINCRONA},
        {veri::CMEDIA_000C, sizeof(veri::CMEDIA_000C), 0x0d8c, 0x000c, 1, 1, 200, Sync::ADATTIVA},
        {veri::EPLUS_9023, sizeof(veri::EPLUS_9023), 0x262a, 0x9023, 3, 1, 400, Sync::ASINCRONA},
    };
    static Configurazione c;  // piu' di 5 KB: non sullo stack
    for (const Caso& k : casi) {
        CHECK(analizza(k.d, k.n, c) == Esito::OK);
        if (c.versione == Versione::UAC2) {
            // UAC2: le frequenze le da' il clock (GET RANGE inventata: solo 48 kHz).
            Frequenze f;
            f.aggiungi(48000, 48000);
            for (uint8_t i = 0; i < c.nStreaming; i++)
                for (uint8_t j = 0; j < c.streaming[i].nAlt; j++)
                    impostaFrequenzeClock(c, c.streaming[i].alt[j].terminale, f);
        }
        Scelta s;
        CHECK(scegli(c, trovaQuirk(k.vid, k.pid), s) == Esito::OK);
        CHECK_EQ(s.interfaccia, k.interfaccia);
        CHECK_EQ(s.alt, k.alt);
        CHECK_EQ(s.maxPacchetto, k.maxPacchetto);
        CHECK_EQ(s.hz, uint32_t(48000));
        CHECK(s.sync == k.sync);
        Percorso p;
        CHECK(percorso(c, s.terminale, p) && p.featureUnit != 0);
    }
    // Rotti: un bLength a zero, e tagliato in mezzo a un descrittore.
    uint8_t rotto[sizeof(veri::CMEDIA_000C)];
    std::memcpy(rotto, veri::CMEDIA_000C, sizeof(rotto));
    rotto[9] = 0;
    CHECK(analizza(rotto, sizeof(rotto), c) == Esito::MALFORMATO);
    CHECK(analizza(veri::CMEDIA_000C, 100, c) == Esito::MALFORMATO);
}
