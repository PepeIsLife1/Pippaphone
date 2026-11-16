// Test di uac_desc sui casi che i dispositivi veri non mostrano: descrittori rotti o strani costruiti a mano,
// l'ordine di scegli(), le risposte GET RANGE, il volume a 0 dB, i nomi dei prodotti, i quirk.
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
using aiuti::Costruttore;

namespace {

aiuti::Protetta& memoria() {
    static aiuti::Protetta m(16384);
    return m;
}

Esito leggi(const std::vector<uint8_t>& d, Configurazione& c) {
    return analizza(memoria().metti(d, true), d.size(), c);
}

std::unique_ptr<Configurazione> nuova() { return std::unique_ptr<Configurazione>(new Configurazione); }

// Un'alternativa in uscita UAC1 classica (endpoint OUT 0x01 isocrono adattivo, CS endpoint con la frequenza).
Costruttore& altUscita1(Costruttore& k, uint8_t interfaccia, uint8_t alt, uint8_t terminale, uint16_t mps,
                        uint8_t byte = 2, uint8_t bit = 16) {
    return k.interfaccia(interfaccia, alt, 1, 0x01, 0x02, 0x00)
        .general1(terminale)
        .formato1(2, byte, bit, {48000, 44100})
        .ep1(0x01, 0x09, mps)
        .csep1(0x01);
}

// Una funzione UAC1 minima: AudioControl `ac` con IT 1 (USB) -> FU 2 -> OT 3 (cuffie), streaming `as` in uscita.
Costruttore& funzione1(Costruttore& k, uint8_t ac, uint8_t as, uint16_t mps = 192) {
    k.interfaccia(ac, 0, 0, 0x01, 0x01, 0x00)
        .header1({as})
        .it1(1, 0x0101)
        .fu1(2, 1, {0x01, 0x02, 0x02})
        .ot1(3, 0x0302, 2)
        .interfaccia(as, 0, 0, 0x01, 0x02, 0x00);
    return altUscita1(k, as, 1, 1, mps);
}

// Una funzione UAC2 minima: AudioControl `ac` con clock 9, IT 1 -> FU 2 -> OT 3, streaming `as` (OUT o IN).
Costruttore& funzione2(Costruttore& k, uint8_t ac, uint8_t as, bool uscita = true) {
    return k.interfaccia(ac, 0, 0, 0x01, 0x01, 0x20)
        .header2()
        .clock2(9)
        .it2(1, 0x0101, 9)
        .fu2(2, 1, {0x3, 0xC, 0xC})
        .ot2(3, 0x0302, 2, 9)
        .interfaccia(as, 0, 0, 0x01, 0x02, 0x20)
        .interfaccia(as, 1, 1, 0x01, 0x02, 0x20)
        .general2(1, 2)
        .formato2(2, 16)
        .ep(uscita ? 0x01 : 0x81, 0x0D, 192)
        .csep2();
}

}  // namespace

TEST(uac_rotti_testa) {
    auto c = nuova();
    CHECK(analizza(nullptr, 0, *c) == Esito::MALFORMATO);
    const uint8_t corto[] = {0x09, 0x02, 0x09, 0x00};
    CHECK(analizza(corto, sizeof(corto), *c) == Esito::MALFORMATO);  // meno di 9 byte
    // Solo la configurazione, giusta: niente audio.
    const uint8_t sola[] = {0x09, 0x02, 0x09, 0x00, 0x00, 0x01, 0x00, 0x80, 0x32};
    CHECK(analizza(sola, sizeof(sola), *c) == Esito::NON_AUDIO);
    // Un byte in piu' contato da wTotalLength: il descrittore dopo esce dalla fine.
    const uint8_t coda[] = {0x09, 0x02, 0x0A, 0x00, 0x00, 0x01, 0x00, 0x80, 0x32, 0x05};
    CHECK(analizza(coda, sizeof(coda), *c) == Esito::MALFORMATO);
    // Un descrittore da due byte (solo bLength e tipo) va benissimo.
    const uint8_t due[] = {0x09, 0x02, 0x0B, 0x00, 0x00, 0x01, 0x00, 0x80, 0x32, 0x02, 0x99};
    CHECK(analizza(due, sizeof(due), *c) == Esito::NON_AUDIO);
}

TEST(uac_non_audio) {
    auto c = nuova();
    // Solo HID (una tastiera).
    Costruttore k;
    k.interfaccia(0, 0, 1, 0x03, 0x01, 0x01).d({0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, 0x3F, 0x00});
    k.ep(0x81, 0x03, 8, 10);
    CHECK(leggi(k.fine(), *c) == Esito::NON_AUDIO);
    // L'adattatore Apple con le interfacce audio diventate "vendor specific" (0xFF).
    std::vector<uint8_t> d(veri::APPLE_110A, veri::APPLE_110A + sizeof(veri::APPLE_110A));
    for (size_t i = 0; i < d.size(); i += d[i])
        if (d[i + 1] == 0x04 && d[i + 5] == 0x01) d[i + 5] = 0xFF;
    CHECK(leggi(d, *c) == Esito::NON_AUDIO);
    // Streaming senza AudioControl, o un'AudioControl solo con alt 1: non e' una funzione audio.
    Costruttore s;
    s.interfaccia(1, 0, 0, 0x01, 0x02, 0x00).interfaccia(1, 1, 1, 0x01, 0x02, 0x00).general1(1);
    s.formato1(2, 2, 16, {48000}).ep1(0x01, 0x09, 192);
    s.interfaccia(0, 1, 0, 0x01, 0x01, 0x00).header1({1});
    CHECK(leggi(s.fine(), *c) == Esito::NON_AUDIO);
    // Un'AudioControl con il descrittore d'interfaccia corto (8 byte) non si vede.
    Costruttore t;
    t.d({0x08, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00});
    CHECK(leggi(t.fine(), *c) == Esito::NON_AUDIO);
}

TEST(uac_solo_uac3) {
    auto c = nuova();
    // L'Apple con l'AudioControl a protocollo 0x30 (come le sue configurazioni 2 e 3).
    std::vector<uint8_t> d(veri::APPLE_110A, veri::APPLE_110A + sizeof(veri::APPLE_110A));
    CHECK_EQ(d[17 + 7], 0x20);  // interfaccia 0, bInterfaceProtocol
    d[17 + 7] = 0x30;
    CHECK(leggi(d, *c) == Esito::SOLO_UAC3);
    CHECK(aiuti::controllaConfigurazione(*c, Esito::SOLO_UAC3).empty());
    // UAC3 e poi UAC2: vale la UAC2.
    Costruttore k;
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x30).interfaccia(1, 0, 0, 0x01, 0x02, 0x30);
    k.interfaccia(1, 1, 1, 0x01, 0x02, 0x30).ep(0x01, 0x0D, 192);
    funzione2(k, 2, 3);
    CHECK(leggi(k.fine(), *c) == Esito::OK);
    CHECK_EQ(c->interfacciaControllo, 2);
    CHECK(c->versione == Versione::UAC2);
    CHECK_EQ(c->nStreaming, 1);
    CHECK_EQ(c->streaming[0].interfaccia, 3);
}

