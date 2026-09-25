// Dati dell'applicazione: rubrica, conversazioni, gruppi, impostazioni.
// Tutti i metodi sono protetti da un mutex: li usano sia la rete sia l'interfaccia.
#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "pippanet/phone.h"

namespace app {

enum class ChatKind : uint8_t { DIRECT = 0, GROUP = 1, GLOBAL = 2 };

struct ChatId {
    ChatKind kind = ChatKind::DIRECT;
    uint16_t id = 0;  // numero (DIRECT), id del gruppo (GROUP), 0 (GLOBAL)
    bool operator<(const ChatId& o) const { return kind != o.kind ? kind < o.kind : id < o.id; }
    bool operator==(const ChatId& o) const { return kind == o.kind && id == o.id; }
};

enum class MsgKind : uint8_t { TEXT = 0, VOICE = 1, POSITION = 2, SOS = 3, INFO = 4 };

struct Message {
    uint32_t id = 0;       // id del messaggio (del mittente)
    uint16_t from = 0;     // chi l'ha scritto
    bool out = false;      // mandato da me
    pn::Delivery status = pn::Delivery::PENDING;
    uint32_t time = 0;     // ora Unix (0 = sconosciuta)
    MsgKind kind = MsgKind::TEXT;
    bool readSent = false;  // ricevuto: conferma di lettura gia' mandata
    uint16_t voiceMs = 0;
    uint32_t voiceFile = 0;  // /dati/voce/<n>.c2
    std::string text;
};

struct Chat {
    ChatId id;
    std::vector<Message> msgs;
    uint16_t unread = 0;
};

struct Contact {
    uint16_t number = 0;
    std::string name;
};

struct Settings {
    uint8_t brightness = 70;   // %
    uint8_t volume = 6;        // 0..10
    bool vibration = true;
    uint8_t ringtone = 1;      // 0 = silenzioso, 1 = classica, 2 = breve
    bool keyLight = true;
    std::string pin;           // vuoto = nessun blocco
    uint16_t screenOffS = 30;
    int8_t txPower = 0;        // dBm, 0 = automatica
};

class Dati {
public:
    void load();

    // ---- rubrica ----
    std::vector<Contact> contacts();
    bool contact(uint16_t number, Contact& out);
    void setContact(const Contact& c);
    void removeContact(uint16_t number);
    // Nome da mostrare: nome in rubrica o il numero.
    std::string nameOf(uint16_t number);

    // ---- conversazioni ----
    // Elenco ordinato dalla piu' recente (senza i messaggi, solo l'ultimo).
    struct Summary {
        ChatId id;
        std::string title;
        Message last;
        uint16_t unread;
    };
    std::vector<Summary> summaries(bool includeGlobal = false);
    Chat chat(const ChatId& id);
    // Aggiunge un messaggio; restituisce false se era gia' presente (stesso mittente e id).
    bool addMessage(const ChatId& id, const Message& m, bool countUnread);
    void setStatus(const ChatId& id, uint32_t msgId, pn::Delivery d);
    // Segna come letti; restituisce gli id dei messaggi a cui mandare la conferma di lettura.
    std::vector<uint32_t> markRead(const ChatId& id);
    void deleteChat(const ChatId& id);
    uint16_t unreadTotal();
    // Chat e messaggio a cui appartiene un id mandato da me.
    bool findOutgoing(uint16_t to, uint32_t msgId, ChatId& chat);

    // ---- gruppi ----
    std::vector<pn::GroupInfo> groups();
    bool group(uint16_t id, pn::GroupInfo& out);
    void setGroup(const pn::GroupInfo& g);
    void removeGroup(uint16_t id);

    // ---- impostazioni ----
    Settings settings();
    void setSettings(const Settings& s);

    uint32_t nextVoiceFile();

private:
    void saveContacts();
    void saveGroups();
    void saveChat(const Chat& c);
    Chat& chatRef(const ChatId& id);
    std::string title(const ChatId& id);

    std::recursive_mutex mu_;
    std::vector<Contact> contacts_;
    std::map<ChatId, Chat> chats_;
    std::vector<pn::GroupInfo> groups_;
    Settings settings_;
};

Dati& dati();

std::string chatFile(const ChatId& id);

}  // namespace app
