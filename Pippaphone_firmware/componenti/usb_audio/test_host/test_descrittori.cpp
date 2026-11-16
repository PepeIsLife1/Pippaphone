// Test di uac_desc con i descrittori VERI (descrittori_veri.h): analizza, entita', strada, clock, scelta, e le
// stesse cose con i descrittori tagliati, rotti a ogni byte e letti in memoria sporca.
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "aiuti.h"
#include "descrittori_veri.h"
#include "testing.h"
#include "usb_audio/uac_desc.h"

using namespace uac;

namespace {

struct Vero {
    const char* nome;
    const uint8_t* d;
    size_t n;
    uint16_t vid, pid;
};

const Vero VERI[] = {
    {"Apple 05ac:110a", veri::APPLE_110A, sizeof(veri::APPLE_110A), 0x05ac, 0x110a},
    {"Google 18d1:5034", veri::GOOGLE_5034, sizeof(veri::GOOGLE_5034), 0x18d1, 0x5034},
    {"Samsung 04e8:a051", veri::SAMSUNG_A051, sizeof(veri::SAMSUNG_A051), 0x04e8, 0xa051},
    {"C-Media 0d8c:000c", veri::CMEDIA_000C, sizeof(veri::CMEDIA_000C), 0x0d8c, 0x000c},
    {"E+ Corp 262a:9023", veri::EPLUS_9023, sizeof(veri::EPLUS_9023), 0x262a, 0x9023},
};

// La memoria protetta comune: ogni lettura fuori dal descrittore fa cadere il test.
aiuti::Protetta& memoria() {
    static aiuti::Protetta m(8192);
    return m;
}

// analizza() su una copia messa contro una pagina proibita (in fondo, o in testa).
Esito leggi(const uint8_t* d, size_t n, Configurazione& c, bool inFondo = true) {
    return analizza(memoria().metti(d, n, inFondo), n, c);
}

std::unique_ptr<Configurazione> nuova() { return std::unique_ptr<Configurazione>(new Configurazione); }

// Il pin chiesto dal selettore di clock: risponde sempre `pin` e conta le domande.
struct Selettori {
    uint8_t pin = 0;
    int domande = 0;
    uint8_t ultimo = 0;
};
uint8_t rispondiPin(void* ctx, uint8_t id) {
    Selettori* s = static_cast<Selettori*>(ctx);
    s->domande++;
    s->ultimo = id;
    return s->pin;
}

const std::vector<uint8_t> SOLO_48K = aiuti::rangeHz({48000, 48000, 0});
const std::vector<uint8_t> DUE = aiuti::rangeHz({44100, 44100, 0, 48000, 48000, 0});

}  // namespace

TEST(uac_veri_catene) {
    // Ogni vettore e' una catena di bLength che arriva esattamente a wTotalLength.
    for (const Vero& v : VERI) {
        const bool ok = aiuti::catenaEsatta(v.d, v.n);
        if (!ok) std::printf("\n    %s: catena dei bLength sbagliata", v.nome);
        CHECK(ok);
    }
}