TEST(uac_solo_microfono) {
    auto c = nuova();
    // Un microfono USB UAC1: IT microfono -> OT USB streaming, streaming in ingresso.
    Costruttore k;
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0201, 1).ot1(2, 0x0101, 1);
    k.interfaccia(1, 0, 0, 0x01, 0x02, 0x00).interfaccia(1, 1, 1, 0x01, 0x02, 0x00).general1(2);
    k.formato1(1, 2, 16, {48000}).ep1(0x81, 0x05, 96).csep1(0x01);
    CHECK(leggi(k.fine(), *c) == Esito::NESSUNA_USCITA);
    CHECK(aiuti::controllaConfigurazione(*c, Esito::NESSUNA_USCITA).empty());
    // L'Apple con l'endpoint delle cuffie girato in ingresso.
    std::vector<uint8_t> d(veri::APPLE_110A, veri::APPLE_110A + sizeof(veri::APPLE_110A));
    int girati = 0;
    for (size_t i = 0; i < d.size(); i += d[i])
        if (d[i + 1] == 0x05 && d[i + 2] == 0x02) {
            d[i + 2] = 0x82;
            girati++;
        }
    CHECK_EQ(girati, 2);
    CHECK(leggi(d, *c) == Esito::NESSUNA_USCITA);
    // scegli() su una configurazione senza streaming.
    c->nStreaming = 0;
    Scelta sc;
    CHECK(scegli(*c, 0, sc) == Esito::NESSUNA_USCITA);
}

TEST(uac_pacchetti_grandi) {
    auto c = nuova();
    Scelta sc;
    for (uint16_t mps : {uint16_t(600), uint16_t(601), uint16_t(768), uint16_t(1023)}) {
        Costruttore k;
        funzione1(k, 0, 1, mps);
        CHECK(leggi(k.fine(), *c) == Esito::OK);
        const Esito e = scegli(*c, 0, sc);
        CHECK(e == (mps <= MAX_PACCHETTO ? Esito::OK : Esito::PACCHETTI_GRANDI));
    }
    // Troppo grande ma anche inadatta (8 bit): FORMATO, non PACCHETTI_GRANDI.
    Costruttore k;
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101).ot1(3, 0x0302, 1);
    k.interfaccia(1, 0, 0, 0x01, 0x02, 0x00);
    altUscita1(k, 1, 1, 1, 768, 1, 8);
    CHECK(leggi(k.fine(), *c) == Esito::OK);
    CHECK(scegli(*c, 0, sc) == Esito::FORMATO);
}

TEST(uac_troppi) {
    // Piu' di ogni MAX_*: 1 + 40 + 2 entita' (oltre 32), un mixer da 12 ingressi (oltre 8), una Feature Unit da 20
    // canali (oltre 15), 5 streaming in ingresso prima di quello in uscita (oltre 4), 10 alternative (oltre 8),
    // 15 frequenze (oltre 10) con 44100 e 48000 in fondo. Si tiene il massimo, troncata = true, e lo streaming in
    // uscita e le due frequenze buone non si perdono.
    Costruttore k;
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1, 2, 3, 4, 5, 6});
    // MIXER_UNIT 60 (UAC1, 10 + 12 + 1 byte): 12 ingressi, 2 canali, wChannelConfig, iChannelNames, bmControls,
    // iMixer.
    k.d({0x17, 0x24, 0x04, 0x3C, 0x0C, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00});
    std::vector<uint8_t> fu = {uint8_t(7 + 21), 0x24, 0x06, 0x3D, 0x01, 0x01};  // FU 61 da 20 canali + master
    for (int i = 0; i < 21; i++) fu.push_back(0x03);
    fu.push_back(0x00);
    k.d(fu);
    k.it1(1, 0x0101);
    for (uint8_t id = 10; id < 50; id++) k.fu1(id, uint8_t(id == 10 ? 1 : id - 1), {0x01, 0x02, 0x02});
    k.ot1(2, 0x0302, 49);
    for (uint8_t i = 1; i <= 5; i++) {
        k.interfaccia(i, 0, 0, 0x01, 0x02, 0x00).interfaccia(i, 1, 1, 0x01, 0x02, 0x00).general1(1);
        k.formato1(1, 2, 16, {48000}).ep1(uint8_t(0x80 | i), 0x05, 96);
    }
    k.interfaccia(6, 0, 0, 0x01, 0x02, 0x00);
    for (uint8_t a = 1; a <= 10; a++) {
        k.interfaccia(6, a, 1, 0x01, 0x02, 0x00).general1(1);
        k.formato1(2, 2, 16, {8000, 11025, 12000, 16000, 22050, 24000, 32000, 64000, 88200, 96000, 176400, 192000,
                              44100, 384000, 48000});
        k.ep1(0x06, 0x09, uint16_t(190 + a * 2)).csep1(0x01);
    }
    auto c = nuova();
    CHECK(leggi(k.fine(), *c) == Esito::OK);
    CHECK(aiuti::controllaConfigurazione(*c, Esito::OK).empty());
    CHECK(c->troncata);
    CHECK_EQ(c->nEntita, uint8_t(MAX_ENTITA));
    CHECK_EQ(c->nStreaming, uint8_t(MAX_STREAMING));
    const Entita* mix = trovaEntita(*c, 60);
    CHECK(mix && mix->nSorgenti == MAX_SORGENTI && mix->sorgenti[7] == 8);
    const Entita* grande = trovaEntita(*c, 61);
    CHECK(grande && grande->canali == MAX_CANALI_FU && grande->muto == 0xFFFF && grande->volume == 0xFFFF);
    const Streaming* uscita = nullptr;
    for (uint8_t i = 0; i < c->nStreaming; i++)
        if (c->streaming[i].uscita) uscita = &c->streaming[i];
    CHECK(uscita != nullptr);
    if (uscita) {
        CHECK_EQ(uscita->interfaccia, 6);
        CHECK_EQ(uscita->nAlt, uint8_t(MAX_ALT));
        CHECK_EQ(uscita->alt[0].frequenze.n, uint8_t(MAX_FREQ));
        CHECK(uscita->alt[0].frequenze.contiene(48000));
        CHECK(uscita->alt[0].frequenze.contiene(44100));
    }
    Scelta sc;
    CHECK(scegli(*c, 0, sc) == Esito::OK);
    CHECK_EQ(sc.interfaccia, 6);
    CHECK_EQ(sc.alt, 1);  // 16 bit stereo 48 kHz tutte: vince il pacchetto piu' piccolo, 192 B
    CHECK_EQ(sc.maxPacchetto, 192);
    CHECK_EQ(sc.hz, uint32_t(48000));
    // L'Output Terminal e' oltre le 32 entita': nessuna strada, ma nessun problema.
    Percorso p;
    CHECK(!percorso(*c, 1, p));
}

TEST(uac_strada_lunga) {
    // IT 1 -> 30 Feature Unit in fila -> OT 2: 32 entita' esatte, la strada piu' lunga possibile.
    Costruttore k;
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101);
    for (uint8_t id = 10; id < 40; id++) k.fu1(id, uint8_t(id == 10 ? 1 : id - 1), {0x01});
    k.ot1(2, 0x0301, 39).interfaccia(1, 0, 0, 0x01, 0x02, 0x00);
    altUscita1(k, 1, 1, 1, 192);
    auto c = nuova();
    CHECK(leggi(k.fine(), *c) == Esito::OK);
    CHECK_EQ(c->nEntita, 32);
    CHECK(!c->troncata);
    Percorso p;
    CHECK(percorso(*c, 1, p));
    CHECK_EQ(p.terminaleUscita, 2);
    CHECK_EQ(p.featureUnit, 10);  // la piu' vicina al terminale streaming
}

