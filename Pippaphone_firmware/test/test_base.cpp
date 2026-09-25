// Test delle parti di base: alfabeto, intestazione, crittografia, tempo in aria.
#include "pippanet/airtime.h"
#include "pippanet/charset.h"
#include "pippanet/crypto.h"
#include "pippanet/packet.h"
#include "pippanet/payloads.h"
#include "testing.h"

using namespace pn;

TEST(alfabeto_accenti_andata_ritorno) {
    const std::string s = "Perché è già lì? Città, però: 10€ e 5° «ciao» …ok\nriga 2";
    uint8_t buf[200];
    size_t n = encodeText(s, buf, sizeof(buf));
    CHECK(n > 0);
    CHECK_EQ(decodeText(buf, n), s);
}

TEST(alfabeto_160_caratteri_in_140_byte) {
    std::string s(160, 'a');
    uint8_t buf[200];
    CHECK_EQ(encodeText(s, buf, sizeof(buf)), size_t(140));
    CHECK_EQ(decodeText(buf, 140), s);
    // Oltre 160 caratteri si tronca.
    std::string lungo(200, 'b');
    size_t n = encodeText(lungo, buf, sizeof(buf));
    CHECK_EQ(n, size_t(140));
    CHECK_EQ(decodeText(buf, n).size(), size_t(160));
}

TEST(alfabeto_caratteri_non_previsti) {
    uint8_t buf[50];
    size_t n = encodeText("ok 😀 ì", buf, sizeof(buf));
    CHECK_EQ(decodeText(buf, n), std::string("ok ? ì"));
    CHECK_EQ(textLength("àèì"), size_t(3));
    CHECK(isEncodable(0x20AC));
    CHECK(!isEncodable(0x1F600));
}

TEST(alfabeto_lunghezze_diverse) {
    for (size_t len = 0; len < 40; len++) {
        std::string s;
        for (size_t i = 0; i < len; i++) s.push_back(char('A' + i % 26));
        uint8_t buf[64];
        size_t n = encodeText(s, buf, sizeof(buf));
        CHECK_EQ(n, (len * 7 + 7) / 8);
        CHECK_EQ(decodeText(buf, n), s);
    }
}

TEST(intestazione_andata_ritorno) {
    Header h;
    h.type = Type::DM_TEXT;
    h.forceFlood = true;
    h.wantAck = true;
    h.hopLimit = 10;
    h.hops = 3;
    h.src = 1203;
    h.dst = 1187;
    h.seq = 0xA1B2C3D4;
    h.via = 0xF003;
    h.lastHop = 0xF001;
    uint8_t buf[HEADER_SIZE];
    encodeHeader(h, buf);
    Header g;
    CHECK(decodeHeader(buf, sizeof(buf), g));
    CHECK(g.type == h.type);
    CHECK(g.forceFlood && g.wantAck);
    CHECK_EQ(g.hopLimit, h.hopLimit);
    CHECK_EQ(g.hops, h.hops);
    CHECK_EQ(g.src, h.src);
    CHECK_EQ(g.dst, h.dst);
    CHECK_EQ(g.seq, h.seq);
    CHECK_EQ(g.via, h.via);
    CHECK_EQ(g.lastHop, h.lastHop);
    patchRelayFields(buf, 4, 0xF009, 0xF003, false);
    CHECK(decodeHeader(buf, sizeof(buf), g));
    CHECK_EQ(g.hops, 4);
    CHECK_EQ(g.via, 0xF009);
    CHECK_EQ(g.lastHop, 0xF003);
    CHECK(!g.forceFlood);
    CHECK(g.wantAck);
    CHECK(!decodeHeader(buf, 5, g));
}

TEST(firma_e_certificato) {
    Identity admin = Identity::generate();
    Identity tel = Identity::generate();
    Cert c;
    c.number = 1203;
    c.edPublic = tel.edPublic;
    c.xPublic = tel.xPublic;
    c.issued = 1790000000;
    c.signWith(admin);
    CHECK(c.verify(admin.edPublic));
    uint8_t buf[CERT_SIZE];
    CHECK_EQ(c.serialize(buf), CERT_SIZE);
    Cert d;
    CHECK(Cert::parse(buf, sizeof(buf), d));
    CHECK(d.verify(admin.edPublic));
    d.number = 1204;  // qualcuno cambia il numero
    CHECK(!d.verify(admin.edPublic));
    Identity falso = Identity::generate();
    CHECK(!c.verify(falso.edPublic));
    CHECK_EQ(fingerprint(tel.edPublic).size(), size_t(19));
}

