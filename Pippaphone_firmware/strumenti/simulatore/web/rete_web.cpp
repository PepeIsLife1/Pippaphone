// Simulatore nel browser: la rete PippaNet finta e i vocali.
//
// Quando si manda qualcosa fa quello che fa app/rete.cpp (messaggi, vocali, richieste di posizione,
// SOS, gruppi) e poi risponde come risponderebbero gli altri telefoni: conferme di consegna e di
// lettura, risposte, posizioni, conferme dell'SOS. Luca Verdi (205) e' fuori portata, per vedere
// anche cosa succede quando un messaggio non arriva. Dalla pagina arrivano gli eventi da far
// succedere (web::command): messaggi, SOS, richieste di posizione, vocali, gruppi, batteria...
#include <algorithm>
#include <cstdio>

#include "app/dati.h"
#include "app/eventi.h"
#include "app/memoria.h"
#include "app/rete.h"
#include "app/voce.h"
#include "sim.h"
#include "web/web.h"

namespace app {

namespace {

constexpr uint16_t FUORI_PORTATA = 205;  // Luca Verdi: i suoi messaggi non arrivano

uint32_t g_nextId = 5000;
uint32_t g_posRequest = 300;
uint32_t g_sosRound = 0;
uint16_t g_nextGroup = 20;
size_t g_reply = 0, g_incoming = 0;
uint32_t g_playGen = 0;
std::function<void()> g_playDone;

const char* const RISPOSTE[] = {"Ricevuto, grazie!", "Ok, ci sentiamo dopo", "Perfetto, a tra poco",
                                "Sono quasi in cima, poi ti scrivo", "Va bene!", "Arrivo tra dieci minuti"};
const char* const DA_MARCO[] = {"Sei già partito? Noi siamo al rifugio", "Hai visto che tramonto?",
                                "Mi mandi la tua posizione?", "Il sentiero è chiuso, passiamo dal bosco"};

uint32_t oraUnix() { return sim::unixNow(); }
uint16_t me() { return rete().number(); }
std::string voiceFile(uint32_t file) { return std::string(DATI) + "/voce/" + std::to_string(file) + ".c2"; }

// Posizione finta di un contatto, vicino al GPS finto (Milano, 45,4642 N 9,19 E).
std::string positionOf(uint16_t n, bool sos) {
    int32_t lat = 454642000 + (int(n % 7) - 3) * 40000, lon = 91900000 + (int(n % 5) - 2) * 60000;
    if (n == 101) lat = 454652000, lon = 91950000;
    if (n == 102) lat = 454610000, lon = 91800000;
    char b[80];
    if (sos) snprintf(b, sizeof(b), "%ld,%ld,%d,%u,%u,%u", long(lat), long(lon), 118, 12u, 40u, 63u);
    else snprintf(b, sizeof(b), "%ld,%ld,%d,%u,%u", long(lat), long(lon), 130, 9u, 30u);
    return b;
}

// Come Rete::onDelivery.
void delivery(const ChatId& c, uint16_t to, uint32_t id, pn::Delivery d) {
    if (d == pn::Delivery::READ) {
        const Chat ch = dati().chat(c);
        for (const auto& m : ch.msgs)
            if (m.out && m.id <= id && m.status == pn::Delivery::DELIVERED) dati().setStatus(c, m.id, d);
    }
    dati().setStatus(c, id, d);
    post({Ev::DELIVERY, c, to, id, int32_t(d)});
}

// Come Rete::onText, onGroupText e onGlobalText.
void incomingText(const ChatId& c, uint16_t from, const std::string& text) {
    Message m;
    m.id = ++g_nextId;
    m.from = from;
    m.time = oraUnix();
    m.text = text;
    if (dati().addMessage(c, m, true)) post({Ev::MESSAGE, c, from, m.id, 0});
}

// Come Rete::onVoiceIncoming e, poco dopo, Rete::onVoice.
void incomingVoice(uint16_t from, uint16_t ms) {
    const ChatId c{ChatKind::DIRECT, from};
    post({Ev::VOICE_INCOMING, c, from, 0, ms});
    web::after(2500, [c, from, ms] {
        const uint32_t file = dati().nextVoiceFile();
        std::vector<uint8_t> f = {uint8_t(pn::Codec::CODEC2_700C), uint8_t(ms), uint8_t(ms >> 8), 0, 0, 0, 0};
        f.resize(f.size() + ms * 700 / 8000 + 1, 0);
        writeFile(voiceFile(file), f);
        Message m;
        m.id = 0x20000000u | file;
        m.from = from;
        m.time = oraUnix();
        m.kind = MsgKind::VOICE;
        m.voiceMs = ms;
        m.voiceFile = file;
        dati().addMessage(c, m, true);
        post({Ev::VOICE_READY, c, from, file, ms});
    });
}

// Come Rete::onVoiceState.
void voiceState(uint16_t to, uint32_t id, pn::VoiceSendState s) {
    const ChatId c{ChatKind::DIRECT, to};
    pn::Delivery d = pn::Delivery::PENDING;
    switch (s) {
        case pn::VoiceSendState::SENDING: d = pn::Delivery::SENT; break;
        case pn::VoiceSendState::DELIVERED:
        case pn::VoiceSendState::PARTIAL: d = pn::Delivery::DELIVERED; break;
        case pn::VoiceSendState::BUSY:
        case pn::VoiceSendState::UNREACHABLE: d = pn::Delivery::UNREACHABLE; break;
        default: break;
    }
    if (d != pn::Delivery::PENDING) dati().setStatus(c, id, d);
    post({Ev::VOICE_STATE, c, to, id, int32_t(s)});
}

// Come Rete::onSos.
void incomingSos(uint16_t from, bool active) {
    if (active) ++g_sosRound;
    Message m;
    m.id = 0x40000000u | (g_sosRound << 1) | (active ? 1 : 0);
    m.from = from;
    m.time = oraUnix();
    m.kind = MsgKind::SOS;
    m.text = active ? positionOf(from, true) : "SOS annullato";
    const ChatId c{ChatKind::DIRECT, from};
    dati().addMessage(c, m, active);
    post({Ev::SOS, c, from, g_sosRound, active ? 1 : 0});
}

// Come Rete::onGroupInvite.
void incomingInvite() {
    pn::GroupInfo g;
    g.id = 12;
    g.name = "Cena di venerdì";
    g.creator = 412;
    g.members = {412, me(), 517, 102};
    dati().setGroup(g);
    Message m;
    m.id = 1;
    m.from = g.creator;
    m.time = oraUnix();
    m.kind = MsgKind::INFO;
    m.text = dati().nameOf(g.creator) + " ti ha aggiunto al gruppo";
    const ChatId c{ChatKind::GROUP, g.id};
    dati().addMessage(c, m, true);
    post({Ev::GROUP_INVITE, c, g.creator, g.id, 0});
}

}  // namespace

// ---------------------------------------------------------------- azioni (come app/rete.cpp)

uint32_t Rete::sendText(const ChatId& chat, const std::string& text) {
    Message m;
    m.id = ++g_nextId;
    m.from = me();
    m.out = true;
    m.time = oraUnix();
    m.text = text;
    // Gruppi e canale globale non hanno conferme: "inviato" appena parte.
    m.status = chat.kind == ChatKind::DIRECT ? pn::Delivery::PENDING : pn::Delivery::SENT;
    dati().addMessage(chat, m, false);
    const uint32_t id = m.id;
    if (chat.kind == ChatKind::DIRECT) {
        const uint16_t to = chat.id;
        web::after(700, [chat, to, id] { delivery(chat, to, id, pn::Delivery::SENT); });
        if (to == FUORI_PORTATA) {
            web::after(6000, [chat, to, id] { delivery(chat, to, id, pn::Delivery::UNREACHABLE); });
        } else {
            web::after(2200, [chat, to, id] { delivery(chat, to, id, pn::Delivery::DELIVERED); });
            web::after(4500, [chat, to, id] { delivery(chat, to, id, pn::Delivery::READ); });
            web::after(7000, [chat, to] { incomingText(chat, to, RISPOSTE[g_reply++ % 6]); });
        }
    } else if (chat.kind == ChatKind::GROUP) {
        pn::GroupInfo g;
        if (dati().group(chat.id, g)) {
            for (uint16_t n : g.members)
                if (n != me() && n != FUORI_PORTATA) {
                    web::after(6000, [chat, n] { incomingText(chat, n, RISPOSTE[g_reply++ % 6]); });
                    break;
                }
        }
    } else {
        web::after(6000, [chat] { incomingText(chat, 318, "Ti sento forte e chiaro"); });
    }
    return id;
}

uint32_t Rete::requestPosition(uint16_t to) {
    const uint32_t id = ++g_posRequest;
    Message m;
    m.id = id;
    m.from = me();
    m.out = true;
    m.time = oraUnix();
    m.kind = MsgKind::INFO;
    m.text = "Posizione chiesta";
    dati().addMessage({ChatKind::DIRECT, to}, m, false);
    if (to != FUORI_PORTATA) {
        // Come Rete::onPosition: la risposta arriva come messaggio nella chat.
        web::after(3500, [to, id] {
            Message p;
            p.id = id ^ 0x80000000u;
            p.from = to;
            p.time = oraUnix();
            p.kind = MsgKind::POSITION;
            p.text = positionOf(to, false);
            const ChatId c{ChatKind::DIRECT, to};
            dati().addMessage(c, p, false);
            post({Ev::POSITION, c, to, p.id, int32_t(pn::PosStatus::OK)});
        });
    }
    return id;
}

void Rete::startSos() {
    sim::State& s = sim::state();
    s.sos = true;
    s.sosAcked.clear();
    int wait = 2500;
    for (uint16_t n : sosContacts()) {
        if (n == FUORI_PORTATA) continue;
        web::after(wait, [n] {
            if (!sim::state().sos) return;
            sim::state().sosAcked.insert(n);
            post({Ev::SOS_ACK, {}, n, 0, 0});
        });
        wait += 1800;
    }
}

pn::GroupInfo Rete::createGroup(const std::string& name, const std::vector<uint16_t>& members) {
    pn::GroupInfo g;
    g.id = g_nextGroup++;
    g.name = name;
    g.members = members;
    g.members.push_back(me());
    g.creator = me();
    dati().setGroup(g);
    Message info;
    info.id = 1;
    info.from = g.creator;
    info.out = true;
    info.status = pn::Delivery::SENT;
    info.kind = MsgKind::INFO;
    info.time = oraUnix();
    info.text = "Gruppo creato";
    dati().addMessage({ChatKind::GROUP, g.id}, info, false);
    return g;
}

uint32_t Rete::sendVoice(uint16_t to, const std::vector<uint8_t>& codec2, uint16_t durationMs) {
    const uint32_t id = ++g_nextId;
    // Lo si conserva anche tra i propri messaggi, per riascoltarlo.
    const uint32_t file = dati().nextVoiceFile();
    std::vector<uint8_t> f = {uint8_t(pn::Codec::CODEC2_700C), uint8_t(durationMs), uint8_t(durationMs >> 8), 0, 0, 0, 0};
    f.insert(f.end(), codec2.begin(), codec2.end());
    writeFile(voiceFile(file), f);
    Message m;
    m.id = id;
    m.from = me();
    m.out = true;
    m.time = oraUnix();
    m.kind = MsgKind::VOICE;
    m.voiceMs = durationMs;
    m.voiceFile = file;
    m.status = pn::Delivery::PENDING;
    dati().addMessage({ChatKind::DIRECT, to}, m, false);
    if (to == FUORI_PORTATA) {
        web::after(5000, [to, id] { voiceState(to, id, pn::VoiceSendState::UNREACHABLE); });
    } else {
        // Chiamata, invio (circa mezzo secondo d'aria per secondo di voce), consegna, risposta.
        const uint32_t sent = 1500 + durationMs / 2;
        web::after(1500, [to, id] { voiceState(to, id, pn::VoiceSendState::SENDING); });
        web::after(sent, [to, id] { voiceState(to, id, pn::VoiceSendState::DELIVERED); });
        web::after(sent + 5000, [to] { incomingVoice(to, 3600); });
    }
    return id;
}

// ---------------------------------------------------------------- vocali: durano davvero (in silenzio)

bool Voce::begin() { return true; }

bool Voce::startRecording() {
    sim::State& s = sim::state();
    s.recording = true;
    s.recStartMs = sim::nowMs();
    s.recordedMs = 0;
    return true;
}

void Voce::stopRecording() {
    sim::State& s = sim::state();
    if (!s.recording) return;
    s.recordedMs = uint32_t(std::min<int64_t>(sim::nowMs() - s.recStartMs, VOICE_MAX_MS));
    s.recording = false;
}

bool Voce::recording() const {
    sim::State& s = sim::state();
    if (s.recording && sim::nowMs() - s.recStartMs >= int64_t(VOICE_MAX_MS)) {
        s.recordedMs = VOICE_MAX_MS;
        s.recording = false;
    }
    return s.recording;
}

uint32_t Voce::recordedMs() const {
    const sim::State& s = sim::state();
    return s.recording ? uint32_t(sim::nowMs() - s.recStartMs) : s.recordedMs;
}

std::vector<uint8_t> Voce::takeRecording(uint16_t& durationMs) {
    durationMs = uint16_t(sim::state().recordedMs);
    return std::vector<uint8_t>(size_t(durationMs) * 700 / 8000 + 1, 0);  // Codec2 700C: 700 bit/s di silenzio
}

void Voce::play(uint32_t file, std::function<void()> done) {
    std::vector<uint8_t> f;
    uint32_t len = 0;
    if (readFile(voiceFile(file), f) && f.size() >= 3) len = uint32_t(f[1] | (f[2] << 8));
    sim::State& s = sim::state();
    s.playing = len > 0;
    s.playingFile = file;
    s.playStartMs = sim::nowMs();
    s.playLenMs = len;
    g_playDone = std::move(done);
    const uint32_t gen = ++g_playGen;
    if (len)
        web::after(len, [gen] {
            if (gen != g_playGen || !sim::state().playing) return;
            sim::state().playing = false;
            std::function<void()> cb = std::move(g_playDone);
            if (cb) cb();
        });
}

void Voce::stop() {
    sim::state().playing = false;
    ++g_playGen;
}

bool Voce::playing() const { return sim::state().playing; }

uint32_t Voce::playedMs() const {
    const sim::State& s = sim::state();
    return s.playing ? uint32_t(std::min<int64_t>(sim::nowMs() - s.playStartMs, s.playLenMs)) : 0;
}

uint32_t Voce::playingFile() const { return sim::state().playingFile; }

}  // namespace app