TEST(uac_iad_e_posizione) {
    auto c = nuova();
    // UAC2 senza IAD: gli streaming della funzione sono quelli dopo la sua AudioControl e prima della prossima.
    Costruttore k;
    funzione2(k, 0, 1);
    funzione2(k, 2, 3);
    CHECK(leggi(k.fine(), *c) == Esito::OK);
    CHECK_EQ(c->interfacciaControllo, 0);
    CHECK_EQ(c->nStreaming, 1);
    CHECK_EQ(c->streaming[0].interfaccia, 1);
    // La prima funzione ha solo un ingresso: vale la seconda.
    Costruttore j;
    funzione2(j, 0, 1, false);
    funzione2(j, 2, 3, true);
    CHECK(leggi(j.fine(), *c) == Esito::OK);
    CHECK_EQ(c->interfacciaControllo, 2);
    CHECK_EQ(c->streaming[0].interfaccia, 3);
    // Un IAD che contiene solo l'AudioControl: lo streaming non e' della funzione.
    Costruttore h;
    h.iad(0, 1);
    funzione2(h, 0, 1);
    CHECK(leggi(h.fine(), *c) == Esito::NESSUNA_USCITA);
    // Un IAD che parte dall'interfaccia dopo l'AudioControl (non la contiene, come il NuForce UDH-100 in Linux):
    // si va per posizione.
    Costruttore g;
    g.iad(1, 2);
    funzione2(g, 0, 1);
    CHECK(leggi(g.fine(), *c) == Esito::OK);
    CHECK_EQ(c->streaming[0].interfaccia, 1);
    // Un IAD giusto che include anche un'interfaccia HID: si tengono solo gli streaming audio.
    Costruttore f;
    f.iad(0, 3);
    funzione2(f, 0, 1);
    f.interfaccia(2, 0, 1, 0x03, 0x00, 0x00).ep(0x82, 0x03, 8, 10);
    CHECK(leggi(f.fine(), *c) == Esito::OK);
    CHECK_EQ(c->nStreaming, 1);
}

TEST(uac_uac1_ordine) {
    auto c = nuova();
    // Lo streaming prima dell'AudioControl: si trova lo stesso (baInterfaceNr).
    Costruttore k;
    k.interfaccia(1, 0, 0, 0x01, 0x02, 0x00);
    altUscita1(k, 1, 1, 1, 192);
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101).ot1(3, 0x0302, 1);
    CHECK(leggi(k.fine(), *c) == Esito::OK);
    CHECK_EQ(c->streaming[0].interfaccia, 1);
    Percorso p;
    CHECK(percorso(*c, 1, p));
    CHECK_EQ(p.featureUnit, 0);  // nessuna Feature Unit: va bene lo stesso
    // L'header con un'interfaccia che non c'e', e un bInCollection piu' lungo del descrittore.
    Costruttore j;
    j.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).d({0x09, 0x24, 0x01, 0x00, 0x01, 0x00, 0x00, 0x05, 0x01});
    j.it1(1, 0x0101).ot1(3, 0x0302, 1).interfaccia(1, 0, 0, 0x01, 0x02, 0x00);
    altUscita1(j, 1, 1, 1, 192);
    CHECK(leggi(j.fine(), *c) == Esito::OK);
    // Senza header: nessuno streaming e' della funzione.
    Costruttore h;
    h.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).it1(1, 0x0101).interfaccia(1, 0, 0, 0x01, 0x02, 0x00);
    altUscita1(h, 1, 1, 1, 192);
    CHECK(leggi(h.fine(), *c) == Esito::NESSUNA_USCITA);
    // Un'AudioControl con protocollo sconosciuto (0x10) si legge come UAC1.
    Costruttore g;
    funzione1(g, 0, 1);
    std::vector<uint8_t> d = g.fine();
    d[9 + 7] = 0x10;
    CHECK(leggi(d, *c) == Esito::OK);
    CHECK(c->versione == Versione::UAC1);
}

TEST(uac_endpoint_strani) {
    auto c = nuova();
    Scelta sc;
    // Endpoint bulk: non e' audio isocrono, l'alternativa non si tiene.
    Costruttore k;
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101).ot1(3, 0x0302, 1);
    k.interfaccia(1, 0, 0, 0x01, 0x02, 0x00).interfaccia(1, 1, 1, 0x01, 0x02, 0x00).general1(1);
    k.formato1(2, 2, 16, {48000}).ep1(0x01, 0x02, 64);
    CHECK(leggi(k.fine(), *c) == Esito::NESSUNA_USCITA);
    // Endpoint numero 0: non vale.
    Costruttore j;
    j.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101).ot1(3, 0x0302, 1);
    j.interfaccia(1, 0, 0, 0x01, 0x02, 0x00).interfaccia(1, 1, 1, 0x01, 0x02, 0x00).general1(1);
    j.formato1(2, 2, 16, {48000}).ep1(0x00, 0x09, 192);
    CHECK(leggi(j.fine(), *c) == Esito::NESSUNA_USCITA);
    // UAC1 con l'endpoint da 7 byte (senza bRefresh e bSynchAddress) e wMaxPacketSize con i bit 11-12 accesi.
    Costruttore h;
    h.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101).ot1(3, 0x0302, 1);
    h.interfaccia(1, 0, 0, 0x01, 0x02, 0x00).interfaccia(1, 1, 1, 0x01, 0x02, 0x00).general1(1);
    h.formato1(2, 2, 16, {48000}).ep(0x01, 0x05, 0x09C0).csep1(0x01);
    CHECK(leggi(h.fine(), *c) == Esito::OK);
    CHECK_EQ(c->streaming[0].alt[0].maxPacchetto, 0x1C0);
    CHECK_EQ(c->streaming[0].alt[0].endpointFeedback, 0);
    CHECK(c->streaming[0].alt[0].frequenzaSuEndpoint);
    // L'alternativa 0 con un endpoint (vietato dalle specifiche): non si tiene.
    Costruttore g;
    g.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101).ot1(3, 0x0302, 1);
    g.interfaccia(1, 0, 1, 0x01, 0x02, 0x00).general1(1).formato1(2, 2, 16, {48000}).ep1(0x01, 0x09, 192);
    CHECK(leggi(g.fine(), *c) == Esito::NESSUNA_USCITA);
    // Un descrittore d'interfaccia corto dopo un'alternativa buona: l'endpoint che lo segue non e' di nessuno.
    Costruttore f;
    funzione1(f, 0, 1);
    f.d({0x08, 0x04, 0x01, 0x02, 0x01, 0x01, 0x02, 0x00}).ep1(0x02, 0x09, 288);
    CHECK(leggi(f.fine(), *c) == Esito::OK);
    CHECK_EQ(c->streaming[0].nAlt, 1);
    CHECK_EQ(c->streaming[0].alt[0].endpoint, 0x01);
    // UAC2: feedback IN (uso 01) prima dell'endpoint dei dati, poi un secondo endpoint dei dati (ignorato).
    Costruttore e;
    e.interfaccia(0, 0, 0, 0x01, 0x01, 0x20).header2().clock2(9).it2(1, 0x0101, 9).ot2(3, 0x0302, 1, 9);
    e.interfaccia(1, 0, 0, 0x01, 0x02, 0x20).interfaccia(1, 1, 3, 0x01, 0x02, 0x20).general2(1, 2).formato2(2, 16);
    e.ep(0x81, 0x11, 4, 4).ep(0x01, 0x05, 392).csep2().ep(0x02, 0x05, 100);
    CHECK(leggi(e.fine(), *c) == Esito::OK);
    const Alternativa& a = c->streaming[0].alt[0];
    CHECK_EQ(a.endpoint, 0x01);
    CHECK_EQ(a.maxPacchetto, 392);
    CHECK(a.sync == Sync::ASINCRONA);
    CHECK_EQ(a.endpointFeedback, 0x81);
    CHECK_EQ(a.intervalloFeedback, 4);
    CHECK(!a.frequenzaSuEndpoint);
}

