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
//  - durante un vocale i ripetitori sul percorso passano al canale voce.
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
    struct Watch {
        uint64_t tag;
        uint8_t hopsSent;
        Millis until;
        Frame frame;
        Channel ch;
        int retries;
        Addr expectFrom;  // ultimo salto verso un telefono: si aspetta che risponda
        Millis sentAt;
    };
    struct VoiceSession {
        Addr a, b;
        Millis lastActivity;
        Millis started;
    };

    void learn(const Header& h, const RxInfo& info, Millis now);
    void relay(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now);
    void schedule(const Header& h, const uint8_t* raw, size_t len, Addr via, bool forceFlood, Millis delay,
                  int dupLimit, Channel ch, Millis now);
    void sendBeacon(Millis now);
    void noteRelayer(uint64_t tag, Addr who, Millis now);
    bool allNeighborsRelayed(uint64_t tag, Addr via, Millis now) const;
    bool voiceActive(Millis now) const;

    std::map<Addr, Route> routes_;
    std::vector<Pending> pending_;
    std::vector<VoiceSession> voice_;
    std::vector<Watch> watch_;
    std::map<uint64_t, std::pair<Millis, std::vector<Addr>>> relayers_;  // chi ha gia' ripetuto cosa
    std::map<Addr, Millis> lastHeard_;                                   // ultimo pacchetto sentito da ciascun vicino
    Addr voiceSwitchA_ = ADDR_NONE, voiceSwitchB_ = ADDR_NONE;  // sessione da aprire dopo la TX
    Millis nextBeacon_ = 0;
};

}  // namespace pn
