// Test sulla rete simulata.
//
//   B ─┐
//      A ── R1 ── R2 ── R3 ── C        (A e B si sentono tra loro e sentono R1)
//
// A e' il telefono dell'amministratore (1000), B = 1001, C = 1203.
#include "sim.h"
#include "testing.h"

using namespace pn;

namespace {

constexpr Millis RETRY_LATER_TEST_MS = CALL_MAX_MS;  // il giro piu' tardi parte dopo almeno 3 minuti

void giveNumber(Phone& p, Addr number, const Identity& admin) {
    Cert c;
    c.number = number;
    c.edPublic = p.identity().edPublic;
    c.xPublic = p.identity().xPublic;
    c.issued = 1790000000;
    c.signWith(admin);
    p.setCredentials(c, admin.edPublic);
}

struct World {
    sim::Network net;
    Identity admin = Identity::generate();
    int A, B, C, R1, R2, R3;

    World() {
        A = net.addPhone(Identity::generate());
        B = net.addPhone(Identity::generate());
        C = net.addPhone(Identity::generate());
        R1 = net.addRepeater(0xF001);
        R2 = net.addRepeater(0xF002);
        R3 = net.addRepeater(0xF003);
        net.phone(A).setAdminIdentity(admin);
        giveNumber(net.phone(A), 1000, admin);
        giveNumber(net.phone(B), 1001, admin);
        giveNumber(net.phone(C), 1203, admin);
        net.link(A, B, 9.0f, -70);
        net.link(A, R1, 4.0f, -95);
        net.link(B, R1, 2.0f, -100);
        net.link(R1, R2, -3.0f, -110);
        net.link(R2, R3, -6.0f, -114);
        net.link(R3, C, 1.0f, -104);
        // Rete gia' avviata: i ripetitori si sono scambiati i beacon e si conoscono.
        net.run(60000);
    }
    Phone& a() { return net.phone(A); }
    Phone& b() { return net.phone(B); }
    Phone& c() { return net.phone(C); }
};

}  // namespace

TEST(rete_sms_attraverso_tre_ripetitori) {
    World w;
    uint32_t id = w.a().sendText(1203, "Arrivo al rifugio tra 20 minuti, ci vediamo lì", w.net.now());
    CHECK(id != 0);
    auto& ra = w.net.rec(w.A);
    auto& rc = w.net.rec(w.C);
    CHECK(w.net.runUntil([&] { return !rc.texts.empty(); }, 180000));
    CHECK_EQ(rc.texts.size(), size_t(1));
    if (!rc.texts.empty()) {
        CHECK_EQ(rc.texts[0].from, Addr(1000));
        CHECK_EQ(rc.texts[0].text, std::string("Arrivo al rifugio tra 20 minuti, ci vediamo lì"));
    }
    CHECK(w.net.runUntil([&] { return ra.delivery[id] == Delivery::DELIVERED; }, 120000));
    // Letto.
    w.c().sendRead(1000, rc.texts.empty() ? 0 : rc.texts[0].id, w.net.now());
    CHECK(w.net.runUntil([&] { return ra.delivery[id] == Delivery::READ; }, 120000));
    // Un solo messaggio anche se arrivano piu' copie.
    w.net.run(60000);
    CHECK_EQ(rc.texts.size(), size_t(1));
    // I ripetitori hanno imparato la strada verso C.
    Repeater::Route r;
    CHECK(w.net.repeater(w.R1).lookup(1203, w.net.now(), r));
    CHECK_EQ(r.next, Addr(0xF002));
}

TEST(rete_risposta_usa_la_strada_imparata) {
    World w;
    auto& rc = w.net.rec(w.C);
    auto& ra = w.net.rec(w.A);
    w.a().sendText(1203, "ciao", w.net.now());
    CHECK(w.net.runUntil([&] { return !rc.texts.empty(); }, 180000));
    w.net.run(30000);
    const uint64_t before = w.net.transmissions;
    uint32_t id = w.c().sendText(1000, "ciao a te", w.net.now());
    CHECK(w.net.runUntil([&] { return rc.delivery[id] == Delivery::DELIVERED; }, 120000));
    CHECK_EQ(ra.texts.size(), size_t(1));
    // Messaggio + conferma, 4 salti ciascuno (piu' qualche beacon): poche trasmissioni.
    CHECK(w.net.transmissions - before < 20);
}