TEST(uac_formati_strani) {
    auto c = nuova();
    Scelta sc;
    // UAC1: wFormatTag non PCM (0x0002, PCM8), o FORMAT_TYPE di tipo II: non PCM.
    Costruttore k;
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101).ot1(3, 0x0302, 1);
    k.interfaccia(1, 0, 0, 0x01, 0x02, 0x00).interfaccia(1, 1, 1, 0x01, 0x02, 0x00).general1(1, 0x0002);
    k.formato1(2, 2, 16, {48000}).ep1(0x01, 0x09, 192);
    k.interfaccia(1, 2, 1, 0x01, 0x02, 0x00).general1(1);
    k.d({0x0E, 0x24, 0x02, 0x02, 0x00, 0x01, 0x00, 0x04, 0x01, 0x80, 0xBB, 0x00, 0x00, 0x00});  // tipo II
    k.ep1(0x01, 0x09, 192);
    CHECK(leggi(k.fine(), *c) == Esito::OK);
    CHECK_EQ(c->streaming[0].nAlt, 2);
    CHECK(!c->streaming[0].alt[0].pcm);
    CHECK(!c->streaming[0].alt[1].pcm);
    CHECK(scegli(*c, 0, sc) == Esito::FORMATO);
    // UAC1 con frequenza continua 8-96 kHz (bSamFreqType 0).
    Costruttore j;
    j.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101).ot1(3, 0x0302, 1);
    j.interfaccia(1, 0, 0, 0x01, 0x02, 0x00).interfaccia(1, 1, 1, 0x01, 0x02, 0x00).general1(1);
    j.d({0x0E, 0x24, 0x02, 0x01, 0x02, 0x02, 0x10, 0x00, 0x40, 0x1F, 0x00, 0x00, 0x77, 0x01});
    j.ep1(0x01, 0x09, 192).csep1(0x01);
    CHECK(leggi(j.fine(), *c) == Esito::OK);
    const Frequenze& f = c->streaming[0].alt[0].frequenze;
    CHECK_EQ(f.n, 1);
    CHECK(f.contiene(44100) && f.contiene(48000) && f.contiene(8000) && !f.contiene(7999));
    CHECK(scegli(*c, 0, sc) == Esito::OK);
    CHECK_EQ(sc.hz, uint32_t(48000));
    // UAC2: AS_GENERAL senza il bit PCM, o con bFormatType 2: non PCM.
    Costruttore h;
    h.interfaccia(0, 0, 0, 0x01, 0x01, 0x20).header2().clock2(9).it2(1, 0x0101, 9).ot2(3, 0x0302, 1, 9);
    h.interfaccia(1, 0, 0, 0x01, 0x02, 0x20).interfaccia(1, 1, 1, 0x01, 0x02, 0x20).general2(1, 2, 0x02);
    h.formato2(2, 16).ep(0x01, 0x0D, 192).interfaccia(1, 2, 1, 0x01, 0x02, 0x20).general2(1, 2, 1, 2);
    h.formato2(2, 16).ep(0x01, 0x0D, 192).interfaccia(1, 3, 1, 0x01, 0x02, 0x20).general2(1, 2);
    h.formato2(2, 16).ep(0x01, 0x0D, 192);
    CHECK(leggi(h.fine(), *c) == Esito::OK);
    CHECK_EQ(c->streaming[0].nAlt, 3);
    CHECK(!c->streaming[0].alt[0].pcm);
    CHECK(!c->streaming[0].alt[1].pcm);
    CHECK(c->streaming[0].alt[2].pcm);
}

TEST(uac_feature_strane) {
    auto c = nuova();
    Costruttore k;
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101);
    k.d({0x07, 0x24, 0x06, 0x05, 0x01, 0x00, 0x00});                          // bControlSize 0
    k.d({0x0D, 0x24, 0x06, 0x06, 0x05, 0x02, 0x03, 0x00, 0x02, 0x01, 0x02, 0x00, 0x00});  // bControlSize 2
    k.d({0x06, 0x24, 0x06, 0x07, 0x06, 0x01});                                // troppo corta per i controlli
    k.ot1(3, 0x0302, 7).interfaccia(1, 0, 0, 0x01, 0x02, 0x00);
    altUscita1(k, 1, 1, 1, 192);
    CHECK(leggi(k.fine(), *c) == Esito::OK);
    const Entita* a = trovaEntita(*c, 5);
    CHECK(a && a->canali == 0 && a->muto == 0 && a->volume == 0 && a->nSorgenti == 1 && a->sorgenti[0] == 1);
    const Entita* b = trovaEntita(*c, 6);
    CHECK(b && b->canali == 2 && b->muto == 0x0001 && b->volume == 0x0007);
    const Entita* d = trovaEntita(*c, 7);
    CHECK(d && d->canali == 0 && d->sorgenti[0] == 6);
    Percorso p;
    CHECK(percorso(*c, 1, p));
    CHECK_EQ(p.featureUnit, 5);  // 3 <- 7 <- 6 <- 5 <- 1: la piu' vicina al terminale
    // UAC2: controlli solo leggibili (0b01) o non validi (0b10) non sono impostabili; 20 canali: si tronca.
    Costruttore j;
    j.interfaccia(0, 0, 0, 0x01, 0x01, 0x20).header2().clock2(9).it2(1, 0x0101, 9);
    j.fu2(2, 1, {0x1, 0x4, 0x2, 0x8, 0xF});
    std::vector<uint8_t> fu = {uint8_t(6 + 4 * 21), 0x24, 0x06, 0x04, 0x02};
    for (int i = 0; i < 21; i++) fu.insert(fu.end(), {0x0F, 0x00, 0x00, 0x00});
    fu.push_back(0x00);
    j.d(fu).d({0x05, 0x24, 0x06, 0x08, 0x04}).ot2(3, 0x0302, 8, 9);
    j.interfaccia(1, 0, 0, 0x01, 0x02, 0x20).interfaccia(1, 1, 1, 0x01, 0x02, 0x20).general2(1, 2);
    j.formato2(2, 16).ep(0x01, 0x0D, 192);
    CHECK(leggi(j.fine(), *c) == Esito::OK);
    CHECK(c->troncata);
    const Entita* e = trovaEntita(*c, 2);
    CHECK(e && e->canali == 4 && e->muto == 0x0010 && e->volume == 0x0010);
    const Entita* g = trovaEntita(*c, 4);
    CHECK(g && g->canali == MAX_CANALI_FU && g->muto == 0xFFFF && g->volume == 0xFFFF);
    const Entita* h = trovaEntita(*c, 8);
    CHECK(h && h->canali == 0 && h->sorgenti[0] == 4);
}

