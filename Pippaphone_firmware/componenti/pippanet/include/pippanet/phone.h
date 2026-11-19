// PippaNet: logica del telefono (messaggi, gruppi, canale globale, posizione, SOS, vocali,
// chiamate in diretta, certificati e assegnazione del numero).
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
    RETRYING = 5,     // non raggiungibile per ora: riprova fra qualche minuto (poi UNREACHABLE o DELIVERED)
};

enum class VoiceSendState : uint8_t { REQUESTING, SENDING, DELIVERED, PARTIAL, BUSY, UNREACHABLE };

// Chiamata in diretta, a turni (si parla tenendo premuto il PTT).
enum class CallState : uint8_t {
    IDLE = 0,
    DIALING = 1,     // chiamo: aspetto che squilli
    RINGBACK = 2,    // squilla dall'altra parte
    INCOMING = 3,    // mi chiamano
    CONNECTING = 4,  // risposto: si apre la strada sul canale voce
    ACTIVE = 5,      // in chiamata
};
// Perche' una chiamata e' finita (con CallState::IDLE).
enum class CallResult : uint8_t {
    NONE = 0,
    ENDED = 1,        // ho riattaccato (o annullato, o rifiutato) io
    HUNG_UP = 2,      // ha riattaccato l'altro
    REFUSED = 3,      // l'altro ha rifiutato
    BUSY = 4,         // l'altro e' occupato
    NO_ANSWER = 5,    // squillava ma non ha risposto
    UNREACHABLE = 6,  // non squilla
    DROPPED = 7,      // caduta: non si sente piu' l'altro
    TOO_LONG = 8,     // arrivata alla durata massima
    MISSED = 9,       // chiamata in arrivo a cui non ho risposto
};

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
    // Stato della chiamata cambiato (result vale solo con CallState::IDLE).
    virtual void onCallState(Addr peer, CallState s, CallResult result) {}
    // Voce dall'altro: 'frames' frame del pacchetto 'index' del turno 'turn' (Codec2 700C, frame
    // uno dietro l'altro senza buchi). prev/prevFrames: il pacchetto prima, se viaggia anche questo
    // (ridondanza). last: l'altro ha finito di parlare.
    virtual void onCallVoice(Addr from, uint8_t turn, uint8_t index, const uint8_t* data, size_t len,
                             uint8_t frames, const uint8_t* prev, size_t prevLen, uint8_t prevFrames, bool last) {}
    virtual void onCertLearned(const Cert& c) {}
    virtual void onProvisionOffer(const Cert& c, const Key32& adminPublic) {}
    virtual void onProvisionHello(uint64_t shortId, const ProvHello& h, const RxInfo& info) {}
    virtual void onBeacon(Addr from, const Beacon& b, const RxInfo& info) {}
    virtual void onTimeSync(uint32_t unixSec) {}
    // Sfide via radio: i byte della mossa li interpreta l'applicazione (pippanet li porta e basta).
    virtual void onGame(Addr from, uint32_t msgId, const uint8_t* data, size_t len) {}
    // Consegnata (DELIVERED), da riprovare piu' tardi (RETRYING) o non raggiungibile (UNREACHABLE).
    virtual void onGameDelivery(Addr to, uint32_t msgId, Delivery d) {}
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
    // Mossa di una sfida via radio (al massimo MAX_GAME_BYTES byte): arriva una volta sola, con
    // conferma e gli stessi tentativi di un SMS.
    static constexpr size_t MAX_GAME_BYTES = 64;
    uint32_t sendGame(Addr to, const uint8_t* data, size_t len, Millis now);
    // Toglie dai tentativi un GAME non ancora confermato (un invito abbandonato non deve arrivare
    // piu' tardi). Della sua consegna non si sapra' piu' niente.
    void cancelGame(Addr to, uint32_t msgId);

    // ---- chiamata in diretta (a turni) ----
    // Chiama un telefono; false se c'e' gia' una chiamata o un vocale in corso.
    bool call(Addr to, Millis now);
    void answerCall(Millis now);
    // Riattacca, rifiuta o annulla.
    void endCall(Millis now);
    // PTT premuto (true) o lasciato. false se adesso parla l'altro.
    bool callTalk(bool on, Millis now);
    // Un pacchetto di voce: callFramesPerPacket() frame Codec2 700C uno dietro l'altro (meno solo
    // nell'ultimo). last = ultimo del turno (si smette di parlare; puo' essere vuoto).
    bool callSendVoice(const uint8_t* data, size_t len, uint8_t frames, bool last, Millis now);
    CallState callState() const { return call_.state; }
    Addr callPeer() const { return call_.peer; }
    bool callTalking() const { return call_.talking; }
    // Si sta ricevendo la voce dell'altro.
    bool callPeerTalking(Millis now) const;
    // Ripetitori sul percorso e se i pacchetti portano anche il precedente (con 0 o 1 ripetitore).
    uint8_t callHops() const { return call_.hops; }
    bool callRedundant() const { return call_.redundant; }
    // Frame di voce in ogni pacchetto (dipende dai ripetitori sul percorso) e ogni quanto parte.
    uint8_t callFramesPerPacket() const { return callFrames(call_.hops); }
    Millis callPacketMs() const { return Millis(callFramesPerPacket()) * 40; }
    // Millisecondi da quando ci si sente (0 se non ancora).
    Millis callElapsedMs(Millis now) const { return call_.connectedAt ? now - call_.connectedAt : 0; }

    // ---- ora ----
    // Ora da qualunque fonte tranne la mano (GPS, beacon): toglie anche il segno di ora "locale".
    // Nasconde Node::setUnixTime apposta, cosi' nessuna strada dimentica di toglierlo.
    // (Se Node la ignora, l'ora e' gia' del GPS e quindi non era locale.)
    void setUnixTime(uint32_t unixSec, Millis now, bool fromGps) {
        Node::setUnixTime(unixSec, now, fromGps);
        timeLocal_ = false;
        timeRestored_ = false;
    }
    // Ora messa a mano: vale per questo telefono ma nei beacon esce come "non sincronizzata", cosi'
    // un'ora sbagliata non passa a ripetitori e telefoni. La sostituisce l'ora del GPS, anche quella
    // di un altro telefono sentito direttamente. false (e non cambia niente) se il GPS ha dato l'ora
    // da meno di 6 ore.
    bool setLocalTime(uint32_t unixSec, Millis now);
    // Ora ritrovata nell'orologio del chip dopo un riavvio: come quella a mano (non va nei beacon),
    // ma la corregge anche l'ora della rete, perche' nel frattempo l'orologio del chip si sposta.
    bool setRestoredTime(uint32_t unixSec, Millis now);
    bool timeLocal() const { return timeLocal_; }
    bool timeRestored() const { return timeRestored_; }

    // Dati aggiornati dall'applicazione.
    Position position;       // ultima posizione GPS
    uint8_t battery = 255;
    Millis sosIntervalMs = 2 * 60 * 1000;
    Millis beaconIntervalMs = 5 * 60 * 1000;

    void tick(Millis now) override;
    Channel activeChannel(Millis now) const override;
    void onTransmitted(const uint8_t* data, size_t len, Millis now) override;
    bool liveVoice(Millis now, Channel ch) const override { (void)now; return ch == Channel::VOICE && callOnVoice(); }

    // Segnale dell'ultimo ripetitore sentito.
    struct Heard { Addr addr = ADDR_NONE; int16_t rssi = 0; float snr = 0; Millis at = 0; };
    const Heard& lastRepeater() const { return lastRepeater_; }
    const std::map<Addr, Heard>& repeatersHeard() const { return repeaters_; }

