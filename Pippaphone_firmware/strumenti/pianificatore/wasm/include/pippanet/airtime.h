// Copia di componenti/pippanet/include/pippanet/airtime.h per il pianificatore.
// Unica differenza: MAIN_PARAMS e VOICE_PARAMS sono variabili, cosi' la simulazione puo' provare
// i profili SF7 / SF8 / SF10 con lo stesso protocollo. Il firmware non usa questo file: lo trova
// prima solo la compilazione del pianificatore (strumenti/pianificatore/wasm/compila.sh).
#pragma once

#include "pippanet/types.h"

namespace pn {

struct RadioParams {
    float freqMHz;
    float bwKHz;
    uint8_t sf;
    uint8_t cr;  // 5..8 = 4/5..4/8
    uint16_t preamble;
    int8_t powerDbm;
};

inline RadioParams MAIN_PARAMS{869.525f, 62.5f, 7, 5, 16, 22};
inline RadioParams VOICE_PARAMS{869.850f, 125.0f, 7, 5, 12, 22};
constexpr uint8_t SYNC_WORD = 0x6C;

inline const RadioParams& paramsFor(Channel ch) { return ch == Channel::VOICE ? VOICE_PARAMS : MAIN_PARAMS; }

constexpr float snrLimit(const RadioParams& p) { return -2.5f * float(p.sf - 4); }

uint32_t airtimeMs(const RadioParams& p, size_t payloadLen);

}  // namespace pn