TEST(uac_apple_110a) {
    auto c = nuova();
    CHECK(leggi(veri::APPLE_110A, sizeof(veri::APPLE_110A), *c) == Esito::OK);
    CHECK(aiuti::controllaConfigurazione(*c, Esito::OK).empty());
    CHECK(c->versione == Versione::UAC2);
    CHECK_EQ(c->bcdADC, 0x0200);
    CHECK_EQ(c->interfacciaControllo, 0);
    CHECK_EQ(c->nEntita, 10);
    CHECK(!c->troncata);
    CHECK_EQ(c->nStreaming, 2);
    // Cuffie: interfaccia 1, due alternative in uscita.
    const Streaming& s = c->streaming[0];
    CHECK_EQ(s.interfaccia, 1);
    CHECK(s.uscita);
    CHECK_EQ(s.nAlt, 2);
    const Alternativa& a1 = s.alt[0];
    CHECK_EQ(a1.numero, 1);
    CHECK_EQ(a1.terminale, 1);
    CHECK(a1.pcm);
    CHECK_EQ(a1.canali, 2);
    CHECK_EQ(a1.byteCampione, 3);
    CHECK_EQ(a1.bit, 24);
    CHECK_EQ(a1.endpoint, 0x02);
    CHECK_EQ(a1.maxPacchetto, 288);
    CHECK_EQ(a1.intervallo, 1);
    CHECK(a1.sync == Sync::SINCRONA);
    CHECK_EQ(a1.endpointFeedback, 0);
    CHECK_EQ(a1.frequenze.n, 0);  // UAC2: le frequenze arrivano dal clock
    const Alternativa& a2 = s.alt[1];
    CHECK_EQ(a2.numero, 2);
    CHECK_EQ(a2.byteCampione, 2);
    CHECK_EQ(a2.bit, 16);
    CHECK_EQ(a2.maxPacchetto, 192);
    // Microfono: interfaccia 2, in ingresso.
    const Streaming& m = c->streaming[1];
    CHECK_EQ(m.interfaccia, 2);
    CHECK(!m.uscita);
    CHECK_EQ(m.nAlt, 2);
    CHECK_EQ(m.alt[0].endpoint, 0x83);
    CHECK_EQ(m.alt[0].terminale, 6);
    CHECK_EQ(m.alt[0].canali, 1);
    CHECK_EQ(m.alt[0].maxPacchetto, 96);
    CHECK_EQ(m.alt[1].maxPacchetto, 144);
    // Entita'.
    const Entita* it = trovaEntita(*c, 1);
    CHECK(it && it->tipo == TipoEntita::TERMINALE_INGRESSO && it->tipoTerminale == TERMINALE_STREAMING_USB);
    CHECK(it && it->clock == 9);
    const Entita* fu = trovaEntita(*c, 2);
    CHECK(fu && fu->tipo == TipoEntita::FEATURE && fu->nSorgenti == 1 && fu->sorgenti[0] == 8);
    CHECK(fu && fu->canali == 2 && fu->muto == 0x0001 && fu->volume == 0x0006);
    const Entita* mix = trovaEntita(*c, 8);
    CHECK(mix && mix->tipo == TipoEntita::MIXER && mix->nSorgenti == 2 && mix->sorgenti[0] == 1 &&
          mix->sorgenti[1] == 7);
    const Entita* ck = trovaEntita(*c, 9);
    CHECK(ck && ck->tipo == TipoEntita::CLOCK_SORGENTE && ck->attributiClock == 7 && ck->controlliClock == 7);
    CHECK(trovaEntita(*c, 11) == nullptr);
    CHECK(trovaEntita(*c, 0) == nullptr);
    // Strada: IT 1 -> MIXER 8 -> FU 2 -> OT 3 (headset); clock 9.
    Percorso p;
    CHECK(percorso(*c, 1, p));
    CHECK_EQ(p.terminaleUscita, 3);
    CHECK_EQ(p.tipoUscita, TERMINALE_CUFFIE_MICROFONO);
    CHECK_EQ(p.featureUnit, 2);
    CHECK_EQ(p.clock, 9);
    CHECK_EQ(sorgenteClock(*c, 9), 9);
    // Senza frequenze (GET RANGE non ancora fatta) non si puo' scegliere.
    Scelta sc;
    CHECK(scegli(*c, 0, sc) == Esito::FORMATO);
    // GET RANGE (inventata): solo 48 kHz. Vince alt 2, 16 bit, 192 B: il contratto.
    CHECK(aiuti::simulaApri(*c, trovaQuirk(0x05ac, 0x110a), SOLO_48K, sc) == Esito::OK);
    CHECK_EQ(sc.indiceStreaming, 0);
    CHECK_EQ(sc.indiceAlt, 1);
    CHECK_EQ(sc.interfaccia, 1);
    CHECK_EQ(sc.alt, 2);
    CHECK_EQ(sc.terminale, 1);
    CHECK_EQ(sc.endpoint, 0x02);
    CHECK_EQ(sc.maxPacchetto, 192);
    CHECK_EQ(sc.canali, 2);
    CHECK_EQ(sc.byteCampione, 2);
    CHECK_EQ(sc.bit, 16);
    CHECK_EQ(sc.hz, uint32_t(48000));
    CHECK(sc.sync == Sync::SINCRONA);
    CHECK(!sc.frequenzaSuEndpoint);
    // Come la descrive il log.
    const std::vector<std::string> r = aiuti::righe(*c);
    CHECK_EQ(r.size(), size_t(5));
    if (r.size() == 5) {
        CHECK_EQ(r[0], std::string("UAC2, bcdADC 2.00, AudioControl if 0, 10 entita', 2 streaming"));
        CHECK_EQ(r[1], std::string("if 1 alt 1: PCM 2 ch 24/3 byte, 48000 Hz, ep 0x02 288 B ogni 1 ms, sincrono"));
        CHECK_EQ(r[2], std::string("if 1 alt 2: PCM 2 ch 16/2 byte, 48000 Hz, ep 0x02 192 B ogni 1 ms, sincrono"));
        // Il clock del microfono non e' stato chiesto: le sue frequenze restano da sapere.
        CHECK_EQ(r[3], std::string("if 2 alt 1 (ingresso): PCM 1 ch 16/2 byte, frequenze dal clock, ep 0x83 96 B "
                                   "ogni 1 ms, sincrono"));
    }
}

