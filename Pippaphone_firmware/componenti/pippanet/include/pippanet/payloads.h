// PippaNet: contenuto dei vari tipi di pacchetto (prima della cifratura).
#pragma once

#include <string>
#include <vector>

#include "pippanet/crypto.h"
#include "pippanet/packet.h"

namespace pn {

// DM_ACK
enum class AckKind : uint8_t { DELIVERED = 1, READ = 2 };
struct AckPayload {
    uint32_t ackedSeq = 0;
    AckKind kind = AckKind::DELIVERED;
};

// DM_TEXT / GROUP_TEXT / GLOBAL_TEXT: ora di invio (4 byte, secondi Unix, 0 se ignota) + testo a 7 bit.
struct TextPayload {
    uint32_t sentAt = 0;
    std::string text;
};

// Posizione GPS (POS_RESP, SOS).
struct Position {
    bool valid = false;
    int32_t latE7 = 0;   // gradi * 1e7
    int32_t lonE7 = 0;
    int16_t altM = 0;
    uint8_t accM = 0;    // precisione in metri (255 = sconosciuta)
    uint16_t ageS = 0;   // da quanti secondi e' stata presa
};

enum class PosStatus : uint8_t { OK = 0, DENIED = 1, NO_FIX = 2 };
struct PosResponse {
    uint32_t requestSeq = 0;
    PosStatus status = PosStatus::OK;
    Position pos;
};

struct SosPayload {
    bool active = true;     // false = SOS annullato
    uint16_t round = 0;     // quante volte e' stato ripetuto
    uint8_t battery = 255;  // % (255 = sconosciuta)
    Position pos;
};

struct GroupInvite {
    uint16_t groupId = 0;
    Key32 key{};
    std::string name;
    std::vector<Addr> members;  // compreso chi crea il gruppo
};

enum class Role : uint8_t { PHONE = 1, REPEATER = 2 };
struct Beacon {
    Role role = Role::PHONE;
    bool gpsTime = false;   // l'ora viene dal GPS
    bool timeValid = false;
    uint8_t battery = 255;
    uint32_t unixTime = 0;
};

struct ProvHello {
    Key32 edPublic{};
    Key32 xPublic{};
};

struct ProvCert {
    uint64_t target = 0;  // shortId() del telefono a cui e' rivolto
    Cert cert;
    Key32 adminPublic{};
};

enum class Codec : uint8_t { CODEC2_700C = 1, CODEC2_1300 = 2, CODEC2_3200 = 3 };
struct VoiceReq {
    uint16_t session = 0;
    Codec codec = Codec::CODEC2_700C;
    uint8_t packets = 0;
    uint16_t durationMs = 0;
};
struct VoiceAccept {
    uint16_t session = 0;
    bool busy = false;
    uint16_t holdMs = 0;  // quanto resta sul canale voce
};
struct VoiceData {
    uint16_t session = 0;
    uint8_t index = 0;
    uint8_t total = 0;
    std::vector<uint8_t> data;
};
struct VoiceEnd {
    uint16_t session = 0;
    bool fromReceiver = false;
    uint32_t missing = 0;  // bit i = pezzo i mancante
};

// ---- chiamata in diretta (a turni) ----
// Bit di voce di un frame e durata del frame per ogni codec.
inline int codecFrameBits(Codec c) { return c == Codec::CODEC2_700C ? 28 : c == Codec::CODEC2_1300 ? 52 : 64; }
inline int codecFrameMs(Codec c) { return c == Codec::CODEC2_3200 ? 20 : 40; }
// Byte che occupano 'frames' frame messi uno dietro l'altro, senza buchi tra un frame e l'altro.
inline size_t codecBytes(Codec c, int frames) { return size_t((codecFrameBits(c) * frames + 7) / 8); }

// Frame di 700C (40 ms l'uno) in un pacchetto: 10 (400 ms) con 0 o 1 ripetitore, 12 (480 ms) con
// 2 o piu'. In una catena il pacchetto nuovo non deve partire prima che il secondo ripetitore abbia
// finito di ripetere il precedente (chi parla non lo sente, il primo ripetitore si'): ci vogliono
// 3 trasmissioni per pacchetto, e con 10 frame non ci stanno.
#ifndef PN_CALL_FRAMES_FAR  // (solo per le prove del simulatore: strumenti/pianificatore)
#define PN_CALL_FRAMES_FAR 12
#endif
constexpr uint8_t CALL_FRAMES_NEAR = 10, CALL_FRAMES_FAR = PN_CALL_FRAMES_FAR;
inline uint8_t callFrames(uint8_t hops) { return hops >= 2 ? CALL_FRAMES_FAR : CALL_FRAMES_NEAR; }
constexpr uint32_t CALL_RELAY_TTL_MS = 400;       // un pezzo che aspetta di piu' nel ripetitore si butta
constexpr uint32_t CALL_MAX_MS = 3 * 60 * 1000;   // durata massima: chi e' in chiamata non sente i messaggi
constexpr uint32_t CALL_TURN_MAX_MS = 60 * 1000;  // un turno di parola al massimo

enum class CallRingStatus : uint8_t { RINGING = 0, BUSY = 1 };
enum class CallEndReason : uint8_t { HANGUP = 0, REFUSED = 1, TOO_LONG = 2 };
struct CallReq {
    uint16_t session = 0;
    Codec codec = Codec::CODEC2_700C;
};
struct CallRing {
    uint16_t session = 0;
    CallRingStatus status = CallRingStatus::RINGING;
};
struct CallAnswer {
    uint16_t session = 0;
};
struct CallEndMsg {
    uint16_t session = 0;
    CallEndReason reason = CallEndReason::HANGUP;
};
// CALL_VOICE: un pacchetto ogni callFrames() frame mentre si parla, vuoto per dire "ci sono" o per
// tenere aperta la strada nel silenzio. Con la ridondanza porta anche la voce del pacchetto prima.
constexpr uint8_t CALL_FLAG_PING = 0x01;  // "ci sono": chi lo riceve risponde con un pacchetto vuoto
constexpr uint8_t CALL_FLAG_LAST = 0x02;  // ultimo pacchetto del turno: ora puo' parlare l'altro
struct CallVoice {
    uint16_t session = 0;
    uint8_t turn = 0;    // turno di parola di chi manda
    uint8_t index = 0;   // pacchetto nel turno (0, 1, 2...)
    uint8_t flags = 0;
    uint8_t frames = 0;      // frame di voce di questo pacchetto (0..15)
    uint8_t prevFrames = 0;  // frame del pacchetto prima (index - 1) portati di nuovo (0 = nessuno)
    const uint8_t* prev = nullptr;  // puntano dentro il pacchetto decodificato
    size_t prevLen = 0;
    const uint8_t* data = nullptr;
    size_t len = 0;
};

// Codifica/decodifica. Le funzioni encode restituiscono i byte scritti (0 = errore).
size_t encodeAck(const AckPayload& p, uint8_t* out, size_t cap);
bool decodeAck(const uint8_t* in, size_t len, AckPayload& p);
size_t encodeTextPayload(const TextPayload& p, uint8_t* out, size_t cap);
bool decodeTextPayload(const uint8_t* in, size_t len, TextPayload& p);
size_t encodePosition(const Position& p, Writer& w);
bool decodePosition(Reader& r, Position& p);
size_t encodePosResponse(const PosResponse& p, uint8_t* out, size_t cap);
bool decodePosResponse(const uint8_t* in, size_t len, PosResponse& p);
size_t encodeSos(const SosPayload& p, uint8_t* out, size_t cap);
bool decodeSos(const uint8_t* in, size_t len, SosPayload& p);
size_t encodeGroupInvite(const GroupInvite& g, uint8_t* out, size_t cap);
bool decodeGroupInvite(const uint8_t* in, size_t len, GroupInvite& g);
size_t encodeBeacon(const Beacon& b, uint8_t* out, size_t cap);
bool decodeBeacon(const uint8_t* in, size_t len, Beacon& b);
size_t encodeProvHello(const ProvHello& p, uint8_t* out, size_t cap);
bool decodeProvHello(const uint8_t* in, size_t len, ProvHello& p);
size_t encodeProvCert(const ProvCert& p, uint8_t* out, size_t cap);
bool decodeProvCert(const uint8_t* in, size_t len, ProvCert& p);
size_t encodeVoiceReq(const VoiceReq& p, uint8_t* out, size_t cap);
bool decodeVoiceReq(const uint8_t* in, size_t len, VoiceReq& p);
size_t encodeVoiceAccept(const VoiceAccept& p, uint8_t* out, size_t cap);
bool decodeVoiceAccept(const uint8_t* in, size_t len, VoiceAccept& p);
size_t encodeVoiceData(const VoiceData& p, uint8_t* out, size_t cap);
bool decodeVoiceData(const uint8_t* in, size_t len, VoiceData& p);
size_t encodeVoiceEnd(const VoiceEnd& p, uint8_t* out, size_t cap);
bool decodeVoiceEnd(const uint8_t* in, size_t len, VoiceEnd& p);
size_t encodeCallReq(const CallReq& p, uint8_t* out, size_t cap);
bool decodeCallReq(const uint8_t* in, size_t len, CallReq& p);
size_t encodeCallRing(const CallRing& p, uint8_t* out, size_t cap);
bool decodeCallRing(const uint8_t* in, size_t len, CallRing& p);
size_t encodeCallAnswer(const CallAnswer& p, uint8_t* out, size_t cap);
bool decodeCallAnswer(const uint8_t* in, size_t len, CallAnswer& p);
size_t encodeCallEnd(const CallEndMsg& p, uint8_t* out, size_t cap);
bool decodeCallEnd(const uint8_t* in, size_t len, CallEndMsg& p);
// La voce si decodifica sapendo il codec della chiamata (per dividere i due pezzi).
size_t encodeCallVoice(const CallVoice& p, uint8_t* out, size_t cap);
bool decodeCallVoice(const uint8_t* in, size_t len, Codec codec, CallVoice& p);

// Byte di dati vocali che stanno in un VOICE_DATA: tolti tag, id del messaggio (4) e
// intestazione del pezzo (4).
constexpr size_t VOICE_DATA_MAX = MAX_PAYLOAD - TAG_SIZE - 4 - 4;
constexpr size_t MAX_VOICE_PACKETS = 32;
// Byte di un pezzo di vocale: multiplo di 4 (un frame di Codec2 700C = 4 byte).
constexpr size_t VOICE_CHUNK = (VOICE_DATA_MAX / 4) * 4;

}  // namespace pn
