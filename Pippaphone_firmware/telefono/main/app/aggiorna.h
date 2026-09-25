// Aggiornamento del firmware via Wi-Fi: il telefono crea una rete Wi-Fi e una pagina web
// dove si carica il file .bin dal computer.
#pragma once

#include <cstdint>
#include <string>

namespace app {

struct OtaStatus {
    bool active = false;
    std::string ssid;
    std::string password;
    std::string address = "192.168.4.1";
    uint32_t received = 0;   // byte ricevuti
    uint32_t total = 0;
    bool done = false;       // aggiornamento scritto: si riavvia
    std::string error;
    std::string version;
};

void otaStart(uint16_t number);
void otaStop();
OtaStatus otaStatus();
// Da chiamare all'avvio: conferma che il nuovo firmware funziona (altrimenti al riavvio
// successivo si torna al precedente).
void otaConfirm();
const char* firmwareVersion();

}  // namespace app