TEST(uac_apple_frequenze) {
    // Se il clock offrisse anche 44,1 kHz: con QUIRK_SOLO_48K resta 48; con solo 44,1 e il quirk non c'e' niente;
    // senza il quirk 44,1 kHz va (45 x 4 = 180 B nei 192).
    auto c = nuova();
    CHECK(leggi(veri::APPLE_110A, sizeof(veri::APPLE_110A), *c) == Esito::OK);
    Scelta sc;
    CHECK(aiuti::simulaApri(*c, QUIRK_SOLO_48K, DUE, sc) == Esito::OK);
    CHECK_EQ(sc.hz, uint32_t(48000));
    CHECK_EQ(sc.alt, 2);
    const auto solo44 = aiuti::rangeHz({44100, 44100, 0});
    CHECK(aiuti::simulaApri(*c, QUIRK_SOLO_48K, solo44, sc) == Esito::FORMATO);
    CHECK(aiuti::simulaApri(*c, 0, solo44, sc) == Esito::OK);
    CHECK_EQ(sc.hz, uint32_t(44100));
    CHECK_EQ(sc.alt, 2);
    // Un clock continuo 8-96 kHz: 48 kHz.
    CHECK(aiuti::simulaApri(*c, 0, aiuti::rangeHz({8000, 96000, 0}), sc) == Esito::OK);
    CHECK_EQ(sc.hz, uint32_t(48000));
    // Se GET RANGE non risponde uac_host suppone 48 kHz.
    CHECK(aiuti::simulaApri(*c, 0, {}, sc) == Esito::OK);
    CHECK_EQ(sc.hz, uint32_t(48000));
}

