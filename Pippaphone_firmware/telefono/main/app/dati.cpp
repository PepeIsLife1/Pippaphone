#include "app/dati.h"

#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app/memoria.h"
#include "pippanet/packet.h"

namespace app {

namespace {
constexpr size_t MAX_MSGS = 100;  // per conversazione
constexpr uint8_t CHAT_MAGIC = 0xC1;

void putStr(std::vector<uint8_t>& v, const std::string& s, size_t maxLen) {
    size_t n = std::min(s.size(), maxLen);
    v.push_back(uint8_t(n & 0xFF));
    v.push_back(uint8_t(n >> 8));
    v.insert(v.end(), s.begin(), s.begin() + long(n));
}

std::string getStr(pn::Reader& r) {
    uint16_t n = r.u16();
    if (r.remaining() < n) return "";
    std::string s(reinterpret_cast<const char*>(r.ptr()), n);
    r.skip(n);
    return s;
}

void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x));
    v.push_back(uint8_t(x >> 8));
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; i++) v.push_back(uint8_t(x >> (8 * i)));
}
}  // namespace

Dati& dati() {
    static Dati d;
    return d;
}

std::string chatFile(const ChatId& id) {
#if CONFIG_SCHEDA_TDECK_PLUS
    char buf[64];  // T-Deck: la cartella dei dati puo' essere sulla microSD
    const char k = id.kind == ChatKind::DIRECT ? 'n' : id.kind == ChatKind::GROUP ? 'g' : 'x';
    snprintf(buf, sizeof(buf), "%s/chat/%c%u.bin", DATI, k, unsigned(id.id));
#else
    char buf[40];
    const char k = id.kind == ChatKind::DIRECT ? 'n' : id.kind == ChatKind::GROUP ? 'g' : 'x';
    snprintf(buf, sizeof(buf), "/dati/chat/%c%u.bin", k, unsigned(id.id));
#endif
    return buf;
}

void Dati::load() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    std::vector<uint8_t> data;
    // Rubrica.
    contacts_.clear();
    if (readFile(DATI_PATH("/rubrica.bin"), data)) {
        pn::Reader r(data.data(), data.size());
        while (r.remaining() >= 4) {
            Contact c;
            c.number = r.u16();
            c.name = getStr(r);
            if (!r.ok()) break;
            contacts_.push_back(c);
        }
    }
    // Gruppi.
    groups_.clear();
    if (readFile(DATI_PATH("/gruppi.bin"), data)) {
        pn::Reader r(data.data(), data.size());
        while (r.remaining() > 36) {
            pn::GroupInfo g;
            g.id = r.u16();
            r.get(g.key.data(), 32);
            g.creator = r.u16();
            uint8_t n = r.u8();
            for (uint8_t i = 0; i < n; i++) g.members.push_back(r.u16());
            g.name = getStr(r);
            if (!r.ok()) break;
            groups_.push_back(g);
        }
    }
    // Conversazioni.
    chats_.clear();
    DIR* d = opendir(DATI_CPATH("/chat"));
    if (d) {
        while (struct dirent* e = readdir(d)) {
            const char* n = e->d_name;
            if (strlen(n) < 5 || strstr(n, ".tmp")) continue;
            ChatId id;
            id.kind = n[0] == 'n' ? ChatKind::DIRECT : n[0] == 'g' ? ChatKind::GROUP : ChatKind::GLOBAL;
            id.id = uint16_t(atoi(n + 1));
            if (!readFile(chatFile(id), data) || data.empty() || data[0] != CHAT_MAGIC) continue;
            Chat c;
            c.id = id;
            pn::Reader r(data.data() + 1, data.size() - 1);
            c.unread = r.u16();
            while (r.remaining() > 18) {
                Message m;
                m.id = r.u32();
                m.from = r.u16();
                uint8_t f = r.u8();
                m.out = f & 1;
                m.readSent = f & 2;
                m.status = pn::Delivery(r.u8());
                m.time = r.u32();
                m.kind = MsgKind(r.u8());
                m.voiceMs = r.u16();
                m.voiceFile = r.u32();
                m.text = getStr(r);
                if (!r.ok()) break;
                c.msgs.push_back(m);
            }
            chats_[id] = c;
        }
        closedir(d);
    }
    // Impostazioni.
    settings_.brightness = uint8_t(getInt("luce", 70));
    settings_.volume = uint8_t(getInt("volume", 6));
    settings_.vibration = getInt("vibra", 1) != 0;
    settings_.ringtone = uint8_t(getInt("suoneria", 1));
    settings_.keyLight = getInt("luce_tasti", 1) != 0;
    settings_.pin = app::getStr("pin", "");
    settings_.screenOffS = uint16_t(getInt("spegni_s", 30));
    settings_.txPower = int8_t(getInt("potenza", 0));
}

