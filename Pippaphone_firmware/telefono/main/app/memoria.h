// Memoria del telefono.
//  - NVS: identita' (semi delle chiavi), certificato, chiave dell'amministratore, contatore
//    dei pacchetti, impostazioni;
//  - partizione "dati" (FAT): rubrica, certificati degli altri, gruppi, conversazioni, vocali.
#pragma once

#include <string>
#include <vector>

#include "pippanet/crypto.h"
#include "pippanet/phone.h"
#include "sdkconfig.h"

namespace app {

bool memoriaInit();

// ---- NVS ----
bool loadIdentity(pn::Identity& id);            // false = primo avvio (la crea e la salva)
bool loadCredentials(pn::Cert& own, pn::Key32& adminPub);
void saveCredentials(const pn::Cert& own, const pn::Key32& adminPub);
bool loadAdminIdentity(pn::Identity& admin);
void saveAdminIdentity(const pn::Identity& admin);
uint32_t loadSeq();
void saveSeq(uint32_t next);
// Ultima ora valida (secondi Unix, 0 se mai), salvata ogni 10 minuti: quando l'ora si perde
// (interruttore del T-Deck, batteria scarica) "Data e ora" riparte da qui e basta correggerla.
uint32_t loadLastTime();
void saveLastTime(uint32_t unixSec);
std::string getStr(const char* key, const std::string& def = "");
void setStr(const char* key, const std::string& v);
int32_t getInt(const char* key, int32_t def);
void setInt(const char* key, int32_t v);
// Cancella tutto (ripristino di fabbrica).
void eraseAll();

// ---- file ----
std::vector<pn::Cert> loadCerts();
void saveCert(const pn::Cert& c);
bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool writeFile(const std::string& path, const std::vector<uint8_t>& data);
void removeFile(const std::string& path);

#if CONFIG_SCHEDA_TDECK_PLUS
// T-Deck Plus: con la microSD i dati (rubrica, certificati degli altri, gruppi, chat, vocali,
// impostazioni) stanno in /sd/pippaphone; senza, nella partizione interna "dati" come sulla PCB.
// Le chiavi, il certificato proprio e il PIN restano sempre nella memoria interna (NVS).
extern const char* DATI;  // "/sd/pippaphone" oppure "/dati"
std::string datiPath(const char* rel);
#define DATI_PATH(rel) (app::datiPath(rel))
#define DATI_CPATH(rel) (app::datiPath(rel).c_str())
bool sdPresente();
// Mappe offline sulla microSD: /sd/mappe/<zoom>/<x>/<y>.png (tile OpenStreetMap).
extern const char* const MAPPE;
// Scrive /sd/pippaphone/info.txt (informazioni del dispositivo, da leggere sul computer).
void writeDeviceInfo(const std::string& text);
#else
extern const char* const DATI;  // punto di montaggio ("/dati")
#define DATI_PATH(rel) ("/dati" rel)
#define DATI_CPATH(rel) ("/dati" rel)
#endif

}  // namespace app