TEST(uac_google_5034) {
    auto c = nuova();
    CHECK(leggi(veri::GOOGLE_5034, sizeof(veri::GOOGLE_5034), *c) == Esito::OK);
    CHECK(aiuti::controllaConfigurazione(*c, Esito::OK).empty());
    CHECK(c->versione == Versione::UAC2);
    CHECK_EQ(c->nEntita, 11);
    CHECK(!c->troncata);
    CHECK_EQ(c->nStreaming, 2);
    // Microfono prima (interfaccia 1), cuffie dopo (interfaccia 2).
    CHECK_EQ(c->streaming[0].interfaccia, 1);
    CHECK(!c->streaming[0].uscita);
    CHECK_EQ(c->streaming[0].alt[0].terminale, 35);
    const Streaming& s = c->streaming[1];
    CHECK_EQ(s.interfaccia, 2);
    CHECK(s.uscita);
    CHECK_EQ(s.nAlt, 2);
    CHECK_EQ(s.alt[0].terminale, 36);
    CHECK_EQ(s.alt[0].maxPacchetto, 384);
    CHECK_EQ(s.alt[0].byteCampione, 2);
    CHECK_EQ(s.alt[1].maxPacchetto, 576);
    CHECK_EQ(s.alt[1].byteCampione, 3);
    // Feature Unit 43 (microfono in cuffia): muto e volume sul master.
    const Entita* fu43 = trovaEntita(*c, 43);
    CHECK(fu43 && fu43->canali == 2 && fu43->muto == 0x0001 && fu43->volume == 0x0001);
    // Strada: IT 36 -> MIXER 44 -> FU 37 -> OT 38; clock 40.
    Percorso p;
    CHECK(percorso(*c, 36, p));
    CHECK_EQ(p.terminaleUscita, 38);
    CHECK_EQ(p.tipoUscita, TERMINALE_CUFFIE_MICROFONO);
    CHECK_EQ(p.featureUnit, 37);
    CHECK_EQ(p.clock, 40);
    CHECK_EQ(sorgenteClock(*c, 40), 40);
    // Il terminale del microfono e' un Output Terminal: nessuna strada.
    CHECK(!percorso(*c, 35, p));
    // Contratto: alt 1, 384 B. GET RANGE inventata: 44,1 e 48 kHz.
    Scelta sc;
    CHECK(aiuti::simulaApri(*c, trovaQuirk(0x18d1, 0x5034), DUE, sc) == Esito::OK);
    CHECK_EQ(sc.interfaccia, 2);
    CHECK_EQ(sc.alt, 1);
    CHECK_EQ(sc.maxPacchetto, 384);
    CHECK_EQ(sc.byteCampione, 2);
    CHECK_EQ(sc.hz, uint32_t(48000));
    CHECK_EQ(sc.endpoint, 0x01);
}

TEST(uac_samsung_a051) {
    auto c = nuova();
    CHECK(leggi(veri::SAMSUNG_A051, sizeof(veri::SAMSUNG_A051), *c) == Esito::OK);
    CHECK(aiuti::controllaConfigurazione(*c, Esito::OK).empty());
    CHECK(c->versione == Versione::UAC2);
    CHECK_EQ(c->nEntita, 10);
    CHECK_EQ(c->nStreaming, 2);
    const Streaming& s = c->streaming[1];
    CHECK_EQ(s.interfaccia, 2);
    CHECK(s.uscita);
    CHECK_EQ(s.nAlt, 3);
    CHECK_EQ(s.alt[2].numero, 3);
    CHECK_EQ(s.alt[2].maxPacchetto, 768);
    CHECK_EQ(s.alt[2].byteCampione, 4);
    CHECK_EQ(s.alt[2].bit, 32);
    // I selettori di clock: 11 (cuffie) e 12 (microfono), tutti e due su 9 e 10.
    const Entita* sel = trovaEntita(*c, 11);
    CHECK(sel && sel->tipo == TipoEntita::CLOCK_SELETTORE && sel->nSorgenti == 2 && sel->sorgenti[0] == 9 &&
          sel->sorgenti[1] == 10 && sel->controlliClock == 0x03);
    // Strada: IT 1 -> FU 2 -> OT 3; il clock del terminale e' il selettore 11.
    Percorso p;
    CHECK(percorso(*c, 1, p));
    CHECK_EQ(p.terminaleUscita, 3);
    CHECK_EQ(p.featureUnit, 2);
    CHECK_EQ(p.clock, 11);
    // Senza chi risponde: primo ingresso, clock 9. Con GET CUR del selettore = 2: clock 10. 0 o fuori dagli
    // ingressi: il primo. Una domanda sola per selettore.
    CHECK_EQ(sorgenteClock(*c, 11), 9);
    Selettori q;
    q.pin = 2;
    CHECK_EQ(sorgenteClock(*c, 11, rispondiPin, &q), 10);
    CHECK_EQ(q.domande, 1);
    CHECK_EQ(q.ultimo, 11);
    q.pin = 0;
    CHECK_EQ(sorgenteClock(*c, 11, rispondiPin, &q), 9);
    q.pin = 7;
    CHECK_EQ(sorgenteClock(*c, 11, rispondiPin, &q), 9);
    CHECK_EQ(q.domande, 3);
    // Quirk: 5 ms dopo ogni richiesta, mai SET sui selettori.
    const uint32_t k = trovaQuirk(0x04e8, 0xa051);
    CHECK_EQ(k, uint32_t(QUIRK_PAUSA_5MS | QUIRK_NON_TOCCARE_SELETTORE));
    CHECK_EQ(pausaRichiesteMs(k), uint32_t(5));
    // Contratto: alt 1, mai alt 3 (768 B). Anche con il selettore sul secondo clock.
    Scelta sc;
    q.pin = 2;
    CHECK(aiuti::simulaApri(*c, k, SOLO_48K, sc, rispondiPin, &q) == Esito::OK);
    CHECK_EQ(sc.alt, 1);
    CHECK_EQ(sc.maxPacchetto, 384);
    CHECK_EQ(sc.hz, uint32_t(48000));
}

