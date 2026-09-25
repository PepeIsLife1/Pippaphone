#include "pippanet/packet.h"

namespace pn {

void encodeHeader(const Header& h, uint8_t* buf) {
    uint8_t flags = (h.forceFlood ? FLAG_FORCE_FLOOD : 0) | (h.wantAck ? FLAG_WANT_ACK : 0);
    buf[0] = uint8_t(flags | (uint8_t(h.type) & 0x3F));
    buf[1] = uint8_t(((h.hopLimit & 0x0F) << 4) | (h.hops & 0x0F));
    buf[2] = uint8_t(h.src);
    buf[3] = uint8_t(h.src >> 8);
    buf[4] = uint8_t(h.dst);
    buf[5] = uint8_t(h.dst >> 8);
    buf[6] = uint8_t(h.seq);
    buf[7] = uint8_t(h.seq >> 8);
    buf[8] = uint8_t(h.seq >> 16);
    buf[9] = uint8_t(h.seq >> 24);
    buf[10] = uint8_t(h.via);
    buf[11] = uint8_t(h.via >> 8);
    buf[12] = uint8_t(h.lastHop);
    buf[13] = uint8_t(h.lastHop >> 8);
}

bool decodeHeader(const uint8_t* buf, size_t len, Header& h) {
    if (len < HEADER_SIZE || len > MAX_FRAME) return false;
    Reader r(buf, len);
    uint8_t b0 = r.u8();
    uint8_t b1 = r.u8();
    h.type = Type(b0 & 0x3F);
    h.forceFlood = (b0 & FLAG_FORCE_FLOOD) != 0;
    h.wantAck = (b0 & FLAG_WANT_ACK) != 0;
    h.hopLimit = uint8_t(b1 >> 4);
    h.hops = uint8_t(b1 & 0x0F);
    h.src = r.u16();
    h.dst = r.u16();
    h.seq = r.u32();
    h.via = r.u16();
    h.lastHop = r.u16();
    if (h.hops > h.hopLimit) return false;
    return r.ok();
}

void headerAd(const Header& h, uint8_t ad[AD_SIZE]) {
    ad[0] = uint8_t(h.type) & 0x3F;
    ad[1] = uint8_t(h.src);
    ad[2] = uint8_t(h.src >> 8);
    ad[3] = uint8_t(h.dst);
    ad[4] = uint8_t(h.dst >> 8);
    ad[5] = uint8_t(h.seq);
    ad[6] = uint8_t(h.seq >> 8);
    ad[7] = uint8_t(h.seq >> 16);
    ad[8] = uint8_t(h.seq >> 24);
}

void patchRelayFields(uint8_t* buf, uint8_t hops, Addr via, Addr lastHop, bool forceFlood) {
    buf[0] = uint8_t((buf[0] & ~FLAG_FORCE_FLOOD) | (forceFlood ? FLAG_FORCE_FLOOD : 0));
    buf[1] = uint8_t((buf[1] & 0xF0) | (hops & 0x0F));
    buf[10] = uint8_t(via);
    buf[11] = uint8_t(via >> 8);
    buf[12] = uint8_t(lastHop);
    buf[13] = uint8_t(lastHop >> 8);
}

}  // namespace pn
