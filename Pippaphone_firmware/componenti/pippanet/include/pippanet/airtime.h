// PippaNet: parametri radio e tempo in aria dei pacchetti LoRa.
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

// Canale messaggi: massima portata.
constexpr RadioParams MAIN_PARAMS{869.525f, 125.0f, 10, 5, 16, 22};
// Canale voce: il doppio piu' veloce.
constexpr RadioParams VOICE_PARAMS{869.850f, 250.0f, 10, 5, 12, 22};
// Sync word privata (diversa da LoRaWAN 0x34, Meshtastic 0x2B e dal "privato" comune 0x12).
constexpr uint8_t SYNC_WORD = 0x6C;

inline const RadioParams& paramsFor(Channel ch) { return ch == Channel::VOICE ? VOICE_PARAMS : MAIN_PARAMS; }

// Tempo in aria in millisecondi (header esplicito, CRC attivo).
uint32_t airtimeMs(const RadioParams& p, size_t payloadLen);

}  // namespace pn