TEST(uac_samsung_solo_768) {
    // Le alternative 1 e 2 rese inadatte (8 bit): resta solo la 3 da 768 B, che andrebbe bene se la FIFO
    // dell'ESP32-S3 la portasse: PACCHETTI_GRANDI, non FORMATO.
    std::vector<uint8_t> d(veri::SAMSUNG_A051, veri::SAMSUNG_A051 + sizeof(veri::SAMSUNG_A051));
    int cambiati = 0;
    uint8_t interfaccia = 0, alt = 0;
    for (size_t i = 0; i + 5 < d.size(); i += d[i]) {
        if (d[i + 1] == 0x04) {
            interfaccia = d[i + 2];
            alt = d[i + 3];
        }
        // FORMAT_TYPE (06 24 02) dell'interfaccia 2, alternative 1 e 2: bBitResolution a 8.
        if (d[i] == 0x06 && d[i + 1] == 0x24 && d[i + 2] == 0x02 && interfaccia == 2 && (alt == 1 || alt == 2)) {
            d[i + 5] = 8;
            cambiati++;
        }
    }
    CHECK_EQ(cambiati, 2);
    auto c = nuova();
    CHECK(leggi(d.data(), d.size(), *c) == Esito::OK);
    Scelta sc;
    CHECK(aiuti::simulaApri(*c, 0, SOLO_48K, sc) == Esito::PACCHETTI_GRANDI);
    CHECK_EQ(sc.alt, 0);
}

