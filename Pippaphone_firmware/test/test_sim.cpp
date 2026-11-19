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

TEST(rete_sfida_via_radio) {
    // Una mossa di una sfida (byte qualsiasi) da A a C attraverso tre ripetitori: arriva una volta
    // sola, con la conferma. I ripetitori la ripetono senza conoscerne il tipo.
    World w;
    const uint8_t mossa[] = {1, 4, 0x12, 0x34, 7};
    const uint32_t id = w.a().sendGame(1203, mossa, sizeof(mossa), w.net.now());
    CHECK(id != 0);
    auto& ra = w.net.rec(w.A);
    auto& rc = w.net.rec(w.C);
    CHECK(w.net.runUntil([&] { return !rc.games.empty(); }, 180000));
    CHECK(w.net.runUntil([&] { return ra.gameDelivery.count(id) && ra.gameDelivery[id] == Delivery::DELIVERED; }, 120000));
    w.net.run(60000);
    CHECK_EQ(rc.games.size(), size_t(1));
    if (!rc.games.empty()) {
        CHECK_EQ(rc.games[0].first, Addr(1000));
        CHECK(rc.games[0].second == std::vector<uint8_t>(mossa, mossa + sizeof(mossa)));
    }
    // Nessun SMS finto: la mossa non finisce tra i messaggi.
    CHECK(rc.texts.empty());
    // Troppo lunga, vuota o a se stessi: rifiutata subito.
    std::vector<uint8_t> lunga(Phone::MAX_GAME_BYTES + 1, 0);
    CHECK_EQ(w.a().sendGame(1203, lunga.data(), lunga.size(), w.net.now()), 0u);
    CHECK_EQ(w.a().sendGame(1203, mossa, 0, w.net.now()), 0u);
    CHECK_EQ(w.a().sendGame(1000, mossa, sizeof(mossa), w.net.now()), 0u);
    // A un numero che non c'e': prima "riprovo", poi (dopo il giro piu' tardi) non raggiungibile.
    const uint32_t id2 = w.a().sendGame(4321, mossa, sizeof(mossa), w.net.now());
    CHECK(w.net.runUntil([&] { return ra.gameDelivery.count(id2) && ra.gameDelivery[id2] == Delivery::RETRYING; }, 400000));
    CHECK(w.net.runUntil([&] { return ra.gameDelivery[id2] == Delivery::UNREACHABLE; }, 600000));
}

TEST(rete_sfida_tolta_dai_tentativi) {
    // Un GAME tolto dai tentativi (un invito abbandonato): C, fuori portata quando e' partito, non lo
    // riceve quando torna, e della sua consegna non si sa piu' niente. Gli altri GAME restano.
    World w;
    auto& ra = w.net.rec(w.A);
    auto& rc = w.net.rec(w.C);
    const uint8_t prima[] = {1, 4, 0x12, 0x34, 1, 0};  // A impara il certificato di C
    const uint32_t id0 = w.a().sendGame(1203, prima, sizeof(prima), w.net.now());
    CHECK(w.net.runUntil([&] { return ra.gameDelivery.count(id0) && ra.gameDelivery[id0] == Delivery::DELIVERED; }, 180000));
    w.net.unlink(w.R3, w.C);
    const uint8_t invito[] = {1, 1, 0x56, 0x78};
    const uint32_t id1 = w.a().sendGame(1203, invito, sizeof(invito), w.net.now());
    CHECK(id1 != 0);
    w.net.run(15000);                    // un paio di tentativi senza risposta
    w.a().cancelGame(1203, id1);
    w.a().cancelGame(1203, id1 + 7);     // un id che non c'e': niente
    w.a().cancelGame(1001, id1);         // un altro numero: niente
    w.net.run(10000);
    w.net.link(w.R3, w.C, 1.0f, -104);
    const uint8_t dopo[] = {1, 4, 0x12, 0x34, 3, 8};
    const uint32_t id2 = w.a().sendGame(1203, dopo, sizeof(dopo), w.net.now());
    CHECK(w.net.runUntil([&] { return ra.gameDelivery.count(id2) && ra.gameDelivery[id2] == Delivery::DELIVERED; }, 180000));
    w.net.run(600000);                   // anche il giro di tentativi piu' tardi
    CHECK_EQ(rc.games.size(), size_t(2));
    for (const auto& gm : rc.games) CHECK(gm.second != std::vector<uint8_t>(invito, invito + sizeof(invito)));
    CHECK(!ra.gameDelivery.count(id1));  // ne' "riprovo" ne' "non raggiungibile"
}

