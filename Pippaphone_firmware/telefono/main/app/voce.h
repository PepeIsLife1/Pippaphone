// Vocali: registrazione con codifica Codec2 700C (700 bit/s) e riproduzione.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace app {

constexpr uint32_t VOICE_MAX_MS = 15000;  // durata massima di un vocale

class Voce {
public:
    bool begin();
    // Registra finche' non si chiama stop() o si arriva a VOICE_MAX_MS.
    bool startRecording();
    void stopRecording();
    bool recording() const;
    uint32_t recordedMs() const;
    // Dati Codec2 dell'ultima registrazione (disponibili quando recording() diventa false).
    std::vector<uint8_t> takeRecording(uint16_t& durationMs);

    // Riproduce un vocale salvato (non blocca).
    void play(uint32_t file, std::function<void()> done = nullptr);
    void stop();
    bool playing() const;
    uint32_t playedMs() const;
    uint32_t playingFile() const;
};

Voce& voce();

}  // namespace app