TEST(uac_cmedia_000c) {
    auto c = nuova();
    CHECK(leggi(veri::CMEDIA_000C, sizeof(veri::CMEDIA_000C), *c) == Esito::OK);
    CHECK(aiuti::controllaConfigurazione(*c, Esito::OK).empty());
    CHECK(c->versione == Versione::UAC1);
    CHECK_EQ(c->bcdADC, 0x0100);
    CHECK_EQ(c->interfacciaControllo, 0);
    CHECK_EQ(c->nEntita, 9);
    CHECK_EQ(c->nStreaming, 2);
    const Alternativa& a = c->streaming[0].alt[0];
    CHECK_EQ(c->streaming[0].interfaccia, 1);
    CHECK(c->streaming[0].uscita);
    CHECK_EQ(a.terminale, 1);
    CHECK(a.pcm);
    CHECK_EQ(a.canali, 2);
    CHECK_EQ(a.byteCampione, 2);
    CHECK_EQ(a.bit, 16);
    CHECK_EQ(a.frequenze.n, 2);
    CHECK(a.frequenze.contiene(48000) && a.frequenze.contiene(44100) && !a.frequenze.contiene(32000));
    CHECK_EQ(a.endpoint, 0x01);
    CHECK_EQ(a.maxPacchetto, 200);
    CHECK(a.sync == Sync::ADATTIVA);
    CHECK(a.frequenzaSuEndpoint);
    CHECK_EQ(a.endpointFeedback, 0);
    const Alternativa& m = c->streaming[1].alt[0];
    CHECK(!c->streaming[1].uscita);
    CHECK_EQ(m.endpoint, 0x82);
    CHECK(m.sync == Sync::ASINCRONA);
    CHECK_EQ(m.canali, 1);
    // Feature Unit UAC1 (bControlSize 1): 9 muto sul master e volume sui canali; 10 (master e un canale, 9 byte)
    // con AGC sul master (bit 6, ignorato).
    const Entita* fu9 = trovaEntita(*c, 9);
    CHECK(fu9 && fu9->canali == 2 && fu9->muto == 0x0001 && fu9->volume == 0x0006 && fu9->sorgenti[0] == 15);
    const Entita* fu10 = trovaEntita(*c, 10);
    CHECK(fu10 && fu10->canali == 1 && fu10->muto == 0x0001 && fu10->volume == 0x0001);
    const Entita* sel = trovaEntita(*c, 8);
    CHECK(sel && sel->tipo == TipoEntita::SELETTORE && sel->nSorgenti == 1 && sel->sorgenti[0] == 10);
    // Strada: IT 1 -> MIXER 15 -> FU 9 -> OT 6 (altoparlante); UAC1: niente clock.
    Percorso p;
    CHECK(percorso(*c, 1, p));
    CHECK_EQ(p.terminaleUscita, 6);
    CHECK_EQ(p.tipoUscita, TERMINALE_ALTOPARLANTE);
    CHECK_EQ(p.featureUnit, 9);
    CHECK_EQ(p.clock, 0);
    // Scelta: 48 kHz, 16 bit, stereo, adattivo; dopo SET_INTERFACE la frequenza va all'endpoint.
    Scelta sc;
    CHECK(scegli(*c, trovaQuirk(0x0d8c, 0x000c), sc) == Esito::OK);
    CHECK_EQ(sc.interfaccia, 1);
    CHECK_EQ(sc.alt, 1);
    CHECK_EQ(sc.hz, uint32_t(48000));
    CHECK_EQ(sc.maxPacchetto, 200);
    CHECK(sc.sync == Sync::ADATTIVA);
    CHECK(sc.frequenzaSuEndpoint);
    CHECK_EQ(trovaQuirk(0x0d8c, 0x000c), uint32_t(0));
    const std::vector<std::string> r = aiuti::righe(*c);
    CHECK_EQ(r.size(), size_t(3));
    if (r.size() == 3)
        CHECK_EQ(r[1], std::string("if 1 alt 1: PCM 2 ch 16/2 byte, 48000, 44100 Hz, ep 0x01 200 B ogni 1 ms, "
                                   "adattivo, frequenza sull'endpoint"));
}

