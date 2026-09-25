#include "pippanet/node.h"

#include <algorithm>

#include "pippanet/crypto.h"

namespace pn {

uint32_t randomBelow(uint32_t n) {
    if (n <= 1) return 0;
    uint32_t v;
    randomBytes(reinterpret_cast<uint8_t*>(&v), sizeof(v));
    return v % n;
}

// ------------------------------------------------------------------ DedupCache

bool DedupCache::contains(Addr src, uint32_t seq, Millis now) const {
    for (const auto& e : items_)
        if (e.src == src && e.seq == seq && now - e.at < ttl_) return true;
    return false;
}

bool DedupCache::checkAndInsert(Addr src, uint32_t seq, Millis now) {
    while (!items_.empty() && (items_.size() >= cap_ || now - items_.front().at >= ttl_)) items_.pop_front();
    if (contains(src, seq, now)) return true;
    items_.push_back({src, seq, now});
    return false;
}

// ------------------------------------------------------------------ Node

Node::Node(Addr self) : self_(self) {}

void Node::setUnixTime(uint32_t unixSec, Millis now, bool fromGps) {
    // Un'ora dal GPS non viene mai sostituita da una presa dalla rete.
    if (timeFromGps_ && !fromGps && now - millisAtSync_ < 6 * 3600 * 1000ULL) return;
    unixAtSync_ = unixSec;
    millisAtSync_ = now;
    timeValid_ = true;
    timeFromGps_ = fromGps;
}

uint32_t Node::unixTime(Millis now) const {
    if (!timeValid_) return 0;
    return unixAtSync_ + uint32_t((now - millisAtSync_) / 1000);
}

uint32_t Node::nextSeq() {
    uint32_t s = seq_++;
    if (seq_ == 0) seq_ = 1;
    if (seq_ >= reserved_) {
        reserved_ = seq_ + 256;
        if (onSeqReserve) onSeqReserve(reserved_);
    }
    return s;
}

void Node::onReceive(const uint8_t* data, size_t len, const RxInfo& info, Millis now) {
    Header h;
    if (!decodeHeader(data, len, h)) {
        rxDropped++;
        return;
    }
    rxCount++;
    // I propri pacchetti ripetuti da un ripetitore: servono solo come "eco".
    if (h.src == self_ && self_ != ADDR_NONE) {
        onDuplicate(h, info, now);
        return;
    }
    // I telefoni senza numero (src 0) non hanno seq affidabili: niente filtro duplicati.
    if (h.src != ADDR_NONE && dedup_.checkAndInsert(h.src, h.seq, now)) {
        onDuplicate(h, info, now);
        return;
    }
    handle(h, data, len, info, now);
}

void Node::tick(Millis now) { (void)now; }

void Node::enqueue(const uint8_t* data, size_t len, Priority prio, Millis notBefore, Channel ch, uint64_t tag) {
    if (len == 0 || len > MAX_FRAME) return;
    TxItem it;
    std::memcpy(it.frame.data, data, len);
    it.frame.len = len;
    it.prio = prio;
    it.notBefore = notBefore;
    it.ch = ch;
    it.tag = tag;
    it.order = order_++;
    queue_.push_back(it);
}

bool Node::cancelQueued(uint64_t tag) {
    auto it = std::remove_if(queue_.begin(), queue_.end(), [tag](const TxItem& t) { return t.tag == tag; });
    bool found = it != queue_.end();
    queue_.erase(it, queue_.end());
    return found;
}

bool Node::isQueued(uint64_t tag) const {
    for (const auto& t : queue_)
        if (t.tag == tag) return true;
    return false;
}

void Node::clearQueue(Channel ch) {
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [ch](const TxItem& t) { return t.ch == ch; }),
                 queue_.end());
}

bool Node::popTx(Millis now, Channel ch, Frame& out) {
    int best = -1;
    for (size_t i = 0; i < queue_.size(); i++) {
        const auto& t = queue_[i];
        if (t.ch != ch || t.notBefore > now || !mayTransmit(t.frame, now)) continue;
        if (best < 0 || t.prio > queue_[best].prio ||
            (t.prio == queue_[best].prio && t.order < queue_[best].order))
            best = int(i);
    }
    if (best < 0) return false;
    out = queue_[best].frame;
    queue_.erase(queue_.begin() + best);
    txCount++;
    txAirtimeMs += airtimeMs(paramsFor(ch), out.len);
    return true;
}

Millis Node::msUntilNextEvent(Millis now) const {
    Millis next = nextWake_ > now ? nextWake_ - now : 0;
    if (nextWake_ == 0) next = 60000;
    for (const auto& t : queue_) {
        Millis d = t.notBefore > now ? t.notBefore - now : 0;
        next = std::min(next, d);
    }
    return next;
}

uint32_t Node::sendRaw(Header h, const uint8_t* payload, size_t len, Millis now, Millis delay, Channel ch,
                       uint64_t tag) {
    if (HEADER_SIZE + len > MAX_FRAME) return 0;
    if (h.seq == 0) h.seq = nextSeq();
    h.lastHop = self_;
    uint8_t buf[MAX_FRAME];
    encodeHeader(h, buf);
    std::memcpy(buf + HEADER_SIZE, payload, len);
    // I propri pacchetti non vanno rielaborati se tornano indietro ripetuti.
    if (h.src != ADDR_NONE) dedup_.checkAndInsert(h.src, h.seq, now);
    enqueue(buf, HEADER_SIZE + len, priorityOf(h.type), now + delay, ch, tag);
    return h.seq;
}

}  // namespace pn
