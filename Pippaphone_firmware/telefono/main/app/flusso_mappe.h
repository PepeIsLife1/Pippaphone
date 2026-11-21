// Mappe dal computer a flusso (T-Deck Plus): la parte che legge i byte della pagina del flasher, senza
// ESP-IDF, provata anche sul PC (test/test_flusso_mappe.cpp). La sessione vera (USB, coda in PSRAM,
// scrittura sulla microSD, crediti) e' in usb_mappe.cpp; il protocollo completo in usb_mappe.h.
//
// Dopo "@FLUSSO" la pagina non aspetta piu' una risposta dopo ogni pezzo: manda di fila, finche' ha
// credito (i byte che il telefono ha gia' tolto dal buffer della USB), queste righe:
//   T <id> <z>/<x>/<y>.png <byte> <crc32>   e subito dopo esattamente <byte> byte (il PNG)
//   C <id> <z> <x0> <y0> <x1> <y1>          quanto pesano le tile del rettangolo (al massimo MAX_DOMANDA)
//   (riga vuota)                            niente: tiene viva la sessione mentre la pagina disegna
//   @FINE                                   fine del flusso
// Qualunque altra cosa rompe il flusso: dopo non si capirebbe piu' dove comincia la riga successiva.
#pragma once

#include <cstddef>
#include <cstdint>

namespace app::flusso {

constexpr uint32_t MAX_FILE = 1024 * 1024;  // come @FILE: una tile piu' grande non e' una tile
constexpr int MAX_DOMANDA = 64;             // tile per domanda (la risposta sta in una riga)
constexpr size_t MAX_RIGA = 96;             // la riga T piu' lunga possibile ne ha 56

struct Tile {
    uint32_t id;
    int z;
    long x, y;
    uint32_t byte;
    uint32_t crc;
};

struct Domanda {
    uint32_t id;
    int z;
    long x0, y0, x1, y1;
    int quante() const { return int((x1 - x0 + 1) * (y1 - y0 + 1)); }
};

// Cosa fa chi usa il lettore. posto() puo' aspettare (la coda e' piena) e da' nullptr solo se il posto non
// ci sara' mai (memoria finita): il flusso si rompe.
class Azioni {
public:
    virtual uint8_t* posto(const Tile& t) = 0;               // dove mettere i t.byte byte della tile
    virtual void tile(const Tile& t, uint8_t* dati) = 0;     // la tile e' arrivata tutta (dati = posto())
    virtual void domanda(const Domanda& d) = 0;
    virtual void fine() = 0;                                  // "@FINE"

protected:
    ~Azioni() = default;
};

class Lettore {
public:
    enum class Stato : uint8_t { RIGA, DATI, FINE, ROTTO };
    explicit Lettore(Azioni& a) : a_(a) {}
    // Legge n byte e dice quanti ne ha usati: tutti, finche' il flusso non finisce o non si rompe.
    size_t consuma(const uint8_t* p, size_t n);
    Stato stato() const { return stato_; }
    const char* motivo() const { return motivo_; }  // perche' si e' rotto
    // Il posto della tile arrivata a meta' (il flusso e' finito prima dei suoi byte): torna a chi l'aveva dato.
    uint8_t* lasciaPosto() {
        uint8_t* d = dati_;
        dati_ = nullptr;
        return d;
    }

private:
    void esegui();
    void rompi(const char* motivo);

    Azioni& a_;
    Stato stato_ = Stato::RIGA;
    const char* motivo_ = "";
    char riga_[MAX_RIGA];
    size_t lung_ = 0;
    Tile tile_ = {};
    uint8_t* dati_ = nullptr;
    uint32_t preso_ = 0;
};

// "z/x/y.png" scritto esattamente cosi' (niente altro: niente "..", zeri davanti, segni), zoom 0-20.
bool percorso(const char* rel, int& z, long& x, long& y);

// CRC-32 come zlib.
uint32_t crc32(const uint8_t* p, size_t n);

}  // namespace app::flusso