TEST(rete_non_raggiungibile) {
    World w;
    auto& ra = w.net.rec(w.A);
    uint32_t id = w.a().sendText(4321, "c'e' qualcuno?", w.net.now());
    // Prima "riprovo piu' tardi", poi (dopo un altro giro 3,5 minuti dopo) "non raggiungibile".
    CHECK(w.net.runUntil([&] { return ra.delivery.count(id) && ra.delivery[id] == Delivery::RETRYING; }, 400000));
    const Millis t = w.net.now();
    CHECK(w.net.runUntil([&] { return ra.delivery[id] == Delivery::UNREACHABLE; }, 600000));
    CHECK(w.net.now() - t > RETRY_LATER_TEST_MS);
}

TEST(rete_messaggio_a_chi_e_in_chiamata) {
    // B scrive ad A mentre A e' in chiamata con C da un minuto e mezzo: B rinuncia al primo giro, ma
    // 3,5 minuti dopo la chiamata e' finita e il messaggio arriva.
    World w;
    auto& ra = w.net.rec(w.A);
    auto& rb = w.net.rec(w.B);
    CHECK(w.a().call(1203, w.net.now()));
    CHECK(w.net.runUntil([&] { return w.c().callState() == CallState::INCOMING; }, 60000));
    w.c().answerCall(w.net.now());
    CHECK(w.net.runUntil([&] { return w.a().callState() == CallState::ACTIVE; }, 30000));
    const uint32_t id = w.b().sendText(1000, "richiamami quando hai finito", w.net.now());
    w.net.run(90000);
    CHECK(rb.sawDelivery(id, Delivery::RETRYING));
    CHECK(ra.texts.empty());
    w.a().endCall(w.net.now());
    CHECK(w.net.runUntil([&] { return rb.delivery[id] == Delivery::DELIVERED; }, 300000));
    CHECK_EQ(ra.texts.size(), size_t(1));
}

TEST(rete_riprova_appena_lo_sente) {
    // C e' fuori portata: il messaggio di A va in "riprovo". Appena C torna e scrive ad A, il
    // messaggio di A riparte subito invece di aspettare i 3,5 minuti.
    World w;
    auto& ra = w.net.rec(w.A);
    auto& rc = w.net.rec(w.C);
    // I certificati si scambiano prima: qui conta solo il nuovo tentativo.
    const uint32_t primo = w.a().sendText(1203, "ci sei?", w.net.now());
    CHECK(w.net.runUntil([&] { return ra.delivery[primo] == Delivery::DELIVERED; }, 180000));
    w.net.unlink(w.R3, w.C);
    const uint32_t id = w.a().sendText(1203, "arrivo alle otto", w.net.now());
    CHECK(w.net.runUntil([&] { return ra.delivery[id] == Delivery::RETRYING; }, 120000));
    w.net.link(w.R3, w.C, 1.0f, -104);
    w.c().sendText(1000, "eccomi, ero in galleria", w.net.now());
    const Millis t = w.net.now();
    CHECK(w.net.runUntil([&] { return ra.delivery[id] == Delivery::DELIVERED; }, 120000));
    CHECK(w.net.now() - t < 60000);
    CHECK_EQ(rc.texts.size(), size_t(2));
}

TEST(rete_canale_globale_e_firma_falsa) {
    World w;
    w.a().sendGlobalText("Sentiero 4 chiuso dopo il ponte", w.net.now());
    auto& rb = w.net.rec(w.B);
    auto& rc = w.net.rec(w.C);
    CHECK(w.net.runUntil([&] { return !rb.globals.empty() && !rc.globals.empty(); }, 180000));
    if (!rc.globals.empty()) CHECK_EQ(rc.globals[0].text, std::string("Sentiero 4 chiuso dopo il ponte"));

    // Un intruso con un certificato fatto da se' si finge il 1001.
    Identity finto = Identity::generate();
    int X = w.net.addPhone(Identity::generate());
    w.net.link(X, w.R3, 3.0f, -100);
    w.net.link(X, w.C, 3.0f, -100);
    giveNumber(w.net.phone(X), 1001, finto);
    w.net.phone(X).sendGlobalText("sono il 1001, fidatevi", w.net.now());
    w.net.run(120000);
    CHECK_EQ(rc.globals.size(), size_t(1));
    CHECK_EQ(w.net.rec(w.A).globals.size(), size_t(0));
}

