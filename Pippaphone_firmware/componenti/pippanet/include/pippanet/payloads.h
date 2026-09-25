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

// Byte di dati vocali che stanno in un VOICE_DATA: tolti tag, id del messaggio (4) e
// intestazione del pezzo (4).
constexpr size_t VOICE_DATA_MAX = MAX_PAYLOAD - TAG_SIZE - 4 - 4;
constexpr size_t MAX_VOICE_PACKETS = 32;
// Byte di un pezzo di vocale: multiplo di 4 (un frame di Codec2 700C = 4 byte).
constexpr size_t VOICE_CHUNK = (VOICE_DATA_MAX / 4) * 4;

}  // namespace pn
