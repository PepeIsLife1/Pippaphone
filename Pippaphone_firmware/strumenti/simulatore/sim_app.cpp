// Simulatore: rete PippaNet, vocali, aggiornamento e memoria finti. I dati (rubrica, chat,
// gruppi, impostazioni) sono quelli veri di app/dati.cpp, salvati in una cartella temporanea.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>

#include "app/aggiorna.h"
#if CONFIG_SCHEDA_TDECK_PLUS
#include "app/cuffie.h"
#endif
#include "app/dati.h"
#include "app/eventi.h"
#include "app/memoria.h"
#ifdef SIM_NUOVE
#include "app/musica.h"
#endif
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
static uint32_t g_lastTime = 0;
uint32_t loadLastTime() { return g_lastTime; }
void saveLastTime(uint32_t t) { g_lastTime = t; }
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

bool otaStart(uint16_t) { return true; }
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

#if CONFIG_SCHEDA_TDECK_PLUS
// ---------------------------------------------------------------- cuffie USB-C (finte)

// Niente USB: accendi() passa subito a "in attesa" (o, con cuffieComputer, dice "Scollega il computer");
// collegate, non supportate e guaste le mette il copione (sim::state() e post(Ev::CUFFIE)). I testi sono gli
// stessi di app/cuffie.cpp.
namespace {
void cuffieCambia(Cuffie::Stato s, Cuffie::Errore e) {
    sim::state().cuffieStato = uint8_t(s);
    sim::state().cuffieErrore = uint8_t(e);
    post({Ev::CUFFIE, {}, 0, uint32_t(e), int32_t(s)});
}
bool cuffieAccendi(bool controllaComputer) {
    if (sim::state().cuffieStato != uint8_t(Cuffie::Stato::SPENTE)) return true;
    if (controllaComputer && sim::state().cuffieComputer) {
        sim::state().cuffieErrore = uint8_t(Cuffie::Errore::COMPUTER);
        return false;
    }
    cuffieCambia(Cuffie::Stato::ACCENSIONE, Cuffie::Errore::NESSUNO);
    cuffieCambia(Cuffie::Stato::IN_ATTESA, Cuffie::Errore::NESSUNO);
    return true;
}
}  // namespace

Cuffie& cuffie() {
    static Cuffie c;
    return c;
}
void Cuffie::avvia() {}
bool Cuffie::accendi() { return cuffieAccendi(true); }
bool Cuffie::prova(uint32_t) { return cuffieAccendi(false); }
void Cuffie::spegni() {
    if (!accese()) return;
    cuffieCambia(Stato::SPEGNIMENTO, Errore::NESSUNO);
    cuffieCambia(Stato::SPENTE, Errore::NESSUNO);
}
bool Cuffie::spegniEAspetta(uint32_t) {
    spegni();
    return true;
}
Cuffie::Stato Cuffie::stato() const { return Stato(sim::state().cuffieStato); }
bool Cuffie::accese() const { return stato() != Stato::SPENTE; }
bool Cuffie::pronta() const { return stato() == Stato::COLLEGATE; }
Cuffie::Errore Cuffie::errore() const { return Errore(sim::state().cuffieErrore); }
std::string Cuffie::nome() const {
    const Stato s = stato();
    return s == Stato::COLLEGATE || s == Stato::NON_SUPPORTATE || s == Stato::ERRORE ? sim::state().cuffieNome : "";
}
std::string Cuffie::testo() const {
    switch (stato()) {
        case Stato::ACCENSIONE:
        case Stato::IN_ATTESA:
        case Stato::COLLEGAMENTO: return "in attesa";
        case Stato::COLLEGATE: return nome().empty() ? "collegate" : nome();
        case Stato::NON_SUPPORTATE: return "non supportate";
        case Stato::ERRORE: return "errore";
        default: return "spente";
    }
}
const char* Cuffie::testoStato(Stato s) {
    switch (s) {
        case Stato::SPENTE: return "spente";
        case Stato::ACCENSIONE: return "accensione";
        case Stato::IN_ATTESA: return "in attesa";
        case Stato::COLLEGAMENTO: return "collegamento";
        case Stato::COLLEGATE: return "collegate";
        case Stato::NON_SUPPORTATE: return "non supportate";
        case Stato::ERRORE: return "errore";
        case Stato::SPEGNIMENTO: return "spegnimento";
    }
    return "?";
}
const char* Cuffie::testoErrore(Errore e) {
    switch (e) {
        case Errore::NESSUNO: return "nessuno";
        case Errore::COMPUTER: return "c'e' un computer collegato";
        case Errore::AGGIORNAMENTO: return "aggiornamento Wi-Fi acceso";
        case Errore::MEMORIA: return "poca memoria";
        case Errore::NON_AUDIO: return "non audio";
        case Errore::SOLO_UAC3: return "solo USB Audio 3";
        case Errore::PACCHETTI_GRANDI: return "pacchetti troppo grandi";
        case Errore::DESCRITTORE_LUNGO: return "descrittore troppo lungo";
        case Errore::FORMATO: return "formato non adatto";
        case Errore::DISPOSITIVO: return "il dispositivo non risponde";
        case Errore::DRIVER: return "USB host non partita";
        case Errore::SENZA_CUFFIE: return "2 minuti senza cuffie";
    }
    return "?";
}
void Cuffie::setVolume(uint8_t v) { sim::state().cuffieVolume = v > 10 ? 10 : v; }
uint8_t Cuffie::volume() const { return sim::state().cuffieVolume; }
Cuffie::Statistiche Cuffie::statistiche() const {
    Statistiche s;
    s.stato = stato();
    s.errore = errore();
    return s;
}
#endif

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
uint32_t Rete::unixTime() {
#ifdef SIM_NUOVE
    if (TimeSource(sim::state().timeSource) == TimeSource::NONE) return 0;  // ora persa (copione)
#endif
    return uint32_t(int64_t(sim::unixNow()) + sim::state().timeOffsetS);
}
bool Rete::timeValid() {
#ifdef SIM_NUOVE
    return TimeSource(sim::state().timeSource) != TimeSource::NONE;
#else
    return true;
#endif
}
#ifdef SIM_NUOVE
Rete::TimeSource Rete::timeSource() { return TimeSource(sim::state().timeSource); }
bool Rete::setManualTime(uint32_t unixSec) {
    if (TimeSource(sim::state().timeSource) == TimeSource::GPS) return false;
    sim::state().timeOffsetS = int64_t(unixSec) - int64_t(sim::unixNow());
    sim::state().timeSource = uint8_t(TimeSource::LOCAL);
    post({Ev::TIME, {}, 0, 0, 0});
    return true;
}
// Sfide: il copione fa rispondere l'avversario chiamando app::giochi().ricevi().
uint32_t Rete::sendGame(uint16_t, const std::vector<uint8_t>&) {
    static uint32_t id = 900;
    return ++id;
}
void Rete::cancelGame(uint16_t, uint32_t) {}
void Rete::onGame(pn::Addr, uint32_t, const uint8_t*, size_t) {}
void Rete::onGameDelivery(pn::Addr, uint32_t, pn::Delivery) {}

