// PippaNet: logica del telefono (messaggi, gruppi, canale globale, posizione, SOS, vocali,
// certificati e assegnazione del numero).
//
// Il telefono non ripete i pacchetti degli altri: parla con il ripetitore piu' vicino o
// direttamente con un altro telefono a portata.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "pippanet/crypto.h"
#include "pippanet/node.h"
#include "pippanet/payloads.h"

namespace pn {

enum class Delivery : uint8_t {
    PENDING = 0,      // in invio (aspetta il certificato o la conferma)
    SENT = 1,         // trasmesso almeno una volta
    DELIVERED = 2,    // ✓ consegnato
    READ = 3,         // ✓✓ letto
    UNREACHABLE = 4,  // non raggiungibile
};

enum class VoiceSendState : uint8_t { REQUESTING, SENDING, DELIVERED, PARTIAL, BUSY, UNREACHABLE };

struct GroupInfo {
    uint16_t id = 0;
    Key32 key{};
    std::string name;
    std::vector<Addr> members;
    Addr creator = ADDR_NONE;
};

// Eventi verso l'applicazione (interfaccia grafica e memoria).
class PhoneListener {
public:
    virtual ~PhoneListener() = default;
    virtual void onText(Addr from, uint32_t msgId, const TextPayload& t) {}
    virtual void onGroupText(uint16_t group, Addr from, uint32_t msgId, const TextPayload& t) {}
    virtual void onGlobalText(Addr from, uint32_t msgId, const TextPayload& t) {}
    virtual void onDelivery(Addr to, uint32_t msgId, Delivery d) {}
    virtual void onPositionRequest(Addr from, uint32_t requestId) {}
    virtual void onPosition(Addr from, const PosResponse& p) {}
    virtual void onSos(Addr from, const SosPayload& s) {}
    virtual void onSosAck(Addr from) {}
    virtual void onGroupInvite(const GroupInfo& g, Addr from) {}
    virtual void onVoiceIncoming(Addr from, const VoiceReq& r) {}
    virtual void onVoice(Addr from, Codec codec, const std::vector<uint8_t>& data, uint16_t durationMs,
                         uint32_t missing) {}
    virtual void onVoiceState(Addr to, uint32_t id, VoiceSendState s) {}
    virtual void onCertLearned(const Cert& c) {}
    virtual void onProvisionOffer(const Cert& c, const Key32& adminPublic) {}
    virtual void onProvisionHello(uint64_t shortId, const ProvHello& h, const RxInfo& info) {}
    virtual void onBeacon(Addr from, const Beacon& b, const RxInfo& info) {}
    virtual void onTimeSync(uint32_t unixSec) {}
};

class Phone : public Node {
public:
    Phone(const Identity& id, PhoneListener& listener);

    // ---- identita' e fiducia ----
    // Certificato proprio e chiave dell'amministratore (dopo l'assegnazione del numero).
    void setCredentials(const Cert& own, const Key32& adminPublic);
    bool hasNumber() const { return self_ != ADDR_NONE; }
    const Identity& identity() const { return id_; }
    const Cert& ownCert() const { return own_; }
    const Key32& adminPublic() const { return adminPub_; }
    // Certificati degli altri caricati dalla memoria.
    void addKnownCert(const Cert& c);
    bool knowsCert(Addr a) const { return certs_.count(a) != 0; }
    const Cert* certOf(Addr a) const;

    // Telefono dell'amministratore: chiave per firmare i certificati.
    void setAdminIdentity(const Identity& admin) { admin_ = admin; isAdmin_ = true; }
    bool isAdmin() const { return isAdmin_; }
    // Assegna un numero a un telefono nuovo sentito con PROV_HELLO.
    bool provision(const ProvHello& hello, Addr number, Millis now);
    // Telefono senza numero: si annuncia per farsi assegnare un numero.
    void setProvisioning(bool on) { provisioning_ = on; }

    // ---- gruppi ----
    void addGroup(const GroupInfo& g) { groups_[g.id] = g; }
    void removeGroup(uint16_t id) { groups_.erase(id); }
    GroupInfo createGroup(const std::string& name, const std::vector<Addr>& others, Millis now);

    // ---- azioni (restituiscono l'id del messaggio, 0 = errore) ----
    uint32_t sendText(Addr to, const std::string& text, Millis now);
    uint32_t sendGroupText(uint16_t group, const std::string& text, Millis now);
    uint32_t sendGlobalText(const std::string& text, Millis now);
    void sendRead(Addr to, uint32_t msgId, Millis now);
    uint32_t requestPosition(Addr to, Millis now);
    void answerPosition(Addr to, uint32_t requestId, PosStatus status, const Position& pos, Millis now);
    // SOS: parte subito verso tutti i contatti e si ripete ogni intervallo finche' non si annulla.
    void startSos(const std::vector<Addr>& contacts, Millis now);
    void stopSos(Millis now);
    bool sosActive() const { return sos_.active; }
    // Vocale gia' codificato (Codec2).
    uint32_t sendVoice(Addr to, Codec codec, const std::vector<uint8_t>& data, uint16_t durationMs, Millis now);

    // Dati aggiornati dall'applicazione.
    Position position;       // ultima posizione GPS
    uint8_t battery = 255;
    Millis sosIntervalMs = 2 * 60 * 1000;
    Millis beaconIntervalMs = 5 * 60 * 1000;