TEST(rete_gruppo) {
    World w;
    GroupInfo g = w.a().createGroup("Squadra A", {1001, 1203}, w.net.now());
    auto& rb = w.net.rec(w.B);
    auto& rc = w.net.rec(w.C);
    CHECK(w.net.runUntil([&] { return !rb.invites.empty() && !rc.invites.empty(); }, 240000));
    if (rc.invites.empty()) return;
    CHECK_EQ(rc.invites[0].name, std::string("Squadra A"));
    CHECK_EQ(rc.invites[0].id, g.id);
    w.c().sendGroupText(g.id, "partenza domani alle 7", w.net.now());
    auto& ra = w.net.rec(w.A);
    CHECK(w.net.runUntil([&] { return !ra.groupTexts.empty() && !rb.groupTexts.empty(); }, 180000));
    if (!rb.groupTexts.empty()) {
        CHECK_EQ(rb.groupTexts[0].first, g.id);
        CHECK_EQ(rb.groupTexts[0].second.from, Addr(1203));
        CHECK_EQ(rb.groupTexts[0].second.text, std::string("partenza domani alle 7"));
    }
}

TEST(rete_posizione_con_conferma) {
    World w;
    auto& rc = w.net.rec(w.C);
    auto& ra = w.net.rec(w.A);
    w.a().requestPosition(1203, w.net.now());
    CHECK(w.net.runUntil([&] { return !rc.posRequests.empty(); }, 180000));
    if (rc.posRequests.empty()) return;
    Position p;
    p.valid = true;
    p.latE7 = 458123000;
    p.lonE7 = 90841000;
    p.altM = 1320;
    p.accM = 6;
    w.c().answerPosition(1000, rc.posRequests[0].second, PosStatus::OK, p, w.net.now());
    CHECK(w.net.runUntil([&] { return !ra.positions.empty(); }, 180000));
    if (!ra.positions.empty()) {
        CHECK(ra.positions[0].second.status == PosStatus::OK);
        CHECK_EQ(ra.positions[0].second.pos.latE7, p.latE7);
        CHECK_EQ(ra.positions[0].second.pos.altM, int16_t(1320));
    }
}

TEST(rete_sos_ai_contatti) {
    World w;
    w.c().position.valid = true;
    w.c().position.latE7 = 457991000;
    w.c().startSos({1000, 1001}, w.net.now());
    auto& ra = w.net.rec(w.A);
    auto& rb = w.net.rec(w.B);
    auto& rc = w.net.rec(w.C);
    CHECK(w.net.runUntil([&] { return !ra.sos.empty() && !rb.sos.empty() && rc.sosAcks >= 2; }, 240000));
    if (!ra.sos.empty()) {
        CHECK(ra.sos[0].second.active);
        CHECK_EQ(ra.sos[0].second.pos.latE7, 457991000);
    }
    // Si ripete.
    CHECK(w.net.runUntil([&] { return ra.sos.size() >= 2; }, 240000));
    w.c().stopSos(w.net.now());
    CHECK(w.net.runUntil([&] { return !ra.sos.empty() && !ra.sos.back().second.active; }, 240000));
}

