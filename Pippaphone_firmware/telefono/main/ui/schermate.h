// Tutte le schermate del telefono (fabbriche).
#pragma once

#include <functional>

#include "app/dati.h"
#include "ui/ui.h"

namespace ui {

// principale e menu (schermate_base.cpp)
Screen* makeMenu();
Screen* makeLock();
bool isLockOpen();

// messaggi (schermate_messaggi.cpp)
Screen* makeChats();
Screen* makeChat(const app::ChatId& id);
bool isChatOpen(const app::ChatId& id);
// Scelta di un contatto (o di un numero digitato); cb riceve il numero scelto.
Screen* makePickContact(const std::string& title, std::function<void(uint16_t)> cb);

// contatti e gruppi (schermate_contatti.cpp)
Screen* makeContacts();
Screen* makeContact(uint16_t number);
Screen* makeEditContact(uint16_t number);  // 0 = nuovo
Screen* makeGroups();
Screen* makeNewGroup();

// walkie-talkie (schermate_ptt.cpp)
// 0 = scegli il contatto; startNow = comincia subito a registrare (PTT gia' premuto).
Screen* makeWalkie(uint16_t number, bool startNow = false);
Screen* makeVoicePlayer(uint16_t from, uint32_t file, bool autoplay);
// Il walkie-talkie aperto con questo contatto riproduce da se' i vocali in arrivo.
bool walkieTakesVoice(uint16_t from);
bool walkieRecording();

// posizione (schermate_posizione.cpp)
Screen* makePosition();
Screen* makePositionRequest(uint16_t from, uint32_t requestId);
Screen* makePositionView(uint16_t from, const std::string& data, uint32_t time);

// SOS (schermate_sos.cpp)
Screen* makeSosActive(bool start);
Screen* makeSosReceived(uint16_t from);
// SOS ricevuto: true se bisogna suonare l'allarme e aprire la schermata (la prima volta, non a
// ogni ripetizione gia' vista).
bool sosShouldAlert(uint16_t from);
void sosCancelled(uint16_t from);

// impostazioni e rete (schermate_impostazioni.cpp)
Screen* makeSettings();
Screen* makeSignal();
Screen* makeOta();
Screen* makeAdmin();

}  // namespace ui