TEST(ora_a_mano_resta_sul_telefono) {
    // A ha l'ora messa a mano: la usa, ma nei beacon esce come non sincronizzata, quindi ne' R1 ne' B
    // la prendono. Poi B ha l'ora dal GPS: col suo beacon arriva ad A (che non e' piu' "a mano") e a R1.
    World w;
    CHECK(!w.a().timeValid());
    CHECK(w.a().setLocalTime(1794231120, w.net.now()));
    CHECK(w.a().timeValid());
    CHECK(w.a().timeLocal());
    CHECK_EQ(w.a().unixTime(w.net.now()), 1794231120u);
    w.net.run(6 * 60 * 1000);  // almeno un beacon di A
    CHECK(!w.net.repeater(w.R1).timeValid());
    CHECK(!w.b().timeValid());
    CHECK(w.a().timeLocal());
    // Ora del GPS su B: quella a mano non la sostituisce.
    w.b().setUnixTime(1794231500, w.net.now(), true);
    CHECK(!w.b().setLocalTime(1794000000, w.net.now()));
    CHECK(!w.b().timeLocal());
    // I beacon non si ripetono: se uno si scontra con un altro pacchetto si aspetta il successivo.
    CHECK(w.net.runUntil([&] { return !w.a().timeLocal() && w.net.repeater(w.R1).timeValid(); }, 13 * 60 * 1000));
    CHECK(w.a().timeValid());
    const int64_t diff = int64_t(w.a().unixTime(w.net.now())) - int64_t(w.b().unixTime(w.net.now()));
    CHECK(diff >= -2 && diff <= 2);
}

TEST(riassegnazione_dopo_una_data_sbagliata) {
    // L'amministratore aveva messo a mano una data sbagliata nel futuro quando ha dato il 1500 al
    // primo telefono. Con l'ora giusta lo stesso numero va a un altro telefono: il certificato nuovo
    // deve battere il vecchio, sia per l'amministratore sia per B che conosceva il primo.
    World w;
    CHECK(w.a().setLocalTime(2900000000u, w.net.now()));  // anno 2061
    auto& ra = w.net.rec(w.A);
    auto& rb = w.net.rec(w.B);
    // doppio: l'amministratore manda il numero due volte (il telefono non aveva ancora confermato) e
    // il telefono tiene l'ultima offerta, piu' recente di un minuto.
    auto assegna = [&](int n, bool doppio) {
        Phone& p = w.net.phone(n);
        auto& rn = w.net.rec(n);
        p.setProvisioning(true);
        const size_t prima = ra.hellos.size();
        if (!w.net.runUntil([&] { return ra.hellos.size() > prima; }, 30000)) return false;
        if (!w.a().provision(ra.hellos.back(), 1500, w.net.now())) return false;
        if (!w.net.runUntil([&] { return rn.offerReceived; }, 30000)) return false;
        if (doppio) {
            w.net.run(60000);
            rn.offerReceived = false;
            if (!w.a().provision(ra.hellos.back(), 1500, w.net.now())) return false;
            if (!w.net.runUntil([&] { return rn.offerReceived; }, 30000)) return false;
        }
        p.setCredentials(rn.offerCert, rn.offerAdmin);
        return p.hasNumber();
    };
    const int N1 = w.net.addPhone(Identity::generate());
    w.net.link(N1, w.A, 8.0f, -60);
    w.net.link(N1, w.B, 5.0f, -90);
    CHECK(assegna(N1, true));
    w.net.phone(N1).sendText(1001, "primo", w.net.now());
    CHECK(w.net.runUntil([&] { return rb.texts.size() == 1; }, 120000));
    // Il primo telefono sparisce. Si riassegna il numero dopo piu' di mezz'ora: prima B scarterebbe i
    // pacchetti del nuovo come doppioni del vecchio (un telefono nuovo numera i pacchetti da 1).
    w.net.unlink(N1, w.A);
    w.net.unlink(N1, w.B);
    w.net.run(31 * 60 * 1000);
    w.a().setUnixTime(1794231120u, w.net.now(), true);  // ora giusta dal GPS
    const int N2 = w.net.addPhone(Identity::generate());
    w.net.link(N2, w.A, 8.0f, -60);
    w.net.link(N2, w.B, 5.0f, -90);
    CHECK(assegna(N2, false));
    CHECK(w.net.rec(N2).offerCert.issued > w.net.rec(N1).offerCert.issued);
    const Cert* c = w.a().certOf(1500);
    CHECK(c && c->edPublic == w.net.phone(N2).identity().edPublic);
    // Scrivendo a B il nuovo gli manda il suo certificato, che B deve preferire al vecchio. (Il testo
    // non si controlla: gli id dei messaggi del telefono nuovo ripartono da capo e B, che ricorda gli
    // ultimi id di quel numero, lo prenderebbe per un doppione del "primo": limite che c'era gia'.)
    w.net.phone(N2).sendText(1001, "secondo", w.net.now());
    CHECK(w.net.runUntil([&] {
        const Cert* cb = w.b().certOf(1500);
        return cb && cb->edPublic == w.net.phone(N2).identity().edPublic;
    }, 180000));
}

