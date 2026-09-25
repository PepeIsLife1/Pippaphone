#include "pippanet/crypto.h"

#include <algorithm>
#include <cstdio>

#include "monocypher-ed25519.h"
#include "monocypher.h"

namespace pn {

void wipe(void* p, size_t len) { crypto_wipe(p, len); }

Identity Identity::fromSeeds(const uint8_t edSeed[32], const uint8_t xSeed[32]) {
    Identity id;
    uint8_t seed[32];
    std::memcpy(seed, edSeed, 32);
    crypto_ed25519_key_pair(id.edSecret, id.edPublic.data(), seed);  // cancella seed
    std::memcpy(id.xSecret.data(), xSeed, 32);
    crypto_x25519_public_key(id.xPublic.data(), id.xSecret.data());
    return id;
}

Identity Identity::generate() {
    uint8_t edSeed[32], xSeed[32];
    randomBytes(edSeed, 32);
    randomBytes(xSeed, 32);
    Identity id = fromSeeds(edSeed, xSeed);
    wipe(edSeed, 32);
    wipe(xSeed, 32);
    return id;
}

void Identity::seeds(uint8_t edSeed[32], uint8_t xSeed[32]) const {
    std::memcpy(edSeed, edSecret, 32);
    std::memcpy(xSeed, xSecret.data(), 32);
}

Sig64 Identity::sign(const uint8_t* msg, size_t len) const {
    Sig64 s{};
    crypto_ed25519_sign(s.data(), edSecret, msg, len);
    return s;
}

bool verifySig(const Key32& edPublic, const uint8_t* msg, size_t len, const uint8_t sig[64]) {
    return crypto_ed25519_check(sig, edPublic.data(), msg, len) == 0;
}

namespace {
constexpr char CERT_PREFIX[] = "PNCERT1";  // separa le firme dei certificati da tutte le altre
constexpr size_t CERT_BODY = 2 + 32 + 32 + 4;

void certBody(const Cert& c, uint8_t* out) {
    Writer w(out, CERT_BODY);
    w.u16(c.number);
    w.put(c.edPublic.data(), 32);
    w.put(c.xPublic.data(), 32);
    w.u32(c.issued);
}
}  // namespace

size_t Cert::serialize(uint8_t* out) const {
    certBody(*this, out);
    std::memcpy(out + CERT_BODY, adminSig.data(), 64);
    return CERT_SIZE;
}

bool Cert::parse(const uint8_t* in, size_t len, Cert& c) {
    if (len < CERT_SIZE) return false;
    Reader r(in, len);
    c.number = r.u16();
    r.get(c.edPublic.data(), 32);
    r.get(c.xPublic.data(), 32);
    c.issued = r.u32();
    r.get(c.adminSig.data(), 64);
    return r.ok();
}

static size_t certMessage(const Cert& c, uint8_t* msg) {
    std::memcpy(msg, CERT_PREFIX, sizeof(CERT_PREFIX) - 1);
    certBody(c, msg + sizeof(CERT_PREFIX) - 1);
    return sizeof(CERT_PREFIX) - 1 + CERT_BODY;
}

bool Cert::verify(const Key32& adminPublic) const {
    uint8_t msg[sizeof(CERT_PREFIX) + CERT_BODY];
    size_t n = certMessage(*this, msg);
    return verifySig(adminPublic, msg, n, adminSig.data());
}

void Cert::signWith(const Identity& admin) {
    uint8_t msg[sizeof(CERT_PREFIX) + CERT_BODY];
    size_t n = certMessage(*this, msg);
    adminSig = admin.sign(msg, n);
}

uint64_t shortId(const Key32& edPublic) {
    uint8_t h[8];
    crypto_blake2b(h, sizeof(h), edPublic.data(), 32);
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | h[i];
    return v;
}

std::string fingerprint(const Key32& edPublic) {
    uint64_t v = shortId(edPublic) % 10000000000000000ULL;  // 16 cifre
    char digits[17];
    std::snprintf(digits, sizeof(digits), "%016llu", (unsigned long long)v);
    std::string s;
    for (int i = 0; i < 16; i++) {
        if (i && i % 4 == 0) s.push_back(' ');
        s.push_back(digits[i]);
    }
    return s;
}

Key32 pairKey(const Identity& me, const Key32& theirXPublic) {
    uint8_t shared[32];
    crypto_x25519(shared, me.xSecret.data(), theirXPublic.data());
    // Le due pubbliche in ordine fisso, cosi' entrambi i lati ottengono la stessa chiave.
    const Key32& a = std::min(me.xPublic, theirXPublic);
    const Key32& b = std::max(me.xPublic, theirXPublic);
    static const char ctx[] = "pippanet-dm-v1";
    crypto_blake2b_ctx h;
    crypto_blake2b_init(&h, 32);
    crypto_blake2b_update(&h, shared, 32);
    crypto_blake2b_update(&h, a.data(), 32);
    crypto_blake2b_update(&h, b.data(), 32);
    crypto_blake2b_update(&h, reinterpret_cast<const uint8_t*>(ctx), sizeof(ctx) - 1);
    Key32 k{};
    crypto_blake2b_final(&h, k.data());
    wipe(shared, sizeof(shared));
    return k;
}

static void makeNonce(const Header& h, uint8_t nonce[24]) {
    std::memset(nonce, 0, 24);
    nonce[0] = uint8_t(h.src);
    nonce[1] = uint8_t(h.src >> 8);
    nonce[2] = uint8_t(h.seq);
    nonce[3] = uint8_t(h.seq >> 8);
    nonce[4] = uint8_t(h.seq >> 16);
    nonce[5] = uint8_t(h.seq >> 24);
    nonce[6] = uint8_t(h.type) & 0x3F;
}

void sealPayload(const Key32& key, const Header& h, const uint8_t* plain, size_t len, uint8_t* out) {
    uint8_t nonce[24], ad[AD_SIZE];
    makeNonce(h, nonce);
    headerAd(h, ad);
    crypto_aead_lock(out, out + len, key.data(), nonce, ad, AD_SIZE, plain, len);
}

bool openPayload(const Key32& key, const Header& h, const uint8_t* cipher, size_t len, uint8_t* out) {
    if (len < TAG_SIZE) return false;
    size_t n = len - TAG_SIZE;
    uint8_t nonce[24], ad[AD_SIZE];
    makeNonce(h, nonce);
    headerAd(h, ad);
    return crypto_aead_unlock(out, cipher + n, key.data(), nonce, ad, AD_SIZE, cipher, n) == 0;
}

}  // namespace pn