TEST(rete_vocale_walkie_talkie) {
    World w;
    std::vector<uint8_t> voce(1300);
    for (size_t i = 0; i < voce.size(); i++) voce[i] = uint8_t(i * 7 + 3);
    auto& ra = w.net.rec(w.A);
    auto& rc = w.net.rec(w.C);
    uint32_t id = w.a().sendVoice(1203, Codec::CODEC2_700C, voce, 14800, w.net.now());
    CHECK(id != 0);
    CHECK(w.net.runUntil([&] { return !rc.voices.empty(); }, 300000));
    if (!rc.voices.empty()) {
        CHECK_EQ(rc.voiceMissing[0], 0u);
        CHECK(rc.voices[0] == voce);
    }
    CHECK(w.net.runUntil([&] { return ra.voiceState.count(id) && ra.voiceState[id] == VoiceSendState::DELIVERED; },
                         60000));
    // Dopo il vocale tutti tornano sul canale dei messaggi e i messaggi passano ancora.
    w.net.run(15000);
    for (int i : {w.A, w.C, w.R1, w.R2, w.R3}) CHECK(w.net.node(i).activeChannel(w.net.now()) == Channel::MAIN);
    uint32_t t = w.c().sendText(1000, "sentito!", w.net.now());
    CHECK(w.net.runUntil([&] { return ra.texts.size() == 1; }, 180000));
    (void)t;
}

// Due ripetitori in parallelo (trovato con il simulatore del pianificatore):
//
//   A ── R1 ─┬─ R2 ─┬─ C
//            └─ R3 ─┘
//
// R1 conosce C attraverso R3 (da un messaggio di prima). Il "chiede vocale" passa da R3, il "si'"
// torna da R2. Passa al canale voce solo chi ha ripetuto il "si'" (R2): i pezzi di vocale devono
// seguire quella strada, non quella della tabella di R1 (R3, rimasto sul canale messaggi).
TEST(rete_vocale_ripetitori_paralleli) {
    sim::Network net;
    Identity admin = Identity::generate();
    const int A = net.addPhone(Identity::generate()), C = net.addPhone(Identity::generate());
    const int R1 = net.addRepeater(0xF001), R2 = net.addRepeater(0xF002), R3 = net.addRepeater(0xF003);
    net.phone(A).setAdminIdentity(admin);
    giveNumber(net.phone(A), 1000, admin);
    giveNumber(net.phone(C), 1203, admin);
    net.link(A, R1, 4.0f, -95);
    net.link(R1, R3, 2.0f, -100);
    net.link(R3, C, 2.0f, -100);
    net.link(R2, R3, 8.0f, -80);
    net.run(60000);
    auto& ra = net.rec(A);
    auto& rc = net.rec(C);
    // Per ora l'unica strada e' da R3: R1 la impara.
    const uint32_t id = net.phone(A).sendText(1203, "ci sei?", net.now());
    CHECK(net.runUntil([&] { return ra.delivery.count(id) && ra.delivery[id] == Delivery::DELIVERED; }, 120000));
    Repeater::Route r{};
    CHECK(net.repeater(R1).lookup(1203, net.now(), r) && r.next == 0xF003);
    // Arriva R2, con lo stesso segnale di R3: R1 non ha motivo di cambiare strada.
    net.link(R1, R2, 2.0f, -100);
    net.link(R2, C, 2.0f, -100);
    net.run(5000);

    std::vector<uint8_t> voce(800);
    for (size_t i = 0; i < voce.size(); i++) voce[i] = uint8_t(i * 13 + 1);
    const uint32_t r3prima = net.repeater(R3).relayed;
    const uint32_t vid = net.phone(A).sendVoice(1203, Codec::CODEC2_700C, voce, 8000, net.now());
    CHECK(vid != 0);
    // Il "chiede vocale" passa da R3; subito dopo R3 smette di sentire C, cosi' il "si'" torna da R2.
    CHECK(net.runUntil([&] { return net.repeater(R3).relayed > r3prima; }, 30000));
    net.unlink(R3, C);
    CHECK(net.runUntil([&] { return ra.voiceState.count(vid) && ra.voiceState[vid] == VoiceSendState::SENDING; }, 30000));
    // Ora R3 sentirebbe di nuovo C, ma e' rimasto sul canale messaggi.
    net.link(R3, C, 2.0f, -100);
    CHECK(net.node(R2).activeChannel(net.now()) == Channel::VOICE);
    CHECK(net.node(R3).activeChannel(net.now()) == Channel::MAIN);
    CHECK(net.runUntil([&] { return !rc.voices.empty(); }, 120000));
    if (!rc.voices.empty()) {
        CHECK_EQ(rc.voiceMissing[0], 0u);
        CHECK(rc.voices[0] == voce);
    }
    CHECK(net.runUntil([&] { return ra.voiceState[vid] == VoiceSendState::DELIVERED; }, 60000));
}

