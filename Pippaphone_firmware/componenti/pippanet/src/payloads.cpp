#include "pippanet/payloads.h"

#include "pippanet/charset.h"

namespace pn {

size_t encodeAck(const AckPayload& p, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.u32(p.ackedSeq);
    w.u8(uint8_t(p.kind));
    return w.ok() ? w.len() : 0;
}

bool decodeAck(const uint8_t* in, size_t len, AckPayload& p) {
    Reader r(in, len);
    p.ackedSeq = r.u32();
    p.kind = AckKind(r.u8());
    return r.ok() && (p.kind == AckKind::DELIVERED || p.kind == AckKind::READ);
}

size_t encodeTextPayload(const TextPayload& p, uint8_t* out, size_t cap) {
    if (cap < 4) return 0;
    Writer w(out, cap);
    w.u32(p.sentAt);
    size_t n = encodeText(p.text, out + 4, cap - 4);
    if (n == 0 && !p.text.empty()) return 0;
    return 4 + n;
}

bool decodeTextPayload(const uint8_t* in, size_t len, TextPayload& p) {
    if (len < 4) return false;
    Reader r(in, len);
    p.sentAt = r.u32();
    p.text = decodeText(in + 4, len - 4);
    return true;
}

size_t encodePosition(const Position& p, Writer& w) {
    size_t start = w.len();
    w.u8(p.valid ? 1 : 0);
    w.i32(p.latE7);
    w.i32(p.lonE7);
    w.i16(p.altM);
    w.u8(p.accM);
    w.u16(p.ageS);
    return w.len() - start;
}

bool decodePosition(Reader& r, Position& p) {
    p.valid = r.u8() != 0;
    p.latE7 = r.i32();
    p.lonE7 = r.i32();
    p.altM = r.i16();
    p.accM = r.u8();
    p.ageS = r.u16();
    return r.ok();
}

size_t encodePosResponse(const PosResponse& p, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.u32(p.requestSeq);
    w.u8(uint8_t(p.status));
    encodePosition(p.pos, w);
    return w.ok() ? w.len() : 0;
}

bool decodePosResponse(const uint8_t* in, size_t len, PosResponse& p) {
    Reader r(in, len);
    p.requestSeq = r.u32();
    p.status = PosStatus(r.u8());
    return decodePosition(r, p.pos) && uint8_t(p.status) <= 2;
}

size_t encodeSos(const SosPayload& p, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.u8(p.active ? 1 : 0);
    w.u16(p.round);
    w.u8(p.battery);
    encodePosition(p.pos, w);
    return w.ok() ? w.len() : 0;
}

bool decodeSos(const uint8_t* in, size_t len, SosPayload& p) {
    Reader r(in, len);
    p.active = r.u8() != 0;
    p.round = r.u16();
    p.battery = r.u8();
    return decodePosition(r, p.pos);
}

size_t encodeGroupInvite(const GroupInvite& g, uint8_t* out, size_t cap) {
    if (g.members.size() > MAX_GROUP_MEMBERS) return 0;
    Writer w(out, cap);
    w.u16(g.groupId);
    w.put(g.key.data(), 32);
    w.u8(uint8_t(g.members.size()));
    for (Addr a : g.members) w.u16(a);
    if (!w.ok()) return 0;
    // Il nome (al massimo 24 caratteri) occupa il resto.
    size_t n = encodeText(g.name, out + w.len(), cap - w.len(), 24);
    return w.len() + n;
}

bool decodeGroupInvite(const uint8_t* in, size_t len, GroupInvite& g) {
    Reader r(in, len);
    g.groupId = r.u16();
    r.get(g.key.data(), 32);
    uint8_t n = r.u8();
    if (n == 0 || n > MAX_GROUP_MEMBERS) return false;
    g.members.clear();
    for (uint8_t i = 0; i < n; i++) g.members.push_back(r.u16());
    if (!r.ok()) return false;
    g.name = decodeText(r.ptr(), r.remaining());
    return true;
}

size_t encodeBeacon(const Beacon& b, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.u8(uint8_t(b.role));
    w.u8(uint8_t((b.gpsTime ? 1 : 0) | (b.timeValid ? 2 : 0)));
    w.u8(b.battery);
    w.u32(b.unixTime);
    return w.ok() ? w.len() : 0;
}

bool decodeBeacon(const uint8_t* in, size_t len, Beacon& b) {
    Reader r(in, len);
    b.role = Role(r.u8());
    uint8_t f = r.u8();
    b.gpsTime = (f & 1) != 0;
    b.timeValid = (f & 2) != 0;
    b.battery = r.u8();
    b.unixTime = r.u32();
    return r.ok() && (b.role == Role::PHONE || b.role == Role::REPEATER);
}

size_t encodeProvHello(const ProvHello& p, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.put(p.edPublic.data(), 32);
    w.put(p.xPublic.data(), 32);
    return w.ok() ? w.len() : 0;
}

bool decodeProvHello(const uint8_t* in, size_t len, ProvHello& p) {
    Reader r(in, len);
    r.get(p.edPublic.data(), 32);
    r.get(p.xPublic.data(), 32);
    return r.ok();
}

size_t encodeProvCert(const ProvCert& p, uint8_t* out, size_t cap) {
    if (cap < 8 + CERT_SIZE + 32) return 0;
    Writer w(out, cap);
    w.u32(uint32_t(p.target));
    w.u32(uint32_t(p.target >> 32));
    uint8_t c[CERT_SIZE];
    p.cert.serialize(c);
    w.put(c, CERT_SIZE);
    w.put(p.adminPublic.data(), 32);
    return w.ok() ? w.len() : 0;
}

bool decodeProvCert(const uint8_t* in, size_t len, ProvCert& p) {
    Reader r(in, len);
    uint64_t lo = r.u32();
    uint64_t hi = r.u32();
    p.target = lo | (hi << 32);
    if (r.remaining() < CERT_SIZE + 32) return false;
    if (!Cert::parse(r.ptr(), CERT_SIZE, p.cert)) return false;
    r.skip(CERT_SIZE);
    r.get(p.adminPublic.data(), 32);
    return r.ok();
}

size_t encodeVoiceReq(const VoiceReq& p, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.u16(p.session);
    w.u8(uint8_t(p.codec));
    w.u8(p.packets);
    w.u16(p.durationMs);
    return w.ok() ? w.len() : 0;
}

bool decodeVoiceReq(const uint8_t* in, size_t len, VoiceReq& p) {
    Reader r(in, len);
    p.session = r.u16();
    p.codec = Codec(r.u8());
    p.packets = r.u8();
    p.durationMs = r.u16();
    return r.ok() && p.packets > 0 && p.packets <= MAX_VOICE_PACKETS;
}

size_t encodeVoiceAccept(const VoiceAccept& p, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.u16(p.session);
    w.u8(p.busy ? 1 : 0);
    w.u16(p.holdMs);
    return w.ok() ? w.len() : 0;
}

bool decodeVoiceAccept(const uint8_t* in, size_t len, VoiceAccept& p) {
    Reader r(in, len);
    p.session = r.u16();
    p.busy = r.u8() != 0;
    p.holdMs = r.u16();
    return r.ok();
}

size_t encodeVoiceData(const VoiceData& p, uint8_t* out, size_t cap) {
    if (p.data.size() > VOICE_DATA_MAX) return 0;
    Writer w(out, cap);
    w.u16(p.session);
    w.u8(p.index);
    w.u8(p.total);
    w.put(p.data.data(), p.data.size());
    return w.ok() ? w.len() : 0;
}

bool decodeVoiceData(const uint8_t* in, size_t len, VoiceData& p) {
    Reader r(in, len);
    p.session = r.u16();
    p.index = r.u8();
    p.total = r.u8();
    if (!r.ok() || p.total == 0 || p.total > MAX_VOICE_PACKETS || p.index >= p.total) return false;
    p.data.assign(r.ptr(), r.ptr() + r.remaining());
    return true;
}

size_t encodeVoiceEnd(const VoiceEnd& p, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.u16(p.session);
    w.u8(p.fromReceiver ? 1 : 0);
    w.u32(p.missing);
    return w.ok() ? w.len() : 0;
}

bool decodeVoiceEnd(const uint8_t* in, size_t len, VoiceEnd& p) {
    Reader r(in, len);
    p.session = r.u16();
    p.fromReceiver = r.u8() != 0;
    p.missing = r.u32();
    return r.ok();
}

size_t encodeCallReq(const CallReq& p, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.u16(p.session);
    w.u8(uint8_t(p.codec));
    return w.ok() ? w.len() : 0;
}

bool decodeCallReq(const uint8_t* in, size_t len, CallReq& p) {
    Reader r(in, len);
    p.session = r.u16();
    p.codec = Codec(r.u8());
    return r.ok() && p.session != 0;
}

size_t encodeCallRing(const CallRing& p, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.u16(p.session);
    w.u8(uint8_t(p.status));
    return w.ok() ? w.len() : 0;
}

bool decodeCallRing(const uint8_t* in, size_t len, CallRing& p) {
    Reader r(in, len);
    p.session = r.u16();
    p.status = CallRingStatus(r.u8());
    return r.ok();
}

size_t encodeCallAnswer(const CallAnswer& p, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.u16(p.session);
    return w.ok() ? w.len() : 0;
}

bool decodeCallAnswer(const uint8_t* in, size_t len, CallAnswer& p) {
    Reader r(in, len);
    p.session = r.u16();
    return r.ok();
}

size_t encodeCallEnd(const CallEndMsg& p, uint8_t* out, size_t cap) {
    Writer w(out, cap);
    w.u16(p.session);
    w.u8(uint8_t(p.reason));
    return w.ok() ? w.len() : 0;
}

bool decodeCallEnd(const uint8_t* in, size_t len, CallEndMsg& p) {
    Reader r(in, len);
    p.session = r.u16();
    p.reason = CallEndReason(r.u8());
    return r.ok();
}

size_t encodeCallVoice(const CallVoice& p, uint8_t* out, size_t cap) {
    if (p.frames > 15 || p.prevFrames > 15) return 0;
    Writer w(out, cap);
    w.u16(p.session);
    w.u8(p.turn);
    w.u8(p.index);
    w.u8(p.flags);
    w.u8(uint8_t(p.frames | (p.prevFrames << 4)));
    w.put(p.prev, p.prevFrames ? p.prevLen : 0);
    w.put(p.data, p.frames ? p.len : 0);
    return w.ok() ? w.len() : 0;
}

bool decodeCallVoice(const uint8_t* in, size_t len, Codec codec, CallVoice& p) {
    Reader r(in, len);
    p.session = r.u16();
    p.turn = r.u8();
    p.index = r.u8();
    p.flags = r.u8();
    const uint8_t f = r.u8();
    if (!r.ok()) return false;
    p.frames = f & 0x0F;
    p.prevFrames = f >> 4;
    p.prevLen = p.prevFrames ? codecBytes(codec, p.prevFrames) : 0;
    p.len = p.frames ? codecBytes(codec, p.frames) : 0;
    if (r.remaining() != p.prevLen + p.len) return false;
    p.prev = p.prevLen ? r.ptr() : nullptr;
    p.data = p.len ? r.ptr() + p.prevLen : nullptr;
    return true;
}

}  // namespace pn
