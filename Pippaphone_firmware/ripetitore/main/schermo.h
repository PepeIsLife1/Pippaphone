// Pagine dell'OLED 128x64 dei ripetitori con lo schermo (Heltec V3, LilyGO T3-S3).
//
// Qui c'e' solo il disegno nel framebuffer, niente hardware: si prova anche sul PC
// (strumenti/simulatore non serve, basta compilare questo file).
#pragma once

#include <cstddef>
#include <cstdint>

namespace schermo {

constexpr int W = 128, H = 64;

// Framebuffer nel formato dell'SSD1306: 8 righe di 128 byte, ogni byte e' una colonna di 8
// pixel (bit 0 in alto).
struct Framebuffer {
    uint8_t buf[W * H / 8];

    void clear();
    void pixel(int x, int y, bool on = true);
    bool get(int x, int y) const;
    // Testo col carattere 5x7 in celle da 6x8 pixel (scala 2: 12x16). Restituisce la x dopo
    // l'ultimo carattere.
    int text(int x, int y, const char* s, int scale = 1);
    // Testo allineato a destra: l'ultimo pixel acceso cade in xDestra.
    void textRight(int xDestra, int y, const char* s, int scale = 1);
};

struct Vicino {
    uint16_t addr;      // ripetitore (0xF001..0xFFFE) o telefono
    float snr;          // dB, dell'ultimo pacchetto sentito da lui
    uint32_t secondi;   // da quanto non lo sente
};

constexpr int MAX_VICINI = 7;

struct Stato {
    uint16_t indirizzo = 0xF001;
    bool radioOk = false;
    int8_t potenza = 0;           // dBm
    bool canaleVoce = false;
    float mhz = 0, khz = 0;       // canale in uso
    int sf = 0;
    uint32_t ripetuti = 0, ricevuti = 0, trasmessi = 0, soppressi = 0, recuperati = 0;
    uint32_t scartati = 0;        // pacchetti con l'intestazione sbagliata
    uint32_t erroriRadio = 0;     // CRC o intestazione LoRa sbagliati
    uint32_t secondiInAria = 0;
    bool haUltimo = false;        // ha gia' ricevuto qualcosa?
    int16_t rssi = 0;             // dell'ultimo pacchetto
    float snr = 0;
    uint32_t ultimoSecondi = 0;   // da quanto e' arrivato l'ultimo pacchetto
    int nVicini = 0;              // vicini diretti sentiti negli ultimi 30 minuti
    int strade = 0;               // destinazioni conosciute
    uint32_t accesoSecondi = 0;
    int batteriaMv = -1;          // -1: non misurata
    int batteriaPercento = -1;    // -1: non nota
    Vicino vicini[MAX_VICINI];    // i migliori, gia' in ordine
    int nElenco = 0;              // quanti ce ne sono in vicini[]
};

constexpr int PAGINE = 3;

// Pagina 0: stato; 1: vicini diretti; 2: radio.
void disegna(Framebuffer& fb, const Stato& s, int pagina);

// Avviso a tutto schermo (per esempio la batteria scarica): titolo grande e due righe.
void avviso(Framebuffer& fb, const char* titolo, const char* riga1, const char* riga2);

}  // namespace schermo
