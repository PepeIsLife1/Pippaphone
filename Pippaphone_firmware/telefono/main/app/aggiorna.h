// Aggiornamento del firmware via Wi-Fi: il telefono crea una rete Wi-Fi e una pagina web
// dove si carica il file .bin dal computer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

// true se chi chiama ha la rete (accesa adesso, o gia' accesa da un altro): allora tocca a lui
// chiamare otaStop() quando non gli serve piu'. false se non e' partita (il motivo in otaStatus()).
bool otaStart(uint16_t number);
void otaStop();
OtaStatus otaStatus();
// Diagnosi (comando USB @RETI?): cerca le reti Wi-Fi intorno, una riga per rete ("nome canale 6
// -60 dBm"). Se va bene, nota dice paese e canali; se no, nota e' il motivo. Non con la rete accesa.
bool otaScan(std::vector<std::string>& reti, std::string& nota);
// Da chiamare all'avvio: conferma che il nuovo firmware funziona (altrimenti al riavvio
// successivo si torna al precedente).
void otaConfirm();
const char* firmwareVersion();

}  // namespace app