TEST(identita_da_semi) {
    Identity a = Identity::generate();
    uint8_t e[32], x[32];
    a.seeds(e, x);
    Identity b = Identity::fromSeeds(e, x);
    CHECK(a.edPublic == b.edPublic);
    CHECK(a.xPublic == b.xPublic);
    const uint8_t msg[] = "ciao";
    Sig64 s = b.sign(msg, 4);
    CHECK(verifySig(a.edPublic, msg, 4, s.data()));
}

TEST(cifratura_coppia) {
    Identity a = Identity::generate(), b = Identity::generate();
    Key32 kab = pairKey(a, b.xPublic), kba = pairKey(b, a.xPublic);
    CHECK(kab == kba);
    Header h;
    h.type = Type::DM_TEXT;
    h.src = 1;
    h.dst = 2;
    h.seq = 77;
    const uint8_t plain[] = "messaggio segreto";
    uint8_t cipher[64], out[64];
    sealPayload(kab, h, plain, sizeof(plain), cipher);
    CHECK(openPayload(kba, h, cipher, sizeof(plain) + TAG_SIZE, out));
    CHECK(std::memcmp(out, plain, sizeof(plain)) == 0);
    // Un campo dell'intestazione cambiato fa fallire la verifica.
    Header h2 = h;
    h2.dst = 3;
    CHECK(!openPayload(kba, h2, cipher, sizeof(plain) + TAG_SIZE, out));
    // I campi che cambiano a ogni salto no.
    Header h3 = h;
    h3.hops = 5;
    h3.via = 9;
    h3.lastHop = 8;
    CHECK(openPayload(kba, h3, cipher, sizeof(plain) + TAG_SIZE, out));
    cipher[3] ^= 1;
    CHECK(!openPayload(kba, h, cipher, sizeof(plain) + TAG_SIZE, out));
}

TEST(tempo_in_aria) {
    RadioParams p{869.525f, 125.0f, 10, 5, 8, 22};
    // Valori calcolati con la formula Semtech: 50 byte = 616 ms, 10 byte = 289 ms.
    CHECK_EQ(airtimeMs(p, 50), uint32_t(617));
    CHECK_EQ(airtimeMs(p, 10), uint32_t(289));
    RadioParams v = VOICE_PARAMS;
    CHECK(airtimeMs(v, 200) < airtimeMs(MAIN_PARAMS, 200) / 2 + 50);
}

TEST(contenuti_pacchetti) {
    uint8_t buf[MAX_PAYLOAD];
    GroupInvite g;
    g.groupId = 0x1234;
    randomBytes(g.key.data(), 32);
    g.name = "Squadra A";
    g.members = {1203, 1187, 1001};
    size_t n = encodeGroupInvite(g, buf, sizeof(buf));
    GroupInvite g2;
    CHECK(decodeGroupInvite(buf, n, g2));
    CHECK_EQ(g2.groupId, g.groupId);
    CHECK(g2.key == g.key);
    CHECK_EQ(g2.name, g.name);
    CHECK(g2.members == g.members);

    SosPayload s;
    s.round = 3;
    s.battery = 42;
    s.pos.valid = true;
    s.pos.latE7 = 458123000;
    s.pos.lonE7 = 90841000;
    n = encodeSos(s, buf, sizeof(buf));
    SosPayload s2;
    CHECK(decodeSos(buf, n, s2));
    CHECK(s2.active && s2.round == 3 && s2.battery == 42);
    CHECK_EQ(s2.pos.latE7, s.pos.latE7);

    VoiceData d{7, 2, 5, std::vector<uint8_t>(100, 0xAB)};
    n = encodeVoiceData(d, buf, sizeof(buf));
    VoiceData d2;
    CHECK(decodeVoiceData(buf, n, d2));
    CHECK(d2.data == d.data && d2.index == 2 && d2.total == 5);
}
