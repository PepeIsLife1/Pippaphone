// PippaNet: identita', certificati e cifratura (Monocypher).
//
// Ogni telefono ha due coppie di chiavi: Ed25519 per firmare e X25519 per cifrare.
// L'amministratore firma il certificato "numero -> chiavi"; chi riceve un certificato lo
// accetta solo se la firma e' della chiave dell'amministratore che conosce.
//
// Messaggi 1:1: chiave della coppia = BLAKE2b(X25519(mia, sua) || chiavi pubbliche),
// cifratura XChaCha20-Poly1305. Il nonce non viaggia: e' ricavato da mittente e seq,
// che sono unici per ogni pacchetto.
#pragma once

#include <array>
#include <string>

#include "pippanet/packet.h"
#include "pippanet/types.h"

namespace pn {

using Key32 = std::array<uint8_t, 32>;
using Sig64 = std::array<uint8_t, 64>;

constexpr size_t TAG_SIZE = 16;
constexpr size_t SIG_SIZE = 64;
constexpr size_t CERT_SIZE = 2 + 32 + 32 + 4 + 64;  // 134 byte

// Riempie buf di byte casuali (implementata dalla piattaforma).
void randomBytes(uint8_t* buf, size_t len);
// Cancella in modo sicuro.
void wipe(void* p, size_t len);

struct Identity {
    uint8_t edSecret[64] = {};  // seme (32) + pubblica (32), formato Monocypher
    Key32 edPublic{};
    Key32 xSecret{};
    Key32 xPublic{};

    static Identity generate();
    // Ricostruisce da due semi da 32 byte (per salvare in memoria solo i semi).
    static Identity fromSeeds(const uint8_t edSeed[32], const uint8_t xSeed[32]);
    void seeds(uint8_t edSeed[32], uint8_t xSeed[32]) const;

    Sig64 sign(const uint8_t* msg, size_t len) const;
};

struct Cert {
    Addr number = ADDR_NONE;
    Key32 edPublic{};
    Key32 xPublic{};
    uint32_t issued = 0;  // secondi Unix (0 se l'amministratore non aveva l'ora)
    Sig64 adminSig{};

    size_t serialize(uint8_t* out) const;                   // CERT_SIZE byte
    static bool parse(const uint8_t* in, size_t len, Cert& c);
    bool verify(const Key32& adminPublic) const;
    // Firma il certificato con la chiave dell'amministratore.
    void signWith(const Identity& admin);
};

// Verifica una firma Ed25519.
bool verifySig(const Key32& edPublic, const uint8_t* msg, size_t len, const uint8_t sig[64]);

// Codice di verifica da 16 cifre di una chiave pubblica ("4821 0937 5512 6604").
std::string fingerprint(const Key32& edPublic);
// 8 byte di impronta (per riconoscere un telefono senza numero).
uint64_t shortId(const Key32& edPublic);

// Chiave simmetrica di una coppia di telefoni.
Key32 pairKey(const Identity& me, const Key32& theirXPublic);

// Cifra/decifra un carico utile. out deve avere len + TAG_SIZE byte (seal) o len - TAG_SIZE (open).
// Il nonce e' ricavato dall'intestazione; i dati autenticati sono headerAd().
void sealPayload(const Key32& key, const Header& h, const uint8_t* plain, size_t len, uint8_t* out);
bool openPayload(const Key32& key, const Header& h, const uint8_t* cipher, size_t len, uint8_t* out);

}  // namespace pn
