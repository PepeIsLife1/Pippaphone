// Eventi dalla rete (e dal resto dell'applicazione) verso l'interfaccia grafica.
#pragma once

#include <cstdint>

#include "app/dati.h"

namespace app {

enum class Ev : uint8_t {
    MESSAGE,         // nuovo messaggio in chat
    DELIVERY,        // stato di un messaggio inviato cambiato
    POS_REQUEST,     // qualcuno chiede la mia posizione (from, id = richiesta)
    POSITION,        // arrivata una posizione (from; chat contiene il messaggio)
    SOS,             // SOS ricevuto o annullato (from, a = 1 attivo / 0 annullato)
    SOS_ACK,         // un contatto ha ricevuto il mio SOS (from)
    GROUP_INVITE,    // aggiunto a un gruppo (id = gruppo)
    VOICE_INCOMING,  // sta arrivando un vocale (from)
    VOICE_READY,     // vocale arrivato (from, id = file)
    VOICE_STATE,     // stato del mio vocale (id, a = pn::VoiceSendState)
    PROV_OFFER,      // l'amministratore mi offre un numero
    PROV_HELLO,      // (amministratore) un telefono nuovo chiede un numero
    TIME,            // ora sincronizzata
    BATTERY,         // batteria: a = percentuale; id = 0 quasi scarica, 1 scarica (si spegne)
};

struct Event {
    Ev type;
    ChatId chat;
    uint16_t from = 0;
    uint32_t id = 0;
    int32_t a = 0;
};

void eventsInit();
void post(const Event& e);
bool poll(Event& e);

}  // namespace app
