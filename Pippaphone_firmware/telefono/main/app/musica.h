// Lettore musicale (solo T-Deck Plus): brani MP3 e WAV della cartella /musica della microSD (e delle
// sue sottocartelle, un livello), in ordine di nome. Un compito legge e decodifica il brano e mette i
// campioni nella coda dell'audio (scheda::Audio::musicPush): la cassa la gestisce l'audio, che
// ferma la musica per avvisi, vocali, chiamate e sveglie e la fa ripartire dopo.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace app {

struct Brano {
    std::string path;     // percorso completo del file
    std::string nome;     // nome da mostrare (dal file, senza estensione; "cartella/nome")
    std::string titolo;   // dal tag ID3 (vuoto se non c'e')
    std::string artista;
};

class Musica {
public:
    // Brani della microSD (la prima volta, o con rileggi, legge la cartella).
    std::vector<Brano> elenco(bool rileggi = false);
    // L'ultima lettura non e' riuscita perche' la microSD non risponde (non perche' manca la cartella):
    // di solito e' stata tolta e rimessa a telefono acceso, e la scheda si prepara solo all'accensione.
    bool sdNonRisponde() const { return sdNonRisponde_; }
    void suona(int indice);  // dall'inizio
    void pausa(bool p);
    bool inPausa() const { return pausa_; }
    void ferma();
    void successivo();
    void precedente();       // dopo i primi 3 secondi torna all'inizio del brano
    void salta(int secondi); // avanti o indietro nel brano
    bool attiva() const { return attiva_; }  // sta suonando o e' in pausa
    int corrente() const { return indice_; }
    Brano brano() const;     // il brano corrente (con titolo e artista dopo la lettura del tag)
    uint32_t posizioneS() const;
    uint32_t durataS() const { return durataS_; }
    std::string errore() const { return errore_; }

    // Usati dal compito che decodifica.
    void compito();

private:
    enum Comando : uint8_t { NESSUNO, SUONA, FERMA, SALTA };
    bool apri(int indice);
    bool decodificaMp3(void* f);
    bool decodificaWav(void* f);
    bool controlla();  // false = c'e' un comando nuovo (ferma, altro brano); un salto non conta
    void spingi(const int16_t* mono, size_t n, uint32_t rate);

    std::vector<Brano> brani_;
    bool letti_ = false;
    bool sdNonRisponde_ = false;
    volatile bool attiva_ = false, pausa_ = false;
    volatile int indice_ = -1;
    volatile Comando cmd_ = NESSUNO;
    volatile int cmdIndice_ = 0, cmdSalto_ = 0;
    volatile uint32_t rate_ = 0;  // lo azzera il compito della musica: si legge una volta sola
    uint64_t campioni_ = 0;   // campioni del file (alla sua frequenza) gia' messi in coda dall'inizio del brano
    // rate_ e' la frequenza del file; la cassa suona a rate_ x fattore_ (il MAX98357A accetta solo
    // alcune frequenze: vedi fattoreUscita in musica.cpp), interpolando. ultimo_ e' il campione prima,
    // che resta da un pezzo all'altro; su_ i campioni interpolati (PSRAM).
    uint32_t fattore_ = 1;
    int16_t ultimo_ = 0;
    bool conUltimo_ = false;
    int16_t* su_ = nullptr;
    uint32_t durataS_ = 0;
    uint32_t kbps_ = 0;       // MP3: per stimare la durata e i salti
    long inizioDati_ = 0, fineDati_ = 0;  // byte del file con l'audio (senza tag)
    uint16_t wavCanali_ = 0, wavBit_ = 0;
    uint32_t wavRate_ = 0;
    std::string errore_;
    std::string titolo_, artista_;
    void* task_ = nullptr;
};

Musica& musica();

}  // namespace app
