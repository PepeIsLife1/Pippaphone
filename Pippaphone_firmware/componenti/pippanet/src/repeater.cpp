#include "pippanet/repeater.h"

#include <algorithm>

namespace pn {

namespace {
constexpr Millis VOICE_IDLE_MS = 15000;   // senza traffico vocale per 15 s si torna al canale messaggi
constexpr Millis VOICE_MAX_MS = 120000;   // in ogni caso non oltre 2 minuti
constexpr int NO_LIMIT = 1000;
constexpr Millis CALL_IDLE_MS = 8000;                // chiamata senza voce per 8 s: si torna ai messaggi
constexpr Millis CALL_HOLD_MS = CALL_MAX_MS + 20000;  // in ogni caso non oltre la durata massima
constexpr Millis CALL_END_LINGER_MS = 1500;          // dopo la fine si ripete ancora la seconda copia
// Ripetuta la risposta, se entro 4 s non passa niente della chiamata la risposta si e' persa piu'
// avanti: si torna subito ai messaggi, per sentire chi risponde quando la rimanda (dopo 5 s).
constexpr Millis CALL_SETUP_MS = 4000;

uint64_t makeTag(Addr src, uint32_t seq) { return (uint64_t(src) << 32) | seq; }

bool isUnicast(const Header& h) {
    return h.dst != ADDR_BROADCAST && h.type != Type::GROUP_TEXT && h.type != Type::GLOBAL_TEXT;
}
}  // namespace

Repeater::Repeater(Addr self) : Node(self) {}

bool Repeater::lookup(Addr dst, Millis now, Route& out) const {
    auto it = routes_.find(dst);
    if (it == routes_.end() || now - it->second.seen > routeTtlMs) return false;
    out = it->second;
    return true;
}

void Repeater::learn(const Header& h, const RxInfo& info, Millis now) {
    if (h.lastHop == ADDR_NONE || h.lastHop == self_) return;
    // Il vicino che ha trasmesso questa copia e' raggiungibile direttamente.
    Route direct{h.lastHop, 0, info.snr, now};
    routes_[h.lastHop] = direct;
    if (h.src == ADDR_NONE || h.src == self_ || h.src == h.lastHop) return;
    // La sorgente e' raggiungibile passando da quel vicino.
    Route r{h.lastHop, uint8_t(h.hops), info.snr, now};
    auto it = routes_.find(h.src);
    bool replace = it == routes_.end() || now - it->second.seen > routeTtlMs / 2 || r.hops < it->second.hops ||
                   (r.hops == it->second.hops && (r.next == it->second.next || r.snr > it->second.snr + 3.0f));
    if (replace) routes_[h.src] = r;
    else it->second.seen = std::max(it->second.seen, now - routeTtlMs / 4);  // ancora valida, un po' piu' vecchia
    // Tabella limitata: toglie le strade piu' vecchie.
    if (routes_.size() > 600) {
        auto oldest = routes_.begin();
        for (auto i = routes_.begin(); i != routes_.end(); ++i)
            if (i->second.seen < oldest->second.seen) oldest = i;
        routes_.erase(oldest);
    }
}

void Repeater::noteRelayer(uint64_t tag, Addr who, Millis now) {
    if (!isRepeaterAddr(who) || who == self_) return;
    auto& e = relayers_[tag];
    e.first = now;
    if (std::find(e.second.begin(), e.second.end(), who) == e.second.end()) e.second.push_back(who);
}

bool Repeater::allNeighborsRelayed(uint64_t tag, Addr via, Millis now) const {
    auto it = relayers_.find(tag);
    const std::vector<Addr> none;
    const auto& heard = it == relayers_.end() ? none : it->second.second;
    auto has = [&](Addr a) { return std::find(heard.begin(), heard.end(), a) != heard.end(); };
    // Inoltro indirizzato: basta sentire il ripetitore designato.
    if (via != ADDR_NONE) return !isRepeaterAddr(via) || has(via);
    for (const auto& r : routes_) {
        if (!isRepeaterAddr(r.first) || r.second.hops != 0 || now - r.second.seen > routeTtlMs) continue;
        if (!has(r.first)) return false;
    }
    return true;
}

// Si e' sentito qualche altro ripetitore ripetere questo pacchetto dopo l'istante t?
bool Repeater::relayedSince(uint64_t tag, Millis t) const {
    auto it = relayers_.find(tag);
    return it != relayers_.end() && it->second.first > t;
}

void Repeater::handle(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now) {
    learn(h, info, now);
    if (h.lastHop != ADDR_NONE) lastHeard_[h.lastHop] = now;
    if (h.hops > 0) noteRelayer(makeTag(h.src, h.seq), h.lastHop, now);

    if (h.type == Type::BEACON) {
        Beacon b;
        if (decodeBeacon(raw + HEADER_SIZE, len - HEADER_SIZE, b) && b.timeValid && b.unixTime &&
            (b.gpsTime || !timeValid()))
            setUnixTime(b.unixTime, now, false);
        return;
    }
    if (isLocalOnly(h.type)) return;

    if (h.dst == self_) {
        if (onAdmin && uint8_t(h.type) >= uint8_t(Type::ADMIN_FIRST) && uint8_t(h.type) <= uint8_t(Type::ADMIN_LAST))
            onAdmin(h, raw + HEADER_SIZE, len - HEADER_SIZE);
        return;
    }

    // Chiamate: ci si segna da quale vicino arrivano chiamata e risposta (strada della voce).
    if (info.channel == Channel::MAIN && h.type == Type::CALL_REQ) noteCall(callFrom_, h.src, h.dst, h.lastHop, now);
    if (info.channel == Channel::MAIN && h.type == Type::CALL_ANSWER) noteCall(answerFrom_, h.dst, h.src, h.lastHop, now);
    if (info.channel == Channel::VOICE && (h.type == Type::CALL_VOICE || h.type == Type::CALL_END)) {
        relayCall(h, raw, len, now);
        return;
    }

    // Traffico vocale: registra la sessione per chi la sta accettando.
    if (h.type == Type::VOICE_ACCEPT && info.channel == Channel::MAIN) {
        voiceSwitchA_ = h.dst;
        voiceSwitchB_ = h.src;
        voiceSwitchToB_ = h.lastHop;  // chi l'ha trasmesso (il telefono stesso, se e' il primo salto)
    }
    if (info.channel == Channel::VOICE) {
        for (auto& s : voice_)
            if ((h.src == s.a && h.dst == s.b) || (h.src == s.b && h.dst == s.a)) s.lastActivity = now;
    }

    relay(h, raw, len, info, now);
}

void Repeater::relay(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now) {
    if (h.hops + 1 > h.hopLimit) return;
    const Channel ch = info.channel;
    // Sul canale voce passano solo i pacchetti vocali.
    if (ch == Channel::VOICE && h.type != Type::VOICE_DATA && h.type != Type::VOICE_END) return;
    if (ch == Channel::MAIN && (h.type == Type::VOICE_DATA || h.type == Type::VOICE_END || h.type == Type::CALL_VOICE))
        return;

    const Millis air = airtimeMs(paramsFor(ch), len);
    if (isUnicast(h) && !h.forceFlood) {
        if (h.via != ADDR_NONE && h.via != self_) return;  // tocca a un altro ripetitore
        Route r;
        const bool known = lookup(h.dst, now, r);
        // Pezzi di vocale: la strada del "si'", fatta solo di ripetitori sul canale voce.
        const Addr voiceNext = ch == Channel::VOICE ? voiceNextHop(h) : ADDR_NONE;
        if (voiceNext != ADDR_NONE && voiceNext != h.lastHop) {
            const bool designated = h.via == self_;
            Millis delay = designated ? randomBelow(120) : 60 + Millis(known ? r.hops : 0) * 80 + randomBelow(200);
            schedule(h, raw, len, voiceNext, false, delay, designated ? NO_LIMIT : 1, ch, now);
            return;
        }
        if (known && r.next != h.lastHop) {
            const bool designated = h.via == self_;
            Millis delay = designated ? randomBelow(120) : 60 + Millis(r.hops) * 80 + randomBelow(200);
            schedule(h, raw, len, r.next, false, delay, designated ? NO_LIMIT : 1, ch, now);
            return;
        }
    }
    // Flood: chi sente peggio ritrasmette prima. Si parte da 5 dB sotto il limite di ricezione
    // dello SF (con SF10 era (snr + 20) / 30).
    float t = (info.snr - snrLimit(paramsFor(ch)) + 5.0f) / 30.0f;
    t = std::min(1.0f, std::max(0.0f, t));
    Millis delay = Millis(t * 2.0f * float(air)) + randomBelow(uint32_t(std::max<Millis>(air / 2, 50)));
    schedule(h, raw, len, ADDR_NONE, h.forceFlood, delay, 2, ch, now);
}

void Repeater::schedule(const Header& h, const uint8_t* raw, size_t len, Addr via, bool forceFlood, Millis delay,
                        int dupLimit, Channel ch, Millis now) {
    uint8_t buf[MAX_FRAME];
    std::memcpy(buf, raw, len);
    patchRelayFields(buf, uint8_t(h.hops + 1), via, self_, forceFlood);
    const uint64_t tag = makeTag(h.src, h.seq);
    enqueue(buf, len, priorityOf(h.type), now + delay, ch, tag);
    pending_.push_back({h.src, h.seq, 0, dupLimit, tag, now + delay + 30000});
}

void Repeater::onDuplicate(const Header& h, const RxInfo& info, Millis now) {
    (void)info;
    if (h.lastHop != ADDR_NONE && h.lastHop != self_) lastHeard_[h.lastHop] = now;
    if (h.hops > 0 && h.lastHop != self_) noteRelayer(makeTag(h.src, h.seq), h.lastHop, now);
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (it->src != h.src || it->seq != h.seq) continue;
        // Conta solo le copie ritrasmesse da altri ripetitori.
        if (h.hops == 0 || h.lastHop == self_) return;
        if (++it->heard >= it->limit && cancelQueued(it->tag)) {
            suppressed++;
            pending_.erase(it);
        }
        return;
    }
}

