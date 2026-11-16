// Audio: amplificatore MAX98357A e microfono ICS-43434 sullo stesso bus I2S (16 kHz).
// All'esterno si lavora a 8 kHz, 16 bit mono (quello che vuole Codec2).
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "sdkconfig.h"
#if CONFIG_SCHEDA_TDECK_PLUS
#include <atomic>
#include <cstddef>
#endif

namespace scheda {

struct Note {
    uint16_t hz;  // 0 = pausa
    uint16_t ms;
};

#if CONFIG_SCHEDA_TDECK_PLUS
// Uscita audio esterna (solo T-Deck): le cuffie USB-C, che app/cuffie.cpp registra all'avvio con
// Audio::setUscitaEsterna. scheda non sa niente della USB: vede solo questa interfaccia. Per ora (fase P1)
// ci va solo la musica: quando l'uscita e' pronta, la musica non passa da setTxRate, Limitatore e I2S (resta
// mono, le cuffie la mettono uguale a sinistra e a destra); tutti gli altri suoni restano sulla cassa.
class UscitaEsterna {
public:
    virtual ~UscitaEsterna() = default;
    // Le cuffie sono collegate e lo stream gira: quello che si spinge si sente. La si chiede a ogni blocco
    // (compito audio_out) e dall'interfaccia: non blocca e non prende lock.
    virtual bool pronta() const = 0;
    // Mette in coda `frame` frame interleaved di `canali` canali (1 o 2) a `hz` (8000..48000 Hz): la
    // conversione alla frequenza delle cuffie la fa lei (e riparte da capo quando cambia hz). Con
    // aspetta = true, se in coda ci sono gia' piu' di ~60 ms aspetta che si svuoti (il passo lo da' il clock
    // della USB, come il DMA dell'I2S per la cassa), ma torna subito se smette di essere pronta o se arriva
    // svuota(), e comunque entro ~200 ms; con aspetta = false prende quello che ci sta e butta il resto.
    // Restituisce i frame presi. La chiama un compito solo (audio_out).
    virtual size_t spingi(const int16_t* pcm, size_t frame, uint8_t canali, uint32_t hz, bool aspetta) = 0;
    // Butta quello che e' in coda (sfumando in qualche ms, senza clic) e dimentica la storia della
    // conversione: per pausa, salto nel brano, stop, o un altro suono che interrompe. Da qualunque compito;
    // non blocca. Quello che si spinge dopo si sente.
    virtual void svuota() = 0;
};
#endif

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
    // Musica (solo T-Deck; sulla PCB non fanno niente): la cassa passa alla frequenza del brano e
    // suona i campioni mono messi in coda (1 secondo, in PSRAM). Avvisi, vocali, chiamate e sveglie
    // hanno la precedenza: la musica si ferma da sola e riparte quando hanno finito; mentre si
    // registra col microfono resta zitta. musicPush aspetta finche' c'e' posto (al massimo waitMs) e
    // dice quanti campioni ha messo. busy() resta false: con la musica lo schermo si spegne lo stesso.
    bool musicStart(uint32_t rate);
    size_t musicPush(const int16_t* mono, size_t n, uint32_t waitMs);
    void musicPause(bool paused);
    void musicStop();
    bool musicActive() const;
    uint32_t musicQueuedMs() const;
#if CONFIG_SCHEDA_TDECK_PLUS
    // Uscita esterna (cuffie USB-C): la registra app/cuffie.cpp una volta all'avvio, nullptr = solo la cassa.
    // Quando c'e' ed e' pronta() la musica va li' invece che sulla cassa.
    void setUscitaEsterna(UscitaEsterna* u) { uscita_.store(u); }
    UscitaEsterna* uscitaEsterna() const { return uscita_.load(); }
#endif

    void startRecording(uint32_t maxMs, std::function<void(const int16_t*, size_t)> onFrame,
                        std::function<void()> onEnd);
    void stopRecording();
    bool recording() const;
    // Livello del microfono 0..100 (per il VU-metro).
    uint8_t level() const { return level_; }

    volatile uint8_t level_ = 0;  // aggiornato dal compito di registrazione

private:
    uint8_t volume_ = 6;
#if CONFIG_SCHEDA_TDECK_PLUS
    std::atomic<UscitaEsterna*> uscita_{nullptr};
#endif
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
extern const std::vector<Note> SVEGLIA;         // sveglia (a ciclo)
extern const std::vector<Note> TIMER;           // timer arrivato a zero (a ciclo)
}  // namespace suoni

}  // namespace scheda