    void tick(Millis now) override;
    Channel activeChannel(Millis now) const override;
    void onTransmitted(const uint8_t* data, size_t len, Millis now) override;

    // Segnale dell'ultimo ripetitore sentito.
    struct Heard { Addr addr = ADDR_NONE; int16_t rssi = 0; float snr = 0; Millis at = 0; };
    const Heard& lastRepeater() const { return lastRepeater_; }
    const std::map<Addr, Heard>& repeatersHeard() const { return repeaters_; }

protected:
    void handle(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now) override;
    void onDuplicate(const Header& h, const RxInfo& info, Millis now) override;
    bool mayTransmit(const Frame& f, Millis now) const override;

private:
    // Messaggio a tutti appena trasmesso: se nessun ripetitore lo ripete, lo si rimanda una volta.
    struct EchoWatch {
        uint32_t seq;
        Millis until;
        Frame frame;
        int retries;
    };
    std::vector<EchoWatch> echo_;
    Millis paceUntil_ = 0;  // prima di questo momento niente nuovi pacchetti miei (non urgenti)
    // Messaggio affidabile in attesa di conferma.
    struct Outgoing {
        uint32_t msgId;
        Addr to;
        Type type;
        std::vector<uint8_t> body;  // contenuto in chiaro dopo msgId
        int attempts;
        int certTries;
        Millis nextTry;
        bool waitingCert;
        Channel ch;
    };
    struct PendingIn {  // pacchetto ricevuto da qualcuno di cui non ho ancora il certificato
        std::vector<uint8_t> raw;
        RxInfo info;
        Millis at;
    };
    struct VoiceOut {
        bool active = false;
        Addr to = ADDR_NONE;
        uint32_t msgId = 0;
        uint16_t session = 0;
        Codec codec = Codec::CODEC2_700C;
        uint16_t durationMs = 0;
        std::vector<std::vector<uint8_t>> chunks;
        enum Phase { REQ, WAIT_SWITCH, SEND, WAIT_REPORT, DONE } phase = REQ;
        int rounds = 0;
        int certTries = 0;
        int reasks = 0;
        Millis deadline = 0;
        Millis channelUntil = 0;
        // Invio a passo: un pezzo alla volta, il successivo quando i ripetitori hanno finito.
        std::deque<uint8_t> toSend;
        bool endPending = false;
        bool inFlight = false;
        uint32_t lastSeq = 0;
        Millis lastAir = 0;
        Millis nextSendAt = 0;
    };
    bool viaRepeaters(Millis now) const;
    void voiceSendStep(Millis now);
    struct VoiceIn {
        bool active = false;
        Addr from = ADDR_NONE;
        uint16_t session = 0;
        Codec codec = Codec::CODEC2_700C;
        uint16_t durationMs = 0;
        uint8_t total = 0;
        std::vector<std::vector<uint8_t>> chunks;
        uint32_t have = 0;
        Millis channelUntil = 0;
        Millis lastActivity = 0;
        bool reported = false;
    };
    struct SosState {
        bool active = false;
        std::vector<Addr> contacts;
        uint16_t round = 0;
        Millis next = 0;
    };

    Key32 keyFor(Addr peer);
    uint32_t sendPairwise(Addr to, Type t, uint32_t msgId, const uint8_t* body, size_t len, bool wantAck, Millis now,
                      bool forceFlood = false, Channel ch = Channel::MAIN, Millis delay = 0);
    uint32_t queueReliable(Addr to, Type t, const uint8_t* body, size_t len, Millis now);
    void trySend(Outgoing& o, Millis now);
    void requestCert(Addr to, Millis now);
    void sendCert(Addr to, Type t, Millis now);
    void sendAck(Addr to, uint32_t msgId, AckKind kind, Millis now);
    Millis ackTimeout(size_t len, int attempt) const;
    void handlePairwise(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now);
    void handleSigned(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now);
    void handleVoice(const Header& h, Type t, uint32_t msgId, const uint8_t* body, size_t len, Millis now);
    void voiceTick(Millis now);
    void finishVoiceIn(Millis now);
    void sendBeacon(Millis now);
    void sendSosRound(Millis now);
    bool learnCert(const Cert& c);
    bool alreadyHandled(Addr from, uint32_t msgId);

    Identity id_;
    Cert own_;
    Key32 adminPub_{};
    bool trusted_ = false;
    Identity admin_;
    bool isAdmin_ = false;
    bool provisioning_ = false;
    PhoneListener& L_;

    std::map<Addr, Cert> certs_;
    std::map<Addr, Key32> keys_;
    std::map<uint16_t, GroupInfo> groups_;
    std::vector<Outgoing> out_;
    std::map<Addr, std::vector<PendingIn>> waitingCert_;
    std::map<Addr, Millis> certAsked_;
    std::map<Addr, Millis> certSent_;
    std::deque<std::pair<Addr, uint32_t>> handled_;
    std::map<Addr, Heard> repeaters_;
    Heard lastRepeater_;
    VoiceOut vOut_;
    VoiceIn vIn_;
    SosState sos_;
    Addr lastVoiceFrom_ = ADDR_NONE;   // ultimo vocale ricevuto completo (per rispondere se il
    uint16_t lastVoiceSession_ = 0;    // mittente non ha sentito il resoconto)
    Millis nextBeacon_ = 0;
    Millis nextHello_ = 0;
};

}  // namespace pn
