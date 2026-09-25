#include "app/rete.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/eventi.h"
#include "app/memoria.h"
#include "esp_log.h"
#include "scheda/pins.h"
#include "scheda/scheda.h"

namespace app {

namespace {
const char* TAG = "rete";
}

Rete& rete() {
    static Rete r;
    return r;
}

void Rete::lock() { xSemaphoreTakeRecursive(lock_, portMAX_DELAY); }
void Rete::unlock() { xSemaphoreGiveRecursive(lock_); }
uint32_t Rete::now32() { return phone_->unixTime(pippa::RadioLink::nowMs()); }

bool Rete::begin() {
    lock_ = xSemaphoreCreateRecursiveMutex();
    pn::Identity id;
    loadIdentity(id);
    phone_ = new pn::Phone(id, *this);
    phone_->setSeq(loadSeq());
    phone_->onSeqReserve = [](uint32_t next) { saveSeq(next); };

    pn::Cert own;
    pn::Key32 adminPub;
    if (loadCredentials(own, adminPub)) {
        phone_->setCredentials(own, adminPub);
        pn::Identity admin;
        if (loadAdminIdentity(admin)) phone_->setAdminIdentity(admin);
        for (const auto& c : loadCerts()) phone_->addKnownCert(c);
        for (const auto& g : dati().groups()) phone_->addGroup(g);
        ESP_LOGI(TAG, "numero %u%s", own.number, phone_->isAdmin() ? " (amministratore)" : "");
    } else {
        ESP_LOGI(TAG, "telefono senza numero: codice %s", pn::fingerprint(id.edPublic).c_str());
    }

    pippa::RadioConfig cfg;
    cfg.sck = scheda::PIN_SPI_SCK;
    cfg.mosi = scheda::PIN_SPI_MOSI;
    cfg.miso = scheda::PIN_SPI_MISO;
    cfg.nss = scheda::PIN_LORA_NSS;
    cfg.busy = scheda::PIN_LORA_BUSY;
    cfg.dio1 = scheda::PIN_LORA_DIO1;
    cfg.resetFn = [](bool level) { scheda::expander().set(scheda::Exp::LORA_RST_N, level); };
    cfg.dio2AsRfSwitch = true;
    cfg.tcxoVoltage = 1.8f;
    cfg.maxPowerDbm = 22;
    if (!radio_.begin(cfg, phone_, lock_)) return false;
    setPower(dati().settings().txPower);
    radio_.start(0, 10);
    return true;
}

// ---------------------------------------------------------------- identita'

bool Rete::hasNumber() {
    lock();
    bool r = phone_->hasNumber();
    unlock();
    return r;
}

uint16_t Rete::number() {
    lock();
    uint16_t r = phone_->address();
    unlock();
    return r;
}

bool Rete::isAdmin() {
    lock();
    bool r = phone_->isAdmin();
    unlock();
    return r;
}

std::string Rete::myCode() { return pn::fingerprint(phone_->identity().edPublic); }

std::string Rete::adminCode() {
    lock();
    std::string s = phone_->hasNumber() ? pn::fingerprint(phone_->adminPublic()) : "";
    unlock();
    return s;
}

std::string Rete::codeOf(uint16_t number) {
    lock();
    const pn::Cert* c = phone_->certOf(number);
    std::string s = c ? pn::fingerprint(c->edPublic) : "";
    unlock();
    return s;
}

// ---------------------------------------------------------------- azioni

uint32_t Rete::sendText(const ChatId& chat, const std::string& text) {
    lock();
    const pn::Millis now = pippa::RadioLink::nowMs();
    uint32_t id = 0;
    switch (chat.kind) {
        case ChatKind::DIRECT: id = phone_->sendText(chat.id, text, now); break;
        case ChatKind::GROUP: id = phone_->sendGroupText(chat.id, text, now); break;
        case ChatKind::GLOBAL: id = phone_->sendGlobalText(text, now); break;
    }
    const uint16_t me = phone_->address();
    const uint32_t t = now32();
    unlock();
    if (!id) return 0;
    Message m;
    m.id = id;
    m.from = me;
    m.out = true;
    m.time = t;
    m.text = text;
    // Gruppi e canale globale non hanno conferme: "inviato" appena parte.
    m.status = chat.kind == ChatKind::DIRECT ? pn::Delivery::PENDING : pn::Delivery::SENT;
    dati().addMessage(chat, m, false);
    radio_.wake();
    return id;
}

void Rete::markRead(const ChatId& chat) {
    std::vector<uint32_t> ids = dati().markRead(chat);
    if (ids.empty() || chat.kind != ChatKind::DIRECT) return;
    lock();
    // Basta confermare l'ultimo: chi riceve segna come letti anche i precedenti.
    phone_->sendRead(chat.id, ids.back(), pippa::RadioLink::nowMs());
    unlock();
    radio_.wake();
}

uint32_t Rete::requestPosition(uint16_t to) {
    lock();
    uint32_t id = phone_->requestPosition(to, pippa::RadioLink::nowMs());
    const uint32_t t = now32();
    const uint16_t me = phone_->address();
    unlock();
    if (id) {
        Message m;
        m.id = id;
        m.from = me;
        m.out = true;
        m.time = t;
        m.kind = MsgKind::INFO;
        m.text = "Posizione chiesta";
        dati().addMessage({ChatKind::DIRECT, to}, m, false);
    }
    radio_.wake();
    return id;
}

void Rete::answerPosition(uint16_t to, uint32_t requestId, bool accept) {
    lock();
    pn::Position p = phone_->position;
    pn::PosStatus st = !accept ? pn::PosStatus::DENIED : (p.valid ? pn::PosStatus::OK : pn::PosStatus::NO_FIX);
    phone_->answerPosition(to, requestId, st, p, pippa::RadioLink::nowMs());
    unlock();
    radio_.wake();
}

void Rete::startSos() {
    std::vector<uint16_t> contacts;
    for (const auto& c : dati().contacts()) contacts.push_back(c.number);
    lock();
    sosContacts_ = contacts;
    sosAcked_.clear();
    phone_->startSos(contacts, pippa::RadioLink::nowMs());
    unlock();
    radio_.wake();
}

void Rete::stopSos() {
    lock();
    phone_->stopSos(pippa::RadioLink::nowMs());
    unlock();
    radio_.wake();
}

bool Rete::sosActive() {
    lock();
    bool r = phone_->sosActive();
    unlock();
    return r;
}

std::vector<uint16_t> Rete::sosContacts() {
    lock();
    auto r = sosContacts_;
    unlock();
    return r;
}

std::set<uint16_t> Rete::sosAcked() {
    lock();
    auto r = sosAcked_;
    unlock();
    return r;
}

pn::GroupInfo Rete::createGroup(const std::string& name, const std::vector<uint16_t>& members) {
    lock();
    std::vector<pn::Addr> m(members.begin(), members.end());
    pn::GroupInfo g = phone_->createGroup(name, m, pippa::RadioLink::nowMs());
    unlock();
    dati().setGroup(g);
    Message info;
    info.id = 1;
    info.from = g.creator;
    info.out = true;
    info.status = pn::Delivery::SENT;
    info.kind = MsgKind::INFO;
    info.time = now32();
    info.text = "Gruppo creato";
    dati().addMessage({ChatKind::GROUP, g.id}, info, false);
    radio_.wake();
    return g;
}

uint32_t Rete::sendVoice(uint16_t to, const std::vector<uint8_t>& codec2, uint16_t durationMs) {
    lock();
    uint32_t id = phone_->sendVoice(to, pn::Codec::CODEC2_700C, codec2, durationMs, pippa::RadioLink::nowMs());
    const uint16_t me = phone_->address();
    const uint32_t t = now32();
    unlock();
    if (!id) return 0;
    // Lo si conserva anche tra i propri messaggi, per riascoltarlo.
    const uint32_t file = dati().nextVoiceFile();
    std::vector<uint8_t> f;
    f.push_back(uint8_t(pn::Codec::CODEC2_700C));
    f.push_back(uint8_t(durationMs));
    f.push_back(uint8_t(durationMs >> 8));
    for (int i = 0; i < 4; i++) f.push_back(0);
    f.insert(f.end(), codec2.begin(), codec2.end());
    writeFile("/dati/voce/" + std::to_string(file) + ".c2", f);
    Message m;
    m.id = id;
    m.from = me;
    m.out = true;
    m.time = t;
    m.kind = MsgKind::VOICE;
    m.voiceMs = durationMs;
    m.voiceFile = file;
    m.status = pn::Delivery::PENDING;
    dati().addMessage({ChatKind::DIRECT, to}, m, false);
    voiceOut_ = {id, to};
    radio_.wake();
    return id;
}

// ---------------------------------------------------------------- numero e amministrazione

void Rete::setProvisioning(bool on) {
    lock();
    phone_->setProvisioning(on);
    unlock();
    radio_.wake();
}

bool Rete::offer(pn::Cert& cert, std::string& code) {
    lock();
    bool r = haveOffer_;
    if (r) {
        cert = offerCert_;
        code = pn::fingerprint(offerAdmin_);
    }
    unlock();
    return r;
}

void Rete::acceptOffer() {
    lock();
    if (haveOffer_) {
        phone_->setCredentials(offerCert_, offerAdmin_);
        saveCredentials(offerCert_, offerAdmin_);
        haveOffer_ = false;
    }
    unlock();
    radio_.wake();
}

void Rete::rejectOffer() {
    lock();
    haveOffer_ = false;
    unlock();
}

bool Rete::createNetwork(uint16_t number) {
    if (!pn::isPhoneAddr(number)) return false;
    pn::Identity admin = pn::Identity::generate();
    saveAdminIdentity(admin);
    lock();
    pn::Cert own;
    own.number = number;
    own.edPublic = phone_->identity().edPublic;
    own.xPublic = phone_->identity().xPublic;
    own.issued = now32();
    own.signWith(admin);
    phone_->setAdminIdentity(admin);
    phone_->setCredentials(own, admin.edPublic);
    saveCredentials(own, admin.edPublic);
    unlock();
    return true;
}

std::vector<Rete::Hello> Rete::hellos() {
    lock();
    auto r = hellos_;
    unlock();
    return r;
}

bool Rete::assign(uint64_t shortId, uint16_t number) {
    lock();
    bool ok = false;
    for (const auto& h : hellos_)
        if (h.shortId == shortId) ok = phone_->provision(h.hello, number, pippa::RadioLink::nowMs());
    unlock();
    radio_.wake();
    return ok;
}

// ---------------------------------------------------------------- stato

Rete::Signal Rete::signal() {
    lock();
    const auto& h = phone_->lastRepeater();
    const pn::Millis now = pippa::RadioLink::nowMs();
    Signal s;
    s.heard = h.addr != pn::ADDR_NONE && now - h.at < 15 * 60 * 1000ULL;
    s.repeater = h.addr;
    s.rssi = h.rssi;
    s.snr = h.snr;
    s.agoS = uint32_t((now - h.at) / 1000);
    unlock();
    if (!s.heard) s.bars = 0;
    else if (s.snr >= 5) s.bars = 4;
    else if (s.snr >= 0) s.bars = 3;
    else if (s.snr >= -7) s.bars = 2;
    else s.bars = 1;
    return s;
}

std::vector<Rete::RepeaterInfo> Rete::repeaters() {
    lock();
    std::vector<RepeaterInfo> out;
    const pn::Millis now = pippa::RadioLink::nowMs();
    for (const auto& kv : phone_->repeatersHeard())
        out.push_back({kv.first, kv.second.rssi, kv.second.snr, uint32_t((now - kv.second.at) / 1000)});
    unlock();
    std::sort(out.begin(), out.end(), [](const RepeaterInfo& a, const RepeaterInfo& b) { return a.snr > b.snr; });
    return out;
}

uint32_t Rete::unixTime() {
    lock();
    uint32_t t = now32();
    unlock();
    return t;
}

bool Rete::timeValid() {
    lock();
    bool r = phone_->timeValid();
    unlock();
    return r;
}

uint64_t Rete::airtimeMs() {
    lock();
    uint64_t r = phone_->txAirtimeMs;
    unlock();
    return r;
}

bool Rete::radioOk() { return radio_.ok(); }

void Rete::setPower(int8_t dbm) {
    powerSetting_ = dbm;
    radio_.setPower(dbm > 0 ? dbm : 22);
}

int8_t Rete::power() { return radio_.power(); }

void Rete::wantGnss(uint32_t seconds) {
    const uint32_t until = uint32_t(pippa::RadioLink::nowMs()) + seconds * 1000;
    lock();
    if (int32_t(until - gnssUntilMs_) > 0) gnssUntilMs_ = until;
    unlock();
}

bool Rete::gnssWanted() {
    lock();
    bool r = sosActive() || int32_t(gnssUntilMs_ - uint32_t(pippa::RadioLink::nowMs())) > 0;
    unlock();
    return r;
}

void Rete::service() {
    if (powerSetting_ > 0) return;
    // Potenza automatica: i telefoni parlano solo con i ripetitori, quindi basta il margine
    // sull'ultimo ripetitore sentito. Dopo un messaggio non consegnato si torna al massimo
    // per 10 minuti.
    const Signal s = signal();
    const uint32_t now = uint32_t(pippa::RadioLink::nowMs());
    int8_t p = 22;
    const bool recentFail = lastFailMs_ && now - lastFailMs_ < 10 * 60 * 1000;
    if (s.heard && s.agoS < 600 && !recentFail) {
        if (s.snr >= 10) p = 10;
        else if (s.snr >= 7) p = 14;
        else if (s.snr >= 4) p = 17;
    }
    if (p != radio_.power()) {
        ESP_LOGI(TAG, "potenza automatica: %d dBm (SNR %.1f)", p, double(s.snr));
        radio_.setPower(p);
    }
}

void Rete::updateGnss(const scheda::Fix& fix, float accuracyM, uint8_t battery) {
    const pn::Millis now = pippa::RadioLink::nowMs();
    lock();
    phone_->battery = battery;
    if (fix.valid) {
        pn::Position& p = phone_->position;
        p.valid = true;
        p.latE7 = int32_t(std::lround(fix.lat * 1e7));
        p.lonE7 = int32_t(std::lround(fix.lon * 1e7));
        p.altM = int16_t(std::lround(fix.altM));
        p.accM = uint8_t(std::min(255.0f, accuracyM));
        p.ageS = uint16_t(std::min<uint64_t>(65535, (now - fix.atMs) / 1000));
    } else if (phone_->position.valid) {
        // Si tiene l'ultima posizione buona, ma se ne aggiorna l'eta'.
        phone_->position.ageS = uint16_t(std::min<uint32_t>(65535, phone_->position.ageS + 5));
    }
    const bool wasValid = phone_->timeValid();
    if (fix.timeValid && now - fix.timeAtMs < 5000)
        phone_->setUnixTime(fix.unixTime + uint32_t((now - fix.timeAtMs) / 1000), now, true);
    unlock();
    if (!wasValid && fix.timeValid) post({Ev::TIME, {}, 0, 0, 0});
}

pn::Position Rete::lastPosition() {
    lock();
    pn::Position p = phone_->position;
    unlock();
    return p;
}

// ---------------------------------------------------------------- eventi

void Rete::onText(pn::Addr from, uint32_t msgId, const pn::TextPayload& t) {
    Message m;
    m.id = msgId;
    m.from = from;
    m.time = t.sentAt ? t.sentAt : now32();
    m.text = t.text;
    ChatId c{ChatKind::DIRECT, from};
    if (dati().addMessage(c, m, true)) post({Ev::MESSAGE, c, from, msgId, 0});
}

void Rete::onGroupText(uint16_t group, pn::Addr from, uint32_t msgId, const pn::TextPayload& t) {
    Message m;
    m.id = msgId;
    m.from = from;
    m.time = t.sentAt ? t.sentAt : now32();
    m.text = t.text;
    ChatId c{ChatKind::GROUP, group};
    if (dati().addMessage(c, m, true)) post({Ev::MESSAGE, c, from, msgId, 0});
}

void Rete::onGlobalText(pn::Addr from, uint32_t msgId, const pn::TextPayload& t) {
    Message m;
    m.id = msgId;
    m.from = from;
    m.time = t.sentAt ? t.sentAt : now32();
    m.text = t.text;
    ChatId c{ChatKind::GLOBAL, 0};
    // Il canale globale non conta come "non letto" in cima alla lista.
    if (dati().addMessage(c, m, true)) post({Ev::MESSAGE, c, from, msgId, 0});
}

void Rete::onDelivery(pn::Addr to, uint32_t msgId, pn::Delivery d) {
    ChatId c;
    if (!dati().findOutgoing(to, msgId, c)) return;
    if (d == pn::Delivery::READ) {
        // Letto: vale anche per tutti i precedenti gia' consegnati.
        Chat ch = dati().chat(c);
        for (const auto& m : ch.msgs)
            if (m.out && m.id <= msgId && m.status == pn::Delivery::DELIVERED) dati().setStatus(c, m.id, d);
    }
    dati().setStatus(c, msgId, d);
    if (d == pn::Delivery::UNREACHABLE) lastFailMs_ = uint32_t(pippa::RadioLink::nowMs());
    post({Ev::DELIVERY, c, to, msgId, int32_t(d)});
}

void Rete::onPositionRequest(pn::Addr from, uint32_t requestId) {
    wantGnss(180);
    post({Ev::POS_REQUEST, {ChatKind::DIRECT, from}, from, requestId, 0});
}

void Rete::onPosition(pn::Addr from, const pn::PosResponse& p) {
    Message m;
    m.id = p.requestSeq ^ 0x80000000u;
    m.from = from;
    m.time = now32();
    m.kind = MsgKind::POSITION;
    char buf[80];
    if (p.status == pn::PosStatus::OK)
        snprintf(buf, sizeof(buf), "%ld,%ld,%d,%u,%u", long(p.pos.latE7), long(p.pos.lonE7), p.pos.altM, p.pos.accM,
                 p.pos.ageS);
    else
        snprintf(buf, sizeof(buf), "%s", p.status == pn::PosStatus::DENIED ? "rifiutata" : "senza GPS");
    m.text = buf;
    ChatId c{ChatKind::DIRECT, from};
    dati().addMessage(c, m, false);
    post({Ev::POSITION, c, from, m.id, int32_t(p.status)});
}

void Rete::onSos(pn::Addr from, const pn::SosPayload& s) {
    Message m;
    m.id = 0x40000000u | (uint32_t(s.round) << 1) | (s.active ? 1 : 0);
    m.from = from;
    m.time = now32();
    m.kind = MsgKind::SOS;
    char buf[80];
    if (s.pos.valid)
        snprintf(buf, sizeof(buf), "%ld,%ld,%d,%u,%u,%u", long(s.pos.latE7), long(s.pos.lonE7), s.pos.altM,
                 s.pos.accM, s.pos.ageS, s.battery);
    else
        snprintf(buf, sizeof(buf), "-,%u", s.battery);
    m.text = s.active ? buf : "SOS annullato";
    ChatId c{ChatKind::DIRECT, from};
    dati().addMessage(c, m, s.active);
    post({Ev::SOS, c, from, s.round, s.active ? 1 : 0});
}

void Rete::onSosAck(pn::Addr from) {
    sosAcked_.insert(from);
    post({Ev::SOS_ACK, {}, from, 0, 0});
}

void Rete::onGroupInvite(const pn::GroupInfo& g, pn::Addr from) {
    dati().setGroup(g);
    Message m;
    m.id = 1;
    m.from = from;
    m.time = now32();
    m.kind = MsgKind::INFO;
    m.text = dati().nameOf(from) + " ti ha aggiunto al gruppo";
    ChatId c{ChatKind::GROUP, g.id};
    dati().addMessage(c, m, true);
    post({Ev::GROUP_INVITE, c, from, g.id, 0});
}

void Rete::onVoiceIncoming(pn::Addr from, const pn::VoiceReq& r) {
    post({Ev::VOICE_INCOMING, {ChatKind::DIRECT, from}, from, 0, r.durationMs});
}

void Rete::onVoice(pn::Addr from, pn::Codec codec, const std::vector<uint8_t>& data, uint16_t durationMs,
                   uint32_t missing) {
    const uint32_t file = dati().nextVoiceFile();
    std::vector<uint8_t> f;
    f.push_back(uint8_t(codec));
    f.push_back(uint8_t(durationMs));
    f.push_back(uint8_t(durationMs >> 8));
    for (int i = 0; i < 4; i++) f.push_back(uint8_t(missing >> (8 * i)));
    f.insert(f.end(), data.begin(), data.end());
    writeFile("/dati/voce/" + std::to_string(file) + ".c2", f);
    Message m;
    m.id = 0x20000000u | file;
    m.from = from;
    m.time = now32();
    m.kind = MsgKind::VOICE;
    m.voiceMs = durationMs;
    m.voiceFile = file;
    ChatId c{ChatKind::DIRECT, from};
    dati().addMessage(c, m, true);
    post({Ev::VOICE_READY, c, from, file, durationMs});
}

void Rete::onVoiceState(pn::Addr to, uint32_t id, pn::VoiceSendState s) {
    ChatId c{ChatKind::DIRECT, to};
    pn::Delivery d = pn::Delivery::PENDING;
    switch (s) {
        case pn::VoiceSendState::SENDING: d = pn::Delivery::SENT; break;
        case pn::VoiceSendState::DELIVERED: d = pn::Delivery::DELIVERED; break;
        case pn::VoiceSendState::PARTIAL: d = pn::Delivery::DELIVERED; break;
        case pn::VoiceSendState::BUSY:
        case pn::VoiceSendState::UNREACHABLE: d = pn::Delivery::UNREACHABLE; break;
        default: break;
    }
    if (d != pn::Delivery::PENDING) dati().setStatus(c, id, d);
    post({Ev::VOICE_STATE, c, to, id, int32_t(s)});
}

void Rete::onCertLearned(const pn::Cert& c) { saveCert(c); }

void Rete::onProvisionOffer(const pn::Cert& c, const pn::Key32& adminPublic) {
    offerCert_ = c;
    offerAdmin_ = adminPublic;
    haveOffer_ = true;
    post({Ev::PROV_OFFER, {}, c.number, 0, 0});
}

void Rete::onProvisionHello(uint64_t shortId, const pn::ProvHello& h, const pn::RxInfo& info) {
    for (auto& x : hellos_)
        if (x.shortId == shortId) {
            x.rssi = info.rssi;
            return;
        }
    hellos_.push_back({shortId, pn::fingerprint(h.edPublic), info.rssi, h});
    post({Ev::PROV_HELLO, {}, 0, 0, 0});
}

void Rete::onTimeSync(uint32_t) { post({Ev::TIME, {}, 0, 0, 0}); }

// ---------------------------------------------------------------- utilita'

std::string formatLatLon(int32_t latE7, int32_t lonE7) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.4f %c · %.4f %c", std::fabs(latE7 / 1e7), latE7 >= 0 ? 'N' : 'S',
             std::fabs(lonE7 / 1e7), lonE7 >= 0 ? 'E' : 'O');
    std::string s = buf;
    for (auto& ch : s)
        if (ch == '.') ch = ',';
    return s;
}

void distanceBearing(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2, double& meters, double& bearing) {
    const double R = 6371000.0, d2r = M_PI / 180.0;
    const double p1 = lat1 / 1e7 * d2r, p2 = lat2 / 1e7 * d2r;
    const double dp = p2 - p1, dl = (lon2 - lon1) / 1e7 * d2r;
    const double a = std::sin(dp / 2) * std::sin(dp / 2) + std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    meters = 2 * R * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    const double y = std::sin(dl) * std::cos(p2);
    const double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
    bearing = std::fmod(std::atan2(y, x) / d2r + 360.0, 360.0);
}

}  // namespace app
