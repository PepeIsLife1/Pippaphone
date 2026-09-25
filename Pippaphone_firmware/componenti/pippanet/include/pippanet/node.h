// PippaNet: base comune di telefono e ripetitore.
//
// Il nodo e' pura logica, senza radio ne' sistema operativo: la piattaforma gli passa i
// pacchetti ricevuti (onReceive), lo fa avanzare nel tempo (tick) e gli chiede cosa
// trasmettere (popTx). Cosi' la stessa logica gira sull'ESP32 e nei test sul PC.
#pragma once

#include <deque>
#include <functional>
#include <vector>

#include "pippanet/airtime.h"
#include "pippanet/packet.h"

namespace pn {

struct RxInfo {
    int16_t rssi = 0;    // dBm
    float snr = 0.0f;    // dB
    Channel channel = Channel::MAIN;
};

// Ricorda i pacchetti gia' visti (mittente + seq) per non elaborarli o ripeterli due volte.
class DedupCache {
public:
    explicit DedupCache(size_t capacity = 512, Millis ttl = 30 * 60 * 1000) : cap_(capacity), ttl_(ttl) {}
    // true se il pacchetto era gia' stato visto; in ogni caso lo segna come visto.
    bool checkAndInsert(Addr src, uint32_t seq, Millis now);
    bool contains(Addr src, uint32_t seq, Millis now) const;

private:
    struct Entry { Addr src; uint32_t seq; Millis at; };
    std::deque<Entry> items_;
    size_t cap_;
    Millis ttl_;
};

class Node {
public:
    explicit Node(Addr self);
    virtual ~Node() = default;

    Addr address() const { return self_; }
    void setAddress(Addr a) { self_ = a; }

    // Contatore dei pacchetti. La piattaforma lo carica all'avvio e lo salva quando
    // onSeqReserve() lo chiede (a blocchi, per non consumare la flash).
    void setSeq(uint32_t next) { seq_ = next; reserved_ = next; }
    std::function<void(uint32_t)> onSeqReserve;

    // Ora "vera" (secondi Unix) se nota.
    void setUnixTime(uint32_t unixSec, Millis now, bool fromGps);
    uint32_t unixTime(Millis now) const;
    bool timeValid() const { return timeValid_; }
    bool timeFromGps() const { return timeFromGps_; }

    // ---- interfaccia verso la piattaforma ----
    void onReceive(const uint8_t* data, size_t len, const RxInfo& info, Millis now);
    virtual void tick(Millis now);
    // Prossimo pacchetto da trasmettere sul canale ch, se ce n'e' uno pronto.
    bool popTx(Millis now, Channel ch, Frame& out);
    // Canale su cui stare in ricezione (e trasmettere) adesso.
    virtual Channel activeChannel(Millis now) const { (void)now; return Channel::MAIN; }
    // La piattaforma avvisa quando un pacchetto e' uscito davvero.
    virtual void onTransmitted(const uint8_t* data, size_t len, Millis now) { (void)data; (void)len; (void)now; }
    // Millisecondi al prossimo evento (per dormire); 0 = subito.
    Millis msUntilNextEvent(Millis now) const;

    // Statistiche.
    uint32_t rxCount = 0, rxDropped = 0, txCount = 0;
    uint64_t txAirtimeMs = 0;

protected:
    // Elabora un pacchetto nuovo (gia' filtrato dai duplicati).
    virtual void handle(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now) = 0;
    // Chiamato anche per i duplicati (i ripetitori li usano per sopprimere le ripetizioni).
    virtual void onDuplicate(const Header& h, const RxInfo& info, Millis now) { (void)h; (void)info; (void)now; }
    // Il nodo puo' trattenere un pacchetto pronto (per distanziare i propri invii).
    virtual bool mayTransmit(const Frame& f, Millis now) const { (void)f; (void)now; return true; }

    // Accoda un pacchetto; tag serve per poterlo togliere dalla coda (0 = nessuno).
    void enqueue(const uint8_t* data, size_t len, Priority prio, Millis notBefore, Channel ch, uint64_t tag = 0);
    bool cancelQueued(uint64_t tag);
    bool isQueued(uint64_t tag) const;
    void clearQueue(Channel ch);

    uint32_t nextSeq();
    // Costruisce e accoda un pacchetto con il carico utile gia' pronto (eventualmente cifrato).
    uint32_t sendRaw(Header h, const uint8_t* payload, size_t len, Millis now, Millis delay = 0,
                     Channel ch = Channel::MAIN, uint64_t tag = 0);

    Addr self_;
    DedupCache dedup_;
    Millis nextWake_ = 0;

private:
    struct TxItem {
        Frame frame;
        Priority prio;
        Millis notBefore;
        Channel ch;
        uint64_t tag;
        uint32_t order;
    };
    std::vector<TxItem> queue_;
    uint32_t order_ = 0;
    uint32_t seq_ = 1;
    uint32_t reserved_ = 1;
    bool timeValid_ = false;
    bool timeFromGps_ = false;
    uint32_t unixAtSync_ = 0;
    Millis millisAtSync_ = 0;
};

// Numero casuale in [0, n).
uint32_t randomBelow(uint32_t n);

}  // namespace pn
