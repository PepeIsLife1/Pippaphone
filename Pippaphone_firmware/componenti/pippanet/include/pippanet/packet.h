// PippaNet: intestazione dei pacchetti e utilita' di (de)serializzazione.
//
// Intestazione (14 byte, little endian):
//   0      flag(2 bit) | tipo(6 bit)      flag: bit7 = FORZA_FLOOD, bit6 = VUOLE_ACK
//   1      limite_salti(4) | salti_fatti(4)
//   2..3   mittente
//   4..5   destinatario (0xFFFF = tutti; id del gruppo per GROUP_TEXT)
//   6..9   seq: contatore del mittente, unico per ogni pacchetto che crea
//   10..11 via: ripetitore che deve ritrasmettere (0 = chiunque, flood)
//   12..13 ultimo: chi ha trasmesso questa copia
// Poi il carico utile. I campi "salti_fatti", "via" e "ultimo" cambiano a ogni salto e
// non entrano nei dati autenticati; tutto il resto si'.
#pragma once

#include <cstring>

#include "pippanet/types.h"

namespace pn {

constexpr size_t HEADER_SIZE = 14;
constexpr size_t AD_SIZE = 9;  // tipo, mittente, destinatario, seq
constexpr size_t MAX_PAYLOAD = MAX_FRAME - HEADER_SIZE;

constexpr uint8_t FLAG_FORCE_FLOOD = 0x80;
constexpr uint8_t FLAG_WANT_ACK = 0x40;

struct Header {
    Type type = Type::BEACON;
    bool forceFlood = false;
    bool wantAck = false;
    uint8_t hopLimit = DEFAULT_HOP_LIMIT;
    uint8_t hops = 0;
    Addr src = ADDR_NONE;
    Addr dst = ADDR_NONE;
    uint32_t seq = 0;
    Addr via = ADDR_NONE;
    Addr lastHop = ADDR_NONE;
};

struct Frame {
    uint8_t data[MAX_FRAME];
    size_t len = 0;
};

// Scrive/legge interi little endian con controllo dei limiti.
class Writer {
public:
    Writer(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}
    bool u8(uint8_t v) { return put(&v, 1); }
    bool u16(uint16_t v) { uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)}; return put(b, 2); }
    bool u32(uint32_t v) {
        uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        return put(b, 4);
    }
    bool i32(int32_t v) { return u32(uint32_t(v)); }
    bool i16(int16_t v) { return u16(uint16_t(v)); }
    bool put(const void* p, size_t n) {
        if (!ok_ || len_ + n > cap_) { ok_ = false; return false; }
        if (n == 0) return true;
        std::memcpy(buf_ + len_, p, n);
        len_ += n;
        return true;
    }
    size_t len() const { return len_; }
    bool ok() const { return ok_; }
    uint8_t* data() { return buf_; }

private:
    uint8_t* buf_;
    size_t cap_;
    size_t len_ = 0;
    bool ok_ = true;
};

class Reader {
public:
    Reader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {}
    uint8_t u8() { uint8_t v = 0; get(&v, 1); return v; }
    uint16_t u16() { uint8_t b[2] = {0, 0}; get(b, 2); return uint16_t(b[0] | (b[1] << 8)); }
    uint32_t u32() {
        uint8_t b[4] = {0, 0, 0, 0};
        get(b, 4);
        return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
    }
    int32_t i32() { return int32_t(u32()); }
    int16_t i16() { return int16_t(u16()); }
    bool get(void* p, size_t n) {
        if (!ok_ || pos_ + n > len_) { ok_ = false; std::memset(p, 0, n); return false; }
        std::memcpy(p, buf_ + pos_, n);
        pos_ += n;
        return true;
    }
    const uint8_t* ptr() const { return buf_ + pos_; }
    size_t remaining() const { return ok_ ? len_ - pos_ : 0; }
    void skip(size_t n) { if (pos_ + n > len_) ok_ = false; else pos_ += n; }
    bool ok() const { return ok_; }

private:
    const uint8_t* buf_;
    size_t len_;
    size_t pos_ = 0;
    bool ok_ = true;
};

// Scrive l'intestazione in buf (almeno HEADER_SIZE byte).
void encodeHeader(const Header& h, uint8_t* buf);
// Legge l'intestazione; false se il pacchetto e' troppo corto o malformato.
bool decodeHeader(const uint8_t* buf, size_t len, Header& h);
// Dati autenticati (AD_SIZE byte): tipo senza flag, mittente, destinatario, seq.
void headerAd(const Header& h, uint8_t ad[AD_SIZE]);
// Aggiorna solo i campi che cambiano a ogni salto in un pacchetto gia' codificato.
void patchRelayFields(uint8_t* buf, uint8_t hops, Addr via, Addr lastHop, bool forceFlood);

}  // namespace pn
