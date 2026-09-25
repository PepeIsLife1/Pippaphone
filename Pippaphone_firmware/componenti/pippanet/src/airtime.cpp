#include "pippanet/airtime.h"

#include <algorithm>
#include <cmath>

namespace pn {

uint32_t airtimeMs(const RadioParams& p, size_t payloadLen) {
    const double tsym = double(1u << p.sf) / (p.bwKHz * 1000.0);  // secondi
    const int de = (tsym > 0.016) ? 1 : 0;                          // low data rate optimize
    const int crc = 1, ih = 0;
    const int cr = p.cr - 4;
    const double num = 8.0 * double(payloadLen) - 4.0 * p.sf + 28 + 16 * crc - 20 * ih;
    const double den = 4.0 * (p.sf - 2 * de);
    double nPayload = 8 + std::max(std::ceil(num / den) * (cr + 4), 0.0);
    // SX126x: per SF5/SF6 il preambolo aggiunge 6.25 simboli, altrimenti 4.25.
    const double nPre = double(p.preamble) + 4.25;
    const double t = (nPre + nPayload) * tsym;
    return uint32_t(std::ceil(t * 1000.0));
}

}  // namespace pn