TEST(uac_percorso_casi) {
    auto c = nuova();
    Percorso p;
    // Un giro senza uscita: OT 4 <- FU 2 <- FU 3 <- FU 2. Non si blocca, nessuna strada.
    Costruttore k;
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101);
    k.fu1(2, 3, {0x01}).fu1(3, 2, {0x01}).ot1(4, 0x0302, 2).interfaccia(1, 0, 0, 0x01, 0x02, 0x00);
    altUscita1(k, 1, 1, 1, 192);
    CHECK(leggi(k.fine(), *c) == Esito::OK);
    CHECK(!percorso(*c, 1, p));
    // Un giro con l'uscita: OT 4 <- MIXER 5 <- (FU 6 <- MIXER 5, IT 1). Strada senza Feature Unit.
    Costruttore j;
    j.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101);
    j.d({0x0D, 0x24, 0x04, 0x05, 0x02, 0x06, 0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00});
    j.fu1(6, 5, {0x01}).ot1(4, 0x0302, 5).interfaccia(1, 0, 0, 0x01, 0x02, 0x00);
    altUscita1(j, 1, 1, 1, 192);
    CHECK(leggi(j.fine(), *c) == Esito::OK);
    CHECK(percorso(*c, 1, p));
    CHECK_EQ(p.terminaleUscita, 4);
    CHECK_EQ(p.featureUnit, 0);
    // Due uscite: OT 7 (altoparlante) direttamente dall'IT, OT 8 (cuffie) con una Feature Unit: vince la 8.
    // Con OT 7 da solo, vince la 7 senza Feature Unit.
    Costruttore h;
    h.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101).ot1(7, 0x0301, 1);
    h.fu1(9, 1, {0x01, 0x02, 0x02}).ot1(8, 0x0302, 9).interfaccia(1, 0, 0, 0x01, 0x02, 0x00);
    altUscita1(h, 1, 1, 1, 192);
    CHECK(leggi(h.fine(), *c) == Esito::OK);
    CHECK(percorso(*c, 1, p));
    CHECK_EQ(p.terminaleUscita, 8);
    CHECK_EQ(p.featureUnit, 9);
    c->entita[3].sorgenti[0] = 99;  // OT 8 <- 99, che non c'e'
    CHECK(percorso(*c, 1, p));
    CHECK_EQ(p.terminaleUscita, 7);
    CHECK_EQ(p.featureUnit, 0);
    // Il terminale non c'e', o non e' un Input Terminal, o arriva solo a un Output Terminal USB streaming.
    CHECK(!percorso(*c, 50, p));
    CHECK(!percorso(*c, 7, p));
    CHECK(!percorso(*c, 0, p));
    Costruttore g;
    g.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101).ot1(2, 0x0101, 1);
    g.interfaccia(1, 0, 0, 0x01, 0x02, 0x00);
    altUscita1(g, 1, 1, 1, 192);
    CHECK(leggi(g.fine(), *c) == Esito::OK);
    CHECK(!percorso(*c, 1, p));
    // UAC2: clock riempito anche senza strada; un clock come sorgente audio non fa strada.
    Costruttore f;
    f.interfaccia(0, 0, 0, 0x01, 0x01, 0x20).header2().clock2(9).it2(1, 0x0101, 9).ot2(3, 0x0302, 9, 9);
    f.interfaccia(1, 0, 0, 0x01, 0x02, 0x20).interfaccia(1, 1, 1, 0x01, 0x02, 0x20).general2(1, 2);
    f.formato2(2, 16).ep(0x01, 0x0D, 192);
    CHECK(leggi(f.fine(), *c) == Esito::OK);
    CHECK(!percorso(*c, 1, p));
    CHECK_EQ(p.clock, 9);
}

namespace {
struct Domande {
    int n = 0;
    uint8_t pin = 0;
};
uint8_t conta(void* ctx, uint8_t) {
    Domande* d = static_cast<Domande*>(ctx);
    d->n++;
    return d->pin;
}
}  // namespace

TEST(uac_clock_casi) {
    Costruttore k;
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x20).header2().clock2(9).clock2(10);
    k.selettore2(11, {12}).selettore2(12, {11});     // giro fra due selettori
    k.moltiplicatore2(13, 9);                        // moltiplicatore -> sorgente
    k.moltiplicatore2(14, 14);                       // moltiplicatore su se stesso
    k.selettore2(15, {9, 10}).moltiplicatore2(16, 15);  // moltiplicatore -> selettore -> sorgente
    k.selettore2(17, {}).selettore2(18, {99});       // nessun ingresso; ingresso che non c'e'
    k.it2(1, 0x0101, 16).ot2(3, 0x0302, 1, 16).interfaccia(1, 0, 0, 0x01, 0x02, 0x20);
    k.interfaccia(1, 1, 1, 0x01, 0x02, 0x20).general2(1, 2).formato2(2, 16).ep(0x01, 0x0D, 192);
    auto c = nuova();
    CHECK(leggi(k.fine(), *c) == Esito::OK);
    Domande d;
    CHECK_EQ(sorgenteClock(*c, 11, conta, &d), 0);
    CHECK_EQ(d.n, 2);  // una domanda per selettore, poi il giro si ferma
    CHECK_EQ(sorgenteClock(*c, 13), 9);
    CHECK_EQ(sorgenteClock(*c, 14), 0);
    d.pin = 2;
    CHECK_EQ(sorgenteClock(*c, 16, conta, &d), 10);
    CHECK_EQ(sorgenteClock(*c, 16), 9);
    CHECK_EQ(sorgenteClock(*c, 17), 0);
    CHECK_EQ(sorgenteClock(*c, 18), 0);
    CHECK_EQ(sorgenteClock(*c, 1), 0);  // un terminale non e' un clock
    CHECK_EQ(sorgenteClock(*c, 0), 0);
    CHECK_EQ(sorgenteClock(*c, 77), 0);
    Percorso p;
    CHECK(percorso(*c, 1, p));
    CHECK_EQ(p.clock, 16);
    // La simulazione di apri(): il clock del terminale passa per il selettore; GET RANGE solo 44,1 kHz.
    Scelta sc;
    CHECK(aiuti::simulaApri(*c, 0, aiuti::rangeHz({44100, 44100, 0}), sc) == Esito::OK);
    CHECK_EQ(sc.hz, uint32_t(44100));
}

namespace {
Alternativa alt(uint8_t numero, uint8_t canali, uint8_t byte, uint8_t bit, std::initializer_list<uint32_t> hz,
                uint16_t mps, Sync sync = Sync::SINCRONA, uint8_t endpoint = 0x01, uint8_t intervallo = 1) {
    Alternativa a;
    a.numero = numero;
    a.terminale = 1;
    a.pcm = true;
    a.canali = canali;
    a.byteCampione = byte;
    a.bit = bit;
    for (uint32_t f : hz) a.frequenze.aggiungi(f, f);
    a.endpoint = endpoint;
    a.maxPacchetto = mps;
    a.intervallo = intervallo;
    a.sync = sync;
    return a;
}

// Una configurazione con un solo streaming (interfaccia 1) e queste alternative.
Configurazione& con(Configurazione& c, std::initializer_list<Alternativa> alternative) {
    c.versione = Versione::UAC1;
    c.nEntita = 0;
    c.nStreaming = 1;
    c.troncata = false;
    c.streaming[0].interfaccia = 1;
    c.streaming[0].uscita = true;
    c.streaming[0].nAlt = 0;
    for (const Alternativa& a : alternative) c.streaming[0].alt[c.streaming[0].nAlt++] = a;
    return c;
}
}  // namespace

