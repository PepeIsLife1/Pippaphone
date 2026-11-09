// Vocali: registrazione con codifica Codec2 700C (700 bit/s) e riproduzione; audio delle chiamate.
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

    // ---- chiamata in diretta ----
    // Riceve ogni pacchetto di voce pronto: frame Codec2 700C uno dietro l'altro, senza buchi;
    // last = fine del turno (puo' essere vuoto).
    using CallSink = std::function<void(const uint8_t* data, size_t len, uint8_t frames, bool last)>;
    // Apre l'audio della chiamata (altoparlante sempre pronto) e lo chiude.
    void callStart(CallSink sink);
    void callStop();
    // PTT: comincia a registrare e mandare (framesPerPacket frame per pacchetto) o smette.
    bool callTalk(bool on, uint8_t framesPerPacket);
    bool callTalking() const;
    uint32_t callTalkMs() const;
    // Voce dall'altro (dal compito della radio). redundant: i pacchetti portano anche il precedente.
    void callReceived(uint8_t turn, uint8_t index, const uint8_t* data, size_t len, uint8_t frames,
                      const uint8_t* prev, size_t prevLen, uint8_t prevFrames, bool last, bool redundant);
    // Si sta sentendo l'altro; voce persa nel suo ultimo turno (%).
    bool callHearing() const;
    uint8_t callLostPct() const;
};

Voce& voce();

}  // namespace app
