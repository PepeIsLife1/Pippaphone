// PippaNet: logica del ripetitore.
//
// Instradamento:
//  - impara le strade guardando chi gli passa i pacchetti ("ultimo" salto) e quanti salti
//    hanno fatto;
//  - un messaggio verso un telefono di cui conosce la strada lo passa solo al prossimo
//    ripetitore giusto (campo "via"): gli altri stanno zitti;
//  - se non conosce la strada, o per i messaggi a tutti, fa flood: ritrasmette dopo un
//    ritardo che dipende dal segnale (chi sente peggio parte prima, cosi' si arriva piu'
//    lontano) e rinuncia se sente altri due ripetitori farlo prima di lui;
//  - durante un vocale i ripetitori sul percorso passano al canale voce. Il percorso e' quello
//    che ha fatto il "si'" (VOICE_ACCEPT): solo chi lo ripete cambia canale, quindi i pezzi di
//    vocale seguono la stessa strada e non quella della tabella, che potrebbe passare da un
//    ripetitore rimasto sul canale messaggi;
//  - le chiamate in diretta fanno lo stesso con "ho risposto" (CALL_ANSWER). La voce della
//    chiamata si ripete subito, senza attese a caso e senza nuovi tentativi: un pezzo che resta in
//    coda piu' di un pacchetto si butta. Otto secondi senza voce e si torna al canale messaggi.
#pragma once

#include <functional>
#include <map>

#include "pippanet/node.h"
#include "pippanet/payloads.h"

namespace pn {

class Repeater : public Node {
public:
    struct Route {
        Addr next;      // vicino a cui passarlo (== destinazione se e' un vicino diretto)
        uint8_t hops;   // salti dal vicino alla destinazione
        float snr;      // segnale del vicino
        Millis seen;
    };

    explicit Repeater(Addr self);

    void tick(Millis now) override;
    Channel activeChannel(Millis now) const override;
    void onTransmitted(const uint8_t* data, size_t len, Millis now) override;
    bool liveVoice(Millis now, Channel ch) const override { return ch == Channel::VOICE && callActive(now); }
    // C'e' una chiamata in diretta che passa di qui.
    bool callActive(Millis now) const;

    uint8_t battery = 255;
    Millis beaconIntervalMs = 5 * 60 * 1000;
    Millis routeTtlMs = 30 * 60 * 1000;

    const std::map<Addr, Route>& routes() const { return routes_; }
    bool lookup(Addr dst, Millis now, Route& out) const;

    // Pacchetti di amministrazione destinati a questo ripetitore (programma separato).
    std::function<void(const Header&, const uint8_t* payload, size_t len)> onAdmin;

    uint32_t relayed = 0, suppressed = 0, rescued = 0;

protected:
    void handle(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now) override;
    void onDuplicate(const Header& h, const RxInfo& info, Millis now) override;

private:
    struct Pending {
        Addr src;
        uint32_t seq;
        int heard;
        int limit;
        uint64_t tag;
        Millis expires;
    };
    // Pacchetto appena ripetuto: se nessun altro ripetitore vicino lo ripete a sua volta
    // entro poco, probabilmente e' andato perso (collisione) e lo si ritrasmette una volta.
    // I messaggi senza conferma (gruppo, a tutti) hanno un tentativo in piu', ma solo se dopo
    // la prima trasmissione nessun ripetitore si e' fatto sentire: chi li manda non ritenta.
    struct Watch {
        uint64_t tag;
        uint8_t hopsSent;
        Millis until;
        Frame frame;
        Channel ch;
        int retries;
        Addr expectFrom;  // ultimo salto verso un telefono: si aspetta che risponda
        Millis sentAt;
        Millis firstSent = 0;
        bool extra = false;  // tentativo in piu' ancora disponibile
    };
    struct VoiceSession {
        Addr a, b;  // a = chi manda il vocale, b = chi l'ha accettato
        Millis lastActivity;
        Millis started;
        Addr toA = ADDR_NONE;  // a chi abbiamo passato il "si'" (verso a); ADDR_NONE se in flood
        Addr toB = ADDR_NONE;  // da chi ci e' arrivato il "si'" (verso b)
    };

    struct CallSession {
        Addr a, b;      // a = chi chiama, b = chi ha risposto
        Addr toA, toB;  // vicini verso a e verso b
        Millis lastActivity, started;
        Millis endAt;   // fine della chiamata ripetuta: si chiude fra poco
        bool confirmed;  // e' passata la voce (o il "ci sono") della chiamata
    };
    struct CallSeen {  // da quale vicino e' arrivata una chiamata (o una risposta) tra a e b
        Addr a, b, from;
        Millis at;
    };

    void learn(const Header& h, const RxInfo& info, Millis now);
    void relayCall(const Header& h, const uint8_t* raw, size_t len, Millis now);
    static bool callLive(const CallSession& s, Millis now);
    static void noteCall(std::vector<CallSeen>& v, Addr a, Addr b, Addr from, Millis now);
    static Addr seenFrom(const std::vector<CallSeen>& v, Addr a, Addr b);
    void relay(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now);
    void schedule(const Header& h, const uint8_t* raw, size_t len, Addr via, bool forceFlood, Millis delay,
                  int dupLimit, Channel ch, Millis now);
    void sendBeacon(Millis now);
    void noteRelayer(uint64_t tag, Addr who, Millis now);
    bool allNeighborsRelayed(uint64_t tag, Addr via, Millis now) const;
    bool relayedSince(uint64_t tag, Millis t) const;
    bool voiceActive(Millis now) const;
    Addr voiceNextHop(const Header& h) const;

    std::map<Addr, Route> routes_;
    std::vector<Pending> pending_;
    std::vector<VoiceSession> voice_;
    std::vector<CallSession> calls_;
    std::vector<CallSeen> callFrom_, answerFrom_;
    std::vector<Watch> watch_;
    std::map<uint64_t, std::pair<Millis, std::vector<Addr>>> relayers_;  // chi ha gia' ripetuto cosa
    std::map<Addr, Millis> lastHeard_;                                   // ultimo pacchetto sentito da ciascun vicino
    Addr voiceSwitchA_ = ADDR_NONE, voiceSwitchB_ = ADDR_NONE;  // sessione da aprire dopo la TX
    Addr voiceSwitchToB_ = ADDR_NONE;                           // vicino da cui e' arrivato il "si'"
    Millis nextBeacon_ = 0;
};

}  // namespace pn