TEST(rete_assegnazione_numero_via_radio) {
    World w;
    int N = w.net.addPhone(Identity::generate());
    w.net.link(N, w.A, 8.0f, -60);
    Phone& nuovo = w.net.phone(N);
    nuovo.setProvisioning(true);
    auto& ra = w.net.rec(w.A);
    auto& rn = w.net.rec(N);
    CHECK(w.net.runUntil([&] { return !ra.hellos.empty(); }, 30000));
    if (ra.hellos.empty()) return;
    // L'amministratore vede lo stesso codice mostrato dal telefono nuovo.
    CHECK_EQ(fingerprint(ra.hellos[0].edPublic), fingerprint(nuovo.identity().edPublic));
    CHECK(w.a().provision(ra.hellos[0], 1500, w.net.now()));
    CHECK(w.net.runUntil([&] { return rn.offerReceived; }, 30000));
    CHECK_EQ(rn.offerCert.number, Addr(1500));
    CHECK(rn.offerAdmin == w.admin.edPublic);
    nuovo.setCredentials(rn.offerCert, rn.offerAdmin);
    CHECK(nuovo.hasNumber());
    // Il nuovo telefono scrive a B.
    w.net.link(N, w.B, 5.0f, -90);
    nuovo.sendText(1001, "eccomi", w.net.now());
    auto& rb = w.net.rec(w.B);
    CHECK(w.net.runUntil([&] { return !rb.texts.empty(); }, 120000));
}

TEST(rete_con_pacchetti_persi) {
    World w;
    w.net.setLossPercent(25);  // ogni ricezione ha il 25% di probabilita' di perdersi
    auto& rc = w.net.rec(w.C);
    auto& ra = w.net.rec(w.A);
    uint32_t id = w.a().sendText(1203, "arrivera' lo stesso", w.net.now());
    CHECK(w.net.runUntil([&] { return !rc.texts.empty(); }, 600000));
    CHECK(w.net.runUntil([&] { return ra.delivery[id] == Delivery::DELIVERED; }, 600000));
    w.net.run(200000);
    CHECK_EQ(rc.texts.size(), size_t(1));
}

// ------------------------------------------------------------------ chiamate in diretta

namespace {

// Tiene premuto il PTT per 'packets' pacchetti (uno ogni 400 o 480 ms), poi lascia.
bool talk(sim::Network& net, int who, int packets, uint8_t seed) {
    Phone& p = net.phone(who);
    if (!p.callTalk(true, net.now())) return false;
    const uint8_t frames = p.callFramesPerPacket();
    const size_t n = codecBytes(Codec::CODEC2_700C, frames);
    for (int i = 0; i < packets; i++) {
        uint8_t d[64];
        for (size_t k = 0; k < n; k++) d[k] = uint8_t(seed + i * 7 + int(k));
        p.callSendVoice(d, n, frames, i + 1 == packets, net.now());
        net.run(p.callPacketMs());
    }
    return true;
}

// Chiama e fa rispondere dopo un paio di secondi di squillo; true se la chiamata e' in corso.
bool connect(sim::Network& net, int from, int to, Addr number) {
    auto& rf = net.rec(from);
    if (!net.phone(from).call(number, net.now())) return false;
    if (!net.runUntil([&] { return net.phone(to).callState() == CallState::INCOMING; }, 60000)) return false;
    net.run(2000);
    net.phone(to).answerCall(net.now());
    // (lo squillo puo' perdersi: basta che arrivi la risposta)
    return net.runUntil([&] {
        return net.phone(from).callState() == CallState::ACTIVE && net.phone(to).callState() == CallState::ACTIVE;
    }, 30000) && rf.callReached(CallState::CONNECTING);
}

// I tre telefoni si sono gia' scritti: hanno i certificati gli uni degli altri.
void knowEachOther(World& w) {
    for (Phone* p : {&w.a(), &w.b(), &w.c()})
        for (Phone* q : {&w.a(), &w.b(), &w.c()})
            if (p != q) p->addKnownCert(q->ownCert());
}

}  // namespace