#if CONFIG_SCHEDA_TDECK_PLUS
// Lettore musicale finto: quattro brani inventati, uno che "suona" da 1:23.
Musica& musica() {
    static Musica m;
    return m;
}
std::vector<Brano> Musica::elenco(bool) {
    if (!letti_) {
        brani_ = {{"/sd/musica/Escursione all'alba.mp3", "Escursione all'alba", "", ""},
                  {"/sd/musica/Rifugio/Canto del rifugio.mp3", "Rifugio/Canto del rifugio", "", ""},
                  {"/sd/musica/Rifugio/Vento sul passo.wav", "Rifugio/Vento sul passo", "", ""},
                  {"/sd/musica/prova radio.wav", "prova radio", "", ""}};
        letti_ = true;
    }
    return brani_;
}
void Musica::suona(int indice) {
    indice_ = indice;
    attiva_ = true;
    pausa_ = false;
    titolo_ = indice == 0 ? "Escursione all'alba" : "";
    artista_ = indice == 0 ? "Coro della valle" : "";
    durataS_ = 214;
    rate_ = 44100;
    campioni_ = 83ull * 44100;
}
void Musica::pausa(bool p) { pausa_ = p; }
void Musica::ferma() {
    attiva_ = false;
    indice_ = -1;
}
void Musica::successivo() {
    if (indice_ + 1 < int(brani_.size())) suona(indice_ + 1);
    else ferma();
}
void Musica::precedente() { suona(indice_ > 0 ? indice_ - 1 : 0); }
void Musica::salta(int) {}
Brano Musica::brano() const {
    Brano b;
    if (indice_ >= 0 && indice_ < int(brani_.size())) b = brani_[size_t(indice_)];
    b.titolo = titolo_;
    b.artista = artista_;
    return b;
}
uint32_t Musica::posizioneS() const { return rate_ ? uint32_t(campioni_ / rate_) : 0; }
void Musica::compito() {}
#endif
#endif
uint64_t Rete::airtimeMs() { return 184300; }
bool Rete::radioOk() { return true; }
void Rete::setPower(int8_t) {}
int8_t Rete::power() { return 14; }
void Rete::wantGnss(uint32_t) {}
bool Rete::gnssWanted() { return false; }
#if CONFIG_SCHEDA_TDECK_PLUS
// Le schermate che usano il GPS (ui.cpp): il copione le vede con SIM_GPS_LOG (sim_main.cpp).
void Rete::gnssSchermata() { gnssSchermataMs_ = pn::Millis(sim::nowMs()); }
bool Rete::gnssSchermataAperta() { return gnssSchermataMs_ && sim::nowMs() - int64_t(gnssSchermataMs_) < 6000; }
#endif
void Rete::service() {}
void Rete::updateGnss(const scheda::Fix&, float, uint8_t) {}
// La mia ultima posizione: quella di adesso (gpsFix) o, col T-Deck, una di 5 minuti fa (gpsUltima).
pn::Position Rete::lastPosition() {
    pn::Position p;
    if (!sim::state().gpsFix && !sim::state().gpsUltima) return p;
    p.valid = true;
    p.latE7 = sim::state().gpsLatE7;
    p.lonE7 = sim::state().gpsLonE7;
    p.altM = 122;
    p.accM = 8;
    p.ageS = sim::state().gpsFix ? 20 : 300;
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
