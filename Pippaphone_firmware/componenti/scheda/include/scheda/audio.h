// Audio: amplificatore MAX98357A e microfono ICS-43434 sullo stesso bus I2S (16 kHz).
// All'esterno si lavora a 8 kHz, 16 bit mono (quello che vuole Codec2).
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace scheda {

struct Note {
    uint16_t hz;  // 0 = pausa
    uint16_t ms;
};

class Audio {
public:
    static constexpr int RATE = 8000;  // frequenza dei campioni scambiati con l'applicazione

    bool begin();
    // Volume 0..10.
    void setVolume(uint8_t v) { volume_ = v > 10 ? 10 : v; }
    uint8_t volume() const { return volume_; }

    // Suona una melodia (non blocca). Una nuova melodia interrompe la precedente.
    void play(const std::vector<Note>& notes, bool loop = false);
    // Riproduce campioni a 8 kHz (non blocca).
    void playPcm(std::vector<int16_t> pcm, std::function<void()> done = nullptr);
    void stop();
    bool busy() const;

    // Riproduzione continua (chiamate): i campioni a 8 kHz si aggiungono man mano con streamPush;
    // quando mancano l'altoparlante suona silenzio. Finche' e' aperta, melodie e playPcm non partono.
    void streamStart();
    void streamPush(const int16_t* pcm, size_t n);
    void streamStop();
    bool streaming() const;
    // Millisecondi di voce in attesa di essere suonati.
    uint32_t streamQueuedMs() const;

    // Registra dal microfono a 8 kHz fino a maxMs o fino a stopRecording().
    // onFrame riceve blocchi di 320 campioni (40 ms, un frame di Codec2 700C).
    void startRecording(uint32_t maxMs, std::function<void(const int16_t*, size_t)> onFrame,
                        std::function<void()> onEnd);
    void stopRecording();
    bool recording() const;
    // Livello del microfono 0..100 (per il VU-metro).
    uint8_t level() const { return level_; }

    volatile uint8_t level_ = 0;  // aggiornato dal compito di registrazione

private:
    uint8_t volume_ = 6;
};

Audio& audio();

// Melodie predefinite.
namespace suoni {
extern const std::vector<Note> SQUILLO;     // messaggio in arrivo
extern const std::vector<Note> CLASSICA;    // suoneria
extern const std::vector<Note> SOS;         // allarme SOS ricevuto
extern const std::vector<Note> CLIC;        // tasto
extern const std::vector<Note> INVIO_OK;    // messaggio consegnato
extern const std::vector<Note> ERRORE;
extern const std::vector<Note> PTT_INIZIO;  // si parla
extern const std::vector<Note> PTT_FINE;
extern const std::vector<Note> SQUILLO_USCITA;  // chiamata: sta squillando dall'altra parte
}  // namespace suoni

}  // namespace scheda
