// Ricevitore GNSS AT6558R: legge le frasi NMEA (9600 baud) e tiene l'ultima posizione e l'ora.
#pragma once

#include <cstdint>

namespace scheda {

struct Fix {
    bool valid = false;       // posizione valida
    bool timeValid = false;   // ora e data valide (arrivano anche senza posizione)
    double lat = 0, lon = 0;  // gradi
    float altM = 0;
    float hdop = 99;
    uint8_t sats = 0;
    uint32_t unixTime = 0;    // secondi UTC dell'ultima frase con ora valida
    uint64_t atMs = 0;        // quando e' arrivata la posizione (ms dall'avvio)
    uint64_t timeAtMs = 0;    // quando e' arrivata l'ora
};

class Gnss {
public:
    bool begin();
    void power(bool on);
    bool powered() const { return on_; }
    Fix fix() const;
    // Precisione stimata in metri (da HDOP).
    float accuracyM() const;

private:
    bool on_ = false;
};

Gnss& gnss();

// Analizzatore NMEA (separato per poterlo provare sul PC).
class NmeaParser {
public:
    // Passa un carattere; true quando una frase valida ha aggiornato il fix.
    bool feed(char c, Fix& fix, uint64_t nowMs);

private:
    bool parse(Fix& fix, uint64_t nowMs);
    char line_[100];
    int len_ = 0;
};

}  // namespace scheda