TEST(ora_ripresa_si_corregge_dalla_rete) {
    // Dopo un riavvio A ritrova l'ora nell'orologio del chip, spostata di 5 minuti. Non la manda nei
    // beacon, ma la corregge l'ora della rete: qui quella di R1, che non viene dal GPS (i ripetitori
    // non lo dicono mai). L'ora messa a mano invece resta (ora_a_mano_resta_sul_telefono).
    World w;
    w.net.unlink(w.A, w.B);  // A sente solo R1
    w.b().setUnixTime(1794231120u, w.net.now(), true);  // B ha il GPS
    CHECK(w.net.runUntil([&] { return w.net.repeater(w.R1).timeValid(); }, 13 * 60 * 1000));
    CHECK(w.a().setRestoredTime(w.b().unixTime(w.net.now()) + 300, w.net.now()));
    CHECK(w.a().timeRestored());
    CHECK(w.a().timeLocal());
    CHECK(w.net.runUntil([&] { return !w.a().timeRestored(); }, 13 * 60 * 1000));
    CHECK(!w.a().timeLocal());
    const int64_t diff = int64_t(w.a().unixTime(w.net.now())) - int64_t(w.b().unixTime(w.net.now()));
    CHECK(diff >= -2 && diff <= 2);
}

// ---------------------------------------------------------------- sonno del compito della radio
//
// Sul telefono il compito della radio (pippa_radio/radio_link.cpp) dorme per msUntilNextEvent(), almeno
// 5 ms. Se il nodo dice "subito" (0) quando non ha niente da trasmettere sul canale su cui si trova, il
// compito gira ogni 5 ms a vuoto finche' la cosa non cambia: CPU sveglia per niente (fino a 2 s col passo
// tra due messaggi, minuti con un beacon scaduto durante una chiamata).