TEST(uac_eplus_9023) {
    auto c = nuova();
    CHECK(leggi(veri::EPLUS_9023, sizeof(veri::EPLUS_9023), *c) == Esito::OK);
    CHECK(aiuti::controllaConfigurazione(*c, Esito::OK).empty());
    CHECK(c->versione == Versione::UAC1);
    CHECK_EQ(c->interfacciaControllo, 1);  // l'interfaccia 0 e' HID
    CHECK_EQ(c->nEntita, 3);
    CHECK_EQ(c->nStreaming, 1);
    const Streaming& s = c->streaming[0];
    CHECK_EQ(s.interfaccia, 3);
    CHECK_EQ(s.nAlt, 2);
    const Alternativa& a = s.alt[0];
    CHECK(a.sync == Sync::ASINCRONA);
    CHECK_EQ(a.endpoint, 0x03);
    CHECK_EQ(a.maxPacchetto, 400);
    CHECK_EQ(a.endpointFeedback, 0x83);
    CHECK_EQ(a.intervalloFeedback, 3);  // bRefresh dell'endpoint 0x83: feedback ogni 8 ms
    CHECK(a.frequenzaSuEndpoint);
    CHECK_EQ(a.frequenze.n, 5);
    CHECK(a.frequenze.contiene(96000) && a.frequenze.contiene(88200) && a.frequenze.contiene(48000) &&
          a.frequenze.contiene(44100) && a.frequenze.contiene(32000));
    CHECK_EQ(s.alt[1].maxPacchetto, 600);
    CHECK_EQ(s.alt[1].byteCampione, 3);
    // Feature Unit: muto solo sul master, volume solo su sinistro e destro.
    const Entita* fu = trovaEntita(*c, 9);
    CHECK(fu && fu->muto == 0x0001 && fu->volume == 0x0006);
    Percorso p;
    CHECK(percorso(*c, 6, p));
    CHECK_EQ(p.terminaleUscita, 2);
    CHECK_EQ(p.tipoUscita, TERMINALE_ALTOPARLANTE);
    CHECK_EQ(p.featureUnit, 9);
    // Contratto: alt 1, asincrono (in P1 suona senza feedback).
    Scelta sc;
    CHECK(scegli(*c, trovaQuirk(0x262a, 0x9023), sc) == Esito::OK);
    CHECK_EQ(sc.interfaccia, 3);
    CHECK_EQ(sc.alt, 1);
    CHECK_EQ(sc.hz, uint32_t(48000));
    CHECK_EQ(sc.maxPacchetto, 400);
    CHECK(sc.sync == Sync::ASINCRONA);
    // Il 24 bit da 600 B sta proprio al limite della FIFO: se il 16 bit non ci fosse, andrebbe quello.
    c->streaming[0].alt[0].pcm = false;
    CHECK(scegli(*c, 0, sc) == Esito::OK);
    CHECK_EQ(sc.alt, 2);
    CHECK_EQ(sc.maxPacchetto, 600);
}

TEST(uac_veri_tagliati) {
    // Ogni vettore tagliato a ogni lunghezza da 0 alla fine, contro la pagina proibita in fondo e in testa:
    // mai una lettura fuori. Tagliato in mezzo a un descrittore: MALFORMATO. Tagliato fra due descrittori: si
    // legge quello che c'e' (mai MALFORMATO).
    int prove = 0, fraDue = 0, inMezzo = 0;
    auto c = nuova();
    for (const Vero& v : VERI) {
        std::vector<bool> confine(v.n + 1, false);
        for (size_t i = 0; i < v.n; i += v.d[i]) confine[i] = true;
        confine[v.n] = true;
        for (size_t n = 0; n <= v.n; n++) {
            for (int verso = 0; verso < 2; verso++) {
                const Esito e = leggi(v.d, n, *c, verso == 0);
                prove++;
                CHECK(aiuti::controllaConfigurazione(*c, e).empty());
                if (n < 9 || !confine[n]) {
                    CHECK(e == Esito::MALFORMATO);
                    if (verso == 0) inMezzo++;
                } else {
                    CHECK(e != Esito::MALFORMATO);
                    if (verso == 0) fraDue++;
                }
            }
        }
    }
    std::printf("\n    %d letture (%d tagli fra due descrittori, %d in mezzo) ", prove, fraDue, inMezzo);
}