// ---------------------------------------------------------------- eventi chiesti dalla pagina

namespace web {

// Codici uguali a quelli di shell.html (PippaSim.cmd).
enum Cmd {
    MSG_MARCO = 1,
    MSG_GRUPPO = 2,
    MSG_GLOBALE = 3,
    POS_RICHIESTA = 4,
    SOS_GIULIA = 5,
    SOS_ANNULLATO = 6,
    VOCALE_MARCO = 7,
    GRUPPO_NUOVO = 8,
    BATTERIA_SCARICA = 9,
    BATTERIA_OK = 10,
    IN_CARICA = 11,
    GPS = 12,
    RIPETITORE = 13,
};

void command(int cmd, int arg) {
    using namespace app;
    sim::State& s = sim::state();
    switch (cmd) {
        case MSG_MARCO: incomingText({ChatKind::DIRECT, 101}, 101, DA_MARCO[g_incoming++ % 4]); break;
        case MSG_GRUPPO:
            incomingText({ChatKind::GROUP, 3}, 102, "Domani si parte alle 7, chi porta il fornelletto?");
            break;
        case MSG_GLOBALE: incomingText({ChatKind::GLOBAL, 0}, 318, "Qualcuno sente il ripetitore R7?"); break;
        case POS_RICHIESTA: post({Ev::POS_REQUEST, {ChatKind::DIRECT, 102}, 102, ++g_posRequest, 0}); break;
        case SOS_GIULIA: incomingSos(102, true); break;
        case SOS_ANNULLATO: incomingSos(102, false); break;
        case VOCALE_MARCO: incomingVoice(101, 4200); break;
        case GRUPPO_NUOVO: incomingInvite(); break;
        case BATTERIA_SCARICA:
            s.battery = 4;
            s.charging = false;
            post({Ev::BATTERY, {}, 0, 0, 4});
            break;
        case BATTERIA_OK: s.battery = 76; break;
        case IN_CARICA: s.charging = arg != 0; break;
        case GPS: s.gpsFix = arg != 0; break;
        case RIPETITORE: s.heard = arg != 0; break;
        default: break;
    }
}

}  // namespace web