TEST(uac_scegli_ordine) {
    auto p = nuova();
    Configurazione& c = *p;
    Scelta s;
    // 1. Stereo prima di mono, anche se il mono e' 16 bit 48 kHz e lo stereo 24 bit 44,1 kHz.
    CHECK(scegli(con(c, {alt(1, 1, 2, 16, {48000}, 96), alt(2, 2, 3, 24, {44100}, 270)}), 0, s) == Esito::OK);
    CHECK_EQ(s.alt, 2);
    CHECK_EQ(s.hz, uint32_t(44100));
    // 2. Poi 16 bit prima di 24 e di 32, anche a 44,1 kHz contro 48.
    CHECK(scegli(con(c, {alt(1, 2, 4, 32, {48000}, 384), alt(2, 2, 3, 24, {48000}, 288),
                         alt(3, 2, 2, 16, {44100}, 180)}),
                 0, s) == Esito::OK);
    CHECK_EQ(s.alt, 3);
    CHECK(scegli(con(c, {alt(1, 2, 4, 32, {48000}, 384), alt(2, 2, 3, 24, {48000}, 288)}), 0, s) == Esito::OK);
    CHECK_EQ(s.alt, 2);
    // 3. Poi 48 kHz prima di 44,1, anche se nella stessa alternativa.
    CHECK(scegli(con(c, {alt(1, 2, 2, 16, {44100}, 192), alt(2, 2, 2, 16, {44100, 48000}, 192)}), 0, s) ==
          Esito::OK);
    CHECK_EQ(s.alt, 2);
    CHECK_EQ(s.hz, uint32_t(48000));
    // 4. Poi sincrono o adattivo prima di asincrono (sincrono e adattivo pari: vince il primo).
    CHECK(scegli(con(c, {alt(1, 2, 2, 16, {48000}, 192, Sync::ASINCRONA), alt(2, 2, 2, 16, {48000}, 196,
                                                                               Sync::ADATTIVA)}),
                 0, s) == Esito::OK);
    CHECK_EQ(s.alt, 2);
    CHECK(scegli(con(c, {alt(1, 2, 2, 16, {48000}, 192, Sync::ADATTIVA), alt(2, 2, 2, 16, {48000}, 192)}), 0, s) ==
          Esito::OK);
    CHECK_EQ(s.alt, 1);
    // 5. Poi il pacchetto piu' piccolo; 6. a pari merito il primo.
    CHECK(scegli(con(c, {alt(1, 2, 2, 16, {48000}, 288), alt(2, 2, 2, 16, {48000}, 192)}), 0, s) == Esito::OK);
    CHECK_EQ(s.alt, 2);
    CHECK(scegli(con(c, {alt(1, 2, 2, 16, {48000}, 192), alt(2, 2, 2, 16, {48000}, 192)}), 0, s) == Esito::OK);
    CHECK_EQ(s.alt, 1);
    CHECK_EQ(s.indiceAlt, 0);
    // Le esclusioni, una per volta: tutte FORMATO.
    const Alternativa no[] = {
        alt(1, 2, 2, 16, {48000}, 192, Sync::SINCRONA, 0x01, 2),  // bInterval 2
        alt(1, 2, 1, 8, {48000}, 96),                             // 8 bit
        alt(1, 3, 2, 16, {48000}, 288),                           // 3 canali
        alt(1, 0, 2, 16, {48000}, 192),                           // 0 canali
        alt(1, 2, 3, 32, {48000}, 288),                           // 32 bit in 3 byte
        alt(1, 2, 2, 12, {48000}, 192),                           // 12 bit
        alt(1, 2, 5, 32, {48000}, 480),                           // 5 byte per campione
        alt(1, 2, 2, 16, {48000}, 191),                           // un byte in meno di 48 x 4
        alt(1, 2, 2, 16, {44100}, 179),                           // 45 x 4 = 180 non ci sta
        alt(1, 2, 2, 16, {32000, 96000}, 384),                    // ne' 48 ne' 44,1 kHz
        alt(1, 2, 2, 16, {}, 192),                                // nessuna frequenza
        alt(1, 2, 2, 16, {48000}, 192, Sync::SINCRONA, 0x81),     // in ingresso
    };
    int esclusa = 0;
    for (const Alternativa& a : no) {
        const Esito e = scegli(con(c, {a}), 0, s);
        const bool ok = e == ((a.endpoint & 0x80) ? Esito::NESSUNA_USCITA : Esito::FORMATO);
        if (!ok) std::printf("\n    esclusione %d non rispettata", esclusa);
        CHECK(ok);
        esclusa++;
    }
    Alternativa nonPcm = alt(1, 2, 2, 16, {48000}, 192);
    nonPcm.pcm = false;
    CHECK(scegli(con(c, {nonPcm}), 0, s) == Esito::FORMATO);
    // I limiti giusti: 45 x 4 = 180 a 44,1 kHz; 48 x 3 x 2 = 288 a 24 bit; mono 16 bit 96 B; 600 B tondi.
    CHECK(scegli(con(c, {alt(1, 2, 2, 16, {44100}, 180)}), 0, s) == Esito::OK);
    CHECK(scegli(con(c, {alt(1, 2, 3, 24, {48000}, 288)}), 0, s) == Esito::OK);
    CHECK(scegli(con(c, {alt(1, 1, 2, 16, {48000}, 96)}), 0, s) == Esito::OK);
    CHECK(scegli(con(c, {alt(1, 2, 3, 24, {48000}, 600)}), 0, s) == Esito::OK);
    CHECK(scegli(con(c, {alt(1, 2, 3, 24, {48000}, 602)}), 0, s) == Esito::PACCHETTI_GRANDI);
    // QUIRK_SOLO_48K: un'alternativa solo a 44,1 kHz non va piu'.
    CHECK(scegli(con(c, {alt(1, 2, 2, 16, {44100}, 192)}), QUIRK_SOLO_48K, s) == Esito::FORMATO);
    // Piu' streaming: gli indici puntano a quello giusto.
    con(c, {alt(1, 1, 2, 16, {48000}, 96)});
    c.nStreaming = 2;
    c.streaming[1].interfaccia = 4;
    c.streaming[1].uscita = true;
    c.streaming[1].nAlt = 2;
    c.streaming[1].alt[0] = alt(1, 2, 3, 24, {48000}, 288);
    c.streaming[1].alt[1] = alt(5, 2, 2, 16, {48000}, 192, Sync::SINCRONA, 0x03);
    CHECK(scegli(c, 0, s) == Esito::OK);
    CHECK_EQ(s.indiceStreaming, 1);
    CHECK_EQ(s.indiceAlt, 1);
    CHECK_EQ(s.interfaccia, 4);
    CHECK_EQ(s.alt, 5);
    CHECK_EQ(s.endpoint, 0x03);
}

