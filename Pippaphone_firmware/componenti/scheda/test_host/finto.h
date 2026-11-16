// Ricevitore GPS finto per provare sul PC il pilota del T-Deck (src/tdeck/gnss_pilota.cpp): un u-blox
// M10 (o un L76K) con l'orologio finto, le frasi a 1 Hz, lo standby col PMREQ, la sveglia con un fronte
// sulla seriale e le risposte a UBX-MON-VER e UBX-MON-HW3. Tutto deterministico.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "gnss_pilota.h"

class Finto : public scheda::gps::Porta {
public:
    // ---- com'e' fatto il ricevitore (si cambia anche a meta' prova) ----
    uint32_t baud = 38400;           // 38400 u-blox, 9600 L76K
    bool ublox = true;               // capisce UBX (PMREQ, CFG-RST, MON-*)
    bool vivo = true;                // false: non parla e non sente niente (scollegato, senza corrente)
    bool ignoraPmreq = false;        // sveglio, riceve il PMREQ e continua a parlare
    uint32_t latenzaSveglia = 300;   // ms dal fronte alla prima frase
    int frontiPersi = 0;             // quanti fronti di sveglia non sente (poi si sveglia)
    uint32_t latenzaStop = 80;       // ms dal PMREQ allo standby
    uint32_t svegliaDaSolo = 0;      // se > 0: ms dopo ogni standby si risveglia da solo
    uint32_t sordoDopoSveglia = 0;   // se > 0: per tanti ms dopo ogni sveglia perde i comandi UBX (l'ipotesi
                                     // per cui il pilota li manda solo a ricevitore sveglio da 1 s)
    uint32_t faseEpoca = 100;        // ogni epoca parte a ogni secondo + faseEpoca ms
    std::vector<std::string> scena;  // corpi delle frasi di ogni epoca (flussi.h)

    // ---- stato ----
    uint64_t t = 1000;               // ms dall'avvio (orologio finto)
    bool dorme = false;
    uint64_t svegliaA = 0, dormeA = 0;  // in programma (0 = niente)
    uint64_t sveglioA = 0;           // ultima sveglia (da addormentato a sveglio), 0 = mai
    uint64_t prossimaEpoca = 1100;
    uint32_t portaBaud = 38400;      // velocita' della UART dell'ESP (velocita())

    // ---- cosa e' successo ----
    struct Scritto {
        uint64_t t;
        std::vector<uint8_t> b;
    };
    std::vector<Scritto> scritti;
    std::vector<std::string> righe;  // registro
    int pmreqRicevuti = 0;           // arrivati da sveglio
    int pmreqPersi = 0;              // arrivati mentre dormiva o si stava svegliando (o appena sveglio, se sordo)
    int ubxPersiDaSveglio = 0;       // comandi UBX persi appena sveglio (sordoDopoSveglia)
    uint8_t ultimoFlag = 0;          // flag dell'ultimo PMREQ ricevuto
    int freddi = 0, poll = 0;
    int doppi = 0;                   // lucchetto preso due volte (sul T-Deck sarebbe un blocco per sempre)
    bool bloccato = false;

    // ---- Porta ----
    uint64_t ms() override { return t; }
    int leggi(uint8_t* buf, size_t n, uint32_t attesaMs) override;
    void scrivi(const uint8_t* p, size_t n) override;
    void velocita(uint32_t b) override { portaBaud = b; }
    void blocca() override {
        if (bloccato) doppi++;
        bloccato = true;
    }
    void sblocca() override { bloccato = false; }
    void registro(bool avviso, const char* testo) override { righe.push_back(std::string(avviso ? "W " : "I ") + testo); }

    // ---- aiuti ----
    // Quante volte e' stato scritto un frame UBX cls/id (o lo 0xFF con cls = 0xFF).
    int contaScritti(uint8_t cls, uint8_t id) const;
    // L'istante dell'n-esimo frame cls/id (0 se non c'e').
    uint64_t quando(uint8_t cls, uint8_t id, int n = 0) const;
    // Byte del frame cls/id numero n (vuoto se non c'e').
    std::vector<uint8_t> frame(uint8_t cls, uint8_t id, int n = 0) const;

private:
    void produci();
    uint64_t prossimo(uint64_t fine) const;
    void emetti(const std::string& b);
    std::string uscita_;                                  // byte arrivati, da leggere
    std::vector<std::pair<uint64_t, std::string>> risposte_;  // risposte UBX in programma
};

// Fa girare il pilota per ms millisecondi di orologio finto.
void gira(scheda::gps::Pilota& p, Finto& f, uint64_t ms);
// Gli eventi della storia di tipo e.
int conta(const scheda::gps::Pilota& p, scheda::Gnss::Ev e);
// L'ultimo evento di tipo e (tipo VOLUTO e ms 0 se non c'e').
scheda::Gnss::Evento ultimo(const scheda::gps::Pilota& p, scheda::Gnss::Ev e);