namespace {

// Nodo minimo: una coda, un canale e un passo (come Phone::mayTransmit) che si decidono dal test.
struct NodoProva : Node {
    Channel canale = Channel::MAIN;
    Millis passoFino = 0;
    NodoProva() : Node(1234) {}
    void accoda(Channel ch, Millis nonPrima) {
        uint8_t b[HEADER_SIZE + 4] = {};
        b[0] = uint8_t(Type::DM_TEXT);
        enqueue(b, sizeof(b), Priority::Normal, nonPrima, ch);
    }
    void sveglia(Millis t) { nextWake_ = t; }
    Channel activeChannel(Millis) const override { return canale; }

protected:
    void handle(const Header&, const uint8_t*, size_t, const RxInfo&, Millis) override {}
    bool mayTransmit(const Frame&, Millis now) const override { return now >= passoFino; }
    Millis heldForMs(const Frame&, Millis now) const override { return passoFino > now ? passoFino - now : 0; }
};

// Passi in cui il telefono non ha niente da trasmettere sul suo canale ma dice "subito".
int giriAVuoto(sim::Network& net, int who, Millis ms) {
    int n = 0;
    net.runUntil([&] {
        Phone& p = net.phone(who);
        const Millis now = net.now();
        if (!p.hasTx(now, p.activeChannel(now)) && p.msUntilNextEvent(now) == 0) n++;
        return false;
    }, ms);
    return n;
}

}  // namespace

TEST(radio_dorme_fino_al_prossimo_evento) {
    NodoProva n;
    const Millis t = 100000;
    n.sveglia(t + 60000);
    CHECK_EQ(n.msUntilNextEvent(t), Millis(60000));
    // Pronto e trasmissibile: subito, come prima.
    n.accoda(Channel::MAIN, t);
    CHECK(n.hasTx(t, Channel::MAIN));
    CHECK_EQ(n.msUntilNextEvent(t), Millis(0));
    // Trattenuto dal passo per 700 ms: si dorme 700 ms, non 0.
    n.passoFino = t + 700;
    CHECK(!n.hasTx(t, Channel::MAIN));
    CHECK_EQ(n.msUntilNextEvent(t), Millis(700));
    CHECK_EQ(n.msUntilNextEvent(t + 650), Millis(50));
    CHECK_EQ(n.msUntilNextEvent(t + 700), Millis(0));
    // Pronto per l'altro canale (si e' sul canale voce): si riguarda ogni tanto, non ogni 5 ms.
    NodoProva v;
    v.sveglia(t + 60000);
    v.canale = Channel::VOICE;
    v.accoda(Channel::MAIN, t);
    CHECK(!v.hasTx(t, Channel::VOICE));
    CHECK(v.msUntilNextEvent(t) > 0);
    CHECK(v.msUntilNextEvent(t) <= 50);
    // Non ancora pronto: fino a quando lo sara'.
    NodoProva d;
    d.sveglia(t + 60000);
    d.accoda(Channel::MAIN, t + 300);
    CHECK_EQ(d.msUntilNextEvent(t), Millis(300));
}

TEST(radio_dorme_col_passo_tra_due_messaggi) {
    // A scrive tre messaggi di fila a C attraverso i ripetitori: il secondo e il terzo aspettano che il
    // primo si sia allontanato (passo). Intanto la radio non deve girare a vuoto.
    World w;
    auto& rc = w.net.rec(w.C);
    for (int i = 0; i < 3; i++) w.a().sendText(1203, "messaggio " + std::to_string(i), w.net.now());
    CHECK_EQ(giriAVuoto(w.net, w.A, 120000), 0);
    CHECK(w.net.runUntil([&] { return rc.texts.size() == 3; }, 180000));
}

TEST(radio_dorme_col_beacon_scaduto_in_chiamata) {
    // Il beacon scade mentre A e' in chiamata (canale voce): parte dopo la chiamata, e intanto la radio
    // non gira a vuoto.
    World w;
    w.a().beaconIntervalMs = 31000;  // dal prossimo beacon, uno ogni 1-61 s
    w.net.run(6 * 60 * 1000);       // il primo beacon col passo di prima e' partito
    CHECK(connect(w.net, w.A, w.B, 1001));
    CHECK(talk(w.net, w.A, 5, 1));
    CHECK_EQ(giriAVuoto(w.net, w.A, 90000), 0);
    w.a().endCall(w.net.now());
    CHECK(w.net.runUntil([&] { return w.a().callState() == CallState::IDLE; }, 10000));
    CHECK_EQ(giriAVuoto(w.net, w.A, 90000), 0);
}
