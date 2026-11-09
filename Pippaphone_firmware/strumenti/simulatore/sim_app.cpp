// Simulatore: rete PippaNet, vocali, aggiornamento e memoria finti. I dati (rubrica, chat,
// gruppi, impostazioni) sono quelli veri di app/dati.cpp, salvati in una cartella temporanea.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>

#include "app/aggiorna.h"
#include "app/dati.h"
#include "app/eventi.h"
#include "app/memoria.h"
#include "app/rete.h"
#include "app/voce.h"
#include "sim.h"

namespace pn {
void randomBytes(uint8_t* buf, size_t len) {
    static uint32_t x = 12345;
    for (size_t i = 0; i < len; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = uint8_t(x >> 16);
    }
}
}  // namespace pn

namespace app {

// ---------------------------------------------------------------- memoria

#if CONFIG_SCHEDA_TDECK_PLUS
const char* DATI = "/dati";
const char* const MAPPE = "sim_sd/mappe";  // tile finti creati dal copione del simulatore
std::string datiPath(const char* rel) { return std::string(DATI) + rel; }
bool sdPresente() { return true; }
void writeDeviceInfo(const std::string&) {}
#else
const char* const DATI = "/dati";
#endif

namespace {
std::string g_dir = "sim_dati";
std::map<std::string, int32_t> g_ints;
std::map<std::string, std::string> g_strs;

std::string real(const std::string& path) {
    if (path.rfind("/dati", 0) == 0) return g_dir + path.substr(5);
    return path;
}
}  // namespace

bool memoriaInit() {
    std::filesystem::remove_all(g_dir);
    std::filesystem::create_directories(g_dir + "/chat");
    std::filesystem::create_directories(g_dir + "/voce");
    return true;
}
bool loadIdentity(pn::Identity&) { return true; }
bool loadCredentials(pn::Cert&, pn::Key32&) { return true; }
void saveCredentials(const pn::Cert&, const pn::Key32&) {}
bool loadAdminIdentity(pn::Identity&) { return false; }
void saveAdminIdentity(const pn::Identity&) {}
uint32_t loadSeq() { return 1; }
void saveSeq(uint32_t) {}
std::string getStr(const char* key, const std::string& def) {
    auto it = g_strs.find(key);
    return it == g_strs.end() ? def : it->second;
}
void setStr(const char* key, const std::string& v) { g_strs[key] = v; }
int32_t getInt(const char* key, int32_t def) {
    auto it = g_ints.find(key);
    return it == g_ints.end() ? def : it->second;
}
void setInt(const char* key, int32_t v) { g_ints[key] = v; }
void eraseAll() {}
std::vector<pn::Cert> loadCerts() { return {}; }
void saveCert(const pn::Cert&) {}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(real(path).c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool ok = n <= 0 || fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = fopen(real(path).c_str(), "wb");
    if (!f) return false;
    bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

void removeFile(const std::string& path) { remove(real(path).c_str()); }

// ---------------------------------------------------------------- aggiornamento

void otaStart(uint16_t) {}
void otaStop() {}
OtaStatus otaStatus() {
    OtaStatus s;
    s.active = true;
    s.ssid = "Pippaphone-100";
    s.password = "74318265";
    s.received = 1200 * 1024;
    s.total = 2150 * 1024;
    s.version = "0.1.0";
    return s;
}
void otaConfirm() {}
const char* firmwareVersion() { return "0.1.0"; }

// ---------------------------------------------------------------- vocali

// Nel browser (SIM_WEB) i vocali, i messaggi inviati, la posizione, l'SOS e i gruppi creati sono in
// web/rete_web.cpp: durano davvero e la rete finta risponde.
#ifndef SIM_WEB
bool Voce::begin() { return true; }
bool Voce::startRecording() {
    sim::state().recording = true;
    return true;
}
void Voce::stopRecording() { sim::state().recording = false; }
bool Voce::recording() const { return sim::state().recording; }
uint32_t Voce::recordedMs() const { return sim::state().recordedMs; }
std::vector<uint8_t> Voce::takeRecording(uint16_t& durationMs) {
    durationMs = uint16_t(sim::state().recordedMs);
    return std::vector<uint8_t>(64, 0);
}
void Voce::play(uint32_t file, std::function<void()>) {
    sim::state().playing = true;
    sim::state().playingFile = file;
}
void Voce::stop() { sim::state().playing = false; }
bool Voce::playing() const { return sim::state().playing; }
uint32_t Voce::playedMs() const { return sim::state().playedMs; }
uint32_t Voce::playingFile() const { return sim::state().playingFile; }
#endif
Voce& voce() {
    static Voce v;
    return v;
}

// ---------------------------------------------------------------- rete

Rete& rete() {
    static Rete r;
    return r;
}

bool Rete::begin() { return true; }
bool Rete::hasNumber() { return sim::state().hasNumber; }
uint16_t Rete::number() { return sim::state().hasNumber ? sim::state().number : 0; }
bool Rete::isAdmin() { return sim::state().admin; }
std::string Rete::myCode() { return "4821 7730 1964 5528"; }
std::string Rete::adminCode() { return "4821 7730 1964 5528"; }
std::string Rete::codeOf(uint16_t number) { return number == 205 ? "" : "3307 1185 9042 6613"; }

#ifndef SIM_WEB
uint32_t Rete::sendText(const ChatId& chat, const std::string& text) {
    Message m;
    static uint32_t id = 900;
    m.id = ++id;
    m.from = number();
    m.out = true;
    m.status = pn::Delivery::SENT;
    m.time = sim::unixNow();
    m.text = text;
    dati().addMessage(chat, m, false);
    return m.id;
}
uint32_t Rete::requestPosition(uint16_t) { return 1; }
void Rete::startSos() { sim::state().sos = true; }
#endif
void Rete::markRead(const ChatId& chat) { dati().markRead(chat); }
void Rete::answerPosition(uint16_t, uint32_t, bool) {}
void Rete::stopSos() { sim::state().sos = false; }
bool Rete::sosActive() { return sim::state().sos; }
std::vector<uint16_t> Rete::sosContacts() { return {101, 102, 205, 318}; }
std::set<uint16_t> Rete::sosAcked() { return sim::state().sosAcked; }
#ifndef SIM_WEB
pn::GroupInfo Rete::createGroup(const std::string& name, const std::vector<uint16_t>& members) {
    pn::GroupInfo g;
    g.id = 9;
    g.name = name;
    g.members = members;
    g.members.push_back(number());
    dati().setGroup(g);
    return g;
}
uint32_t Rete::sendVoice(uint16_t, const std::vector<uint8_t>&, uint16_t) { return 77; }
#endif

void Rete::setProvisioning(bool) {}
bool Rete::offer(pn::Cert& cert, std::string& adminCode) {
    if (!sim::state().offer) return false;
    cert.number = 101;
    adminCode = "4821 7730 1964 5528";
    return true;
}
void Rete::acceptOffer() {}
void Rete::rejectOffer() {}
bool Rete::createNetwork(uint16_t) { return true; }
std::vector<Rete::Hello> Rete::hellos() {
    if (!sim::state().hello) return {};
    return {Hello{0x1234, "6620 4417 0938 2251", -61, {}}};
}
bool Rete::assign(uint64_t, uint16_t) { return true; }

Rete::Signal Rete::signal() {
    Signal s;
    if (!sim::state().heard) return s;
    s.heard = true;
    s.repeater = pn::ADDR_REPEATER_FIRST + 3;
    s.rssi = -97;
    s.snr = 6.5f;
    s.agoS = 12;
    s.bars = 3;
    return s;
}
std::vector<Rete::RepeaterInfo> Rete::repeaters() {
    if (!sim::state().heard) return {};
    return {{uint16_t(pn::ADDR_REPEATER_FIRST + 3), -97, 6.5f, 12}, {uint16_t(pn::ADDR_REPEATER_FIRST + 7), -112, -3.0f, 95}};
}
uint32_t Rete::unixTime() { return sim::unixNow(); }
bool Rete::timeValid() { return true; }
uint64_t Rete::airtimeMs() { return 184300; }
bool Rete::radioOk() { return true; }
void Rete::setPower(int8_t) {}
int8_t Rete::power() { return 14; }
void Rete::wantGnss(uint32_t) {}
bool Rete::gnssWanted() { return false; }
void Rete::service() {}
void Rete::updateGnss(const scheda::Fix&, float, uint8_t) {}
pn::Position Rete::lastPosition() {
    pn::Position p;
    if (!sim::state().gpsFix) return p;
    p.valid = true;
    p.latE7 = 454642000;
    p.lonE7 = 91900000;
    p.altM = 122;
    p.accM = 8;
    p.ageS = 20;
    return p;
}

void Rete::onText(pn::Addr, uint32_t, const pn::TextPayload&) {}
void Rete::onGroupText(uint16_t, pn::Addr, uint32_t, const pn::TextPayload&) {}
void Rete::onGlobalText(pn::Addr, uint32_t, const pn::TextPayload&) {}
void Rete::onDelivery(pn::Addr, uint32_t, pn::Delivery) {}
void Rete::onPositionRequest(pn::Addr, uint32_t) {}
void Rete::onPosition(pn::Addr, const pn::PosResponse&) {}
void Rete::onSos(pn::Addr, const pn::SosPayload&) {}
void Rete::onSosAck(pn::Addr) {}
void Rete::onGroupInvite(const pn::GroupInfo&, pn::Addr) {}
void Rete::onVoiceIncoming(pn::Addr, const pn::VoiceReq&) {}
void Rete::onVoice(pn::Addr, pn::Codec, const std::vector<uint8_t>&, uint16_t, uint32_t) {}
void Rete::onVoiceState(pn::Addr, uint32_t, pn::VoiceSendState) {}
void Rete::onCallState(pn::Addr, pn::CallState, pn::CallResult) {}
void Rete::onCallVoice(pn::Addr, uint8_t, uint8_t, const uint8_t*, size_t, uint8_t, const uint8_t*, size_t, uint8_t, bool) {}
void Rete::onCertLearned(const pn::Cert&) {}
void Rete::onProvisionOffer(const pn::Cert&, const pn::Key32&) {}
void Rete::onProvisionHello(uint64_t, const pn::ProvHello&, const pn::RxInfo&) {}
void Rete::onTimeSync(uint32_t) {}

// ---------------------------------------------------------------- chiamata (finta)

bool Rete::call(uint16_t to) {
    auto& s = sim::state();
    if (s.call) return false;
    s.call = uint8_t(pn::CallState::DIALING);
    s.callPeer = to;
    s.callOut = true;
    s.callTalking = false;
    s.callT0 = sim::nowMs();
    return true;
}

void Rete::answerCall() {
    auto& s = sim::state();
    if (s.call != uint8_t(pn::CallState::INCOMING)) return;
    s.call = uint8_t(pn::CallState::ACTIVE);
    s.callT0 = sim::nowMs() + 800;  // la strada si apre in meno di un secondo
}

void Rete::endCall() {
    auto& s = sim::state();
    if (!s.call) return;
    const uint16_t peer = s.callPeer;
    s.call = 0;
    s.callTalking = false;
    post({Ev::CALL, {ChatKind::DIRECT, peer}, peer, uint32_t(pn::CallResult::ENDED), int32_t(pn::CallState::IDLE)});
}

bool Rete::callTalk(bool on) {
    auto& s = sim::state();
    if (s.call != uint8_t(pn::CallState::ACTIVE)) return false;
    if (on && callInfo().hearing) return false;
    s.callTalking = on;
    return true;
}

Rete::CallInfo Rete::callInfo() {
    auto& s = sim::state();
    const int64_t now = sim::nowMs();
    // Chi chiama: squilla dopo 1 s, risponde dopo 4, ci si sente dopo 4,8.
    if (s.callOut && s.call && s.call != uint8_t(pn::CallState::ACTIVE)) {
        const int64_t t = now - s.callT0;
        s.call = uint8_t(t < 1000 ? pn::CallState::DIALING
                         : t < 4000 ? pn::CallState::RINGBACK
                         : t < 4800 ? pn::CallState::CONNECTING
                                    : pn::CallState::ACTIVE);
        if (s.call == uint8_t(pn::CallState::ACTIVE)) s.callT0 += 4800;
    }
    CallInfo ci;
    ci.state = pn::CallState(s.call);
    ci.peer = s.callPeer;
    ci.outgoing = s.callOut;
    ci.hops = s.callHops;
    if (ci.state == pn::CallState::ACTIVE) {
        ci.elapsedMs = now > s.callT0 ? uint32_t(now - s.callT0) : 0;
        ci.leftMs = ci.elapsedMs < pn::CALL_MAX_MS ? pn::CALL_MAX_MS - ci.elapsedMs : 0;
        ci.talking = s.callTalking;
        ci.hearing = !s.callTalking && (s.callHearing >= 0 ? s.callHearing == 1 : ci.elapsedMs % 12000 >= 2000 &&
                                                                                      ci.elapsedMs % 12000 < 6000);
        ci.lostPct = ci.elapsedMs > 6000 ? 3 : 0;
    }
    return ci;
}

// Uguali a quelle di rete.cpp.
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
