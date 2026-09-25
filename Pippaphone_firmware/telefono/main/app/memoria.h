// Memoria del telefono.
//  - NVS: identita' (semi delle chiavi), certificato, chiave dell'amministratore, contatore
//    dei pacchetti, impostazioni;
//  - partizione "dati" (FAT): rubrica, certificati degli altri, gruppi, conversazioni, vocali.
#pragma once

#include <string>
#include <vector>

#include "pippanet/crypto.h"
#include "pippanet/phone.h"

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
extern const char* const DATI;  // punto di montaggio ("/dati")

}  // namespace app