TEST(chiamata_diretta_a_turni) {
    World w;
    auto& ra = w.net.rec(w.A);
    auto& rb = w.net.rec(w.B);
    CHECK(connect(w.net, w.A, w.B, 1001));
    // A e B si sentono: niente ripetitori, ogni pacchetto porta anche il precedente.
    CHECK_EQ(w.a().callHops(), uint8_t(0));
    CHECK(w.a().callRedundant() && w.b().callRedundant());
    CHECK(w.net.node(w.R1).activeChannel(w.net.now()) == Channel::MAIN);
    CHECK(talk(w.net, w.A, 10, 1));
    w.net.run(1000);
    CHECK_EQ(rb.callPackets(1), size_t(10));
    // Mentre B parla, A non puo' prendere il turno.
    CHECK(w.b().callTalk(true, w.net.now()));
    uint8_t d[35] = {};
    w.b().callSendVoice(d, sizeof(d), 10, false, w.net.now());
    w.net.run(400);
    CHECK(!w.a().callTalk(true, w.net.now()));
    w.b().callSendVoice(d, sizeof(d), 10, true, w.net.now());
    w.net.run(1500);
    CHECK_EQ(ra.callPackets(1), size_t(2));
    CHECK(!ra.callVoice.empty() && ra.callVoice.back().last);
    // Ora tocca di nuovo ad A; il contenuto arriva uguale.
    CHECK(talk(w.net, w.A, 3, 50));
    w.net.run(1000);
    bool same = false;
    for (const auto& p : rb.callVoice)
        if (p.turn == 2 && p.index == 0 && !p.copy) same = p.data.size() == 35 && p.data[0] == 50 && p.data[34] == 84;
    CHECK(same);
    // Riattacca B.
    w.b().endCall(w.net.now());
    CHECK(w.net.runUntil([&] { return w.a().callState() == CallState::IDLE && w.b().callState() == CallState::IDLE; },
                         5000));
    CHECK(ra.lastCallResult() == CallResult::HUNG_UP);
    CHECK(rb.lastCallResult() == CallResult::ENDED);
    CHECK(w.net.node(w.A).activeChannel(w.net.now()) == Channel::MAIN);
    // I messaggi riprendono.
    const uint32_t id = w.a().sendText(1001, "ci sentiamo dopo", w.net.now());
    CHECK(w.net.runUntil([&] { return ra.delivery[id] == Delivery::DELIVERED; }, 60000));
}

TEST(chiamata_attraverso_tre_ripetitori) {
    World w;
    auto& ra = w.net.rec(w.A);
    auto& rb = w.net.rec(w.B);
    auto& rc = w.net.rec(w.C);
    CHECK(connect(w.net, w.A, w.C, 1203));
    CHECK_EQ(w.c().callHops(), uint8_t(3));
    CHECK(!w.a().callRedundant() && !w.c().callRedundant());
    for (int r : {w.R1, w.R2, w.R3}) CHECK(w.net.node(r).activeChannel(w.net.now()) == Channel::VOICE);
    // A scrive a B durante la chiamata: parte dopo.
    const uint32_t idA = w.a().sendText(1001, "sono al telefono", w.net.now());
    CHECK(talk(w.net, w.A, 10, 1));
    w.net.run(1500);
    CHECK(rc.callPackets(1) >= 9);
    CHECK(talk(w.net, w.C, 10, 100));
    w.net.run(1500);
    CHECK(ra.callPackets(1) >= 9);
    // B scrive ad A verso la fine della chiamata: A non lo sente ora, ma B riprova e arriva dopo.
    // (B rinuncia dopo una quarantina di secondi: in una chiamata lunga sarebbe "non raggiungibile".)
    const uint32_t idB = w.b().sendText(1000, "sei libero?", w.net.now());
    // Silenzio: la chiamata resta aperta con i pacchetti vuoti.
    w.net.run(15000);
    CHECK(w.a().callState() == CallState::ACTIVE && w.c().callState() == CallState::ACTIVE);
    for (int r : {w.R1, w.R2, w.R3}) CHECK(w.net.node(r).activeChannel(w.net.now()) == Channel::VOICE);
    CHECK(rb.texts.empty());
    w.a().endCall(w.net.now());
    CHECK(w.net.runUntil([&] { return w.c().callState() == CallState::IDLE; }, 5000));
    CHECK(rc.lastCallResult() == CallResult::HUNG_UP);
    CHECK(ra.lastCallResult() == CallResult::ENDED);
    w.net.run(2000);
    for (int r : {w.R1, w.R2, w.R3}) CHECK(w.net.node(r).activeChannel(w.net.now()) == Channel::MAIN);
    CHECK(w.net.runUntil([&] { return !ra.texts.empty() && ra.delivery[idA] == Delivery::DELIVERED; }, 120000));
    CHECK(w.net.runUntil([&] { return rb.delivery[idB] == Delivery::DELIVERED; }, 60000));
}

