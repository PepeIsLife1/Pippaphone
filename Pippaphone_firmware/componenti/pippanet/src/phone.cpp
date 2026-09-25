#include "pippanet/phone.h"

#include <algorithm>

namespace pn {

namespace {
constexpr int MAX_ATTEMPTS = 4;
constexpr Millis CERT_RETRY_MS = 60000;  // andata e ritorno di un certificato su 4 salti, con ritrasmissioni
constexpr Millis CERT_ANSWER_MIN_MS = 30000;
constexpr Millis VOICE_GUARD_MS = 400;      // attesa perche' i ripetitori passino al canale voce
constexpr size_t PAIR_OVERHEAD = 4 + TAG_SIZE;          // msgId + tag

uint16_t randomId() {
    uint16_t v = 0;
    while (v == 0 || v == 0xFFFF) v = uint16_t(randomBelow(0x10000));
    return v;
}
}  // namespace

Phone::Phone(const Identity& id, PhoneListener& listener) : Node(ADDR_NONE), id_(id), L_(listener) {}

// ------------------------------------------------------------------ identita'

void Phone::setCredentials(const Cert& own, const Key32& adminPublic) {
    own_ = own;
    adminPub_ = adminPublic;
    trusted_ = true;
    setAddress(own.number);
    provisioning_ = false;
    certs_[own.number] = own;
}

void Phone::addKnownCert(const Cert& c) {
    if (c.number == ADDR_NONE) return;
    certs_[c.number] = c;
    keys_.erase(c.number);
}

const Cert* Phone::certOf(Addr a) const {
    auto it = certs_.find(a);
    return it == certs_.end() ? nullptr : &it->second;
}

bool Phone::learnCert(const Cert& c) {
    if (!trusted_ || c.number == ADDR_NONE || !isPhoneAddr(c.number)) return false;
    if (!c.verify(adminPub_)) return false;
    auto it = certs_.find(c.number);
    bool changed = it == certs_.end() || it->second.edPublic != c.edPublic || it->second.xPublic != c.xPublic;
    // Un certificato piu' vecchio non sostituisce quello che ho gia'.
    if (it != certs_.end() && changed && c.issued < it->second.issued) return false;
    if (!changed) return true;
    certs_[c.number] = c;
    keys_.erase(c.number);
    L_.onCertLearned(c);
    return true;
}

Key32 Phone::keyFor(Addr peer) {
    auto it = keys_.find(peer);
    if (it != keys_.end()) return it->second;
    Key32 k = pairKey(id_, certs_.at(peer).xPublic);
    keys_[peer] = k;
    return k;
}

bool Phone::provision(const ProvHello& hello, Addr number, Millis now) {
    if (!isAdmin_ || !isPhoneAddr(number)) return false;
    ProvCert p;
    p.target = shortId(hello.edPublic);
    p.cert.number = number;
    p.cert.edPublic = hello.edPublic;
    p.cert.xPublic = hello.xPublic;
    p.cert.issued = unixTime(now);
    p.cert.signWith(admin_);
    p.adminPublic = admin_.edPublic;
    uint8_t buf[MAX_PAYLOAD];
    size_t n = encodeProvCert(p, buf, sizeof(buf));
    if (!n) return false;
    Header h;
    h.type = Type::PROV_CERT;
    h.hopLimit = 0;
    h.src = self_;
    h.dst = ADDR_NONE;
    sendRaw(h, buf, n, now);
    if (trusted_) learnCert(p.cert);
    return true;
}

// ------------------------------------------------------------------ invio affidabile

uint32_t Phone::sendPairwise(Addr to, Type t, uint32_t msgId, const uint8_t* body, size_t len, bool wantAck,
                             Millis now, bool forceFlood, Channel ch, Millis delay) {
    if (!hasNumber() || !knowsCert(to)) return 0;
    if (HEADER_SIZE + len + PAIR_OVERHEAD > MAX_FRAME) return 0;
    Header h;
    h.type = t;
    h.forceFlood = forceFlood;
    h.wantAck = wantAck;
    h.src = self_;
    h.dst = to;
    h.seq = nextSeq();
    uint8_t plain[MAX_FRAME];
    Writer w(plain, sizeof(plain));
    w.u32(msgId);
    w.put(body, len);
    uint8_t cipher[MAX_FRAME];
    sealPayload(keyFor(to), h, plain, w.len(), cipher);
    wipe(plain, sizeof(plain));
    return sendRaw(h, cipher, w.len() + TAG_SIZE, now, delay, ch);
}

Millis Phone::ackTimeout(size_t len, int attempt) const {
    const Millis air = airtimeMs(MAIN_PARAMS, HEADER_SIZE + len + PAIR_OVERHEAD);
    const Millis ackAir = airtimeMs(MAIN_PARAMS, HEADER_SIZE + PAIR_OVERHEAD + 5);
    const Millis hops = attempt <= 1 ? 4 : 10;
    return 4000 + hops * (2 * air + 2 * ackAir);
}

uint32_t Phone::queueReliable(Addr to, Type t, const uint8_t* body, size_t len, Millis now) {
    if (!hasNumber() || !isPhoneAddr(to) || to == self_) return 0;
    if (HEADER_SIZE + len + PAIR_OVERHEAD > MAX_FRAME) return 0;
    Outgoing o;
    o.msgId = nextSeq();
    o.to = to;
    o.type = t;
    o.body.assign(body, body + len);
    o.attempts = 0;
    o.certTries = 0;
    o.nextTry = now;
    o.waitingCert = false;
    o.ch = Channel::MAIN;
    out_.push_back(o);
    trySend(out_.back(), now);
    return o.msgId;
}

void Phone::trySend(Outgoing& o, Millis now) {
    if (!knowsCert(o.to)) {
        // Prima serve il certificato: i tentativi per averlo si contano a parte.
        o.certTries++;
        o.waitingCert = true;
        requestCert(o.to, now);
        o.nextTry = now + CERT_RETRY_MS + randomBelow(3000);
        return;
    }
    o.attempts++;
    o.waitingCert = false;
    sendPairwise(o.to, o.type, o.msgId, o.body.data(), o.body.size(), true, now, o.attempts > 1);
    o.nextTry = now + ackTimeout(o.body.size(), o.attempts) + randomBelow(2000);
    if (o.attempts == 1 && o.type == Type::DM_TEXT) L_.onDelivery(o.to, o.msgId, Delivery::SENT);
}

void Phone::requestCert(Addr to, Millis now) {
    auto it = certAsked_.find(to);
    if (it != certAsked_.end() && now - it->second < CERT_RETRY_MS) return;
    certAsked_[to] = now;
    sendCert(to, Type::CERT_REQ, now);
}

void Phone::sendCert(Addr to, Type t, Millis now) {
    if (!hasNumber()) return;
    uint8_t buf[CERT_SIZE];
    own_.serialize(buf);
    Header h;
    h.type = t;
    h.src = self_;
    h.dst = to;
    sendRaw(h, buf, sizeof(buf), now, randomBelow(2000));
}

void Phone::sendAck(Addr to, uint32_t msgId, AckKind kind, Millis now) {
    AckPayload a{msgId, kind};
    uint8_t buf[8];
    size_t n = encodeAck(a, buf, sizeof(buf));
    // Piccolo ritardo casuale per non scontrarsi con le ripetizioni del messaggio stesso.
    sendPairwise(to, Type::DM_ACK, 0, buf, n, false, now, false, Channel::MAIN, 150 + randomBelow(500));
}

bool Phone::alreadyHandled(Addr from, uint32_t msgId) {
    for (const auto& e : handled_)
        if (e.first == from && e.second == msgId) return true;
    handled_.emplace_back(from, msgId);
    if (handled_.size() > 256) handled_.pop_front();
    return false;
}

// ------------------------------------------------------------------ azioni

uint32_t Phone::sendText(Addr to, const std::string& text, Millis now) {
    TextPayload t{unixTime(now), text};
    uint8_t buf[MAX_PAYLOAD];
    size_t n = encodeTextPayload(t, buf, MAX_PAYLOAD - PAIR_OVERHEAD);
    if (!n) return 0;
    return queueReliable(to, Type::DM_TEXT, buf, n, now);
}

void Phone::sendRead(Addr to, uint32_t msgId, Millis now) { sendAck(to, msgId, AckKind::READ, now); }

uint32_t Phone::requestPosition(Addr to, Millis now) { return queueReliable(to, Type::POS_REQ, nullptr, 0, now); }

void Phone::answerPosition(Addr to, uint32_t requestId, PosStatus status, const Position& pos, Millis now) {
    PosResponse r;
    r.requestSeq = requestId;
    r.status = status;
    if (status == PosStatus::OK) r.pos = pos;
    uint8_t buf[32];
    size_t n = encodePosResponse(r, buf, sizeof(buf));
    queueReliable(to, Type::POS_RESP, buf, n, now);
}

void Phone::startSos(const std::vector<Addr>& contacts, Millis now) {
    sos_.active = true;
    sos_.contacts = contacts;
    sos_.round = 0;
    sos_.next = now;
}

void Phone::stopSos(Millis now) {
    if (!sos_.active) return;
    sos_.active = false;
    // Toglie i tentativi ancora in corso e avvisa che l'SOS e' finito.
    out_.erase(std::remove_if(out_.begin(), out_.end(), [](const Outgoing& o) { return o.type == Type::SOS; }),
               out_.end());
    SosPayload p;
    p.active = false;
    p.round = sos_.round;
    p.battery = battery;
    p.pos = position;
    uint8_t buf[32];
    size_t n = encodeSos(p, buf, sizeof(buf));
    for (Addr c : sos_.contacts) queueReliable(c, Type::SOS, buf, n, now);
}

void Phone::sendSosRound(Millis now) {
    SosPayload p;
    p.active = true;
    p.round = sos_.round++;
    p.battery = battery;
    p.pos = position;
    uint8_t buf[32];
    size_t n = encodeSos(p, buf, sizeof(buf));
    // I tentativi del giro precedente non servono piu'.
    out_.erase(std::remove_if(out_.begin(), out_.end(), [](const Outgoing& o) { return o.type == Type::SOS; }),
               out_.end());
    for (Addr c : sos_.contacts) queueReliable(c, Type::SOS, buf, n, now);
    sos_.next = now + sosIntervalMs;
}

GroupInfo Phone::createGroup(const std::string& name, const std::vector<Addr>& others, Millis now) {
    GroupInfo g;
    g.id = randomId();
    randomBytes(g.key.data(), 32);
    g.name = name;
    g.creator = self_;
    g.members.push_back(self_);
    for (Addr a : others)
        if (a != self_ && g.members.size() < MAX_GROUP_MEMBERS &&
            std::find(g.members.begin(), g.members.end(), a) == g.members.end())
            g.members.push_back(a);
    groups_[g.id] = g;
    GroupInvite inv{g.id, g.key, g.name, g.members};
    uint8_t buf[MAX_PAYLOAD];
    size_t n = encodeGroupInvite(inv, buf, MAX_PAYLOAD - PAIR_OVERHEAD);
    for (Addr a : g.members)
        if (a != self_) queueReliable(a, Type::GROUP_INVITE, buf, n, now);
    return g;
}

uint32_t Phone::sendGroupText(uint16_t group, const std::string& text, Millis now) {
    auto it = groups_.find(group);
    if (it == groups_.end() || !hasNumber()) return 0;
    TextPayload t{unixTime(now), text};
    uint8_t body[MAX_PAYLOAD];
    size_t n = encodeTextPayload(t, body, MAX_PAYLOAD - TAG_SIZE - SIG_SIZE);
    if (!n) return 0;
    Header h;
    h.type = Type::GROUP_TEXT;
    h.src = self_;
    h.dst = group;
    h.seq = nextSeq();
    uint8_t payload[MAX_FRAME];
    sealPayload(it->second.key, h, body, n, payload);
    size_t clen = n + TAG_SIZE;
    uint8_t msg[AD_SIZE + MAX_FRAME];
    headerAd(h, msg);
    std::memcpy(msg + AD_SIZE, payload, clen);
    Sig64 s = id_.sign(msg, AD_SIZE + clen);
    std::memcpy(payload + clen, s.data(), SIG_SIZE);
    sendRaw(h, payload, clen + SIG_SIZE, now);
    return h.seq;
}

uint32_t Phone::sendGlobalText(const std::string& text, Millis now) {
    if (!hasNumber()) return 0;
    TextPayload t{unixTime(now), text};
    uint8_t payload[MAX_FRAME];
    size_t n = encodeTextPayload(t, payload, MAX_PAYLOAD - SIG_SIZE);
    if (!n) return 0;
    Header h;
    h.type = Type::GLOBAL_TEXT;
    h.src = self_;
    h.dst = ADDR_BROADCAST;
    h.seq = nextSeq();
    uint8_t msg[AD_SIZE + MAX_FRAME];
    headerAd(h, msg);
    std::memcpy(msg + AD_SIZE, payload, n);
    Sig64 s = id_.sign(msg, AD_SIZE + n);
    std::memcpy(payload + n, s.data(), SIG_SIZE);
    sendRaw(h, payload, n + SIG_SIZE, now);
    return h.seq;
}

uint32_t Phone::sendVoice(Addr to, Codec codec, const std::vector<uint8_t>& data, uint16_t durationMs, Millis now) {
    if (vOut_.active || vIn_.active || !hasNumber() || data.empty() || !isPhoneAddr(to)) return 0;
    size_t chunks = (data.size() + VOICE_CHUNK - 1) / VOICE_CHUNK;
    if (chunks > MAX_VOICE_PACKETS) return 0;
    vOut_ = VoiceOut();
    vOut_.active = true;
    vOut_.to = to;
    vOut_.msgId = nextSeq();
    vOut_.session = randomId();
    vOut_.codec = codec;
    vOut_.durationMs = durationMs;
    for (size_t i = 0; i < chunks; i++) {
        size_t a = i * VOICE_CHUNK, b = std::min(data.size(), a + VOICE_CHUNK);
        vOut_.chunks.emplace_back(data.begin() + a, data.begin() + b);
    }
    vOut_.phase = VoiceOut::REQ;
    vOut_.rounds = 0;
    vOut_.deadline = now;  // il tick manda subito la richiesta
    L_.onVoiceState(to, vOut_.msgId, VoiceSendState::REQUESTING);
    voiceTick(now);
    return vOut_.msgId;
}

// ------------------------------------------------------------------ ricezione

void Phone::handle(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now) {
    // Segnale dei ripetitori (per le tacche e la schermata Segnale).
    if (isRepeaterAddr(h.lastHop)) {
        Heard hr{h.lastHop, info.rssi, info.snr, now};
        repeaters_[h.lastHop] = hr;
        lastRepeater_ = hr;
    }
    const uint8_t* payload = raw + HEADER_SIZE;
    const size_t plen = len - HEADER_SIZE;

    switch (h.type) {
        case Type::BEACON: {
            Beacon b;
            if (!decodeBeacon(payload, plen, b)) return;
            if (b.timeValid && b.unixTime && !timeFromGps() && (!timeValid() || b.gpsTime)) {
                setUnixTime(b.unixTime, now, false);
                L_.onTimeSync(b.unixTime);
            }
            L_.onBeacon(h.src, b, info);
            return;
        }
        case Type::PROV_HELLO: {
            ProvHello p;
            if (isAdmin_ && decodeProvHello(payload, plen, p)) L_.onProvisionHello(shortId(p.edPublic), p, info);
            return;
        }
        case Type::PROV_CERT: {
            ProvCert p;
            if (!provisioning_ || hasNumber() || !decodeProvCert(payload, plen, p)) return;
            if (p.target != shortId(id_.edPublic) || p.cert.edPublic != id_.edPublic ||
                p.cert.xPublic != id_.xPublic || !p.cert.verify(p.adminPublic))
                return;
            L_.onProvisionOffer(p.cert, p.adminPublic);
            return;
        }
        case Type::CERT_REQ:
        case Type::CERT_RESP: {
            Cert c;
            if (!Cert::parse(payload, plen, c) || c.number != h.src) return;
            bool ok = learnCert(c);
            if (!ok) return;
            if (h.type == Type::CERT_REQ && h.dst == self_) {
                auto it = certSent_.find(h.src);
                if (it == certSent_.end() || now - it->second > CERT_ANSWER_MIN_MS) {
                    certSent_[h.src] = now;
                    sendCert(h.src, Type::CERT_RESP, now);
                }
            }
            // Ora posso leggere cio' che aspettava e spedire cio' che era fermo.
            auto w = waitingCert_.find(h.src);
            if (w != waitingCert_.end()) {
                std::vector<PendingIn> list = std::move(w->second);
                waitingCert_.erase(w);
                for (auto& p : list) {
                    Header ph;
                    if (!decodeHeader(p.raw.data(), p.raw.size(), ph)) continue;
                    if (ph.type == Type::GLOBAL_TEXT || ph.type == Type::GROUP_TEXT)
                        handleSigned(ph, p.raw.data(), p.raw.size(), p.info, now);
                    else
                        handlePairwise(ph, p.raw.data(), p.raw.size(), p.info, now);
                }
            }
            for (auto& o : out_)
                if (o.to == h.src && o.waitingCert) trySend(o, now);
            if (vOut_.active && vOut_.to == h.src && vOut_.phase == VoiceOut::REQ) vOut_.deadline = now;
            return;
        }
        case Type::GLOBAL_TEXT:
        case Type::GROUP_TEXT:
            handleSigned(h, raw, len, info, now);
            return;
        default:
            break;
    }
    if (isPairwise(h.type)) handlePairwise(h, raw, len, info, now);
}

void Phone::handleSigned(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now) {
    if (!hasNumber()) return;
    GroupInfo* g = nullptr;
    if (h.type == Type::GROUP_TEXT) {
        auto it = groups_.find(h.dst);
        if (it == groups_.end()) return;
        g = &it->second;
        if (std::find(g->members.begin(), g->members.end(), h.src) == g->members.end()) return;
    }
    const uint8_t* payload = raw + HEADER_SIZE;
    const size_t plen = len - HEADER_SIZE;
    if (plen <= SIG_SIZE) return;
    if (!knowsCert(h.src)) {
        auto& list = waitingCert_[h.src];
        if (list.size() < 4) list.push_back({std::vector<uint8_t>(raw, raw + len), info, now});
        requestCert(h.src, now);
        return;
    }
    const size_t blen = plen - SIG_SIZE;
    uint8_t msg[AD_SIZE + MAX_FRAME];
    headerAd(h, msg);
    std::memcpy(msg + AD_SIZE, payload, blen);
    if (!verifySig(certs_[h.src].edPublic, msg, AD_SIZE + blen, payload + blen)) return;  // firma falsa
    TextPayload t;
    if (g) {
        uint8_t plain[MAX_FRAME];
        if (blen <= TAG_SIZE || !openPayload(g->key, h, payload, blen, plain)) return;
        if (!decodeTextPayload(plain, blen - TAG_SIZE, t)) return;
        L_.onGroupText(g->id, h.src, h.seq, t);
    } else {
        if (!decodeTextPayload(payload, blen, t)) return;
        L_.onGlobalText(h.src, h.seq, t);
    }
}

void Phone::handlePairwise(const Header& h, const uint8_t* raw, size_t len, const RxInfo& info, Millis now) {
    if (!hasNumber() || h.dst != self_) return;
    if (!knowsCert(h.src)) {
        auto& list = waitingCert_[h.src];
        if (list.size() < 4) list.push_back({std::vector<uint8_t>(raw, raw + len), info, now});
        requestCert(h.src, now);
        return;
    }
    const size_t clen = len - HEADER_SIZE;
    if (clen < PAIR_OVERHEAD) return;
    uint8_t plain[MAX_FRAME];
    if (!openPayload(keyFor(h.src), h, raw + HEADER_SIZE, clen, plain)) return;
    const size_t n = clen - TAG_SIZE;
    Reader r(plain, n);
    const uint32_t msgId = r.u32();
    const uint8_t* body = plain + 4;
    const size_t blen = n - 4;

    if (h.type == Type::DM_ACK) {
        AckPayload a;
        if (!decodeAck(body, blen, a)) return;
        for (auto it = out_.begin(); it != out_.end(); ++it) {
            if (it->to != h.src || it->msgId != a.ackedSeq) continue;
            Type t = it->type;
            out_.erase(it);
            if (t == Type::SOS) L_.onSosAck(h.src);
            if (t == Type::DM_TEXT) L_.onDelivery(h.src, a.ackedSeq, Delivery::DELIVERED);
            if (t == Type::POS_REQ) L_.onDelivery(h.src, a.ackedSeq, Delivery::DELIVERED);
            break;
        }
        if (a.kind == AckKind::READ) L_.onDelivery(h.src, a.ackedSeq, Delivery::READ);
        return;
    }
    if (h.type == Type::VOICE_REQ || h.type == Type::VOICE_ACCEPT || h.type == Type::VOICE_DATA ||
        h.type == Type::VOICE_END) {
        handleVoice(h, h.type, msgId, body, blen, now);
        return;
    }
    // La conferma si rimanda sempre (anche per i duplicati: forse la prima e' andata persa).
    if (h.wantAck) sendAck(h.src, msgId, AckKind::DELIVERED, now);
    if (alreadyHandled(h.src, msgId)) return;

    switch (h.type) {
        case Type::DM_TEXT: {
            TextPayload t;
            if (decodeTextPayload(body, blen, t)) L_.onText(h.src, msgId, t);
            break;
        }
        case Type::POS_REQ:
            L_.onPositionRequest(h.src, msgId);
            break;
        case Type::POS_RESP: {
            PosResponse p;
            if (decodePosResponse(body, blen, p)) L_.onPosition(h.src, p);
            break;
        }
        case Type::SOS: {
            SosPayload s;
            if (decodeSos(body, blen, s)) L_.onSos(h.src, s);
            break;
        }
        case Type::GROUP_INVITE: {
            GroupInvite inv;
            if (!decodeGroupInvite(body, blen, inv)) break;
            if (std::find(inv.members.begin(), inv.members.end(), self_) == inv.members.end()) break;
            GroupInfo g{inv.groupId, inv.key, inv.name, inv.members, h.src};
            groups_[g.id] = g;
            L_.onGroupInvite(g, h.src);
            break;
        }
        default:
            break;
    }
}

// ------------------------------------------------------------------ vocali

bool Phone::viaRepeaters(Millis now) const {
    return lastRepeater_.addr != ADDR_NONE && now - lastRepeater_.at < 30 * 60 * 1000ULL;
}

// I pezzi del vocale partono uno alla volta. Con dei ripetitori in mezzo il successivo parte
// solo quando il primo ripetitore ha finito di ripetere il precedente (lo si sente) e ha
// avuto il tempo di farlo anche il secondo: altrimenti il primo non sente il pezzo nuovo
// (sta trasmettendo) o lo riceve sovrapposto al secondo.
void Phone::voiceSendStep(Millis now) {
    if (vOut_.phase != VoiceOut::SEND || vOut_.inFlight || now < vOut_.nextSendAt) return;
    uint8_t buf[MAX_PAYLOAD];
    size_t n = 0;
    Type t;
    if (!vOut_.toSend.empty()) {
        const uint8_t i = vOut_.toSend.front();
        vOut_.toSend.pop_front();
        VoiceData d{vOut_.session, i, uint8_t(vOut_.chunks.size()), vOut_.chunks[i]};
        n = encodeVoiceData(d, buf, sizeof(buf));
        t = Type::VOICE_DATA;
    } else if (vOut_.endPending) {
        VoiceEnd e{vOut_.session, false, 0};
        n = encodeVoiceEnd(e, buf, sizeof(buf));
        vOut_.endPending = false;
        t = Type::VOICE_END;
    } else {
        return;
    }
    vOut_.lastSeq = sendPairwise(vOut_.to, t, 0, buf, n, false, now, false, Channel::VOICE);
    vOut_.lastAir = airtimeMs(VOICE_PARAMS, HEADER_SIZE + PAIR_OVERHEAD + n);
    vOut_.inFlight = vOut_.lastSeq != 0;
    // Riserva: se l'eco non arriva si riparte comunque dopo questo tempo dalla fine della TX.
    vOut_.nextSendAt = now + 60000;
    vOut_.channelUntil = std::max(vOut_.channelUntil, now + 20000);
}

void Phone::handleVoice(const Header& h, Type t, uint32_t msgId, const uint8_t* body, size_t len, Millis now) {
    (void)msgId;
    if (t == Type::VOICE_REQ) {
        VoiceReq req;
        if (!decodeVoiceReq(body, len, req)) return;
        // Stessa sessione gia' accettata: la risposta era andata persa, si rimanda.
        const bool same = vIn_.active && vIn_.from == h.src && vIn_.session == req.session;
        const bool busy = !same && (vIn_.active || vOut_.active);
        const Millis perPacket = airtimeMs(VOICE_PARAMS, MAX_FRAME) + 150;
        const Millis hold = std::min<Millis>(60000, 6000 + Millis(req.packets) * perPacket * 2);
        if (!busy && !same) {
            vIn_ = VoiceIn();
            vIn_.active = true;
            vIn_.from = h.src;
            vIn_.session = req.session;
            vIn_.codec = req.codec;
            vIn_.durationMs = req.durationMs;
            vIn_.total = req.packets;
            vIn_.chunks.assign(req.packets, {});
            vIn_.channelUntil = 0;  // si passa al canale voce appena trasmesso il "si'"
            vIn_.lastActivity = now;
            L_.onVoiceIncoming(h.src, req);
        }
        VoiceAccept a{req.session, busy, uint16_t(hold)};
        uint8_t buf[8];
        size_t n = encodeVoiceAccept(a, buf, sizeof(buf));
        sendPairwise(h.src, Type::VOICE_ACCEPT, 0, buf, n, false, now);
        return;
    }
    if (t == Type::VOICE_ACCEPT) {
        VoiceAccept a;
        if (!decodeVoiceAccept(body, len, a) || !vOut_.active || vOut_.to != h.src || a.session != vOut_.session ||
            vOut_.phase != VoiceOut::REQ)
            return;
        if (a.busy) {
            L_.onVoiceState(vOut_.to, vOut_.msgId, VoiceSendState::BUSY);
            vOut_.active = false;
            return;
        }
        // Si passa al canale voce: prima i pezzi, poi la fine.
        vOut_.phase = VoiceOut::SEND;
        vOut_.rounds = 0;
        vOut_.toSend.clear();
        for (uint8_t i = 0; i < vOut_.chunks.size(); i++) vOut_.toSend.push_back(i);
        vOut_.endPending = true;
        vOut_.inFlight = false;
        vOut_.nextSendAt = now + VOICE_GUARD_MS;
        vOut_.channelUntil = now + std::max<Millis>(a.holdMs, 20000);
        vOut_.deadline = 0;
        L_.onVoiceState(vOut_.to, vOut_.msgId, VoiceSendState::SENDING);
        return;
    }
    if (t == Type::VOICE_DATA) {
        VoiceData d;
        if (!decodeVoiceData(body, len, d) || !vIn_.active || vIn_.from != h.src || d.session != vIn_.session ||
            d.total != vIn_.total)
            return;
        vIn_.chunks[d.index] = d.data;
        vIn_.have |= (1u << d.index);
        vIn_.lastActivity = now;
        vIn_.channelUntil = std::max(vIn_.channelUntil, now + 15000);
        return;
    }
    if (t == Type::VOICE_END) {
        VoiceEnd e;
        if (!decodeVoiceEnd(body, len, e)) return;
        if (!e.fromReceiver) {
            // Vocale gia' ricevuto tutto: il mittente non ha sentito il resoconto, lo rimando.
            if (!vIn_.active && h.src == lastVoiceFrom_ && e.session == lastVoiceSession_) {
                VoiceEnd rep{e.session, true, 0};
                uint8_t buf[8];
                size_t n = encodeVoiceEnd(rep, buf, sizeof(buf));
                sendPairwise(h.src, Type::VOICE_END, 0, buf, n, false, now, false, Channel::VOICE);
                vIn_.channelUntil = std::max(vIn_.channelUntil, now + 3000);
                return;
            }
            // Il mittente ha finito: rispondo con i pezzi mancanti.
            if (!vIn_.active || vIn_.from != h.src || e.session != vIn_.session) return;
            const uint32_t all = vIn_.total >= 32 ? 0xFFFFFFFFu : ((1u << vIn_.total) - 1);
            const uint32_t missing = all & ~vIn_.have;
            VoiceEnd rep{vIn_.session, true, missing};
            uint8_t buf[8];
            size_t n = encodeVoiceEnd(rep, buf, sizeof(buf));
            sendPairwise(h.src, Type::VOICE_END, 0, buf, n, false, now, false, Channel::VOICE);
            vIn_.lastActivity = now;
            if (missing == 0) {
                lastVoiceFrom_ = vIn_.from;
                lastVoiceSession_ = vIn_.session;
                finishVoiceIn(now);
                // Resto ancora un po' sul canale voce: se il resoconto si perde, il mittente lo richiede.
                vIn_.channelUntil = now + 14000;
            } else {
                vIn_.channelUntil = std::max(vIn_.channelUntil, now + 20000);
            }
            return;
        }
        if (!vOut_.active || vOut_.to != h.src || e.session != vOut_.session || vOut_.phase != VoiceOut::WAIT_REPORT)
            return;
        if (e.missing == 0) {
            L_.onVoiceState(vOut_.to, vOut_.msgId, VoiceSendState::DELIVERED);
            vOut_.phase = VoiceOut::DONE;
            vOut_.channelUntil = now + 500;
            return;
        }
        if (vOut_.rounds >= 2) {
            L_.onVoiceState(vOut_.to, vOut_.msgId, VoiceSendState::PARTIAL);
            vOut_.phase = VoiceOut::DONE;
            vOut_.channelUntil = now + 500;
            return;
        }
        vOut_.rounds++;
        vOut_.toSend.clear();
        for (uint8_t i = 0; i < vOut_.chunks.size(); i++)
            if (e.missing & (1u << i)) vOut_.toSend.push_back(i);
        vOut_.endPending = true;
        vOut_.inFlight = false;
        vOut_.phase = VoiceOut::SEND;
        vOut_.nextSendAt = now + 300;
        vOut_.channelUntil = std::max(vOut_.channelUntil, now + 30000);
    }
}

void Phone::finishVoiceIn(Millis now) {
    (void)now;
    if (!vIn_.active) return;
    if (vIn_.have == 0) {  // non e' arrivato niente: nessun vocale da riprodurre
        vIn_.active = false;
        return;
    }
    const uint32_t all = vIn_.total >= 32 ? 0xFFFFFFFFu : ((1u << vIn_.total) - 1);
    const uint32_t missing = all & ~vIn_.have;
    // Pezzi mancanti riempiti di zeri della stessa lunghezza (l'app li trasforma in silenzio).
    size_t chunkLen = VOICE_CHUNK;
    std::vector<uint8_t> data;
    for (uint8_t i = 0; i < vIn_.total; i++) {
        if (vIn_.have & (1u << i)) data.insert(data.end(), vIn_.chunks[i].begin(), vIn_.chunks[i].end());
        else if (i + 1 < vIn_.total) data.insert(data.end(), chunkLen, 0);
    }
    L_.onVoice(vIn_.from, vIn_.codec, data, vIn_.durationMs, missing);
    vIn_.active = false;
    vIn_.have = 0;
}

void Phone::voiceTick(Millis now) {
    if (vOut_.active) {
        if (vOut_.phase == VoiceOut::REQ && now >= vOut_.deadline) {
            if (vOut_.rounds >= MAX_ATTEMPTS || vOut_.certTries >= MAX_ATTEMPTS) {
                L_.onVoiceState(vOut_.to, vOut_.msgId, VoiceSendState::UNREACHABLE);
                vOut_.active = false;
            } else if (!knowsCert(vOut_.to)) {
                requestCert(vOut_.to, now);
                vOut_.certTries++;
                vOut_.deadline = now + CERT_RETRY_MS;
            } else {
                VoiceReq r{vOut_.session, vOut_.codec, uint8_t(vOut_.chunks.size()), vOut_.durationMs};
                uint8_t buf[8];
                size_t n = encodeVoiceReq(r, buf, sizeof(buf));
                sendPairwise(vOut_.to, Type::VOICE_REQ, vOut_.msgId, buf, n, false, now, vOut_.rounds > 0);
                vOut_.rounds++;
                vOut_.deadline = now + ackTimeout(n, vOut_.rounds);
            }
        } else if (vOut_.phase == VoiceOut::SEND) {
            voiceSendStep(now);
        } else if (vOut_.phase == VoiceOut::WAIT_REPORT && vOut_.deadline && now >= vOut_.deadline) {
            if (vOut_.reasks < 2) {
                // Nessun resoconto: si richiede mandando di nuovo la fine.
                vOut_.reasks++;
                vOut_.endPending = true;
                vOut_.inFlight = false;
                vOut_.phase = VoiceOut::SEND;
                vOut_.nextSendAt = now;
                vOut_.channelUntil = std::max(vOut_.channelUntil, now + 20000);
            } else {
                L_.onVoiceState(vOut_.to, vOut_.msgId, VoiceSendState::PARTIAL);
                vOut_.phase = VoiceOut::DONE;
                vOut_.channelUntil = now;
            }
        }
        if (vOut_.phase == VoiceOut::DONE && now >= vOut_.channelUntil) {
            vOut_.active = false;
            vOut_.rounds = 0;
        }
    }
    if (vIn_.active && vIn_.channelUntil != 0 && now >= vIn_.channelUntil) finishVoiceIn(now);
    // Sul canale voce ma non arriva niente: forse il mio "si'" si e' perso. Torno ad
    // ascoltare i messaggi, cosi' sento la richiesta ripetuta.
    if (vIn_.active && vIn_.channelUntil != 0 && vIn_.have == 0 && now - vIn_.lastActivity > 12000) {
        vIn_.active = false;
        vIn_.channelUntil = 0;
    }
    // Richiesta accettata ma nessun pezzo arrivato entro un minuto: si chiude.
    if (vIn_.active && vIn_.channelUntil == 0 && now - vIn_.lastActivity > 60000) vIn_.active = false;
}

Channel Phone::activeChannel(Millis now) const {
    if (vOut_.active && vOut_.phase != VoiceOut::REQ && now < vOut_.channelUntil) return Channel::VOICE;
    if (vIn_.channelUntil != 0 && now < vIn_.channelUntil) return Channel::VOICE;
    return Channel::MAIN;
}

void Phone::onDuplicate(const Header& h, const RxInfo& info, Millis now) {
    (void)info;
    // Il primo ripetitore ha ripetuto il mio pezzo di vocale: tra poco si puo' mandare il prossimo.
    if (h.src == self_ && h.hops == 1 && vOut_.active && h.seq == vOut_.lastSeq && vOut_.phase == VoiceOut::SEND)
        vOut_.nextSendAt = now + vOut_.lastAir + 700;
    // Un ripetitore ha ripetuto un mio messaggio: e' partito.
    if (h.src == self_ && h.hops > 0)
        echo_.erase(std::remove_if(echo_.begin(), echo_.end(), [&](const EchoWatch& e) { return e.seq == h.seq; }),
                    echo_.end());
}

bool Phone::mayTransmit(const Frame& f, Millis now) const {
    if (now >= paceUntil_ || f.len < HEADER_SIZE) return true;
    // Conferme, vocali e SOS non aspettano.
    const Type t = Type(f.data[0] & 0x3F);
    return t == Type::DM_ACK || t == Type::SOS || t == Type::VOICE_ACCEPT || t == Type::VOICE_DATA ||
           t == Type::VOICE_END || t == Type::BEACON;
}

void Phone::onTransmitted(const uint8_t* data, size_t len, Millis now) {
    Header h;
    if (!decodeHeader(data, len, h)) return;
    // Due miei pacchetti uno dietro l'altro in una catena di ripetitori si pestano i piedi:
    // il secondo parte quando il primo si e' allontanato di un paio di salti.
    if (h.src == self_ && viaRepeaters(now) && h.type != Type::DM_ACK && h.hopLimit > 0)
        paceUntil_ = now + 2 * airtimeMs(MAIN_PARAMS, len) + randomBelow(1000);
    // Ogni pacchetto creato da me (tranne i vocali, che hanno il loro passo, e quelli solo per
    // i vicini) deve essere ripetuto da un ripetitore: se non lo sento, lo rimando una volta.
    const bool broadcastLike = h.type == Type::GLOBAL_TEXT || h.type == Type::GROUP_TEXT;
    const bool voice = h.type == Type::VOICE_REQ || h.type == Type::VOICE_ACCEPT || h.type == Type::VOICE_DATA ||
                       h.type == Type::VOICE_END;
    if (h.src == self_ && h.hopLimit > 0 && !isLocalOnly(h.type) && !voice && (broadcastLike || viaRepeaters(now))) {
        bool known = false;
        for (auto& e : echo_)
            if (e.seq == h.seq) known = true;
        if (!known) {
            EchoWatch e;
            e.seq = h.seq;
            const Millis air = airtimeMs(MAIN_PARAMS, len);
            e.until = now + 2 * air + 1500 + randomBelow(uint32_t(2 * air));
            std::memcpy(e.frame.data, data, len);
            e.frame.len = len;
            e.retries = 1;
            echo_.push_back(e);
        }
    }
    // Pezzo di vocale uscito: senza ripetitori si va avanti subito, altrimenti si aspetta l'eco.
    if (vOut_.active && h.src == self_ && h.seq == vOut_.lastSeq && vOut_.inFlight) {
        vOut_.inFlight = false;
        vOut_.nextSendAt = viaRepeaters(now) ? now + 3 * vOut_.lastAir + 1500 : now + 200;
        if (h.type == Type::VOICE_END && vOut_.toSend.empty() && !vOut_.endPending) {
            vOut_.phase = VoiceOut::WAIT_REPORT;
            const Millis small = airtimeMs(VOICE_PARAMS, HEADER_SIZE + PAIR_OVERHEAD + 8);
            vOut_.deadline = now + (viaRepeaters(now) ? 6 : 1) * (2 * small + 600) + 2000;
            vOut_.channelUntil = std::max(vOut_.channelUntil, vOut_.deadline + 1000);
        }
    }
    // Appena trasmesso il "si'" a un vocale, si passa al canale voce.
    if (h.type == Type::VOICE_ACCEPT && vIn_.active && vIn_.from == h.dst && vIn_.channelUntil == 0) {
        const Millis perPacket = airtimeMs(VOICE_PARAMS, MAX_FRAME) + 150;
        vIn_.channelUntil = now + std::min<Millis>(60000, 6000 + Millis(vIn_.total) * perPacket * 2);
        vIn_.lastActivity = now;
    }
}

// ------------------------------------------------------------------ tempo

void Phone::sendBeacon(Millis now) {
    Beacon b;
    b.role = Role::PHONE;
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

void Phone::tick(Millis now) {
    // Telefono nuovo: si annuncia all'amministratore.
    if (provisioning_ && !hasNumber() && now >= nextHello_) {
        ProvHello p{id_.edPublic, id_.xPublic};
        uint8_t buf[80];
        size_t n = encodeProvHello(p, buf, sizeof(buf));
        Header h;
        h.type = Type::PROV_HELLO;
        h.hopLimit = 0;
        h.src = ADDR_NONE;
        h.dst = ADDR_BROADCAST;
        h.seq = randomBelow(0xFFFFFFF0u) + 1;
        sendRaw(h, buf, n, now);
        nextHello_ = now + 5000 + randomBelow(2000);
    }
    if (hasNumber()) {
        if (nextBeacon_ == 0) nextBeacon_ = now + 10000 + randomBelow(30000);
        if (now >= nextBeacon_ && activeChannel(now) == Channel::MAIN) {
            sendBeacon(now);
            nextBeacon_ = now + beaconIntervalMs - 30000 + randomBelow(60000);
        }
    }
    if (sos_.active && now >= sos_.next) sendSosRound(now);

    // Ritrasmissioni e "non raggiungibile".
    for (size_t i = 0; i < out_.size();) {
        Outgoing& o = out_[i];
        if (now < o.nextTry) { i++; continue; }
        if (o.attempts >= MAX_ATTEMPTS || o.certTries >= MAX_ATTEMPTS) {
            if (o.type == Type::DM_TEXT || o.type == Type::POS_REQ)
                L_.onDelivery(o.to, o.msgId, Delivery::UNREACHABLE);
            out_.erase(out_.begin() + long(i));
            continue;
        }
        trySend(o, now);
        i++;
    }
    // Pacchetti in attesa di certificato: si richiede di nuovo, quelli troppo vecchi si buttano.
    for (auto it = waitingCert_.begin(); it != waitingCert_.end();) {
        auto& list = it->second;
        list.erase(std::remove_if(list.begin(), list.end(), [now](const PendingIn& p) { return now - p.at > 120000; }),
                   list.end());
        if (list.empty()) {
            it = waitingCert_.erase(it);
            continue;
        }
        requestCert(it->first, now);  // limitata a una richiesta ogni CERT_RETRY_MS
        ++it;
    }
    voiceTick(now);
    for (size_t i = 0; i < echo_.size();) {
        EchoWatch& e = echo_[i];
        if (now < e.until) { i++; continue; }
        if (e.retries-- > 0) {
            Header eh;
            decodeHeader(e.frame.data, e.frame.len, eh);
            enqueue(e.frame.data, e.frame.len, priorityOf(eh.type), now, Channel::MAIN);
            e.until = now + 2 * airtimeMs(MAIN_PARAMS, e.frame.len) + 3000;
            i++;
            continue;
        }
        echo_.erase(echo_.begin() + long(i));
    }

    Millis wake = now + 60000;
    for (const auto& e : echo_) wake = std::min(wake, e.until);
    if (provisioning_ && !hasNumber()) wake = std::min(wake, nextHello_);
    if (hasNumber()) wake = std::min(wake, nextBeacon_);
    if (sos_.active) wake = std::min(wake, sos_.next);
    for (const auto& o : out_) wake = std::min(wake, o.nextTry);
    if (!waitingCert_.empty()) wake = std::min(wake, now + 1000);
    if (vOut_.active) wake = std::min(wake, std::max(now + 50, vOut_.deadline));
    if (vIn_.active) wake = std::min(wake, now + 500);
    nextWake_ = wake;
}

}  // namespace pn