TEST(uac_range_frequenze) {
    Frequenze f;
    auto r = aiuti::rangeHz({48000, 48000, 0});
    CHECK(analizzaRangeFrequenze(r.data(), r.size(), f));
    CHECK_EQ(f.n, 1);
    CHECK(f.contiene(48000) && !f.contiene(44100));
    r = aiuti::rangeHz({44100, 44100, 0, 48000, 48000, 0});
    CHECK(analizzaRangeFrequenze(r.data(), r.size(), f));
    CHECK_EQ(f.n, 2);
    r = aiuti::rangeHz({32000, 96000, 16000});
    CHECK(analizzaRangeFrequenze(r.data(), r.size(), f));
    CHECK(f.contiene(48000) && !f.contiene(44100) && f.contiene(96000));
    // Rotte: nessuna voce, troppo corta per quello che dice, meno di 2 byte, niente buffer.
    r = aiuti::rangeHz({});
    CHECK(!analizzaRangeFrequenze(r.data(), r.size(), f));
    CHECK_EQ(f.n, 0);
    r = aiuti::rangeHz({44100, 44100, 0, 48000, 48000, 0});
    CHECK(!analizzaRangeFrequenze(r.data(), r.size() - 1, f));
    CHECK_EQ(f.n, 0);
    CHECK(!analizzaRangeFrequenze(r.data(), 1, f));
    CHECK(!analizzaRangeFrequenze(nullptr, 26, f));
    // Una voce con dMIN > dMAX si salta; se e' l'unica, nessuna voce.
    r = aiuti::rangeHz({48000, 44100, 0});
    CHECK(!analizzaRangeFrequenze(r.data(), r.size(), f));
    r = aiuti::rangeHz({48000, 44100, 0, 48000, 48000, 0});
    CHECK(analizzaRangeFrequenze(r.data(), r.size(), f));
    CHECK_EQ(f.n, 1);
    // 14 frequenze con 44100 e 48000 in fondo: se ne tengono 10, ma quelle due ci sono.
    r = aiuti::rangeHz({8000,   8000,   0, 11025,  11025,  0, 12000,  12000,  0, 16000, 16000, 0, 22050, 22050, 0,
                        24000,  24000,  0, 32000,  32000,  0, 64000,  64000,  0, 88200, 88200, 0, 96000, 96000, 0,
                        176400, 176400, 0, 192000, 192000, 0, 44100,  44100,  0, 48000, 48000, 0});
    CHECK(analizzaRangeFrequenze(r.data(), r.size(), f));
    CHECK_EQ(f.n, uint8_t(MAX_FREQ));
    CHECK(f.contiene(48000) && f.contiene(44100));
    // Memoria protetta: ogni lunghezza da 0 alla fine.
    for (size_t n = 0; n <= r.size(); n++) {
        const uint8_t* d = memoria().metti(r.data(), n, true);
        const bool ok = analizzaRangeFrequenze(d, n, f);
        CHECK(ok == (n == r.size()));
    }
}

TEST(uac_range_volume) {
    RangeVolume v;
    auto r = aiuti::rangeVolume({-15360, 0, 256});
    CHECK(analizzaRangeVolume(r.data(), r.size(), v));
    CHECK_EQ(v.min, -15360);
    CHECK_EQ(v.max, 0);
    CHECK_EQ(v.passo, 256);
    r = aiuti::rangeVolume({-6400, 1536, 256, -12800, -6400, 128});
    CHECK(analizzaRangeVolume(r.data(), r.size(), v));
    CHECK_EQ(v.min, -12800);
    CHECK_EQ(v.max, 1536);
    CHECK_EQ(v.passo, 256);  // il passo del primo intervallo
    r = aiuti::rangeVolume({});
    CHECK(!analizzaRangeVolume(r.data(), r.size(), v));
    r = aiuti::rangeVolume({-15360, 0, 256});
    CHECK(!analizzaRangeVolume(r.data(), r.size() - 1, v));
    CHECK_EQ(v.min, 0);
    CHECK_EQ(v.max, 0);
    CHECK(!analizzaRangeVolume(nullptr, 8, v));
    for (size_t n = 0; n <= r.size(); n++) {
        const uint8_t* d = memoria().metti(r.data(), n, true);
        CHECK(analizzaRangeVolume(d, n, v) == (n == r.size()));
    }
}

TEST(uac_volume_zero_db) {
    struct Caso {
        int16_t min, max, passo, atteso;
    };
    const Caso casi[] = {
        {-15360, 0, 256, 0},         // -60..0 dB a passi di 1 dB: 0 dB
        {-12800, 1536, 384, -128},   // a passi di 1,5 dB da -50 dB: -0,5 dB e' il piu' alto non sopra 0
        {-32768, 0, 256, 0},         // UAC1 con il minimo a 0x8000
        {-32768, 32767, 1, 0},       // tutto l'intervallo
        {-10000, -256, 100, -256},   // il massimo e' sotto 0 dB: il massimo
        {256, 1024, 256, 256},       // il minimo e' sopra 0 dB: il minimo
        {-15360, 0, 0, 0},           // passo sconosciuto
        {-15360, 768, -5, 0},        // passo senza senso
        {-100, 100, 7, -2},          // -100 + 14 x 7 = -2
        {0, 0, 0, 0},
    };
    for (const Caso& k : casi) {
        RangeVolume r;
        r.min = k.min;
        r.max = k.max;
        r.passo = k.passo;
        const int16_t v = volumeZeroDb(r);
        if (v != k.atteso) std::printf("\n    {%d, %d, %d}: %d invece di %d", k.min, k.max, k.passo, v, k.atteso);
        CHECK_EQ(v, k.atteso);
    }
}

TEST(uac_testo_stringa) {
    char b[64];
    auto prova = [&](const std::vector<uint8_t>& d, size_t cap) {
        return std::string(b, testoStringa(d.data(), d.size(), b, cap));
    };
    CHECK_EQ(prova(aiuti::stringa("USB-C to 3.5mm Headphone Jack Adapter"), sizeof(b)),
             std::string("USB-C to 3.5mm Headphone Jack Adapter"));
    CHECK_EQ(prova(aiuti::stringa("USB-C to 3.5mm Headphone Jack Adapter"), 33),
             std::string("USB-C to 3.5mm Headphone Jack Ad"));
    // Accenti (2 byte), euro (3 byte), coppia surrogata e surrogati soli ('?').
    CHECK_EQ(prova(aiuti::stringa({0x00C9, 'c', 'o', 'u', 't', 'e', 'u', 'r', 's', ' ', 0x00E0}), sizeof(b)),
             std::string("\xC3\x89""couteurs \xC3\xA0"));
    CHECK_EQ(prova(aiuti::stringa({'5', 0x20AC}), sizeof(b)), std::string("5\xE2\x82\xAC"));
    CHECK_EQ(prova(aiuti::stringa({'A', 0xD83C, 0xDFA7, 'B'}), sizeof(b)), std::string("A?B"));
    CHECK_EQ(prova(aiuti::stringa({'A', 0xD83C}), sizeof(b)), std::string("A?"));
    CHECK_EQ(prova(aiuti::stringa({0xDFA7, 'B'}), sizeof(b)), std::string("?B"));
    // Caratteri di controllo: spazi; spazi e zeri in fondo: via.
    CHECK_EQ(prova(aiuti::stringa({'A', 0x09, 'B', 0x7F, 'C', 0x85, 'D'}), sizeof(b)), std::string("A B C D"));
    CHECK_EQ(prova(aiuti::stringa({'A', 'B', ' ', ' ', 0, 0}), sizeof(b)), std::string("AB"));
    CHECK_EQ(prova(aiuti::stringa({' ', 0}), sizeof(b)), std::string(""));
    // Un carattere non si spezza: "aE'" (3 byte) con cap 3 e' solo "a".
    CHECK_EQ(prova(aiuti::stringa({'a', 0x00C9}), 3), std::string("a"));
    CHECK_EQ(prova(aiuti::stringa({'a', 0x00C9}), 4), std::string("a\xC3\x89"));
    CHECK_EQ(prova(aiuti::stringa({'a', 0x20AC}), 4), std::string("a"));
    CHECK_EQ(prova(aiuti::stringa({'a', 0x20AC}), 5), std::string("a\xE2\x82\xAC"));
    CHECK_EQ(prova(aiuti::stringa("abc"), 1), std::string(""));
    // Descrittori non validi: tipo sbagliato, bLength < 2, meno di 2 byte. bLength dispari: l'ultimo byte si
    // ignora; bLength oltre n: si legge fino a n.
    std::vector<uint8_t> d = aiuti::stringa("abc");
    d[1] = 0x02;
    CHECK_EQ(prova(d, sizeof(b)), std::string(""));
    d = aiuti::stringa("abc");
    d[0] = 1;
    CHECK_EQ(prova(d, sizeof(b)), std::string(""));
    CHECK_EQ(testoStringa(d.data(), 1, b, sizeof(b)), size_t(0));
    CHECK_EQ(testoStringa(nullptr, 8, b, sizeof(b)), size_t(0));
    CHECK_EQ(b[0], '\0');
    d = aiuti::stringa("abc");
    d[0] = 7;
    CHECK_EQ(prova(d, sizeof(b)), std::string("ab"));
    d[0] = 200;
    CHECK_EQ(prova(d, sizeof(b)), std::string("abc"));
    // Mai una scrittura oltre cap (l'uscita sta contro la pagina proibita), mai una lettura oltre n.
    const std::vector<uint8_t> lunga = aiuti::stringa({0x00C9, 0x20AC, 'x', 0xD83C, 0xDFA7, 0x00E0, 'y', 'z', 0x20AC});
    for (size_t cap = 0; cap <= 24; cap++) {
        for (size_t n = 0; n <= lunga.size(); n++) {
            char* uscita = reinterpret_cast<char*>(memoria().spazioInFondo(cap));
            static aiuti::Protetta ingresso(4096);
            const uint8_t* in = ingresso.metti(lunga.data(), n, true);
            const size_t k = testoStringa(in, n, cap ? uscita : nullptr, cap);
            CHECK(cap == 0 ? k == 0 : (k < cap && uscita[k] == '\0' && std::strlen(uscita) == k));
        }
    }
}