TEST(chiamata_rifiutata_occupato_persa) {
    World w;
    knowEachOther(w);
    auto& ra = w.net.rec(w.A);
    auto& rb = w.net.rec(w.B);
    auto& rc = w.net.rec(w.C);
    // Rifiutata.
    CHECK(w.a().call(1203, w.net.now()));
    CHECK(w.net.runUntil([&] { return w.a().callState() == CallState::RINGBACK; }, 60000));
    w.c().endCall(w.net.now());
    CHECK(w.net.runUntil([&] { return w.a().callState() == CallState::IDLE; }, 30000));
    CHECK(ra.lastCallResult() == CallResult::REFUSED);
    CHECK(rc.lastCallResult() == CallResult::ENDED);
    w.net.run(5000);
    // Occupato: C squilla per A, intanto chiama B.
    CHECK(w.a().call(1203, w.net.now()));
    CHECK(w.net.runUntil([&] { return w.c().callState() == CallState::INCOMING; }, 60000));
    CHECK(w.b().call(1203, w.net.now()));
    CHECK(w.net.runUntil([&] { return w.b().callState() == CallState::IDLE; }, 60000));
    CHECK(rb.lastCallResult() == CallResult::BUSY);
    // A rinuncia: per C e' una chiamata persa.
    w.a().endCall(w.net.now());
    CHECK(w.net.runUntil([&] { return w.c().callState() == CallState::IDLE; }, 30000));
    CHECK(rc.lastCallResult() == CallResult::MISSED);
    // Non risponde: squilla 45 secondi.
    w.net.run(5000);
    CHECK(w.a().call(1203, w.net.now()));
    CHECK(w.net.runUntil([&] { return w.a().callState() == CallState::IDLE; }, 60000));
    CHECK(ra.lastCallResult() == CallResult::NO_ANSWER);
    CHECK(w.net.runUntil([&] { return w.c().callState() == CallState::IDLE; }, 30000));
    CHECK(rc.lastCallResult() == CallResult::MISSED);
}

TEST(chiamata_non_raggiungibile_e_caduta) {
    World w;
    auto& ra = w.net.rec(w.A);
    auto& rc = w.net.rec(w.C);
    w.net.unlink(w.R3, w.C);
    CHECK(w.a().call(1203, w.net.now()));
    CHECK(w.net.runUntil([&] { return w.a().callState() == CallState::IDLE; }, 60000));
    CHECK(ra.lastCallResult() == CallResult::UNREACHABLE);
    CHECK(!rc.callReached(CallState::INCOMING));
    w.net.link(w.R3, w.C, 1.0f, -104);
    w.net.run(30000);
    // Caduta: a meta' chiamata R2 e R3 non si sentono piu'.
    CHECK(connect(w.net, w.A, w.C, 1203));
    w.net.unlink(w.R2, w.R3);
    CHECK(w.net.runUntil([&] { return w.a().callState() == CallState::IDLE && w.c().callState() == CallState::IDLE; },
                         15000));
    CHECK(ra.lastCallResult() == CallResult::DROPPED);
    CHECK(rc.lastCallResult() == CallResult::DROPPED);
    w.net.run(10000);
    for (int r : {w.R1, w.R2, w.R3}) CHECK(w.net.node(r).activeChannel(w.net.now()) == Channel::MAIN);
}

