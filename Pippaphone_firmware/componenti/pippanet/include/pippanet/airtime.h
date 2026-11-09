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

// SF7 su tutti e due i canali (scelta del 2026-11-09, prima era SF10 a 125/250 kHz): pacchetti
// circa 3 volte piu' corti, in cambio di 4,5 dB di sensibilita' (piu' ripetitori, piu' vicini).
// Canale messaggi: banda stretta, 3 dB in piu' della voce.
constexpr RadioParams MAIN_PARAMS{869.525f, 62.5f, 7, 5, 16, 22};
// Canale voce: il doppio piu' veloce.
constexpr RadioParams VOICE_PARAMS{869.850f, 125.0f, 7, 5, 12, 22};
// Sync word privata (diversa da LoRaWAN 0x34, Meshtastic 0x2B e dal "privato" comune 0x12).
constexpr uint8_t SYNC_WORD = 0x6C;

inline const RadioParams& paramsFor(Channel ch) { return ch == Channel::VOICE ? VOICE_PARAMS : MAIN_PARAMS; }

// SNR minimo per ricevere con questo SF (dB): SF7 -7,5 ... SF10 -15 ... SF12 -20.
// Le soglie sul segnale si contano da qui, cosi' non dipendono dallo SF scelto.
constexpr float snrLimit(const RadioParams& p) { return -2.5f * float(p.sf - 4); }

// Tempo in aria in millisecondi (header esplicito, CRC attivo).
uint32_t airtimeMs(const RadioParams& p, size_t payloadLen);

}  // namespace pn