protected:
    void handle(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now) override;
    void onDuplicate(const Header& h, const RxInfo& info, Millis now) override;
    bool mayTransmit(const Frame& f, Millis now) const override;
    Millis heldForMs(const Frame& f, Millis now) const override;

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
        int laterRounds = 0;  // giri di tentativi gia' rimandati a piu' tardi
        bool later = false;   // in attesa del giro piu' tardi
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
    struct Call {
        CallState state = CallState::IDLE;
        Addr peer = ADDR_NONE;
        uint16_t session = 0;
        bool caller = false;
        bool ringing = false;    // (chi chiama) l'altro squilla
        bool flood = false;      // (chi chiama) la strada imparata non andava: si chiama a tutti
        bool answered = false;   // (chi risponde) ho risposto
        bool confirmed = false;  // ho sentito l'altro sul canale voce
        bool talking = false;
        bool listenMain = false; // (chi risponde) di nuovo sul canale messaggi: la voce non arrivava
        bool closing = false;    // riattacco: mando la fine sul canale voce e poi chiudo
        CallResult closeResult = CallResult::ENDED;
        CallEndReason closeReason = CallEndReason::HANGUP;
        int endsLeft = 0;        // copie della fine ancora da trasmettere
        Addr via = ADDR_NONE;    // primo salto della voce (ripetitore, o l'altro telefono)
        uint8_t hops = 0;
        bool redundant = false;
        int attempts = 0, certTries = 0, waits = 0, pings = 0;
        Millis started = 0, nextReq = 0, deadline = 0, lastReqHeard = 0, voiceWaitUntil = 0, nextPing = 0;
        Millis lastRx = 0, lastTx = 0, connectedAt = 0, peerVoiceAt = 0, closeUntil = 0, talkStarted = 0;
        uint8_t turn = 0, index = 0;
        std::vector<uint8_t> prev;  // voce del mio pacchetto prima (ridondanza)
        uint8_t prevFrames = 0;
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
    // chiamate
    uint64_t callTag() const { return 0xCA11000000000000ULL | call_.session; }
    uint32_t sendCallPacket(Addr to, Type t, const uint8_t* body, size_t len, Channel ch, Addr via, bool wantAck,
                            bool forceFlood, Millis ttl, uint64_t tag, Millis now);
    void sendCallEnd(CallEndReason why, Channel ch, uint64_t tag, Millis now);
    void sendCallAnswer(Millis now);
    void beginClose(CallResult r, CallEndReason why, Millis now);
    void sendCallVoice(uint8_t flags, const uint8_t* data, size_t len, uint8_t frames, Millis now);
    void handleCall(const Header& h, const uint8_t* body, size_t len, Millis now);
    void handleCallVoice(const Header& h, const uint8_t* body, size_t len, Millis now);
    void callTick(Millis now);
    void setCallState(CallState s, Millis now);
    void closeCall(CallResult r, Millis now);
    bool callOnVoice() const;
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
    Call call_;
    Addr lastVoiceFrom_ = ADDR_NONE;   // ultimo vocale ricevuto completo (per rispondere se il
    uint16_t lastVoiceSession_ = 0;    // mittente non ha sentito il resoconto)
    Millis nextBeacon_ = 0;
    Millis nextHello_ = 0;
    bool timeLocal_ = false;  // l'ora valida e' stata messa a mano: non si manda nei beacon
    bool timeRestored_ = false;  // ...oppure ritrovata dopo un riavvio: la corregge anche la rete
};

}  // namespace pn
