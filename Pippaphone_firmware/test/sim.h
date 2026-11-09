// Simulatore di rete per i test: nodi, collegamenti radio, collisioni e canali.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <vector>

#include "pippanet/phone.h"
#include "pippanet/repeater.h"

namespace sim {

using pn::Millis;

// Ascoltatore che registra tutto cio' che succede su un telefono.
struct Recorder : pn::PhoneListener {
    struct Msg { pn::Addr from; uint32_t id; std::string text; };
    std::vector<Msg> texts, globals;
    std::vector<std::pair<uint16_t, Msg>> groupTexts;
    std::map<uint32_t, pn::Delivery> delivery;
    std::vector<std::pair<uint32_t, pn::Delivery>> deliveryLog;  // tutti gli stati, in ordine
    std::vector<std::pair<pn::Addr, uint32_t>> posRequests;
    std::vector<std::pair<pn::Addr, pn::PosResponse>> positions;
    std::vector<std::pair<pn::Addr, pn::SosPayload>> sos;
    int sosAcks = 0;
    std::vector<pn::GroupInfo> invites;
    std::vector<std::vector<uint8_t>> voices;
    std::vector<uint32_t> voiceMissing;
    std::map<uint32_t, pn::VoiceSendState> voiceState;
    std::vector<pn::Cert> certs;
    bool offerReceived = false;
    pn::Cert offerCert;
    pn::Key32 offerAdmin{};
    std::vector<pn::ProvHello> hellos;

