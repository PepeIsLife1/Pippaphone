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
};

State& state();

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
bool savePng(const std::string& path);
bool savePngRgb(const std::string& path, int w, int h, const std::vector<uint8_t>& rgb);

// Copione: passi eseguiti uno dopo l'altro, ognuno dopo l'attesa del precedente.
void step(int waitMs, std::function<void()> fn);
void run();  // chiamato da vTaskDelay

}  // namespace sim
