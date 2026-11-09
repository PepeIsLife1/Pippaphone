// PippaNet: tipi e costanti comuni a telefono e ripetitore.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pn {

// Indirizzo di rete a 16 bit.
//   1..59999      telefoni (il "numero" assegnato dall'amministratore)
//   0xF000..FFFE  ripetitori
//   0x0000        nessuno / telefono senza numero
//   0xFFFF        tutti (canale globale, beacon)
using Addr = uint16_t;
constexpr Addr ADDR_NONE = 0x0000;
constexpr Addr ADDR_BROADCAST = 0xFFFF;
constexpr Addr ADDR_REPEATER_FIRST = 0xF000;
constexpr Addr ADDR_PHONE_MAX = 59999;

inline bool isRepeaterAddr(Addr a) { return a >= ADDR_REPEATER_FIRST && a != ADDR_BROADCAST; }
inline bool isPhoneAddr(Addr a) { return a >= 1 && a <= ADDR_PHONE_MAX; }

// Tempo in millisecondi da un'origine qualsiasi (monotono).
using Millis = uint64_t;

// Canali radio: i telefoni ascoltano sempre MAIN; VOICE si usa solo durante un vocale.
enum class Channel : uint8_t { MAIN = 0, VOICE = 1 };

// Tipi di pacchetto (6 bit).
enum class Type : uint8_t {
    DM_TEXT = 0x01,       // SMS 1:1, cifrato end-to-end
    DM_ACK = 0x02,        // conferma di consegna/lettura, cifrata
    GROUP_TEXT = 0x03,    // messaggio di gruppo, cifrato con la chiave del gruppo e firmato
    GLOBAL_TEXT = 0x04,   // canale globale, in chiaro e firmato
    CERT_REQ = 0x05,      // "mandami il tuo certificato" (contiene il mio)
    CERT_RESP = 0x06,     // certificato (a tutti, cosi' lo imparano anche gli altri)
    POS_REQ = 0x07,       // richiesta di posizione, cifrata
    POS_RESP = 0x08,      // posizione o rifiuto, cifrata
    SOS = 0x09,           // SOS con posizione, cifrato, un pacchetto per contatto
    GROUP_INVITE = 0x0A,  // invito a un gruppo (chiave e membri), cifrato
    BEACON = 0x10,        // "ci sono" ogni 5 minuti, non ripetuto
    PROV_HELLO = 0x11,    // telefono nuovo senza numero, non ripetuto
    PROV_CERT = 0x12,     // l'amministratore assegna un numero, non ripetuto
    VOICE_REQ = 0x20,     // chiede di aprire un vocale sul canale voce
    VOICE_ACCEPT = 0x21,  // il destinatario accetta: si passa al canale voce
    VOICE_DATA = 0x22,    // pezzo di vocale (canale voce)
    VOICE_END = 0x23,     // fine vocale / pezzi mancanti (canale voce)
    CALL_REQ = 0x24,      // chiamata in diretta: "ti chiamo" (canale messaggi)
    CALL_RING = 0x25,     // "squilla" oppure "occupato" (canale messaggi)
    CALL_ANSWER = 0x26,   // "ho risposto": chi lo ripete passa al canale voce
    CALL_VOICE = 0x27,    // voce della chiamata (canale voce)
    CALL_END = 0x28,      // fine o rifiuto della chiamata (su tutti e due i canali)
    ADMIN_FIRST = 0x30,   // 0x30..0x3F riservati al programma di amministrazione dei ripetitori
    ADMIN_LAST = 0x3F,
};

// Priorita' di trasmissione (piu' alto = prima).
enum class Priority : uint8_t { Background = 0, Normal = 1, Urgent = 2, Emergency = 3 };

inline Priority priorityOf(Type t) {
    switch (t) {
        case Type::SOS: return Priority::Emergency;
        case Type::DM_ACK:
        case Type::VOICE_ACCEPT:
        case Type::CALL_RING:
        case Type::CALL_ANSWER:
        case Type::CALL_VOICE: return Priority::Urgent;  // VOICE_END no: deve uscire dopo i dati
        case Type::BEACON:
        case Type::PROV_HELLO: return Priority::Background;
        default: return Priority::Normal;
    }
}

// Tipi cifrati con la chiave della coppia (end-to-end).
inline bool isPairwise(Type t) {
    switch (t) {
        case Type::DM_TEXT:
        case Type::DM_ACK:
        case Type::POS_REQ:
        case Type::POS_RESP:
        case Type::SOS:
        case Type::GROUP_INVITE:
        case Type::VOICE_REQ:
        case Type::VOICE_ACCEPT:
        case Type::VOICE_DATA:
        case Type::VOICE_END:
        case Type::CALL_REQ:
        case Type::CALL_RING:
        case Type::CALL_ANSWER:
        case Type::CALL_VOICE:
        case Type::CALL_END: return true;
        default: return false;
    }
}

// Pacchetti della chiamata in diretta.
inline bool isCall(Type t) { return uint8_t(t) >= uint8_t(Type::CALL_REQ) && uint8_t(t) <= uint8_t(Type::CALL_END); }

// Pacchetti che i ripetitori non ritrasmettono mai (solo vicini diretti).
inline bool isLocalOnly(Type t) {
    return t == Type::BEACON || t == Type::PROV_HELLO || t == Type::PROV_CERT;
}

constexpr size_t MAX_FRAME = 255;         // limite LoRa
constexpr size_t MAX_TEXT_CHARS = 160;    // come un SMS
constexpr uint8_t DEFAULT_HOP_LIMIT = 10; // salti massimi
constexpr uint8_t MAX_GROUP_MEMBERS = 16;

}  // namespace pn
