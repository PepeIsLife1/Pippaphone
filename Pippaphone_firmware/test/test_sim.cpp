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
    CHECK(w.net.runUntil([&] { return ra.delivery.count(id) && ra.delivery[id] == Delivery::UNREACHABLE; }, 400000));
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