    void onText(pn::Addr from, uint32_t id, const pn::TextPayload& t) override { texts.push_back({from, id, t.text}); }
    void onGroupText(uint16_t g, pn::Addr from, uint32_t id, const pn::TextPayload& t) override {
        groupTexts.push_back({g, {from, id, t.text}});
    }
    void onGlobalText(pn::Addr from, uint32_t id, const pn::TextPayload& t) override {
        globals.push_back({from, id, t.text});
    }
    void onDelivery(pn::Addr, uint32_t id, pn::Delivery d) override {
        deliveryLog.push_back({id, d});
        auto it = delivery.find(id);
        const bool failing = d == pn::Delivery::UNREACHABLE || d == pn::Delivery::RETRYING;
        const bool failed = it != delivery.end() &&
                            (it->second == pn::Delivery::UNREACHABLE || it->second == pn::Delivery::RETRYING);
        if (it == delivery.end() || failing || failed || d > it->second) delivery[id] = d;
    }
    bool sawDelivery(uint32_t id, pn::Delivery d) const {
        for (const auto& x : deliveryLog)
            if (x.first == id && x.second == d) return true;
        return false;
    }
    void onPositionRequest(pn::Addr from, uint32_t id) override { posRequests.push_back({from, id}); }
    void onPosition(pn::Addr from, const pn::PosResponse& p) override { positions.push_back({from, p}); }
    void onSos(pn::Addr from, const pn::SosPayload& s) override { sos.push_back({from, s}); }
    void onSosAck(pn::Addr) override { sosAcks++; }
    void onGroupInvite(const pn::GroupInfo& g, pn::Addr) override { invites.push_back(g); }
    void onVoice(pn::Addr, pn::Codec, const std::vector<uint8_t>& d, uint16_t, uint32_t missing) override {
        voices.push_back(d);
        voiceMissing.push_back(missing);
    }
    void onVoiceState(pn::Addr, uint32_t id, pn::VoiceSendState s) override { voiceState[id] = s; }
    // Chiamate.
    struct CallEv { pn::Addr peer; pn::CallState state; pn::CallResult result; };
    std::vector<CallEv> calls;
    struct CallPkt { uint8_t turn, index, frames; std::vector<uint8_t> data; bool last, copy; };
    std::vector<CallPkt> callVoice;
    pn::CallResult lastCallResult() const {
        for (auto it = calls.rbegin(); it != calls.rend(); ++it)
            if (it->state == pn::CallState::IDLE) return it->result;
        return pn::CallResult::NONE;
    }
    bool callReached(pn::CallState s) const {
        for (const auto& c : calls)
            if (c.state == s) return true;
        return false;
    }
    // Pacchetti di voce diversi arrivati in un turno (contando anche le copie della ridondanza).
    size_t callPackets(uint8_t turn) const {
        std::set<int> seen;
        for (const auto& p : callVoice)
            if (p.turn == turn && p.frames) seen.insert(p.index);
        return seen.size();
    }
    void onCallState(pn::Addr peer, pn::CallState s, pn::CallResult r) override { calls.push_back({peer, s, r}); }
    void onCallVoice(pn::Addr, uint8_t turn, uint8_t index, const uint8_t* data, size_t len, uint8_t frames,
                     const uint8_t* prev, size_t prevLen, uint8_t prevFrames, bool last) override {
        if (prevFrames && index > 0)
            callVoice.push_back({turn, uint8_t(index - 1), prevFrames, std::vector<uint8_t>(prev, prev + prevLen), false, true});
        callVoice.push_back({turn, index, frames, std::vector<uint8_t>(data, data + len), last, false});
    }
    void onCertLearned(const pn::Cert& c) override { certs.push_back(c); }
    void onProvisionOffer(const pn::Cert& c, const pn::Key32& a) override {
        offerReceived = true;
        offerCert = c;
        offerAdmin = a;
    }
    void onProvisionHello(uint64_t, const pn::ProvHello& h, const pn::RxInfo&) override { hellos.push_back(h); }
};

struct Transmission {
    int from;
    pn::Channel ch;
    Millis start, end;
    pn::Frame frame;
};

class Network {
public:
    // Aggiunge un telefono; restituisce l'indice.
    int addPhone(const pn::Identity& id) {
        auto rec = std::make_unique<Recorder>();
        auto ph = std::make_unique<pn::Phone>(id, *rec);
        nodes_.push_back(ph.get());
        phones_[int(nodes_.size()) - 1] = {std::move(ph), std::move(rec)};
        busyUntil_.push_back(0);
        return int(nodes_.size()) - 1;
    }
    int addRepeater(pn::Addr addr) {
        auto r = std::make_unique<pn::Repeater>(addr);
        nodes_.push_back(r.get());
        repeaters_[int(nodes_.size()) - 1] = std::move(r);
        busyUntil_.push_back(0);
        return int(nodes_.size()) - 1;
    }
    pn::Phone& phone(int i) { return *phones_.at(i).first; }
    Recorder& rec(int i) { return *phones_.at(i).second; }
    pn::Repeater& repeater(int i) { return *repeaters_.at(i); }
    pn::Node& node(int i) { return *nodes_.at(i); }

    // Collegamento in entrambe le direzioni.
    void link(int a, int b, float snr = 5.0f, int16_t rssi = -100) {
        links_[{a, b}] = {rssi, snr};
        links_[{b, a}] = {rssi, snr};
    }
    void unlink(int a, int b) {
        links_.erase({a, b});
        links_.erase({b, a});
    }
    void setLossPercent(int p) { lossPct_ = p; }  // probabilita' di perdere ogni ricezione

    Millis now() const { return now_; }

    void run(Millis ms) {
        const Millis end = now_ + ms;
        while (now_ < end) step();
    }
    // Avanza finche' la condizione e' vera o scade il tempo; true se la condizione si e' avverata.
    template <typename F>
    bool runUntil(F cond, Millis maxMs) {
        const Millis end = now_ + maxMs;
        while (now_ < end) {
            step();
            if (cond()) return true;
        }
        return false;
    }

    uint64_t collisions = 0, delivered = 0, transmissions = 0;

private:
    bool hears(int rx, int tx) const { return links_.count({tx, rx}) != 0; }