// ---------------------------------------------------------------- rubrica

std::vector<Contact> Dati::contacts() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    std::vector<Contact> c = contacts_;
    std::sort(c.begin(), c.end(), [](const Contact& a, const Contact& b) { return a.name < b.name; });
    return c;
}

bool Dati::contact(uint16_t number, Contact& out) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    for (const auto& c : contacts_)
        if (c.number == number) {
            out = c;
            return true;
        }
    return false;
}

void Dati::setContact(const Contact& c) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    for (auto& x : contacts_)
        if (x.number == c.number) {
            x = c;
            saveContacts();
            return;
        }
    contacts_.push_back(c);
    saveContacts();
}

void Dati::removeContact(uint16_t number) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    contacts_.erase(std::remove_if(contacts_.begin(), contacts_.end(),
                                   [number](const Contact& c) { return c.number == number; }),
                    contacts_.end());
    saveContacts();
}

std::string Dati::nameOf(uint16_t number) {
    Contact c;
    if (contact(number, c) && !c.name.empty()) return c.name;
    return std::to_string(number);
}

void Dati::saveContacts() {
    std::vector<uint8_t> v;
    for (const auto& c : contacts_) {
        put16(v, c.number);
        putStr(v, c.name, 40);
    }
    writeFile(DATI_PATH("/rubrica.bin"), v);
}

// ---------------------------------------------------------------- conversazioni

std::string Dati::title(const ChatId& id) {
    if (id.kind == ChatKind::GLOBAL) return "Canale globale";
    if (id.kind == ChatKind::GROUP) {
        for (const auto& g : groups_)
            if (g.id == id.id) return g.name;
        return "Gruppo";
    }
    return nameOf(id.id);
}

Chat& Dati::chatRef(const ChatId& id) {
    auto it = chats_.find(id);
    if (it == chats_.end()) {
        Chat c;
        c.id = id;
        it = chats_.emplace(id, c).first;
    }
    return it->second;
}

std::vector<Dati::Summary> Dati::summaries(bool includeGlobal) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    std::vector<Summary> out;
    for (auto& kv : chats_) {
        const Chat& c = kv.second;
        if (c.msgs.empty() || (!includeGlobal && c.id.kind == ChatKind::GLOBAL)) continue;
        out.push_back({c.id, title(c.id), c.msgs.back(), c.unread});
    }
    std::sort(out.begin(), out.end(), [](const Summary& a, const Summary& b) {
        // Prima i non letti, poi i piu' recenti (ora, altrimenti id).
        if ((a.unread > 0) != (b.unread > 0)) return a.unread > 0;
        return a.last.time != b.last.time ? a.last.time > b.last.time : a.last.id > b.last.id;
    });
    return out;
}

Chat Dati::chat(const ChatId& id) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return chatRef(id);
}

bool Dati::addMessage(const ChatId& id, const Message& m, bool countUnread) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    Chat& c = chatRef(id);
    for (const auto& x : c.msgs)
        if (x.from == m.from && x.id == m.id && x.out == m.out) return false;
    c.msgs.push_back(m);
    if (c.msgs.size() > MAX_MSGS) {
        // Toglie i piu' vecchi (e i loro vocali).
        for (size_t i = 0; i < c.msgs.size() - MAX_MSGS; i++)
            if (c.msgs[i].voiceFile) removeFile(DATI_PATH("/voce/") + std::to_string(c.msgs[i].voiceFile) + ".c2");
        c.msgs.erase(c.msgs.begin(), c.msgs.end() - MAX_MSGS);
    }
    if (countUnread) c.unread++;
    saveChat(c);
    return true;
}

void Dati::setStatus(const ChatId& id, uint32_t msgId, pn::Delivery d) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    Chat& c = chatRef(id);
    for (auto& m : c.msgs)
        if (m.out && m.id == msgId) {
            // Lo stato puo' solo migliorare. "Non raggiungibile" e "riprovo piu' tardi" valgono
            // solo se non era ancora consegnato; un nuovo tentativo o una conferma arrivata in
            // ritardo li correggono.
            const bool failing = d == pn::Delivery::UNREACHABLE || d == pn::Delivery::RETRYING;
            const bool failed = m.status == pn::Delivery::UNREACHABLE || m.status == pn::Delivery::RETRYING;
            const bool delivered = m.status == pn::Delivery::DELIVERED || m.status == pn::Delivery::READ;
            const bool better = failing ? !delivered && m.status != d : (failed || d > m.status);
            if (better) {
                m.status = d;
                saveChat(c);
            }
            return;
        }
}