void Repeater::onTransmitted(const uint8_t* data, size_t len, Millis now) {
    Header h;
    if (!decodeHeader(data, len, h)) return;
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [&](const Pending& p) { return p.src == h.src && p.seq == h.seq; }),
                   pending_.end());
    if (h.src != self_) {
        relayed++;
        const uint64_t tag = makeTag(h.src, h.seq);
        bool retx = false;
        for (auto& w : watch_)
            if (w.tag == tag) retx = true;
        // Ultimo salto verso un telefono: se il pacchetto chiede una risposta e il telefono non
        // trasmette nulla subito dopo, probabilmente non l'ha ricevuto.
        const bool finalHop = h.via != ADDR_NONE && !isRepeaterAddr(h.via);
        // (dopo un certificato il telefono di solito manda subito il messaggio che aspettava)
        const bool expectsAnswer =
            h.wantAck || h.type == Type::CERT_REQ || h.type == Type::CERT_RESP || h.type == Type::VOICE_REQ;
        // Il "si'" al vocale e la risposta alla chiamata no: subito dopo si cambia canale e non si
        // sentirebbe chi li ripete. La voce della chiamata non si ripete mai due volte.
        const bool callVoice = h.type == Type::CALL_ANSWER || h.type == Type::CALL_VOICE ||
                               (h.type == Type::CALL_END && activeChannel(now) == Channel::VOICE);
        const bool watchable = !isLocalOnly(h.type) && h.type != Type::VOICE_ACCEPT && !callVoice &&
                               (finalHop ? expectsAnswer : h.hops < h.hopLimit);
        if (!retx && watchable) {
            Watch w;
            w.tag = tag;
            w.hopsSent = h.hops;
            const Channel ch = activeChannel(now);
            const Millis air = airtimeMs(paramsFor(ch), len);
            w.until = now + 2 * air + 1500 + randomBelow(uint32_t(2 * air));
            std::memcpy(w.frame.data, data, len);
            w.frame.len = len;
            w.ch = ch;
            w.retries = 1;
            w.expectFrom = finalHop ? h.via : ADDR_NONE;
            w.sentAt = now;
            w.firstSent = now;
            w.extra = h.type == Type::GROUP_TEXT || h.type == Type::GLOBAL_TEXT;
            if (finalHop) w.until = now + airtimeMs(paramsFor(ch), len) + 2500 + randomBelow(500);
            watch_.push_back(w);
        }
    }
    // Appena ripetuta la risposta a una chiamata si passa al canale voce, segnandosi i vicini: verso
    // chi chiama quello a cui l'abbiamo passata (o, se e' andata a tutti, quello da cui era arrivata
    // la chiamata), verso chi risponde quello da cui ci e' arrivata.
    if (h.type == Type::CALL_ANSWER && h.src != self_) {
        const Addr a = h.dst, b = h.src;
        const Addr toA = h.via != ADDR_NONE ? h.via : seenFrom(callFrom_, a, b);
        const Addr toB = seenFrom(answerFrom_, a, b);
        if (toA != ADDR_NONE && toB != ADDR_NONE) {
            calls_.erase(std::remove_if(calls_.begin(), calls_.end(),
                                        [&](const CallSession& c) { return c.a == a && c.b == b; }),
                         calls_.end());
            calls_.push_back({a, b, toA, toB, now, now, 0, false});
        }
    }
    // Appena ripetuto il "si'" a un vocale, si passa al canale voce. Ci si segna la strada:
    // verso chi manda (a chi abbiamo passato il "si'") e verso chi riceve (da chi ci e' arrivato).
    if (h.type == Type::VOICE_ACCEPT && h.src == voiceSwitchB_ && h.dst == voiceSwitchA_) {
        voice_.push_back({voiceSwitchA_, voiceSwitchB_, now, now, h.via, voiceSwitchToB_});
        voiceSwitchA_ = voiceSwitchB_ = voiceSwitchToB_ = ADDR_NONE;
    }
}