TEST(chiamata_durata_massima_e_sos) {
    World w;
    auto& ra = w.net.rec(w.A);
    auto& rb = w.net.rec(w.B);
    CHECK(connect(w.net, w.A, w.B, 1001));
    w.net.run(CALL_MAX_MS + 3000);
    CHECK(ra.lastCallResult() == CallResult::TOO_LONG);
    CHECK(rb.lastCallResult() == CallResult::TOO_LONG);
    // Un SOS durante una chiamata la chiude: chi e' in chiamata non sente i messaggi.
    w.net.run(5000);
    CHECK(connect(w.net, w.A, w.B, 1001));
    w.a().startSos({1001}, w.net.now());
    CHECK(w.net.runUntil([&] { return !rb.sos.empty(); }, 60000));
    CHECK(ra.lastCallResult() == CallResult::ENDED);
    CHECK(rb.lastCallResult() == CallResult::HUNG_UP);
    w.a().stopSos(w.net.now());
}

TEST(chiamata_ripetitori_paralleli) {
    // A - R1 -+- R2 -+- C      la voce segue la strada della chiamata: passa da uno solo dei
    //         +- R3 -+         due ripetitori, l'altro resta sui messaggi
    sim::Network net;
    Identity admin = Identity::generate();
    const int A = net.addPhone(Identity::generate()), C = net.addPhone(Identity::generate());
    const int R1 = net.addRepeater(0xF001), R2 = net.addRepeater(0xF002), R3 = net.addRepeater(0xF003);
    net.phone(A).setAdminIdentity(admin);
    giveNumber(net.phone(A), 1000, admin);
    giveNumber(net.phone(C), 1203, admin);
    net.link(A, R1, 4.0f, -95);
    net.link(R1, R2, 2.0f, -100);
    net.link(R1, R3, 2.0f, -100);
    net.link(R2, C, 2.0f, -100);
    net.link(R3, C, 2.0f, -100);
    net.link(R2, R3, 8.0f, -80);
    net.run(60000);
    auto& ra = net.rec(A);
    auto& rc = net.rec(C);
    CHECK(connect(net, A, C, 1203));
    const int onVoice = int(net.node(R2).activeChannel(net.now()) == Channel::VOICE) +
                        int(net.node(R3).activeChannel(net.now()) == Channel::VOICE);
    CHECK_EQ(onVoice, 1);
    CHECK(net.node(R1).activeChannel(net.now()) == Channel::VOICE);
    CHECK(talk(net, A, 10, 1));
    net.run(1500);
    CHECK(rc.callPackets(1) >= 9);
    CHECK(talk(net, C, 10, 1));
    net.run(1500);
    CHECK(ra.callPackets(1) >= 9);
    net.phone(C).endCall(net.now());
    CHECK(net.runUntil([&] { return net.phone(A).callState() == CallState::IDLE; }, 5000));
    CHECK(ra.lastCallResult() == CallResult::HUNG_UP);
}

TEST(chiamata_strada_cambiata) {
    // La strada imparata passa da R3, che poi non sente piu' C: dal terzo tentativo la chiamata va
    // a tutti e trova R2.
    sim::Network net;
    Identity admin = Identity::generate();
    const int A = net.addPhone(Identity::generate()), C = net.addPhone(Identity::generate());
    const int R1 = net.addRepeater(0xF001), R2 = net.addRepeater(0xF002), R3 = net.addRepeater(0xF003);
    net.phone(A).setAdminIdentity(admin);
    giveNumber(net.phone(A), 1000, admin);
    giveNumber(net.phone(C), 1203, admin);
    net.link(A, R1, 4.0f, -95);
    net.link(R1, R3, 2.0f, -100);
    net.link(R3, C, 2.0f, -100);
    net.link(R1, R2, 2.0f, -100);
    net.run(60000);
    auto& ra = net.rec(A);
    const uint32_t id = net.phone(A).sendText(1203, "ci sei?", net.now());
    CHECK(net.runUntil([&] { return ra.delivery[id] == Delivery::DELIVERED; }, 120000));
    net.unlink(R3, C);
    net.link(R2, C, 2.0f, -100);
    CHECK(connect(net, A, C, 1203));
    CHECK(net.node(R2).activeChannel(net.now()) == Channel::VOICE);
    CHECK(talk(net, A, 5, 1));
    net.run(1500);
    CHECK(net.rec(C).callPackets(1) >= 4);
}
