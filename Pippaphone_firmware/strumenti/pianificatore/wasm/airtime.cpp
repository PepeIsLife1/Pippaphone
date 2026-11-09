// Tempo in aria per il pianificatore: come componenti/pippanet/src/airtime.cpp, piu' il caso
// SF5/SF6 dell'SX126x (datasheet 6.1.4, uguale a RadioLib SX126x::calculateTimeOnAir): il
// preambolo aggiunge 6,25 simboli invece di 4,25 e nel conto del carico manca il +8.
// A SF7..SF12 da' esattamente gli stessi numeri del firmware.
#include "pippanet/airtime.h"

#include <algorithm>
#include <cmath>

namespace pn {

uint32_t airtimeMs(const RadioParams& p, size_t payloadLen) {
    const double tsym = double(1u << p.sf) / (p.bwKHz * 1000.0);  // secondi
    const int de = (tsym > 0.016) ? 1 : 0;                          // low data rate optimize
    const bool sfBasso = p.sf <= 6;
    const int crc = 1, ih = 0;
    const int cr = p.cr - 4;
    const double num = 8.0 * double(payloadLen) - 4.0 * p.sf + (sfBasso ? 20 : 28) + 16 * crc - 20 * ih;
    const double den = 4.0 * (p.sf - 2 * de);
    const double nPayload = 8 + std::max(std::ceil(num / den) * (cr + 4), 0.0);
    const double nPre = double(p.preamble) + (sfBasso ? 6.25 : 4.25);
    const double t = (nPre + nPayload) * tsym;
    return uint32_t(std::ceil(t * 1000.0));
}

}  // namespace pn