bool Dati::findOutgoing(uint16_t to, uint32_t msgId, ChatId& chat) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    ChatId id{ChatKind::DIRECT, to};
    auto it = chats_.find(id);
    if (it == chats_.end()) return false;
    for (const auto& m : it->second.msgs)
        if (m.out && m.id == msgId) {
            chat = id;
            return true;
        }
    return false;
}

std::vector<uint32_t> Dati::markRead(const ChatId& id) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    Chat& c = chatRef(id);
    std::vector<uint32_t> ids;
    bool changed = c.unread != 0;
    c.unread = 0;
    for (auto& m : c.msgs)
        if (!m.out && !m.readSent && id.kind == ChatKind::DIRECT && m.kind == MsgKind::TEXT) {
            m.readSent = true;
            ids.push_back(m.id);
            changed = true;
        }
    if (changed) saveChat(c);
    return ids;
}

void Dati::deleteChat(const ChatId& id) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    auto it = chats_.find(id);
    if (it == chats_.end()) return;
    for (const auto& m : it->second.msgs)
        if (m.voiceFile) removeFile(DATI_PATH("/voce/") + std::to_string(m.voiceFile) + ".c2");
    chats_.erase(it);
    removeFile(chatFile(id));
}

uint16_t Dati::unreadTotal() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    uint16_t n = 0;
    for (const auto& kv : chats_) n += kv.second.unread;
    return n;
}

void Dati::saveChat(const Chat& c) {
    std::vector<uint8_t> v;
    v.push_back(CHAT_MAGIC);
    put16(v, c.unread);
    for (const auto& m : c.msgs) {
        put32(v, m.id);
        put16(v, m.from);
        v.push_back(uint8_t((m.out ? 1 : 0) | (m.readSent ? 2 : 0)));
        v.push_back(uint8_t(m.status));
        put32(v, m.time);
        v.push_back(uint8_t(m.kind));
        put16(v, m.voiceMs);
        put32(v, m.voiceFile);
        putStr(v, m.text, 400);
    }
    writeFile(chatFile(c.id), v);
}

// ---------------------------------------------------------------- gruppi

std::vector<pn::GroupInfo> Dati::groups() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return groups_;
}

bool Dati::group(uint16_t id, pn::GroupInfo& out) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    for (const auto& g : groups_)
        if (g.id == id) {
            out = g;
            return true;
        }
    return false;
}

void Dati::setGroup(const pn::GroupInfo& g) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    for (auto& x : groups_)
        if (x.id == g.id) {
            x = g;
            saveGroups();
            return;
        }
    groups_.push_back(g);
    saveGroups();
}

void Dati::removeGroup(uint16_t id) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    groups_.erase(std::remove_if(groups_.begin(), groups_.end(), [id](const pn::GroupInfo& g) { return g.id == id; }),
                  groups_.end());
    saveGroups();
}

void Dati::saveGroups() {
    std::vector<uint8_t> v;
    for (const auto& g : groups_) {
        put16(v, g.id);
        v.insert(v.end(), g.key.begin(), g.key.end());
        put16(v, g.creator);
        v.push_back(uint8_t(g.members.size()));
        for (auto a : g.members) put16(v, a);
        putStr(v, g.name, 40);
    }
    writeFile(DATI_PATH("/gruppi.bin"), v);
}

// ---------------------------------------------------------------- impostazioni

Settings Dati::settings() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    return settings_;
}

void Dati::setSettings(const Settings& s) {
    std::lock_guard<std::recursive_mutex> l(mu_);
    settings_ = s;
    setInt("luce", s.brightness);
    setInt("volume", s.volume);
    setInt("vibra", s.vibration);
    setInt("suoneria", s.ringtone);
    setInt("luce_tasti", s.keyLight);
    app::setStr("pin", s.pin);
    setInt("spegni_s", s.screenOffS);
    setInt("potenza", s.txPower);
}

uint32_t Dati::nextVoiceFile() {
    std::lock_guard<std::recursive_mutex> l(mu_);
    int32_t n = getInt("voce_n", 1);
    setInt("voce_n", n + 1);
    return uint32_t(n);
}

}  // namespace app
