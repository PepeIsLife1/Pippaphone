// Ricevitore GPS finto (vedi finto.h) e aiuti dei test.
#include "finto.h"

#include <algorithm>
#include <cstring>
#include <random>

#include "flussi.h"
#include "testing.h"

// Il seme dei numeri casuali dei test (test/main.cpp lo rimette a ogni test).
static std::mt19937 g_caso(12345);
void seedRandom(uint32_t seed) { g_caso.seed(seed); }

namespace {
// Un frame UBX costruito dal pilota stesso (frameUbx e' provato a parte, coi byte veri).
std::string frameUbxStringa(uint8_t cls, uint8_t id, const std::vector<uint8_t>& dati) {
    std::vector<uint8_t> b(dati.size() + 8);
    const size_t n = scheda::gps::frameUbx(cls, id, dati.data(), uint16_t(dati.size()), b.data(), b.size());
    return std::string(reinterpret_cast<const char*>(b.data()), n);
}

void campo(std::vector<uint8_t>& d, const char* testo, size_t n, const char* coda = "") {
    std::vector<uint8_t> c(n, 0);
    std::memcpy(c.data(), testo, std::min(n, std::strlen(testo)));
    // Dopo lo zero finale qualcosa che l'analizzatore NMEA non deve vedere: '$', a capo.
    const size_t l = std::strlen(testo);
    for (size_t i = 0; coda[i] && l + 1 + i < n; i++) c[l + 1 + i] = uint8_t(coda[i]);
    d.insert(d.end(), c.begin(), c.end());
}

// UBX-MON-VER come lo manda un MIA-M10Q (SPG 5.10), con '$' e "\r\n" nascosti nei riempitivi.
std::string monVer() {
    std::vector<uint8_t> d;
    campo(d, "ROM SPG 5.10 (7b202e)", 30, "$GN\r\n");
    campo(d, "000A0000", 10);
    campo(d, "FWVER=SPG 5.10", 30, "$\n");
    campo(d, "PROTVER=34.10", 30);
    campo(d, "MOD=MIA-M10Q", 30);
    campo(d, "GPS;GLO;GAL;BDS", 30);
    campo(d, "SBAS;QZSS", 30, "\r\n$GNGGA");
    return frameUbxStringa(0x0A, 0x04, d);
}

// UBX-MON-HW3: versione 0, 2 pin, flag xtalAbsent (manca il quarzo RTC), hwVersion.
std::string monHw3() {
    std::vector<uint8_t> d = {0x00, 0x02, 0x04};
    campo(d, "000A0000", 10);
    d.insert(d.end(), 9, 0);                                    // reserved0
    const uint8_t pin[12] = {1, 0, 0x10, 0, 0, 0, 2, 0, 0x20, 0, 0, 0};  // pinId, pinMask, VP, reserved1
    d.insert(d.end(), pin, pin + 12);
    return frameUbxStringa(0x0A, 0x37, d);
}
}  // namespace

void Finto::emetti(const std::string& b) {
    if (portaBaud == baud) {
        uscita_ += b;
        return;
    }
    // Alla velocita' sbagliata arrivano solo byte senza senso (mai '$', mai a capo).
    for (char c : b) uscita_ += char(0x80 | (uint8_t(c) & 0x7F));
}

void Finto::produci() {
    if (dormeA && t >= dormeA) {
        dorme = true;
        dormeA = 0;
        if (svegliaDaSolo) svegliaA = t + svegliaDaSolo;
    }
    if (svegliaA && t >= svegliaA) {
        dorme = false;
        svegliaA = 0;
        sveglioA = t;
        prossimaEpoca = t;  // la prima epoca subito
    }
    for (size_t i = 0; i < risposte_.size();) {
        if (risposte_[i].first <= t) {
            if (vivo && !dorme) emetti(risposte_[i].second);
            risposte_.erase(risposte_.begin() + long(i));
        } else {
            i++;
        }
    }
    if (vivo && !dorme && prossimaEpoca <= t) {
        emetti(flusso(scena));
        prossimaEpoca = (t / 1000 + 1) * 1000 + faseEpoca;
    }
}

uint64_t Finto::prossimo(uint64_t fine) const {
    uint64_t p = fine;
    if (vivo && !dorme && prossimaEpoca > t) p = std::min(p, prossimaEpoca);
    if (dormeA > t) p = std::min(p, dormeA);
    if (svegliaA > t) p = std::min(p, svegliaA);
    for (const auto& r : risposte_)
        if (r.first > t) p = std::min(p, r.first);
    return p;
}

