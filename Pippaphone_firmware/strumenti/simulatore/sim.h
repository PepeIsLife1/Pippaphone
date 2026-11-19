// Simulatore dell'interfaccia: stato finto (rete, GPS, batteria) che il copione puo' cambiare.
#pragma once

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "scheda/tastiera.h"

namespace sim {

struct State {
    bool hasNumber = true;
    uint16_t number = 100;
    bool admin = true;
    bool sos = false;
    bool offer = false;          // l'amministratore offre un numero (primo avvio)
    bool hello = true;           // un telefono nuovo chiede un numero (amministrazione)
    bool gpsFix = true;
    // GPS finto del T-Deck (sim_scheda.cpp), oltre a gpsFix (la posizione di adesso): muto (non risponde da
    // 12 s), satelliti in vista, con segnale e usati, C/N0 migliore, ultima posizione nota anche senza quella
    // di adesso (di 5 minuti fa), dove sono. Acceso o spento lo dice scheda::gnss().power(), come sul telefono
    // (il GPS finto e' acceso dall'avvio).
    bool gpsMuto = false;
    uint8_t gpsInVista = 14, gpsConSegnale = 9, gpsUsati = 9, gpsSnrMax = 41;
    bool gpsUltima = false;
    int32_t gpsLatE7 = 454642000, gpsLonE7 = 91900000;
    bool heard = true;           // ripetitore sentito
    uint8_t battery = 76;
    bool charging = false;
    uint8_t micLevel = 0;
    bool recording = false;
    uint32_t recordedMs = 0;
    bool playing = false;
    uint32_t playingFile = 0;
    uint32_t playedMs = 0;
    std::set<uint16_t> sosAcked = {101};
    // Simulatore nel browser: vocali che durano davvero.
    int64_t recStartMs = 0;
    int64_t playStartMs = 0;
    uint32_t playLenMs = 0;
    // Chiamata finta (pn::CallState): chi chiamo risponde dopo 4 s, poi parla 4 s ogni 12.
    uint8_t call = 0;
    uint16_t callPeer = 0;
    bool callOut = false;
    bool callTalking = false;
    int64_t callT0 = 0;    // inizio (o, in chiamata, quando ci si e' sentiti)
    int callHearing = -1;  // -1 = come capita; 0/1 = forzato (screenshot)
    uint8_t callHops = 1;
    // Ora: da dove viene (app::Rete::TimeSource) e spostamento dall'ora simulata (messa a mano).
    uint8_t timeSource = 2;  // NETWORK
    int64_t timeOffsetS = 0;
    // Cuffie USB-C finte (T-Deck): app::Cuffie::Stato ed Errore, nome del prodotto, volume, computer attaccato
    // (accendi() allora dice "Scollega il computer").
    uint8_t cuffieStato = 0;   // SPENTE
    uint8_t cuffieErrore = 0;  // NESSUNO
    std::string cuffieNome;
    uint8_t cuffieVolume = 4;
    bool cuffieComputer = false;
};

State& state();

// Rubrica, chat e gruppi di prova; tile finti per la mappa (labels = coordinate scritte sopra).
void fillData();
void makeTiles(bool labels);

// Tempo simulato (ms dall'avvio).
int64_t nowMs();
// Ora Unix simulata (lunedi' 9 novembre 2026, 14:32 ora italiana all'avvio).
uint32_t unixNow();

// Tasti: arrivano all'interfaccia come dalla tastiera vera.
void key(scheda::Key k, bool pressed);
void tap(scheda::Key k);

// Pixel dello schermo simulato (RGB565).
void setSize(int w, int h);
int width();
int height();
const uint16_t* pixels();
void draw(int x1, int y1, int x2, int y2, const void* px);
// Pixel mandati allo schermo dall'avvio (controllo del risparmio: a schermo spento non si disegna).
uint64_t drawnPixels();
bool savePng(const std::string& path);
bool savePngRgb(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb);

// Copione: passi eseguiti uno dopo l'altro, ognuno dopo l'attesa del precedente.
void step(int waitMs, std::function<void()> fn);
void run();  // chiamato da vTaskDelay

}  // namespace sim