TEST(uac_quirk) {
    CHECK_EQ(trovaQuirk(0x04e8, 0xa051), uint32_t(QUIRK_PAUSA_5MS | QUIRK_NON_TOCCARE_SELETTORE));
    CHECK_EQ(trovaQuirk(0x05ac, 0x110a), uint32_t(QUIRK_SOLO_48K));
    CHECK_EQ(trovaQuirk(0x05ac, 0x110b), uint32_t(QUIRK_SOLO_48K));
    CHECK_EQ(trovaQuirk(0x05ac, 0x1234), uint32_t(0));  // altri prodotti Apple: niente
    CHECK_EQ(trovaQuirk(0x0d8c, 0x0014), uint32_t(QUIRK_PAUSA_1MS));
    CHECK_EQ(trovaQuirk(0x0d8c, 0x000c), uint32_t(0));
    CHECK_EQ(trovaQuirk(0x054c, 0x0b8c), uint32_t(QUIRK_INTERFACCIA_PRIMA));
    CHECK_EQ(trovaQuirk(0x2d95, 0x8011), uint32_t(QUIRK_PAUSA_1MS));
    CHECK_EQ(trovaQuirk(0x2fc6, 0xf06b), uint32_t(QUIRK_PAUSA_20MS));
    CHECK_EQ(trovaQuirk(0x046d, 0x0a8f), uint32_t(QUIRK_PAUSA_1MS));  // Logitech: tutto il produttore
    CHECK_EQ(trovaQuirk(0x047f, 0xc056), uint32_t(QUIRK_PAUSA_20MS));  // Plantronics
    CHECK_EQ(trovaQuirk(0x1235, 0x8210), uint32_t(QUIRK_NON_TOCCARE_SELETTORE));  // Focusrite
    CHECK_EQ(trovaQuirk(0x1234, 0x5678), uint32_t(0));
    CHECK_EQ(trovaQuirk(0x0000, 0x0000), uint32_t(0));
    CHECK_EQ(pausaRichiesteMs(0), uint32_t(0));
    CHECK_EQ(pausaRichiesteMs(QUIRK_PAUSA_1MS), uint32_t(1));
    CHECK_EQ(pausaRichiesteMs(QUIRK_PAUSA_5MS | QUIRK_NON_TOCCARE_SELETTORE), uint32_t(5));
    CHECK_EQ(pausaRichiesteMs(QUIRK_PAUSA_1MS | QUIRK_PAUSA_5MS), uint32_t(5));
    CHECK_EQ(pausaRichiesteMs(QUIRK_PAUSA_20MS | QUIRK_PAUSA_1MS), uint32_t(20));
    CHECK_EQ(pausaRichiesteMs(QUIRK_SOLO_48K), uint32_t(0));
}

TEST(uac_testo_esito) {
    CHECK_EQ(std::string(testoEsito(Esito::OK)), std::string("ok"));
    CHECK_EQ(std::string(testoEsito(Esito::MALFORMATO)), std::string("descrittore non valido"));
    CHECK_EQ(std::string(testoEsito(Esito::NON_AUDIO)), std::string("non audio"));
    CHECK_EQ(std::string(testoEsito(Esito::SOLO_UAC3)), std::string("solo USB Audio 3"));
    CHECK_EQ(std::string(testoEsito(Esito::NESSUNA_USCITA)), std::string("senza uscita audio"));
    CHECK_EQ(std::string(testoEsito(Esito::PACCHETTI_GRANDI)), std::string("pacchetti troppo grandi"));
    CHECK_EQ(std::string(testoEsito(Esito::FORMATO)), std::string("formato non adatto"));
    CHECK_EQ(std::string(testoEsito(Esito(99))), std::string("?"));
}

TEST(uac_descrivi_righe) {
    // Righe lunghe (molte frequenze) restano dentro il buffer; nessuna riga senza chi le riceve.
    Costruttore k;
    k.interfaccia(0, 0, 0, 0x01, 0x01, 0x00).header1({1}).it1(1, 0x0101).ot1(3, 0x0302, 1);
    k.interfaccia(1, 0, 0, 0x01, 0x02, 0x00).interfaccia(1, 1, 1, 0x01, 0x02, 0x00).general1(1);
    k.formato1(2, 2, 16, {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 96000});
    k.ep1(0x01, 0x05, 192, 1, 0, 0x81).csep1(0x01).ep1(0x81, 0x01, 3, 1, 3, 0);
    auto c = nuova();
    CHECK(leggi(k.fine(), *c) == Esito::OK);
    const std::vector<std::string> r = aiuti::righe(*c);
    CHECK_EQ(r.size(), size_t(2));
    for (const std::string& s : r) CHECK(s.size() < 200);
    if (r.size() == 2) {
        CHECK(r[1].find("8000, 11025, 12000") != std::string::npos);
        CHECK(r[1].find("feedback 0x81") != std::string::npos);
        CHECK(r[1].find("asincrono") != std::string::npos);
    }
    descrivi(*c, nullptr, nullptr);
    // Intervalli: ogni 2^(bInterval-1) ms; 0 e oltre 16 come numero; intervallo a passi.
    c->streaming[0].alt[0].intervallo = 4;
    c->streaming[0].alt[0].frequenze.n = 0;
    c->streaming[0].alt[0].frequenze.aggiungi(32000, 96000, 16000);
    CHECK(aiuti::righe(*c)[1].find("32000-96000/16000 Hz, ep 0x01 192 B ogni 8 ms") != std::string::npos);
    c->streaming[0].alt[0].intervallo = 0;
    CHECK(aiuti::righe(*c)[1].find("bInterval 0") != std::string::npos);
    c->troncata = true;
    CHECK(aiuti::righe(*c)[0].find(", troncata") != std::string::npos);
}