TEST(uac_veri_wtotallength) {
    std::vector<uint8_t> d(veri::APPLE_110A, veri::APPLE_110A + sizeof(veri::APPLE_110A));
    auto c = nuova();
    // wTotalLength piu' grande del buffer: si legge fino a n.
    d[2] = 0xFF;
    d[3] = 0xFF;
    CHECK(leggi(d.data(), d.size(), *c) == Esito::OK);
    CHECK_EQ(c->nStreaming, 2);
    CHECK(leggi(d.data(), 300, *c) == Esito::MALFORMATO);  // 300 cade in mezzo a un descrittore
    // wTotalLength piu' piccolo: si legge fino a wTotalLength. 233 = la fine dell'alternativa 1 delle cuffie.
    d[2] = 233;
    d[3] = 0;
    CHECK(leggi(d.data(), d.size(), *c) == Esito::OK);
    CHECK_EQ(c->nStreaming, 1);
    CHECK_EQ(c->streaming[0].nAlt, 1);
    Scelta sc;
    CHECK(aiuti::simulaApri(*c, 0, SOLO_48K, sc) == Esito::OK);
    CHECK_EQ(sc.alt, 1);
    CHECK_EQ(sc.byteCampione, 3);
    CHECK_EQ(sc.maxPacchetto, 288);
    // wTotalLength piu' corto della configurazione stessa, o che finisce in mezzo a un descrittore.
    d[2] = 5;
    CHECK(leggi(d.data(), d.size(), *c) == Esito::MALFORMATO);
    d[2] = 0;
    CHECK(leggi(d.data(), d.size(), *c) == Esito::MALFORMATO);
    d[2] = 20;
    CHECK(leggi(d.data(), d.size(), *c) == Esito::MALFORMATO);
    // n piu' grande dei dati veri (byte in piu' dopo wTotalLength): si ignorano.
    std::vector<uint8_t> lungo(veri::CMEDIA_000C, veri::CMEDIA_000C + sizeof(veri::CMEDIA_000C));
    lungo.insert(lungo.end(), {0x00, 0x01, 0xFF});
    CHECK(leggi(lungo.data(), lungo.size(), *c) == Esito::OK);
}

TEST(uac_veri_blength_rotti) {
    // bLength 0 o 1 all'inizio di ogni descrittore (anche il primo): sempre MALFORMATO. Un bLength che fa
    // uscire dalla fine: MALFORMATO.
    int prove = 0;
    auto c = nuova();
    for (const Vero& v : VERI) {
        std::vector<uint8_t> d(v.d, v.d + v.n);
        for (size_t i = 0; i < v.n; i += v.d[i]) {
            for (uint8_t cattivo : {uint8_t(0), uint8_t(1)}) {
                d[i] = cattivo;
                CHECK(leggi(d.data(), d.size(), *c) == Esito::MALFORMATO);
                CHECK(aiuti::controllaConfigurazione(*c, Esito::MALFORMATO).empty());
                prove++;
            }
            d[i] = v.d[i];
        }
        // L'ultimo descrittore che esce di un byte.
        size_t ultimo = 0;
        for (size_t i = 0; i < v.n; i += v.d[i]) ultimo = i;
        d[ultimo] = uint8_t(d[ultimo] + 1);
        CHECK(leggi(d.data(), d.size(), *c) == Esito::MALFORMATO);
        prove++;
    }
    // La configurazione: tipo sbagliato, bLength 8, niente buffer.
    std::vector<uint8_t> d(veri::APPLE_110A, veri::APPLE_110A + sizeof(veri::APPLE_110A));
    d[1] = 0x01;
    CHECK(leggi(d.data(), d.size(), *c) == Esito::MALFORMATO);
    d[1] = 0x02;
    d[0] = 8;
    CHECK(leggi(d.data(), d.size(), *c) == Esito::MALFORMATO);
    CHECK(analizza(nullptr, 100, *c) == Esito::MALFORMATO);
    std::printf("\n    %d prove ", prove);
}

TEST(uac_veri_memoria_sporca) {
    // analizza() non dipende da quello che c'era in out (azzera in posto, mai letture di campi vecchi): stessa
    // configurazione da memoria a zero, a 0xA5 e dopo un'altra analisi.
    auto a = nuova();
    auto b = nuova();
    for (const Vero& v : VERI) {
        std::memset(static_cast<void*>(a.get()), 0x00, sizeof(Configurazione));
        std::memset(static_cast<void*>(b.get()), 0xA5, sizeof(Configurazione));
        CHECK(leggi(v.d, v.n, *a) == Esito::OK);
        CHECK(leggi(v.d, v.n, *b) == Esito::OK);
        CHECK(aiuti::stessaConfigurazione(*a, *b));
        CHECK(leggi(veri::SAMSUNG_A051, sizeof(veri::SAMSUNG_A051), *b) == Esito::OK);
        CHECK(leggi(v.d, v.n, *b) == Esito::OK);
        CHECK(aiuti::stessaConfigurazione(*a, *b));
    }
}