void Repeater::noteCall(std::vector<CallSeen>& v, Addr a, Addr b, Addr from, Millis now) {
    if (from == ADDR_NONE) return;
    for (auto& c : v)
        if (c.a == a && c.b == b) {
            c.from = from;
            c.at = now;
            return;
        }
    if (v.size() >= 16) v.erase(v.begin());
    v.push_back({a, b, from, now});
}

Addr Repeater::seenFrom(const std::vector<CallSeen>& v, Addr a, Addr b) {
    for (const auto& c : v)
        if (c.a == a && c.b == b) return c.from;
    return ADDR_NONE;
}

// Voce (o fine) della chiamata sul canale voce: la ripete solo il ripetitore indicato, subito.
void Repeater::relayCall(const Header& h, const uint8_t* raw, size_t len, Millis now) {
    for (auto& s : calls_) {
        const bool towardB = h.src == s.a && h.dst == s.b;
        const bool towardA = h.src == s.b && h.dst == s.a;
        if (!towardB && !towardA) continue;
        if (h.via != self_) return;  // tocca a un altro ripetitore, o va dritta al telefono
        if (s.endAt && h.type != Type::CALL_END) return;
        s.lastActivity = now;
        s.confirmed = true;
        const Addr next = towardB ? s.toB : s.toA;
        if (next == ADDR_NONE || h.hops + 1 > h.hopLimit) return;
        uint8_t buf[MAX_FRAME];
        std::memcpy(buf, raw, len);
        patchRelayFields(buf, uint8_t(h.hops + 1), next, self_, false);
        enqueue(buf, len, Priority::Urgent, now, Channel::VOICE, makeTag(h.src, h.seq),
                now + (h.type == Type::CALL_END ? 2000 : CALL_RELAY_TTL_MS));
        if (h.type == Type::CALL_END && !s.endAt) s.endAt = now + CALL_END_LINGER_MS;
        return;
    }
}