int Finto::leggi(uint8_t* buf, size_t n, uint32_t attesaMs) {
    const uint64_t fine = t + attesaMs;
    for (;;) {
        produci();
        if (!uscita_.empty()) {
            const size_t k = std::min(n, uscita_.size());
            std::memcpy(buf, uscita_.data(), k);
            uscita_.erase(0, k);
            t += std::max<uint64_t>(1, k * 10000 / portaBaud);  // 10 bit per byte
            return int(k);
        }
        if (t >= fine) return 0;
        t = std::max(t + 1, prossimo(fine));
        if (t > fine) t = fine;
    }
}

void Finto::scrivi(const uint8_t* p, size_t n) {
    scritti.push_back({t, std::vector<uint8_t>(p, p + n)});
    t += 1 + n * 10000 / portaBaud;  // uart_wait_tx_done
    if (!vivo) return;
    const bool pmreq = n == 24 && p[0] == 0xB5 && p[1] == 0x62 && p[2] == 0x02 && p[3] == 0x41;
    if (dorme || svegliaA) {  // dorme (o si sta svegliando): ogni byte e' un fronte, i comandi si perdono
        if (pmreq) pmreqPersi++;
        if (dorme && !svegliaA) {
            if (frontiPersi > 0) frontiPersi--;
            else svegliaA = t + latenzaSveglia;
        }
        return;
    }
    if (portaBaud != baud) return;  // per il ricevitore sono byte senza senso
    if (n == 1 && p[0] == 0xFF) return;
    if (ublox && n >= 8 && p[0] == 0xB5 && p[1] == 0x62) {
        const uint8_t cls = p[2], id = p[3];
        if (sordoDopoSveglia && sveglioA && t < sveglioA + sordoDopoSveglia) {  // appena sveglio: non lo sente
            ubxPersiDaSveglio++;
            if (pmreq) pmreqPersi++;
            return;
        }
        if (pmreq) {
            pmreqRicevuti++;
            ultimoFlag = p[6 + 8];
            if (!ignoraPmreq) dormeA = t + latenzaStop;
        } else if (cls == 0x06 && id == 0x04) {
            freddi++;
        } else if (cls == 0x0A && id == 0x04 && n == 8) {
            poll++;
            risposte_.push_back({t + 40, monVer()});
        } else if (cls == 0x0A && id == 0x37 && n == 8) {
            poll++;
            risposte_.push_back({t + 60, monHw3()});
        }
        return;
    }
    if (!ublox && n > 7 && !std::memcmp(p, "$PCAS10,", 8)) freddi++;
}

int Finto::contaScritti(uint8_t cls, uint8_t id) const {
    int k = 0;
    for (const auto& s : scritti) {
        if (cls == 0xFF && s.b.size() == 1 && s.b[0] == 0xFF) k++;
        else if (s.b.size() >= 8 && s.b[0] == 0xB5 && s.b[2] == cls && s.b[3] == id) k++;
    }
    return k;
}

uint64_t Finto::quando(uint8_t cls, uint8_t id, int n) const {
    for (const auto& s : scritti) {
        const bool e = (cls == 0xFF && s.b.size() == 1 && s.b[0] == 0xFF) ||
                       (cls != 0xFF && s.b.size() >= 8 && s.b[0] == 0xB5 && s.b[2] == cls && s.b[3] == id);
        if (e && n-- == 0) return s.t;
    }
    return 0;
}

std::vector<uint8_t> Finto::frame(uint8_t cls, uint8_t id, int n) const {
    for (const auto& s : scritti)
        if (s.b.size() >= 8 && s.b[0] == 0xB5 && s.b[2] == cls && s.b[3] == id && n-- == 0) return s.b;
    return {};
}

void gira(scheda::gps::Pilota& p, Finto& f, uint64_t ms) {
    const uint64_t fine = f.t + ms;
    while (f.t < fine) p.giro();
}

int conta(const scheda::gps::Pilota& p, scheda::Gnss::Ev e) {
    scheda::Gnss::Evento ev[scheda::Gnss::STORIA];
    const int n = p.storia(ev, scheda::Gnss::STORIA);
    int k = 0;
    for (int i = 0; i < n; i++) k += ev[i].tipo == e;
    return k;
}

scheda::Gnss::Evento ultimo(const scheda::gps::Pilota& p, scheda::Gnss::Ev e) {
    scheda::Gnss::Evento ev[scheda::Gnss::STORIA];
    const int n = p.storia(ev, scheda::Gnss::STORIA);
    for (int i = n - 1; i >= 0; i--)
        if (ev[i].tipo == e) return ev[i];
    return scheda::Gnss::Evento();
}