    void step() {
        now_ += 10;
        for (auto* n : nodes_) n->tick(now_);
        // Fine delle trasmissioni: consegna a chi ascoltava sullo stesso canale.
        for (auto it = air_.begin(); it != air_.end();) {
            if (it->end > now_) { ++it; continue; }
            Transmission t = *it;
            for (int rx = 0; rx < int(nodes_.size()); rx++) {
                if (rx == t.from || !hears(rx, t.from)) continue;
                if (nodes_[rx]->activeChannel(now_) != t.ch) continue;
                // In trasmissione durante la ricezione: non sente nulla.
                if (txDuring(rx, t.start, t.end)) continue;
                // Collisione: un altro nodo udibile trasmetteva sullo stesso canale.
                bool coll = false;
                for (const auto& o : log_) {
                    if (&o == &*it || o.from == t.from || o.ch != t.ch || !hears(rx, o.from)) continue;
                    if (o.start < t.end && o.end > t.start) { coll = true; break; }
                }
                if (coll) {
                    collisions++;
                    if (std::getenv("PN_TRACE")) std::printf("\n%8.2fs  n%d collisione (da n%d)", now_ / 1000.0, rx, t.from);
                    continue;
                }
                if (lossPct_ && int(lossRng_() % 100) < lossPct_) continue;
                const auto& q = links_.at({t.from, rx});
                pn::RxInfo info{q.rssi, q.snr, t.ch};
                nodes_[rx]->onReceive(t.frame.data, t.frame.len, info, now_);
                delivered++;
            }
            nodes_[t.from]->onTransmitted(t.frame.data, t.frame.len, now_);
            it = air_.erase(it);
        }
        // Nuove trasmissioni (con ascolto prima di parlare).
        for (int i = 0; i < int(nodes_.size()); i++) {
            if (busyUntil_[i] > now_) continue;
            const pn::Channel ch = nodes_[i]->activeChannel(now_);
            bool channelBusy = false;
            for (const auto& t : air_)
                if (t.ch == ch && hears(i, t.from)) { channelBusy = true; break; }
            if (channelBusy) continue;
            pn::Frame f;
            if (!nodes_[i]->popTx(now_, ch, f)) continue;
            const Millis air = pn::airtimeMs(pn::paramsFor(ch), f.len);
            Transmission t{i, ch, now_, now_ + air, f};
            if (std::getenv("PN_TRACE")) {
                pn::Header h;
                pn::decodeHeader(f.data, f.len, h);
                std::printf("\n%8.2fs  n%d tx %s tipo=%02x %u->%u seq=%u hops=%u/%u via=%04x ult=%04x len=%zu%s",
                            now_ / 1000.0, i, ch == pn::Channel::VOICE ? "VOCE" : "msg ", unsigned(h.type), h.src,
                            h.dst, h.seq, h.hops, h.hopLimit, h.via, h.lastHop, f.len, h.forceFlood ? " FLOOD" : "");
            }
            air_.push_back(t);
            log_.push_back(t);
            busyUntil_[i] = now_ + air + 20;
            transmissions++;
        }
        while (!log_.empty() && log_.front().end + 5000 < now_) log_.erase(log_.begin());
    }

    bool txDuring(int node, Millis a, Millis b) const {
        for (const auto& o : log_)
            if (o.from == node && o.start < b && o.end > a) return true;
        return false;
    }

    struct Q { int16_t rssi; float snr; };
    std::vector<pn::Node*> nodes_;
    std::map<int, std::pair<std::unique_ptr<pn::Phone>, std::unique_ptr<Recorder>>> phones_;
    std::map<int, std::unique_ptr<pn::Repeater>> repeaters_;
    std::map<std::pair<int, int>, Q> links_;
    std::vector<Transmission> air_;
    std::vector<Transmission> log_;
    std::vector<Millis> busyUntil_;
    Millis now_ = 1000;
    int lossPct_ = 0;
    std::mt19937 lossRng_{4242};
};

}  // namespace sim