bool Repeater::callLive(const CallSession& s, Millis now) {
    if (now - s.started >= CALL_HOLD_MS) return false;
    if (s.endAt) return now < s.endAt;
    if (!s.confirmed) return now - s.started < CALL_SETUP_MS;
    return now - s.lastActivity < CALL_IDLE_MS;
}

bool Repeater::callActive(Millis now) const {
    for (const auto& s : calls_)
        if (callLive(s, now)) return true;
    return false;
}

Addr Repeater::voiceNextHop(const Header& h) const {
    for (const auto& s : voice_) {
        if (h.src == s.a && h.dst == s.b) return s.toB;
        if (h.src == s.b && h.dst == s.a) return s.toA;
    }
    return ADDR_NONE;
}

bool Repeater::voiceActive(Millis now) const {
    for (const auto& s : voice_)
        if (now - s.lastActivity < VOICE_IDLE_MS && now - s.started < VOICE_MAX_MS) return true;
    return false;
}

Channel Repeater::activeChannel(Millis now) const {
    return voiceActive(now) || callActive(now) ? Channel::VOICE : Channel::MAIN;
}

void Repeater::sendBeacon(Millis now) {
    Beacon b;
    b.role = Role::REPEATER;
    b.timeValid = timeValid();
    b.gpsTime = timeFromGps();
    b.unixTime = unixTime(now);
    b.battery = battery;
    uint8_t p[16];
    size_t n = encodeBeacon(b, p, sizeof(p));
    Header h;
    h.type = Type::BEACON;
    h.hopLimit = 0;
    h.src = self_;
    h.dst = ADDR_BROADCAST;
    sendRaw(h, p, n, now);
}

void Repeater::tick(Millis now) {
    if (nextBeacon_ == 0) nextBeacon_ = now + 5000 + randomBelow(20000);
    if (now >= nextBeacon_ && activeChannel(now) == Channel::MAIN) {
        sendBeacon(now);
        nextBeacon_ = now + beaconIntervalMs - 30000 + randomBelow(60000);
    }
    // Sessioni vocali e chiamate finite: si butta cio' che restava in coda sul canale voce.
    bool wasVoice = !voice_.empty() || !calls_.empty();
    voice_.erase(std::remove_if(voice_.begin(), voice_.end(),
                                [now](const VoiceSession& s) {
                                    return now - s.lastActivity >= VOICE_IDLE_MS || now - s.started >= VOICE_MAX_MS;
                                }),
                 voice_.end());
    calls_.erase(std::remove_if(calls_.begin(), calls_.end(), [now](const CallSession& s) { return !callLive(s, now); }),
                 calls_.end());
    if (wasVoice && voice_.empty() && calls_.empty()) clearQueue(Channel::VOICE);
    for (auto* v : {&callFrom_, &answerFrom_})
        v->erase(std::remove_if(v->begin(), v->end(), [now](const CallSeen& c) { return now - c.at > 60000; }),
                 v->end());
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [now](const Pending& p) { return now > p.expires; }),
                   pending_.end());
    for (size_t i = 0; i < watch_.size();) {
        Watch& w = watch_[i];
        if (now < w.until) { i++; continue; }
        Header h;
        decodeHeader(w.frame.data, w.frame.len, h);
        bool delivered;
        if (w.expectFrom != ADDR_NONE) {
            auto lh = lastHeard_.find(w.expectFrom);
            delivered = lh != lastHeard_.end() && lh->second > w.sentAt;
        } else {
            delivered = allNeighborsRelayed(w.tag, h.via, now);
        }
        bool again = false;
        if (w.ch == activeChannel(now) && !delivered) {
            if (w.retries > 0) {
                w.retries--;
                again = true;
            } else if (w.extra && !relayedSince(w.tag, w.firstSent)) {
                // Nessun ripetitore l'ha ripetuto dopo di noi: e' andato perso davvero.
                // (Nelle reti fitte qualcuno tace perche' l'ha gia' sentito due volte: li' non serve.)
                w.extra = false;
                again = true;
            }
        }
        if (again) {
            enqueue(w.frame.data, w.frame.len, priorityOf(h.type), now + randomBelow(300), w.ch, w.tag);
            w.until = now + 2 * airtimeMs(paramsFor(w.ch), w.frame.len) + 3000;
            w.sentAt = now;
            rescued++;
            i++;
            continue;
        }
        watch_.erase(watch_.begin() + long(i));
    }
    for (auto it = relayers_.begin(); it != relayers_.end();)
        it = (now - it->second.first > 120000) ? relayers_.erase(it) : std::next(it);
    if (lastHeard_.size() > 600)
        for (auto it = lastHeard_.begin(); it != lastHeard_.end();)
            it = (now - it->second > routeTtlMs) ? lastHeard_.erase(it) : std::next(it);
    nextWake_ = nextBeacon_;
    for (const auto& w : watch_) nextWake_ = std::min(nextWake_, w.until);
    if (!calls_.empty()) nextWake_ = std::min(nextWake_, now + 500);
}

}  // namespace pn
